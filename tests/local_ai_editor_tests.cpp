#include "ai_mcp/protocol.hpp"
#include "editor/main_window.hpp"
#include "editor/script_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDir>
#include <QUndoStack>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace apo::editor;

namespace {
class Client {
public:
    QProcess process;
    int nextId = 0;

    void write(const QJsonObject &message) {
        process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }

    QJsonObject request(const QString &method, const QJsonObject &params = {}) {
        const int id = ++nextId;
        write({{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}});
        QElapsedTimer timer;
        timer.start();
        while (!process.canReadLine() && process.state() != QProcess::NotRunning &&
               timer.elapsed() < 8000)
            QTest::qWait(5); // Deliver the local socket's events on the GUI thread.
        const auto reply = QJsonDocument::fromJson(process.readLine()).object();
        if (reply.value("id").toInt(-1) != id)
            return {};
        return reply;
    }

    QJsonObject tool(const QString &name, const QJsonObject &args = {}) {
        return request("tools/call", {{"name", name}, {"arguments", args}}).value("result").toObject();
    }

    ~Client() {
        process.closeWriteChannel();
        if (process.state() != QProcess::NotRunning && !process.waitForFinished(1500)) {
            process.kill();
            process.waitForFinished();
        }
    }
};

QJsonObject firstContent(const QJsonObject &result) {
    return result.value("content").toArray().at(0).toObject();
}

QJsonObject textData(const QJsonObject &result) {
    return QJsonDocument::fromJson(firstContent(result).value("text").toString().toUtf8()).object();
}

bool toggleConnection(MainWindow &window, bool enable) {
    bool clicked = false;
    auto *action = window.findChild<QAction *>("actionLocalAiConnection");
    if (!action)
        return false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto *button = dialog->findChild<QPushButton *>(enable ? "aiEnable" : "aiDisable");
        if (button && button->isEnabled()) {
            button->click();
            clicked = true;
        }
        dialog->accept();
    });
    action->trigger();
    return clicked;
}
}

