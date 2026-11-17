#pragma once

#include "flame.hpp"
#include <QImage>
#include <QWidget>
#include <QColor>
#include <QPolygonF>
#include <cmath>

namespace apo::editor {

QColor transformColor(int index, bool final = false);

class PreviewCanvas : public QWidget {
    Q_OBJECT
  public:
    explicit PreviewCanvas(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void setMessage(const QString &message);
    void fit();
    void actualPixels();
    const QImage &image() const {
        return image_;
    }
  signals:
    void viewportChanged(QSize size);
    void zoomChanged(double factor);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    double imageScale() const;
    QImage image_;
    QString message_ = "Open a flame library to begin";
    double zoom_ = 1;
    bool fit_ = true, dragging_ = false;
    QPointF pan_, lastMouse_;
};

class TriangleCanvas : public QWidget {
    Q_OBJECT
  public:
    explicit TriangleCanvas(QWidget *parent = nullptr);
    void setFlame(const std::optional<Flame> &flame, int selected, bool final);
    void setPost(bool post);
    void setSnap(bool snap) {
        snap_ = snap;
    }
    void setGrid(bool grid) {
        grid_ = grid;
        update();
    }
    void setGridSpacing(double value) {
        if (std::isfinite(value) && value > 0) {
            spacing_ = value;
            update();
        }
    }
    void setLocked(bool locked) {
        locked_ = locked;
    }
    void fit();
    QPointF toScreen(Point point) const;
    Point fromScreen(QPointF point) const;
  signals:
    void selected(int index, bool final);
    void affinePreview(const Affine &value);
    void affineCommitted(const Affine &value);
    void editCanceled();
    void coordinateChanged(double x, double y);

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    Affine &affine();
    QPolygonF triangle(const Affine &affine) const;
    std::optional<Flame> flame_;
    int selected_ = 0, handle_ = -1;
    bool final_ = false, post_ = false, snap_ = false, grid_ = true, locked_ = false;
    bool panning_ = false, editing_ = false, autoFit_ = true;
    double pixelsPerUnit_ = 78, spacing_ = 0.25;
    QPointF pan_, lastMouse_;
    Point startPoint_;
    Affine initial_;
};
} // namespace apo::editor
