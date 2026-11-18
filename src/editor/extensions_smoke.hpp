#pragma once
#include <QString>
namespace apo::editor {
class MainWindow;
void extensionsSmoke(MainWindow &, const QString &,
                     const QString &trustedManifest);
} // namespace apo::editor
