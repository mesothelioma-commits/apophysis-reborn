#pragma once

#include "canvases.hpp"
#include "creative.hpp"
#include "legacy_document.hpp"
#include "preview_controller.hpp"
#include <QDialog>
#include <functional>
#include <map>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QToolButton;

namespace apo::editor {

class BackgroundJobs;
template <class Target> class BackgroundReceiver;

class PaletteStrip : public QWidget {
    Q_OBJECT
  public:
    explicit PaletteStrip(QWidget *parent = nullptr);
    void setColors(const Palette &colors);
    void setIndex(int index);
  signals:
    void indexSelected(int index);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

  private:
    Palette colors_ = default_palette();
    int index_ = 0;
    void choose(double x);
};

class GradientDialog : public QDialog {
    Q_OBJECT
  public:
    explicit GradientDialog(const Flame &flame, QWidget *parent = nullptr,
                            const PreviewSettings &settings = {});
    ~GradientDialog() override;
    const Flame &resultFlame() const {
        return flame_;
    }
    void importPaletteFile(const QString &path);
    void exportPaletteFile(const QString &path) const;

  private:
    Flame flame_;
    PreviewController preview_;
    PreviewCanvas *canvas_;
    PaletteStrip *strip_;
    QSpinBox *index_, *red_, *green_, *blue_, *hue_, *saturation_, *value_, *rotate_;
    QSpinBox *hslHue_, *hslSaturation_, *lightness_;
    QComboBox *presets_, *space_, *paletteMode_;
    QTableWidget *stops_;
    QLabel *status_;
    QLineEdit *seed_;
    QPushButton *undo_, *redo_;
    std::vector<Palette> history_;
    std::size_t historyIndex_ = 0;
    std::vector<creative::PalettePreset> presetsData_;
    bool syncing_ = false;
    void setColors(const Palette &colors, bool remember = true);
    void refreshColor();
    void requestPreview();
    void loadPresets();
    void fillStops();
    void buildStops();
    void run(const std::function<void()> &operation);
};

class AdjustmentDialog : public QDialog {
    Q_OBJECT
  public:
    explicit AdjustmentDialog(const Flame &flame, QWidget *parent = nullptr,
                              const PreviewSettings &settings = {});
    ~AdjustmentDialog() override;
    const Flame &resultFlame() const {
        return flame_;
    }
    LegacyEdits edits() const;

  private:
    Flame original_, flame_;
    PreviewController preview_;
    PreviewCanvas *canvas_;
    QLabel *status_;
    QPushButton *background_, *apply_;
    QSpinBox *width_, *height_, *oversample_;
    QCheckBox *aspect_, *density_;
    QComboBox *filter_;
    std::map<std::string, QLineEdit *> fields_;
    bool syncing_ = false;
    double aspectRatio_;
    void refreshFields();
    bool updateFlame();
    void requestPreview();
};

class ExploreDialog : public QDialog {
    Q_OBJECT
  public:
    enum class Mode { random, mutation };
    enum class Disposition { replace, append };
    ExploreDialog(Mode mode, const Flame &parentFlame, QWidget *parent = nullptr,
                  const PreviewSettings &settings = {}, PreviewController::Renderer renderer = apo::render);
    ~ExploreDialog() override;
    const std::vector<Flame> &candidates() const {
        return candidates_;
    }
    const std::vector<Flame> &acceptedFlames() const {
        return accepted_;
    }
    Disposition disposition() const {
        return disposition_;
    }
    int selectedIndex() const {
        return selected_;
    }
    int branchCount() const {
        return int(branches_.size());
    }

  private:
    struct Branch {
        Flame flame;
        int parent;
    };
    Mode mode_;
    Flame initial_;
    std::vector<Branch> branches_;
    int parentBranch_ = 0, selected_ = -1, thumbnail_ = 0;
    std::vector<Flame> candidates_, accepted_;
    Disposition disposition_ = Disposition::append;
    PreviewController preview_, thumbnails_;
    PreviewCanvas *canvas_;
    QGridLayout *grid_;
    QLabel *status_, *selectedName_;
    QLineEdit *seed_;
    QCheckBox *lockSeed_, *geometry_, *variations_, *colors_, *parameters_, *palette_, *final_,
        *reflection_, *showParent_;
    QSpinBox *count_, *minimum_, *maximum_, *symmetry_;
    QDoubleSpinBox *strength_, *trend_, *minContraction_, *maxContraction_, *translation_;
    QComboBox *history_;
    QListWidget *allowlist_;
    QPushButton *generate_, *useParent_, *apply_, *addSelected_, *addAll_, *addBranch_;
    std::vector<QToolButton *> tiles_;
    std::shared_ptr<BackgroundJobs> generationJobs_;
    std::shared_ptr<BackgroundReceiver<ExploreDialog>> generationReceiver_;
    bool generating_ = false;
    void generate(bool advanceSeed);
    void showCandidates(std::vector<Flame> flames);
    void select(int index);
    void nextThumbnail();
    void showSelected();
    void chooseResult(Disposition disposition, bool all = false, bool branch = false);
    void updateActions();
};

// Apply only changed, supported fields. Mutation topology must match its parent.
void applyCreativeFlame(LegacyDocument &document, std::size_t index, const Flame &flame);

} // namespace apo::editor
