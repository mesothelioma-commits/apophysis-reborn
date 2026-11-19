#include "local_ai_connection.hpp"
#include "../ai_mcp/protocol.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>
#include <array>
#include <stdexcept>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace apo::editor {
namespace {
// Apply permissions *before* writing the secret. Qt's Windows permission bits
// do not set a Windows ACL. Grant access only to the current process user.
bool makePrivate(QTemporaryFile &file) {
#ifdef Q_OS_WIN
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  QByteArray user(static_cast<qsizetype>(size), '\0');
  const bool read = size && GetTokenInformation(token, TokenUser, user.data(), size, &size);
  CloseHandle(token);
  if (!read)
    return false;
  EXPLICIT_ACCESSW access{};
  access.grfAccessPermissions = FILE_ALL_ACCESS;
  access.grfAccessMode = SET_ACCESS;
  access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  access.Trustee.TrusteeType = TRUSTEE_IS_USER;
  access.Trustee.ptstrName = static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);
  PACL acl = nullptr;
  if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS)
    return false;
  auto path = QDir::toNativeSeparators(file.fileName()).toStdWString();
  const auto result = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
  LocalFree(acl);
  return result == ERROR_SUCCESS;
#else
  return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
}

void publishSession(const QString &path, const QJsonObject &descriptor) {
  QTemporaryFile file(path + ".XXXXXX");
  if (!file.open() || !makePrivate(file))
    throw std::runtime_error("Cannot create a private AI session file.");
  const auto data = QJsonDocument(descriptor).toJson(QJsonDocument::Compact);
  if (file.write(data) != data.size() || !file.flush())
    throw std::runtime_error("Cannot write the AI session file.");
  file.close();
  // The process lock is held. A previous crash can leave a stale descriptor.
  if (QFileInfo::exists(path) && !QFile::remove(path))
    throw std::runtime_error("Cannot replace the inactive AI session file.");
  if (!file.rename(path))
    throw std::runtime_error("Cannot publish the AI session file.");
  file.setAutoRemove(false);
}
}

LocalAiConnection::LocalAiConnection(Handler handler, QObject *parent)
    : QObject(parent), handler_(std::move(handler)) {
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  server_.setMaxPendingConnections(4);
  connect(&server_, &QLocalServer::newConnection, this, &LocalAiConnection::acceptClients);
}
LocalAiConnection::~LocalAiConnection() { stop(); }

void LocalAiConnection::start() {
  if (enabled())
    return;
  const auto path = ai::sessionPath();
  if (path.isEmpty()) throw std::runtime_error("No per-user AI configuration location is available.");
  const QFileInfo directory(QFileInfo(path).absolutePath());
  if (!QDir().mkpath(directory.absoluteFilePath()) || directory.isSymLink())
    throw std::runtime_error("Cannot open the local AI configuration directory.");
#ifndef Q_OS_WIN
  struct stat st{};
  if (stat(QFile::encodeName(directory.absoluteFilePath()).constData(), &st) != 0 ||
      st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH)))
    throw std::runtime_error("The local AI configuration directory must be owned by you and writable only by you.");
#endif
  auto lock = std::make_unique<QLockFile>(path + ".lock");
  lock->setStaleLockTime(0); // Never expire a live editor's lock by age.
  if (!lock->tryLock(0))
    throw std::runtime_error("Another editor is sharing a session or the AI settings directory is unavailable.");
  const auto name = "apophysis-reborn-ai-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!server_.listen(name))
    throw std::runtime_error("Cannot open a current-user local connection.");
  std::array<quint32, 8> random{};
  QRandomGenerator::system()->fillRange(random.data(), random.size());
  token_ = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(random.data()), sizeof random).toHex());
  try {
    publishSession(path, {{"version", 1}, {"server", name}, {"token", token_}});
  } catch (...) {
    server_.close();
    token_.clear();
    throw;
  }
  lock_ = std::move(lock);
}
void LocalAiConnection::stop() {
  server_.close();
  token_.clear();
  const auto clients = clients_;
  for (auto *client : clients)
    client->abort();
  if (lock_) {
    QFile::remove(ai::sessionPath());
    lock_.reset();
  }
}

void LocalAiConnection::acceptClients() {
  while (auto *client = server_.nextPendingConnection()) {
    if (clients_.size() >= 4) {
      client->abort();
      client->deleteLater();
      continue;
    }
    clients_.insert(client);
    client->setReadBufferSize(ai::maxRequestBytes + 1);
    connect(client, &QLocalSocket::disconnected, this, [this, client] {
      clients_.remove(client);
      client->deleteLater();
    });
    QTimer::singleShot(5000, client, [client] { client->abort(); });
    connect(client, &QLocalSocket::readyRead, this, [this, client] { receive(client); });
    receive(client); // Bytes may already have arrived before newConnection.
  }
}
void LocalAiConnection::receive(QLocalSocket *client) {
  if (client->property("answered").toBool())
    return;
  if (client->bytesAvailable() > ai::maxRequestBytes) {
    client->abort();
    return;
  }
  if (!client->canReadLine())
    return;
  client->setProperty("answered", true);
  const auto request = QJsonDocument::fromJson(client->readLine()).object();
  QJsonObject reply;
  try {
    if (!enabled() || request.value("token").toString() != token_ ||
        request.value("version") != QJsonValue(1))
      throw std::runtime_error("Invalid or expired local AI session.");
    if (!request.value("arguments").isObject() || !request.value("tool").isString())
      throw std::runtime_error("Expected a tool name and arguments object.");
    reply = {{"ok", true}, {"result", handler_(request["tool"].toString(), request["arguments"].toObject())}};
  } catch (const std::exception &e) {
    reply = {{"ok", false}, {"error", QString::fromUtf8(e.what())}};
  }
  auto bytes = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
  if (bytes.size() > ai::maxResponseBytes)
    bytes = "{\"ok\":false,\"error\":\"This flame exceeds the AI response size limit.\"}\n";
  // A Windows named pipe can discard unread bytes on server disconnect.
  // The helper closes after its newline; the per-client deadline handles abandoned clients.
  client->write(bytes);
  client->flush();
}
} // namespace apo::editor
