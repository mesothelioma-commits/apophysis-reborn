#include "editor/document_session.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace apo;
using namespace apo::editor;

namespace {
std::string fixture() {
    return "<?xml version='1.0' encoding='ISO-8859-1'?>\r\n"
           "<Flames name='Library'>\r\n"
           "<!-- owner caf\xe9 -->\r\n"
           "<flame name='Caf\xe9 &amp; light' size='80 40' scale='6'>"
           "<xform linear='1' weight='1' coefs='0 0 0 0 0 0'/>"
           "<color index='0' rgb='255 80 0'/></flame>\r\n"
           "<flame name='Vendor entry' custom='retain &amp; keep'>"
           "<history><entry when='old'/></history></flame>\r\n"
           "</Flames>\r\n";
}

void writeBytes(const QString& path, const QByteArray& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw std::runtime_error("Could not write session test fixture");
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Could not read session test fixture");
    return file.readAll();
}

QByteArray asBytes(const std::string& value) {
    return QByteArray(value.data(), qsizetype(value.size()));
}

void renameFirst(DocumentSession& session, const std::string& name) {
    session.change(QStringLiteral("Rename flame"), [&](LegacyDocument& document) {
        LegacyEdits edits;
        edits.name = name;
        document.edit(0, edits);
    });
}
}

