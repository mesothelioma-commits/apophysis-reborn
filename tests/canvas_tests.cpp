#include "editor/canvases.hpp"

#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <cmath>
#include <limits>

Q_DECLARE_METATYPE(apo::Affine)

using namespace apo;
using namespace apo::editor;

namespace {
Flame singleTriangle() {
    Flame flame;
    flame.transforms.emplace_back();
    return flame;
}

void prepare(TriangleCanvas& canvas, const Flame& flame, int selection = 0) {
    canvas.resize(560, 400);
    canvas.setFlame(flame, selection, false);
    canvas.show();
    QCoreApplication::processEvents();
}

void moveWhilePressed(TriangleCanvas& canvas, Point from, Point to) {
    const auto start = canvas.toScreen(from).toPoint();
    const auto end = canvas.toScreen(to).toPoint();
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&canvas, (start + end) / 2);
    QTest::mouseMove(&canvas, end);
}

void drag(TriangleCanvas& canvas, Point from, Point to) {
    moveWhilePressed(canvas, from, to);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.toScreen(to).toPoint());
}

Affine committed(const QSignalSpy& spy) {
    return qvariant_cast<Affine>(spy.back().at(0));
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-9;
}
}

class CanvasTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<Affine>("apo::Affine");
        qRegisterMetaType<Affine>("Affine");
    }

    void dragOriginAndBasis() {
        TriangleCanvas canvas;
        const auto flame = singleTriangle();
        prepare(canvas, flame);
        QSignalSpy previews(&canvas, &TriangleCanvas::affinePreview);
        QSignalSpy commits(&canvas, &TriangleCanvas::affineCommitted);

        drag(canvas, {0, 0}, {.5, -.5});
        QCOMPARE(commits.count(), 1);
        QVERIFY(previews.count() >= 1);
        auto affine = committed(commits);
        QVERIFY(near(affine.a, 1) && near(affine.b, 0) && near(affine.c, 0) && near(affine.d, 1));
        QVERIFY(near(affine.e, .5) && near(affine.f, -.5));

        canvas.setFlame(flame, 0, false);
        previews.clear();
        commits.clear();
        const auto xEndpoint = canvas.fromScreen(canvas.toScreen({1.5, .5}).toPoint());
        drag(canvas, {1, 0}, {1.5, .5});
        QCOMPARE(commits.count(), 1);
        affine = committed(commits);
        // Mouse events carry integral screen pixels after the fit calculation.
        QVERIFY(near(affine.a, xEndpoint.x) && near(affine.b, xEndpoint.y));
        QVERIFY(near(affine.c, 0) && near(affine.d, 1) && near(affine.e, 0) && near(affine.f, 0));

        canvas.setFlame(flame, 0, false);
        commits.clear();
        const auto yEndpoint = canvas.fromScreen(canvas.toScreen({-.5, 1.5}).toPoint());
        drag(canvas, {0, 1}, {-.5, 1.5});
        QCOMPARE(commits.count(), 1);
        affine = committed(commits);
        QVERIFY(near(affine.a, 1) && near(affine.b, 0));
        QVERIFY(near(affine.c, yEndpoint.x) && near(affine.d, yEndpoint.y) && near(affine.e, 0) && near(affine.f, 0));
    }

    void snapAndCancel() {
        TriangleCanvas canvas;
        const auto flame = singleTriangle();
        prepare(canvas, flame);
        canvas.setGridSpacing(.25);
        canvas.setSnap(true);
        // Invalid spacing must retain the usable grid while a real gesture snaps.
        canvas.setGridSpacing(0);
        canvas.setGridSpacing(std::numeric_limits<double>::quiet_NaN());
        QCoreApplication::processEvents();
        QSignalSpy commits(&canvas, &TriangleCanvas::affineCommitted);
        QSignalSpy canceled(&canvas, &TriangleCanvas::editCanceled);
        QSignalSpy previews(&canvas, &TriangleCanvas::affinePreview);
        drag(canvas, {0, 0}, {.37, .41});
        QCOMPARE(commits.count(), 1);
        auto affine = committed(commits);
        QVERIFY(near(affine.e, .25) && near(affine.f, .5));

        canvas.setFlame(flame, 0, false);
        canvas.setSnap(false);
        commits.clear();
        moveWhilePressed(canvas, {1, 0}, {1.5, .5});
        QVERIFY(!previews.isEmpty());
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, canvas.toScreen({1.5, .5}).toPoint());
        QCOMPARE(canceled.count(), 1);
        QCOMPARE(commits.count(), 0);

        // The canceled basis must be restored before the following translation.
        drag(canvas, {0, 0}, {.5, .5});
        QCOMPARE(commits.count(), 1);
        affine = committed(commits);
        QVERIFY(near(affine.a, 1) && near(affine.b, 0) && near(affine.c, 0) && near(affine.d, 1));
        QVERIFY(near(affine.e, .5) && near(affine.f, .5));
    }

    void worldCoordinatesPanAndZoom() {
        TriangleCanvas canvas;
        prepare(canvas, singleTriangle());
        for (Point point : {Point{0, 0}, Point{-1.25, 3.5}, Point{8.125, -.3125}}) {
            const auto restored = canvas.fromScreen(canvas.toScreen(point));
            QVERIFY(near(point.x, restored.x) && near(point.y, restored.y));
        }
        const auto origin = canvas.toScreen({0, 0});
        const QPoint start(250, 180), end(300, 220);
        QTest::mousePress(&canvas, Qt::MiddleButton, Qt::NoModifier, start);
        QTest::mouseMove(&canvas, end);
        QTest::mouseRelease(&canvas, Qt::MiddleButton, Qt::NoModifier, end);
        QCOMPARE(canvas.toScreen({0, 0}), origin + QPointF(50, 40));

        const QPoint anchor(210, 150);
        const auto anchoredWorld = canvas.fromScreen(anchor);
        const double before = canvas.toScreen({1, 0}).x() - canvas.toScreen({0, 0}).x();
        QWheelEvent wheel(anchor, canvas.mapToGlobal(anchor), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&canvas, &wheel);
        const auto afterWorld = canvas.fromScreen(anchor);
        const double after = canvas.toScreen({1, 0}).x() - canvas.toScreen({0, 0}).x();
        QVERIFY(near(anchoredWorld.x, afterWorld.x) && near(anchoredWorld.y, afterWorld.y));
        QVERIFY(near(after / before, 1.18));
    }

    void emptySelectionAndCollapsedOrigin() {
        TriangleCanvas canvas;
        auto flame = singleTriangle();
        prepare(canvas, flame, -1);
        QSignalSpy selected(&canvas, &TriangleCanvas::selected);
        QSignalSpy commits(&canvas, &TriangleCanvas::affineCommitted);
        drag(canvas, {0, 0}, {.5, .5});
        QCOMPARE(commits.count(), 1);
        QVERIFY(!selected.isEmpty());
        QCOMPARE(selected.back().at(0).toInt(), 0);

        flame.transforms[0].pre = {0, 0, 0, 0, 0, 0};
        canvas.setFlame(flame, 0, false);
        commits.clear();
        drag(canvas, {0, 0}, {.5, .5});
        QCOMPARE(commits.count(), 1);
        const auto affine = committed(commits);
        QVERIFY(near(affine.a, 0) && near(affine.b, 0) && near(affine.c, 0) && near(affine.d, 0));
        QVERIFY(near(affine.e, .5) && near(affine.f, .5));
    }
};

QTEST_MAIN(CanvasTests)
#include "canvas_tests.moc"
