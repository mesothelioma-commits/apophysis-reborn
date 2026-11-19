#pragma once

#include "render_proof.hpp"
#include <QDialog>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

class QCheckBox;
class QImage;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace apo::editor {
class BackgroundJobs;
template <class Target> class BackgroundReceiver;
class ProofCanvas;

class RenderProofDialog : public QDialog {
    Q_OBJECT
public:
    using Sampler = std::function<std::shared_ptr<const RenderProof>(
        const Flame &, const RenderOptions &, ProofRegion)>;
    RenderProofDialog(const Flame &outputFlame, const RenderOptions &outputOptions,
                      bool transparent, QWidget *parent = nullptr, bool sixteenBit = false,
                      Sampler sampler = sample_render_proof);
    ~RenderProofDialog() override;

    double brightness() const;
    double gamma() const;
    double vibrancy() const;

protected:
    void done(int result) override;

private:
    std::shared_ptr<const Flame> flame_;
    RenderOptions options_;
    Sampler sampler_;
    std::shared_ptr<BackgroundJobs> jobs_;
    std::shared_ptr<BackgroundReceiver<RenderProofDialog>> receiver_;
    std::shared_ptr<const RenderProof> retained_;
    quint64 cropGeneration_ = 0, toneGeneration_ = 0;
    bool sampling_ = false, developing_ = false;
    const bool sixteenBit_;
    QTimer toneDebounce_;
    QSpinBox *x_, *y_, *width_, *height_;
    QLineEdit *brightness_, *gamma_, *vibrancy_;
    QCheckBox *transparent_;
    QLabel *status_, *device_;
    QProgressBar *progress_;
    QPushButton *start_, *stop_, *apply_;
    ProofCanvas *canvas_;

    void updateCropBounds();
    void invalidateCrop();
    void sample();
    void toneChanged();
    std::optional<ProofTone> readTone() const;
    void develop();
    void updateButtons();
    void cancelJobs();
};
} // namespace apo::editor