class LocalAiEditorTests : public QObject {
    Q_OBJECT
private slots:
    void reviewCurrentSession() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
#ifdef Q_OS_WIN
        // Qt uses the Windows Known Folder API for this location. Exercise
        // its real per-user ACL only on a disposable Windows account.
        if (qEnvironmentVariable("APO_AI_ISOLATED_WINDOWS_TEST") != "1")
            QSKIP("The Windows native IPC test needs a disposable profile.");
        QVERIFY(!QFileInfo::exists(apo::ai::sessionPath()));
#else
        qputenv("XDG_CONFIG_HOME", profile.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", profile.filePath("data").toUtf8());
        QVERIFY(apo::ai::sessionPath().startsWith(profile.path() + "/"));
#endif
        QCoreApplication::setOrganizationName("ApophysisRebornTests");
        QCoreApplication::setApplicationName("LocalAiReview");

        MainWindow window(false);
        auto &session = window.session();
        auto preview = window.previewController().settings();
        preview.sample_limit = 40000;
        preview.threads = 1;
        preview.debounce_ms = 1;
        window.previewController().setSettings(preview);
        QVERIFY(window.openStarterLibrary());
        window.resize(900, 650);
        window.show();

        Client client;
        client.process.start(QCoreApplication::applicationDirPath() + "/apophysis-mcp", QStringList{});
        QVERIFY(client.process.waitForStarted());
        const auto initialized = client.request("initialize", {
            {"protocolVersion", "2025-11-25"}, {"capabilities", QJsonObject{}},
            {"clientInfo", QJsonObject{{"name", "editor-integration-test"}, {"version", "1"}}}
        });
        QVERIFY(initialized.value("result").toObject().value("capabilities").toObject().contains("tools"));
        client.write({{"jsonrpc","2.0"},{"method","notifications/initialized"}});
        QCOMPARE(client.request("tools/list").value("result").toObject().value("tools").toArray().size(), 3);
        QVERIFY(client.tool("inspect_flame").value("isError").toBool());
        QVERIFY(toggleConnection(window, true));
        const auto original = textData(client.tool("inspect_flame"));
        QCOMPARE(original.value("name").toString(), QString("Ember spiral"));
        session.selectFlame(1);
        session.change("Edit unsaved name", [](apo::LegacyDocument &doc) {
            apo::LegacyEdits edit;
            edit.name = "Unsaved fern";
            doc.edit(1, edit);
        });
        const auto baseline = session.document().bytes();
        const auto model = textData(client.tool("inspect_flame"));
        QCOMPARE(model.value("name").toString(), QString("Unsaved fern"));
        QCOMPARE(model.value("index").toInt(), 2);
        QVERIFY(model.value("flameXml").toString().contains("Unsaved fern"));
        const auto revision = model.value("revision").toString();
        QVERIFY(!revision.isEmpty() && revision != original.value("revision").toString());
        const auto stale = client.tool("get_preview", {{"revision", original.value("revision")}});
        QVERIFY(stale.value("isError").toBool());
        QImage live;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const auto image = firstContent(client.tool("get_preview", {{"revision", revision}}));
            return image.value("mimeType") == "image/png" &&
                   live.loadFromData(QByteArray::fromBase64(image.value("data").toString().toLatin1()), "PNG");
        }(), 15000);

        const QJsonObject proposal{{"revision", revision}, {"title", "Compare two colors"},
            {"script", "apo.duplicate_flame('Copper'); apo.set_transform(1,{color=0.2}); "
                       "apo.duplicate_flame('Violet'); apo.set_transform(1,{color=0.8})"}};
        QCOMPARE(textData(client.tool("propose_script", proposal)).value("status").toString(),
                 QString("awaiting_review"));
        QTRY_VERIFY(window.findChild<ScriptDialog *>("scriptDialog"));
        QPointer<ScriptDialog> review = window.findChild<ScriptDialog *>("scriptDialog");
        QVERIFY(client.tool("propose_script", proposal).value("isError").toBool());
        QCOMPARE(session.document().bytes(), baseline);
        QSignalSpy ran(review, &ScriptDialog::executionFinished);
        auto *run = review->findChild<QPushButton *>("scriptRun");
        QVERIFY(run);
        run->click();
        QTRY_VERIFY_WITH_TIMEOUT(ran.count() == 1, 10000);
        QVERIFY(ran.at(0).at(0).toBool());
        QCOMPARE(session.document().bytes(), baseline);
        auto *entries = review->findChild<QComboBox *>("aiPreviewEntry");
        auto *image = review->findChild<QLabel *>("aiPreviewImage");
        QVERIFY(entries && image);
        const int copper = entries->findText("Copper", Qt::MatchContains);
        QVERIFY(copper >= 0);
        entries->setCurrentIndex(copper);
        QTRY_VERIFY_WITH_TIMEOUT(!image->pixmap().isNull(), 10000);
        auto *apply = review->findChild<QPushButton *>("scriptApply");
        QVERIFY(apply && apply->isEnabled());
        apply->click();
        QTRY_COMPARE(session.document().entries().size(), size_t(10));
        QCOMPARE(QString::fromStdString(session.document().entries().at(session.selectedFlame()).name),
                 QString("Copper"));
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), baseline);
        QVERIFY(client.tool("propose_script", proposal).value("isError").toBool());

        // An edit between review and Apply must never replace newer user work.
        auto fresh = proposal;
        fresh["revision"] = textData(client.tool("inspect_flame")).value("revision");
        QCOMPARE(textData(client.tool("propose_script", fresh)).value("status").toString(),
                 QString("awaiting_review"));
        QTRY_VERIFY(qobject_cast<ScriptDialog *>(QApplication::activeModalWidget()));
        review = qobject_cast<ScriptDialog *>(QApplication::activeModalWidget());
        QSignalSpy secondRun(review, &ScriptDialog::executionFinished);
        review->findChild<QPushButton *>("scriptRun")->click();
        QTRY_COMPARE_WITH_TIMEOUT(secondRun.count(), 1, 10000);
        QVERIFY(secondRun.at(0).at(0).toBool());
        session.selectFlame(0);
        if (review && review->isVisible())
            review->findChild<QPushButton *>("scriptApply")->click();
        QTRY_VERIFY(!QApplication::activeModalWidget());
        QCOMPARE(session.document().bytes(), baseline);
        QVERIFY(toggleConnection(window, false));
        QVERIFY(!QFileInfo::exists(apo::ai::sessionPath()));
        QVERIFY(client.tool("inspect_flame").value("isError").toBool());
    }
};

QTEST_MAIN(LocalAiEditorTests)
#include "local_ai_editor_tests.moc"
