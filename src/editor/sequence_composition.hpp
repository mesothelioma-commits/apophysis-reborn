#pragma once
#include <QStringList>
#include <functional>
#include <stop_token>
#include <cstdint>

class QWidget;

namespace apo::editor {
// Each layer is an animated project or the first numbered PNG, bottom to top.
void composeSequence(const QStringList &layers, const QString &destination, bool video,
                     unsigned fpsNum, unsigned fpsDen,
                     std::uint64_t samples, std::stop_token stop = {},
                     const std::function<void(QString, int, int)> &progress = {});
void showSequenceComposer(QWidget *parent);
}
