#include "canvases.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace apo::editor {

QColor transformColor(int index, bool final) {
    if (final)
        return QColor("#e7c16c");
    static const QColor colors[]{"#56c8da", "#eb897a", "#b59aeb", "#8acd9a", "#e5b168", "#7eabed"};
    return colors[std::size_t(index) % std::size(colors)];
}

PreviewCanvas::PreviewCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("previewCanvas");
    setMinimumSize(260, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip("Wheel to zoom the preview. Drag to pan. Fit returns to the whole image.");
}
void PreviewCanvas::setImage(const QImage &image) {
    image_ = image;
    message_.clear();
    update();
}
void PreviewCanvas::setMessage(const QString &message) {
    image_ = {};
    message_ = message;
    update();
}
void PreviewCanvas::fit() {
    fit_ = true;
    zoom_ = 1;
    pan_ = {};
    emit zoomChanged(imageScale());
    update();
}
void PreviewCanvas::actualPixels() {
    fit_ = false;
    zoom_ = 1;
    pan_ = {};
    emit zoomChanged(imageScale());
    update();
}
double PreviewCanvas::imageScale() const {
    if (image_.isNull())
        return 1;
    const double base =
        fit_ ? std::min(double(width()) / image_.width(), double(height()) / image_.height()) : 1;
    return base * zoom_;
}
void PreviewCanvas::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#11151a"));
    if (image_.isNull()) {
        painter.setPen(QColor("#aab5c2"));
        painter.drawText(rect().adjusted(32, 32, -32, -32), Qt::AlignCenter | Qt::TextWordWrap,
                         message_);
        return;
    }
    const QSizeF size = QSizeF(image_.size()) * imageScale();
    const QRectF target(QPointF(width() * .5, height() * .5) -
                            QPointF(size.width() * .5, size.height() * .5) + pan_,
                        size);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, imageScale() < 1);
    painter.drawImage(target, image_);
}
void PreviewCanvas::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    emit viewportChanged(size());
}
void PreviewCanvas::wheelEvent(QWheelEvent *e) {
    const auto center = QPointF(width() * .5, height() * .5);
    const double before = zoom_;
    zoom_ = std::clamp(zoom_ * std::pow(1.18, e->angleDelta().y() / 120.0), .1, 32.0);
    pan_ = e->position() - center - (e->position() - center - pan_) * (zoom_ / before);
    emit zoomChanged(imageScale());
    update();
    e->accept();
}
void PreviewCanvas::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton && e->button() != Qt::MiddleButton)
        return;
    dragging_ = true;
    lastMouse_ = e->position();
    setCursor(Qt::ClosedHandCursor);
}
void PreviewCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (!dragging_)
        return;
    pan_ += e->position() - lastMouse_;
    lastMouse_ = e->position();
    update();
}
void PreviewCanvas::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = false;
    unsetCursor();
}

