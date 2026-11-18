#pragma once

#include "color_curves.hpp"
#include <QDialog>
#include <QWidget>
#include <array>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace apo::editor {

class CurvePlot : public QWidget {
    Q_OBJECT
public:
    explicit CurvePlot(QWidget* parent = nullptr);
    void setCurves(const ColorCurves& curves, unsigned channel);
    QPointF pointPosition(unsigned index) const;
signals:
    void pointEdited(unsigned index, double x, double y);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    ColorCurves curves_;
    unsigned channel_ = 0;
    int dragged_ = -1;
    QRectF graphRect() const;
    void dragPoint(QPointF position);
};

class ColorCurveDialog : public QDialog {
    Q_OBJECT
public:
    explicit ColorCurveDialog(const ColorCurves& curves, QWidget* parent = nullptr);
    const ColorCurves& resultCurves() const { return curves_; }
private:
    ColorCurves original_, curves_;
    CurvePlot* plot_;
    QComboBox *channel_, *mode_, *preset_;
    QLabel *status_, *help_;
    QPushButton* apply_;
    std::array<std::array<QLineEdit*, 3>, 4> fields_{};
    bool syncing_ = false;
    bool updateCurves();
    void refresh();
    void preset(int index);
};

} // namespace apo::editor
