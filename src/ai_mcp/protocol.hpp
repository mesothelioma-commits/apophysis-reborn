#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

namespace apo::ai {
inline QString sessionPath() {
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (directory.isEmpty()) return {};
    return QDir(directory).filePath(QStringLiteral("ApophysisReborn/ai-session.json"));
}

inline constexpr int maxScriptBytes = 64 * 1024;
inline constexpr int maxRequestBytes = 512 * 1024;
inline constexpr int maxResponseBytes = 2 * 1024 * 1024;
} // namespace apo::ai
