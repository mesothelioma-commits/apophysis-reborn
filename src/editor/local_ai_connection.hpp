#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QLockFile>
#include <QSet>
#include <functional>
#include <memory>

class QLocalSocket;

namespace apo::editor {

// One authenticated request per local socket. The bridge has no network listener.
class LocalAiConnection final : public QObject {
public:
  using Handler = std::function<QJsonObject(const QString &, const QJsonObject &)>;
  explicit LocalAiConnection(Handler handler, QObject *parent);
  ~LocalAiConnection() override;
  void start();
  void stop();
  bool enabled() const { return server_.isListening(); }
  QString sessionId() const { return server_.serverName(); }

private:
  Handler handler_;
  QLocalServer server_;
  QSet<QLocalSocket *> clients_;
  std::unique_ptr<QLockFile> lock_;
  QString token_;
  void acceptClients();
  void receive(QLocalSocket *client);
};
} // namespace apo::editor
