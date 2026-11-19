#pragma once

#include "legacy_document.hpp"
#include "preview_controller.hpp"
#include <QDialog>
#include <array>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

namespace apo::editor {
class BackgroundJobs;
template <class Target> class BackgroundReceiver;

class VariationExplorerDialog : public QDialog {
    Q_OBJECT
  public:
    VariationExplorerDialog(const LegacyDocument &document, std::size_t flameIndex,
                            std::size_t transformIndex, bool final, Variation initial,
                            QWidget *parent = nullptr, const PreviewSettings &settings = {},
                            PreviewController::Renderer renderer = apo::render);
    ~VariationExplorerDialog() override;
    LegacyTransformEdits selectedEdit() const;

  protected:
    void done(int result) override;

  private:
    std::shared_ptr<const LegacyDocument> source_;
    Transform original_;
    std::size_t transformIndex_;
    bool final_;
    PreviewController previews_;
    std::shared_ptr<BackgroundJobs> preparation_;
    std::shared_ptr<BackgroundReceiver<VariationExplorerDialog>> receiver_;
    QTimer debounce_;
    quint64 generation_ = 0;
    QListWidget *browser_;
    QComboBox *category_, *control_;
    QLineEdit *search_, *low_, *high_;
    QLabel *description_, *status_;
    QPushButton *apply_;
    std::array<QToolButton *, 6> tiles_{};
    QButtonGroup *choices_;
    std::vector<double> values_;
    std::size_t next_ = 0;
    int selected_ = -1;

    void filterCatalog();
    void selectVariation();
    void setDefaultRange();
    void invalidate();
    void schedule();
    void startSweep();
    void prepareNext();
    void finishTile(const QImage &image, const QString &error = {});
    Variation variation() const;
    const VariationParameterInfo *parameter() const;
    double currentValue() const;
    LegacyTransformEdits editFor(double value) const;
};
} // namespace apo::editor