TriangleCanvas::TriangleCanvas(QWidget *parent) : QWidget(parent) {
    setObjectName("triangleCanvas");
    setMinimumSize(250, 190);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setToolTip("Drag O to translate, X or Y to reshape the affine map. Middle-drag to pan. Wheel to "
               "zoom. Esc cancels a drag.");
}
void TriangleCanvas::setFlame(const std::optional<Flame> &flame, int selected, bool final) {
    flame_ = flame;
    selected_ = selected;
    final_ = final;
    editing_ = false;
    update();
}
void TriangleCanvas::setPost(bool post) {
    post_ = post;
    editing_ = false;
    update();
}
Affine &TriangleCanvas::affine() {
    auto &transform =
        final_ ? flame_->final_transform.value() : flame_->transforms.at(std::size_t(selected_));
    return post_ ? transform.post : transform.pre;
}
QPointF TriangleCanvas::toScreen(Point p) const {
    return QPointF(width() * .5, height() * .5) + pan_ + QPointF(p.x, -p.y) * pixelsPerUnit_;
}
Point TriangleCanvas::fromScreen(QPointF p) const {
    p = (p - QPointF(width() * .5, height() * .5) - pan_) / pixelsPerUnit_;
    return {p.x(), -p.y()};
}
QPolygonF TriangleCanvas::triangle(const Affine &a) const {
    return {toScreen({a.e, a.f}), toScreen({a.e + a.a, a.f + a.b}), toScreen({a.e + a.c, a.f + a.d})};
}
void TriangleCanvas::fit() {
    autoFit_ = true;
    QRectF bounds(-1, -1, 2, 2);
    if (flame_) {
        auto extend = [&](const Transform &t) {
            const auto &a = post_ ? t.post : t.pre;
            for (Point p : {Point{a.e, a.f}, Point{a.e + a.a, a.f + a.b}, Point{a.e + a.c, a.f + a.d}})
                if (std::abs(p.x) < 1e9 && std::abs(p.y) < 1e9)
                    bounds |= QRectF(QPointF(p.x, -p.y), QSizeF(.001, .001));
        };
        for (const auto &t : flame_->transforms)
            extend(t);
        if (flame_->final_transform)
            extend(*flame_->final_transform);
    }
    pixelsPerUnit_ = std::clamp(
        std::min((width() - 60) / bounds.width(), (height() - 60) / bounds.height()), .000001, 2000.0);
    pan_ = -bounds.center() * pixelsPerUnit_;
    update();
}
void TriangleCanvas::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (autoFit_)
        fit();
}
void TriangleCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#171e27"));
    if (grid_) {
        double step = spacing_;
        while (step * pixelsPerUnit_ < 22)
            step *= 2;
        const auto low = fromScreen({0, double(height())});
        const auto high = fromScreen({double(width()), 0});
        p.setPen(QColor("#28323e"));
        for (double x = std::ceil(low.x / step) * step; x <= high.x; x += step)
            p.drawLine(toScreen({x, low.y}), toScreen({x, high.y}));
        for (double y = std::ceil(low.y / step) * step; y <= high.y; y += step)
            p.drawLine(toScreen({low.x, y}), toScreen({high.x, y}));
        p.setPen(QColor("#4a5868"));
        p.drawLine(toScreen({0, low.y}), toScreen({0, high.y}));
        p.drawLine(toScreen({low.x, 0}), toScreen({high.x, 0}));
    }
    p.setRenderHint(QPainter::Antialiasing);
    if (!flame_)
        return;
    auto draw = [&](const Transform &t, int index, bool final) {
        const auto &a = post_ ? t.post : t.pre;
        const bool active = selected_ == index && final_ == final;
        const auto polygon = triangle(a);
        const auto color = transformColor(index, final);
        p.setPen(QPen(color, active ? 2.4 : 1.1, final ? Qt::DashLine : Qt::SolidLine));
        auto fill = color;
        fill.setAlpha(active ? 30 : 8);
        p.setBrush(fill);
        p.drawPolygon(polygon);
        p.setPen(color);
        p.setBrush(active ? color : QColor("#171e27"));
        for (int k = 0; k < 3; ++k)
            p.drawEllipse(polygon[k], active ? 4.5 : 3, active ? 4.5 : 3);
        p.drawText(polygon[0] + QPointF(8, -8), final ? "F" : QString::number(index + 1));
        if (active) {
            p.drawText(polygon[1] + QPointF(8, -4), "X");
            p.drawText(polygon[2] + QPointF(8, -4), "Y");
        }
    };
    for (int i = 0; i < int(flame_->transforms.size()); ++i)
        if (final_ || i != selected_)
            draw(flame_->transforms[i], i, false);
    if (flame_->final_transform && !final_)
        draw(*flame_->final_transform, 0, true);
    if (final_ && flame_->final_transform)
        draw(*flame_->final_transform, 0, true);
    else if (selected_ >= 0 && selected_ < int(flame_->transforms.size()))
        draw(flame_->transforms[selected_], selected_, false);
}
void TriangleCanvas::mousePressEvent(QMouseEvent *e) {
    setFocus();
    lastMouse_ = e->position();
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        autoFit_ = false;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (!flame_ || e->button() != Qt::LeftButton)
        return;
    auto hit = [&](int index, bool final) {
        const auto &t = final ? *flame_->final_transform : flame_->transforms[index];
        auto points = triangle(post_ ? t.post : t.pre);
        int vertex = -1;
        double nearest = 11;
        for (int i = 0; i < 3; ++i) {
            const double distance = QLineF(points[i], e->position()).length();
            if (distance < nearest) {
                nearest = distance;
                vertex = i;
            }
        }
        if (vertex < 0 && !points.containsPoint(e->position(), Qt::OddEvenFill))
            return false;
        selected_ = index;
        final_ = final;
        emit selected(index, final);
        if (!locked_) {
            initial_ = affine();
            startPoint_ = fromScreen(e->position());
            handle_ = std::max(0, vertex);
            editing_ = true;
        }
        update();
        return true;
    };
    if (final_ && flame_->final_transform && hit(0, true))
        return;
    if (!final_ && selected_ >= 0 && selected_ < int(flame_->transforms.size()) && hit(selected_, false))
        return;
    if (flame_->final_transform && hit(0, true))
        return;
    for (int i = int(flame_->transforms.size()) - 1; i >= 0; --i)
        if (hit(i, false))
            return;
}
void TriangleCanvas::mouseMoveEvent(QMouseEvent *e) {
    auto point = fromScreen(e->position());
    emit coordinateChanged(point.x, point.y);
    if (panning_) {
        pan_ += e->position() - lastMouse_;
        lastMouse_ = e->position();
        update();
        return;
    }
    if (!editing_)
        return;
    Affine value = initial_;
    if (handle_ == 0) {
        value.e += point.x - startPoint_.x;
        value.f += point.y - startPoint_.y;
        if (snap_) {
            value.e = std::round(value.e / spacing_) * spacing_;
            value.f = std::round(value.f / spacing_) * spacing_;
        }
    } else {
        if (snap_) {
            point.x = std::round(point.x / spacing_) * spacing_;
            point.y = std::round(point.y / spacing_) * spacing_;
        }
        if (handle_ == 1) {
            value.a = point.x - value.e;
            value.b = point.y - value.f;
        } else {
            value.c = point.x - value.e;
            value.d = point.y - value.f;
        }
    }
    affine() = value;
    emit affinePreview(value);
    update();
}
void TriangleCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if (panning_ && e->button() == Qt::MiddleButton) {
        panning_ = false;
        unsetCursor();
    }
    if (editing_ && e->button() == Qt::LeftButton) {
        const auto value = affine();
        editing_ = false;
        emit affineCommitted(value);
    }
}
void TriangleCanvas::wheelEvent(QWheelEvent *e) {
    autoFit_ = false;
    const auto center = QPointF(width() * .5, height() * .5);
    const double before = pixelsPerUnit_;
    pixelsPerUnit_ =
        std::clamp(pixelsPerUnit_ * std::pow(1.18, e->angleDelta().y() / 120.0), .000001, 1000000.0);
    pan_ = e->position() - center - (e->position() - center - pan_) * (pixelsPerUnit_ / before);
    update();
    e->accept();
}
void TriangleCanvas::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && editing_) {
        affine() = initial_;
        editing_ = false;
        emit editCanceled();
        update();
    } else
        QWidget::keyPressEvent(e);
}
} // namespace apo::editor
