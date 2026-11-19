#include "editor/flame_favorites.hpp"
#include "editor/document_session.hpp"
#include "flame_io.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

using namespace apo;
using namespace apo::editor;

namespace {
const auto latin = std::string("<?xml version='1.0' encoding='windows-1252'?>\n<flames>")
    + "<flame name='Caf\xe9' odd='\x80'><history><!-- Caf\xe9 --></history></flame>"
      "<flame name='Other'/></flames>";

std::string utf16(bool little, const QString &text) {
    std::string result = little ? "\xff\xfe" : "\xfe\xff";
    for (QChar c : text) {
        auto word = c.unicode();
        result += static_cast<char>(little ? word : word >> 8);
        result += static_cast<char>(little ? word >> 8 : word);
    }
    return result;
}
}

class FlameFavoritesTests : public QObject {
    Q_OBJECT
private slots:
    void favoriteSurvivesSourceAndSession() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto sourcePath = temp.filePath("Original.flame");
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        auto bytes = serialize_flame(demo_flame());
        QCOMPARE(source.write(QByteArray::fromStdString(bytes)), qint64(bytes.size()));
        source.close();
        auto storePath = temp.filePath("favorites");
        DocumentSession session;
        session.load(sourcePath);
        session.change("Change name", [](LegacyDocument &doc) {
            LegacyEdits edits;
            edits.name = "Copper study";
            doc.edit(0, edits);
        });
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(Qt::darkYellow);
        QTimer::singleShot(0, [&] {
            auto dlg = QApplication::activeModalWidget();
            QVERIFY(dlg);
            dlg->findChild<QLineEdit *>("favoriteTags")->setText(" Copper, warm, copper ");
            dlg->findChild<QPushButton *>("favoriteSave")->click();
        });
        saveSelectedFavorite(session, nullptr, image, storePath);
        QVERIFY(source.rename(temp.filePath("Moved.flame")));
        FavoriteStore reopened(storePath);
        QStringList errors;
        const auto list = reopened.entries(errors);
        QVERIFY(errors.isEmpty());
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.front().name, "Copper study");
        QCOMPARE(list.front().tags, QStringList({"Copper", "warm"}));
        QVERIFY(!QImage::fromData(list.front().thumbnail).isNull());
        const auto snapshot = reopened.snapshot(list.front().id);
        QCOMPARE(LegacyDocument::parse(snapshot).entries()[0].name, std::string("Copper study"));
        DocumentSession destination;
        destination.loadBytes(serialize_flame(demo_flame()));
        const auto before = destination.document().bytes();
        bool filtered = false;
        QTimer poll;
        connect(&poll, &QTimer::timeout, [&] {
            auto dlg = QApplication::activeModalWidget();
            auto listWidget = dlg ? dlg->findChild<QListWidget *>("favoritesList") : nullptr;
            if (!listWidget || !listWidget->count()) return;
            poll.stop();
            auto search = dlg->findChild<QLineEdit *>("favoritesSearch");
            search->setText("missing");
            filtered = listWidget->item(0)->isHidden();
            search->setText("WaRm COPPER");
            filtered = filtered && !listWidget->item(0)->isHidden();
            dlg->findChild<QPushButton *>("favoriteInsert")->click();
        });
        QTimer safety;
        safety.setSingleShot(true);
        connect(&safety, &QTimer::timeout, [] {
            if (auto d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) d->reject();
        });
        safety.start(10000);
        poll.start(10);
        browseFlameFavorites(destination, nullptr, storePath);
        QVERIFY(filtered);
        QCOMPARE(destination.document().entries().size(), std::size_t(2));
        QCOMPARE(destination.selectedFlame(), 1);
        QCOMPARE(destination.document().entries()[1].name, std::string("Copper study"));
        destination.undoStack().undo();
        QCOMPARE(destination.document().bytes(), before);
        QCOMPARE(reopened.snapshot(list.front().id), snapshot);
    }

    void concurrentCatalogEditsAndCorruptRecord() {
        QTemporaryDir temp;
        FavoriteStore first(temp.path()), second(temp.path());
        auto legacy = LegacyDocument::parse(latin);
        auto original = first.save(legacy.standalone_flame(0), {"original"}, {});
        auto other = second.save(legacy.standalone_flame(1), {"second"}, {});
        second.setTags(original, {" New ", "new", "keep"});
        QVERIFY_EXCEPTION_THROWN(first.setTags(original, {"stale"}), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(first.remove(original), std::runtime_error);
        QStringList errors;
        auto list = first.entries(errors);
        QVERIFY(errors.isEmpty());
        QCOMPARE(list.size(), 2);
        for (const auto &e : list) if (e.id == original.id) {
            QCOMPARE(e.tags, QStringList({"New", "keep"}));
            first.remove(e);
        }
        auto badPath = temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".apofavorite");
        QFile bad(badPath);
        QVERIFY(bad.open(QIODevice::WriteOnly));
        QVERIFY(bad.write("bad\n") == 4);
        bad.close();
        QCOMPARE(first.entries(errors).size(), 1);
        QVERIFY(!errors.isEmpty());
        auto doc = LegacyDocument::parse(second.snapshot(other.id));
        QCOMPARE(doc.entries().front().name, std::string("Other"));
        QVERIFY_EXCEPTION_THROWN(first.snapshot("../unrelated"), std::invalid_argument);
    }

    void retainedSourceAndDestinationEncodings() {
        const auto source = LegacyDocument::parse(latin);
        auto extracted = source.standalone_flame(0);
        auto portable = LegacyDocument::parse(extracted);
        QCOMPARE(portable.entries().front().name, std::string("Café"));
        QVERIFY(extracted.find("<!-- Café -->") != std::string::npos);
        QVERIFY(extracted.find("Other") == std::string::npos);
        auto target = LegacyDocument::parse(latin);
        target.append_entry(portable, 0);
        QCOMPARE(target.element_bytes(0), target.element_bytes(2));
        for (bool little : {true, false}) {
            const auto wide = LegacyDocument::parse(utf16(little, "<flame name='星🌠'><!-- 星🌠 --></flame>"));
            auto narrow = LegacyDocument::parse("<flame name='first'/>");
            narrow.append_entry(wide, 0);
            QCOMPARE(narrow.entries()[1].name, std::string("星🌠"));
            auto destination = LegacyDocument::parse(wide.standalone_flame(0));
            destination.append_entry(narrow, 1);
            QCOMPARE(destination.element_bytes(0), destination.element_bytes(1));
            const auto before = target.bytes();
            QVERIFY_EXCEPTION_THROWN(target.append_entry(wide, 0), std::runtime_error);
            QCOMPARE(target.bytes(), before);
        }
    }
};
QTEST_MAIN(FlameFavoritesTests)
#include "flame_favorites_tests.moc"
