#include "build_info.hpp"
#include "editor/support_dialogs.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUrl>

using namespace apo;
using namespace apo::editor;

class SupportEditorTests : public QObject {
  Q_OBJECT
private slots:
  void manualIsEmbeddedAndSearchable() {
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const auto old = QDir::currentPath();
    QVERIFY(QDir::setCurrent(elsewhere.path()));
    UserManualDialog dialog;
    QVERIFY(QDir::setCurrent(old));
    auto browser = dialog.findChild<QTextBrowser *>("manualBrowser");
    auto search = dialog.findChild<QLineEdit *>("manualSearch");
    auto next = dialog.findChild<QPushButton *>("manualNext");
    QVERIFY(browser && search && next);
    QVERIFY(!browser->openLinks() && !browser->openExternalLinks());
    QVERIFY(browser->toPlainText().contains("Apophysis Reborn"));
    QVERIFY(browser->toPlainText().contains("checkpoint", Qt::CaseInsensitive));
    dialog.show();
    search->setText("checkpoint");
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(browser->textCursor().selectedText().toLower(), QString("checkpoint"));
    search->setFocus();
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(dialog.isVisible());
    QCOMPARE(browser->textCursor().selectedText().toLower(), QString("checkpoint"));
    search->setText("text-that-is-not-in-this-manual-1234567");
    QTest::mouseClick(next, Qt::LeftButton);
    QCOMPARE(dialog.findChild<QLabel *>("manualSearchStatus")->text(), QString("No matching text in the manual."));
    const auto contents = browser->toPlainText();
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Q_ARG(QUrl, QUrl("file:///unrelated-private-file"))));
    QVERIFY(QMetaObject::invokeMethod(browser, "anchorClicked", Q_ARG(QUrl, QUrl("https://example.invalid/"))));
    QCOMPARE(browser->toPlainText(), contents);
  }
  void reportPreservesActualAndRequestedRendererSeparately() {
    PreviewSettings settings;
    settings.backend = RenderBackend::vulkan;
    settings.device = "device \"quoted\" / \xe6\x9d\xb1\xe4\xba\xac";
    settings.allow_cpu_fallback = true;
    settings.sample_limit = UINT64_MAX;
    const auto report = system_information(settings, "CPU", "CPU", "Selected device unavailable");
    const auto restored = QJsonDocument::fromJson(QJsonDocument(report).toJson()).object();
    QCOMPARE(restored, report);
    QCOMPARE(report.value("build").toObject().value("version").toString(), QString::fromLatin1(application_version()));
    const auto preview = report.value("preview").toObject();
    QCOMPARE(preview.value("sample_limit").toString(), QString("18446744073709551615"));
    QCOMPARE(preview.value("requested_device").toString(), QString::fromStdString(settings.device));
    QCOMPARE(preview.value("last_reported_backend").toString(), QString("CPU"));
    QCOMPARE(preview.value("last_fallback_reason").toString(), QString("Selected device unavailable"));
    QVERIFY(!report.contains("project") && !report.contains("environment") && !report.contains("paths"));
  }
  void reportCopyAndAtomicSave() {
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const auto report = system_information(PreviewSettings{}, "CPU", "CPU", {});
    SystemInformationDialog dialog(report);
    dialog.show();
    auto text = dialog.findChild<QPlainTextEdit *>("systemInformationText");
    auto copy = dialog.findChild<QPushButton *>("copySystemInformation");
    QVERIFY(text && copy && text->isReadOnly());
    const auto previousClipboard = QApplication::clipboard()->text();
    QTest::mouseClick(copy, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), text->toPlainText());
    QApplication::clipboard()->setText(previousClipboard);
    const auto jsonPath = folder.path() + "/report.json";
    const auto textPath = folder.path() + "/report.txt";
    dialog.saveReport(jsonPath, true); dialog.saveReport(textPath, false);
    QFile json(jsonPath), plain(textPath);
    QVERIFY(json.open(QIODevice::ReadOnly) && plain.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(json.readAll()).object(), report);
    QCOMPARE(QString::fromUtf8(plain.readAll()), text->toPlainText());
    QVERIFY_THROWS_EXCEPTION(std::exception, dialog.saveReport(folder.path(), true));
  }
};
QTEST_MAIN(SupportEditorTests)
#include "support_editor_tests.moc"