class DocumentSessionTests : public QObject {
    Q_OBJECT
private:
    QString recoveryRoot_;
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("Apophysis Reborn Tests"));
        QCoreApplication::setApplicationName(QStringLiteral("session-") +
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        recoveryRoot_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }

    void cleanupTestCase() {
        QDir(recoveryRoot_).removeRecursively();
    }

    void rollbackAndExactUndo() {
        DocumentSession session;
        const auto original = fixture();
        session.loadBytes(original);
        QVERIFY(session.hasDocument());
        QVERIFY(!session.isModified());
        QSignalSpy changes(&session, &DocumentSession::changed);
        session.selectFlame(1);
        session.change(QStringLiteral("Rename second"), [](LegacyDocument& document) {
            LegacyEdits edits;
            edits.name = "Changed <vendor>";
            document.edit(1, edits);
        });
        const auto edited = session.document().bytes();
        QVERIFY(edited != original);
        QVERIFY(edited.find("custom='retain &amp; keep'") != std::string::npos);
        QVERIFY(session.isModified());
        QCOMPARE(session.undoStack().count(), 1);
        session.selectFlame(0);
        session.undoStack().undo();
        QVERIFY(session.document().bytes() == original);
        QCOMPARE(session.selectedFlame(), 1);
        QVERIFY(!session.isModified());
        session.undoStack().redo();
        QVERIFY(session.document().bytes() == edited);
        QVERIFY(session.isModified());

        const auto count = session.undoStack().count();
        const auto index = session.undoStack().index();
        QVERIFY_EXCEPTION_THROWN(session.change(QStringLiteral("Fail after edit"),
            [](LegacyDocument& document) {
                LegacyEdits edits;
                edits.scale = 9.25;
                document.edit(0, edits);
                throw std::runtime_error("Deliberate mutation failure");
            }), std::runtime_error);
        QVERIFY(session.document().bytes() == edited);
        QCOMPARE(session.undoStack().count(), count);
        QCOMPARE(session.undoStack().index(), index);
        QVERIFY_EXCEPTION_THROWN(session.loadBytes("<flame broken"), std::runtime_error);
        QVERIFY(session.document().bytes() == edited);
        QCOMPARE(session.selectedFlame(), 1);

        session.change(QStringLiteral("No change"), [](LegacyDocument& document) {
            LegacyEdits edits;
            edits.name = "Changed <vendor>";
            document.edit(1, edits);
        });
        QCOMPARE(session.undoStack().count(), count);
        QVERIFY(changes.count() >= 3);
    }

    void sourceEncodingAndSaveCopy() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString source = folder.filePath(QStringLiteral("original caf\u00e9.flame"));
        const QString copy = folder.filePath(QStringLiteral("saved copy.flame"));
        writeBytes(source, asBytes(fixture()));
        DocumentSession session;
        session.load(source);
        session.save(source);
        QCOMPARE(readBytes(source), asBytes(fixture()));
        renameFirst(session, "Renamed & light");
        const auto edited = asBytes(session.document().bytes());
        QVERIFY(edited.contains("encoding='ISO-8859-1'"));
        QVERIFY(edited.contains("<!-- owner caf\xe9 -->\r\n"));
        QVERIFY(edited.contains("name='Renamed &amp; light'"));
        session.save(copy);
        QCOMPARE(readBytes(copy), edited);
        QCOMPARE(readBytes(source), asBytes(fixture()));
        QCOMPARE(session.path(), QFileInfo(copy).absoluteFilePath());
        QVERIFY(!session.isModified());
        session.undoStack().undo();
        QVERIFY(session.isModified());
        QVERIFY(session.document().bytes() == fixture());
        session.undoStack().redo();
        QVERIFY(!session.isModified());
    }

    void externalChangesAndExplicitReplacement() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString source = folder.filePath(QStringLiteral("original.flame"));
        const QString copy = folder.filePath(QStringLiteral("my copy.flame"));
        writeBytes(source, asBytes(fixture()));
        DocumentSession session;
        session.load(source);
        renameFirst(session, "Local edit");
        const auto local = asBytes(session.document().bytes());
        const QByteArray external("<flame name='External edit'/>");
        writeBytes(source, external);
        QVERIFY_EXCEPTION_THROWN(session.save(source), FileConflict);
        QCOMPARE(readBytes(source), external);
        QCOMPARE(asBytes(session.document().bytes()), local);
        QVERIFY(session.isModified());
        session.save(copy);
        QCOMPARE(readBytes(copy), local);
        QCOMPARE(readBytes(source), external);

        // A fresh session recognizes replacement authorization for its own path.
        session.load(source);
        renameFirst(session, "Explicit replacement");
        writeBytes(source, "<flame name='Changed again'/>");
        QVERIFY_EXCEPTION_THROWN(session.save(source), FileConflict);
        session.save(source, true);
        QCOMPARE(readBytes(source), asBytes(session.document().bytes()));
        QVERIFY(!session.isModified());

        renameFirst(session, "After deletion");
        QVERIFY(QFile::remove(source));
        QVERIFY_EXCEPTION_THROWN(session.save(source), FileConflict);
        QVERIFY(!QFileInfo::exists(source));
        session.save(source, true);
        QVERIFY(QFileInfo::exists(source));
    }

    void relativeLoadedPathStillDetectsDeletion() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString absolute = folder.filePath(QStringLiteral("relative.flame"));
        const QString relative = QDir::current().relativeFilePath(absolute);
        writeBytes(absolute, asBytes(fixture()));
        DocumentSession session;
        session.loadBytes(fixture(), relative);
        renameFirst(session, "Locally changed");
        QVERIFY(QFile::remove(absolute));
        QVERIFY_EXCEPTION_THROWN(session.save(relative), FileConflict);
        QVERIFY(!QFileInfo::exists(absolute));
    }

    void recoverClosedSessionAndRejectLiveOwner() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString source = folder.filePath(QStringLiteral("source.flame"));
        const QString destination = folder.filePath(QStringLiteral("recovered.flame"));
        writeBytes(source, asBytes(fixture()));
        QString recovery;
        QByteArray recoveredBytes;
        {
            DocumentSession owner;
            owner.load(source);
            renameFirst(owner, "Recovered edits");
            recovery = owner.writeRecovery();
            recoveredBytes = asBytes(owner.document().bytes());
            QVERIFY(QFileInfo::exists(recovery + ".lock"));

            DocumentSession other;
            other.loadBytes(fixture());
            renameFirst(other, "Independent document");
            other.selectFlame(1);
            const auto unchanged = asBytes(other.document().bytes());
            const auto otherRecovery = other.writeRecovery();
            const auto undoCount = other.undoStack().count();
            QVERIFY_EXCEPTION_THROWN(other.recover(recovery), std::runtime_error);
            QCOMPARE(asBytes(other.document().bytes()), unchanged);
            QCOMPARE(other.recoveryPath(), otherRecovery);
            QCOMPARE(other.selectedFlame(), 1);
            QCOMPARE(other.undoStack().count(), undoCount);
            QVERIFY(other.isModified());
            QVERIFY(QFileInfo::exists(otherRecovery));
            other.discardRecovery();
        }
        // Session destruction releases its lock while retaining unsaved recovery data.
        QVERIFY(QFileInfo::exists(recovery));
        QVERIFY(QFileInfo::exists(recovery + ".json"));
        QVERIFY(!QFileInfo::exists(recovery + ".lock"));

        DocumentSession recovered;
        recovered.recover(recovery);
        QCOMPARE(asBytes(recovered.document().bytes()), recoveredBytes);
        QCOMPARE(recovered.recoveryPath(), recovery);
        QVERIFY(recovered.path().isEmpty()); // Recovery saves through an explicit destination choice.
        QVERIFY(recovered.isModified());
        QVERIFY(QFileInfo::exists(recovery + ".lock"));
        QCOMPARE(recovered.writeRecovery(), recovery);
        recovered.save(destination);
        QCOMPARE(readBytes(destination), recoveredBytes);
        QCOMPARE(readBytes(source), asBytes(fixture()));
        QVERIFY(!recovered.isModified());
        QVERIFY(recovered.recoveryPath().isEmpty());
        QVERIFY(!QFileInfo::exists(recovery));
        QVERIFY(!QFileInfo::exists(recovery + ".json"));
        QVERIFY(!QFileInfo::exists(recovery + ".lock"));
    }

    void recoveryAndCleanup() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString source = folder.filePath(QStringLiteral("original.flame"));
        writeBytes(source, asBytes(fixture()));
        DocumentSession session;
        session.load(source);
        QVERIFY(session.writeRecovery().isEmpty());
        renameFirst(session, "Recovery one");
        const QString recovery = session.writeRecovery();
        QVERIFY(!recovery.isEmpty());
        QCOMPARE(readBytes(recovery), asBytes(session.document().bytes()));
        const auto metadata = QJsonDocument::fromJson(readBytes(recovery + ".json")).object();
        QCOMPARE(metadata.value("source").toString(), session.path());
        const auto originalHash = QCryptographicHash::hash(asBytes(fixture()), QCryptographicHash::Sha256).toHex();
        QCOMPARE(metadata.value("original_sha256").toString(), QString::fromLatin1(originalHash));
        renameFirst(session, "Recovery two");
        QCOMPARE(session.writeRecovery(), recovery);
        QCOMPARE(readBytes(recovery), asBytes(session.document().bytes()));

        session.undoStack().undo();
        session.undoStack().undo();
        QVERIFY(!session.isModified());
        QVERIFY(session.writeRecovery().isEmpty());
        QVERIFY(!QFileInfo::exists(recovery));
        QVERIFY(!QFileInfo::exists(recovery + ".json"));

        renameFirst(session, "Save recovery");
        const auto secondRecovery = session.writeRecovery();
        QVERIFY(QFileInfo::exists(secondRecovery));
        session.save(source);
        QVERIFY(!QFileInfo::exists(secondRecovery));
        QVERIFY(!QFileInfo::exists(secondRecovery + ".json"));
        QVERIFY(session.recoveryPath().isEmpty());

        renameFirst(session, "Discard recovery");
        const auto thirdRecovery = session.writeRecovery();
        session.discardRecovery();
        QVERIFY(!QFileInfo::exists(thirdRecovery));
        QVERIFY(!QFileInfo::exists(thirdRecovery + ".json"));
        QVERIFY(session.recoveryPath().isEmpty());
        QVERIFY(session.isModified());
    }
};

QTEST_GUILESS_MAIN(DocumentSessionTests)
#include "document_session_tests.moc"
