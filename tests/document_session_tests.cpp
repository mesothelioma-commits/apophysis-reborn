#include "editor/document_session.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

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

// Bounded injected work makes GUI responsiveness and teardown deterministic;
// no actual slow drive or device-specific timing is needed for these checks.
struct RecoveryGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::atomic<unsigned> entered = 0, finished = 0;
    std::atomic<bool> timedOut = false;
    void wait() {
        ++entered;
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; }))
            timedOut = true;
        ++finished;
    }
    void release() {
        { std::lock_guard lock(mutex); released = true; }
        changed.notify_all();
    }
};
struct ReleaseRecoveryGate {
    std::shared_ptr<RecoveryGate> gate;
    ~ReleaseRecoveryGate() { gate->release(); }
};
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

    void libraryChangesRestoreBeforeAndAfterSelection() {
        DocumentSession session;
        const auto original = fixture();
        session.loadBytes(original);
        session.selectFlame(1);
        auto generated = demo_flame();
        generated.name = "Generated entry";
        session.change(QStringLiteral("Append generated flame"), [&](LegacyDocument& document) {
            document.append_flame(generated);
        }, 0, false, 2);
        const auto appended = session.document().bytes();
        QCOMPARE(session.document().entries().size(), std::size_t(3));
        QCOMPARE(session.selectedFlame(), 2);
        QCOMPARE(session.selectedTransform(), 0);
        QVERIFY(!session.isFinalTransform());
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), original);
        QCOMPARE(session.selectedFlame(), 1);
        QVERIFY(!session.isModified());
        session.undoStack().redo();
        QCOMPARE(session.document().bytes(), appended);
        QCOMPARE(session.selectedFlame(), 2);

        session.selectFlame(0);
        session.change(QStringLiteral("Duplicate source entry"), [](LegacyDocument& document) {
            document.duplicate_flame(0, "Duplicate \xc3\xa9");
        }, 0, false, 3);
        const auto duplicated = session.document().bytes();
        QCOMPARE(session.selectedFlame(), 3);
        QCOMPARE(session.document().entries()[3].name, std::string("Duplicate \xc3\xa9"));
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), appended);
        QCOMPARE(session.selectedFlame(), 0);
        session.undoStack().redo();
        QCOMPARE(session.document().bytes(), duplicated);
        QCOMPARE(session.selectedFlame(), 3);

        session.change(QStringLiteral("Delete selected entry"), [](LegacyDocument& document) {
            document.remove_flame(3);
        }, 0, false, 2);
        const auto deleted = session.document().bytes();
        QCOMPARE(session.document().entries().size(), std::size_t(3));
        QCOMPARE(session.selectedFlame(), 2);
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), duplicated);
        QCOMPARE(session.selectedFlame(), 3);
        session.undoStack().redo();
        QCOMPARE(session.document().bytes(), deleted);
        QCOMPARE(session.selectedFlame(), 2);
        QCOMPARE(session.undoStack().count(), 3);
        session.undoStack().undo();
        session.undoStack().undo();
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), original);
        QCOMPARE(session.selectedFlame(), 1);
        QVERIFY(!session.isModified());
        QVERIFY(session.document().bytes().find("<!-- owner caf\xe9 -->\r\n") != std::string::npos);
    }

    void invalidNextFlameRollsBackMutationAndHistory() {
        DocumentSession session;
        session.loadBytes(fixture());
        session.selectFlame(1);
        const auto before = session.document().bytes();
        QSignalSpy changes(&session, &DocumentSession::changed);
        QVERIFY_EXCEPTION_THROWN(session.change(QStringLiteral("Invalid append destination"),
            [](LegacyDocument& document) {
                document.append_flame(demo_flame());
            }, 0, false, 3), std::invalid_argument);
        QCOMPARE(session.document().bytes(), before);
        QCOMPARE(session.selectedFlame(), 1);
        QCOMPARE(session.undoStack().count(), 0);
        QCOMPARE(session.undoStack().index(), 0);
        QCOMPARE(changes.count(), 0);
        QVERIFY(!session.isModified());

        renameFirst(session, "Keep this edit");
        const auto edited = session.document().bytes();
        const auto undoCount = session.undoStack().count();
        const auto undoIndex = session.undoStack().index();
        QVERIFY_EXCEPTION_THROWN(session.change(QStringLiteral("Invalid deletion destination"),
            [](LegacyDocument& document) {
                document.remove_flame(1);
            }, 0, false, 1), std::invalid_argument);
        QCOMPARE(session.document().bytes(), edited);
        QCOMPARE(session.selectedFlame(), 1);
        QCOMPARE(session.undoStack().count(), undoCount);
        QCOMPARE(session.undoStack().index(), undoIndex);
        QVERIFY(session.isModified());
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), before);
        QCOMPARE(session.selectedFlame(), 1);
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
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(recovery + ".lock"), 5000);

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
        QTRY_VERIFY_WITH_TIMEOUT(!recovered.isRecoveryBusy(), 5000);
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
        QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy(), 5000);
        QVERIFY(!QFileInfo::exists(secondRecovery));
        QVERIFY(!QFileInfo::exists(secondRecovery + ".json"));
        QVERIFY(session.recoveryPath().isEmpty());

        renameFirst(session, "Discard recovery");
        const auto thirdRecovery = session.writeRecovery();
        session.discardRecovery();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy(), 5000);
        QVERIFY(!QFileInfo::exists(thirdRecovery));
        QVERIFY(!QFileInfo::exists(thirdRecovery + ".json"));
        QVERIFY(session.recoveryPath().isEmpty());
        QVERIFY(session.isModified());
    }

    void recoveryRunsOffGuiAndCoalescesLatestSnapshot() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        ProjectFile project;
        project.library_bytes = fixture();
        project.metadata["vendor"] = "preserve this";
        project.animation_data = "opaque future animation";
        const auto source = folder.filePath("source.apoproject");
        writeBytes(source, asBytes(serialize_project(project)));
        DocumentSession session;
        session.load(source);
        renameFirst(session, "First snapshot");
        QSignalSpy saved(&session, &DocumentSession::recoverySaved);
        QSignalSpy failed(&session, &DocumentSession::recoveryFailed);
        auto gate = std::make_shared<RecoveryGate>();
        ReleaseRecoveryGate release{gate};
        auto preparationCalls = std::make_shared<std::atomic<unsigned>>(0);
        auto wrongThread = std::make_shared<std::atomic<bool>>(false);
        const auto guiThread = std::this_thread::get_id();
        QElapsedTimer capture;
        capture.start();
        session.requestRecovery(session.projectSnapshot(),
            [gate, preparationCalls, wrongThread, guiThread](ProjectFile &snapshot) {
                ++*preparationCalls;
                *wrongThread = std::this_thread::get_id() == guiThread;
                gate->wait();
                snapshot.preview_settings["prepared"] = "first";
            });
        QVERIFY2(capture.elapsed() < 500, "Submitting recovery waited for background preparation");
        const auto recovery = session.recoveryPath();
        QVERIFY(!recovery.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(gate->entered.load(), 1U, 3000);
        QVERIFY(session.isRecoveryBusy());
        unsigned heartbeats = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; });
        heartbeat.start();
        QTRY_VERIFY_WITH_TIMEOUT(heartbeats >= 8, 1000);
        // The worker is blocked. Each new request replaces its one pending job.
        for (unsigned i = 0; i < 12; ++i) {
            renameFirst(session, "Latest snapshot " + std::to_string(i));
            auto snapshot = session.projectSnapshot();
            snapshot.workspace["snapshot"] = std::to_string(i);
            session.requestRecovery(std::move(snapshot), [preparationCalls](ProjectFile &p) {
                ++*preparationCalls;
                p.preview_settings["prepared"] = "latest";
            });
        }
        QCOMPARE(preparationCalls->load(), 1U);
        gate->release();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!saved.empty(), 3000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(preparationCalls->load(), 2U);
        QVERIFY(!wrongThread->load());
        QVERIFY(!gate->timedOut.load());
        const auto recovered = parse_project(readBytes(recovery).toStdString());
        QCOMPARE(recovered.library_bytes, session.document().bytes());
        QCOMPARE(recovered.workspace.at("snapshot"), std::string("11"));
        QCOMPARE(recovered.preview_settings.at("prepared"), std::string("latest"));
        QCOMPARE(recovered.metadata.at("vendor"), std::string("preserve this"));
        QCOMPARE(recovered.animation_data, project.animation_data);
        QVERIFY(recovered.library_bytes.find("<!-- owner caf\xe9 -->\r\n") != std::string::npos);
        const auto metadata = QJsonDocument::fromJson(readBytes(recovery + ".json")).object();
        QCOMPARE(metadata.value("source").toString(), source);
        const auto originalHash = QCryptographicHash::hash(readBytes(source), QCryptographicHash::Sha256).toHex();
        QCOMPARE(metadata.value("original_sha256").toString(), QString::fromLatin1(originalHash));
        QCOMPARE(session.projectSnapshot().workspace.count("snapshot"), std::size_t(0));
        session.discardRecovery();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy(), 5000);
        QVERIFY(!QFileInfo::exists(recovery));
    }

    void openingNewDocumentInvalidatesBlockedRecovery() {
        ProjectFile oldProject;
        oldProject.library_bytes = fixture();
        oldProject.metadata["generation"] = "old";
        DocumentSession session;
        session.loadBytes(serialize_project(oldProject), {}, true);
        auto gate = std::make_shared<RecoveryGate>();
        ReleaseRecoveryGate release{gate};
        QSignalSpy saved(&session, &DocumentSession::recoverySaved);
        QSignalSpy failed(&session, &DocumentSession::recoveryFailed);
        session.requestRecovery(session.projectSnapshot(), [gate](ProjectFile &) { gate->wait(); });
        const auto obsolete = session.recoveryPath();
        QTRY_COMPARE_WITH_TIMEOUT(gate->entered.load(), 1U, 3000);
        ProjectFile nextProject;
        nextProject.library_bytes = "<flame name='New document'><xform linear='1'/></flame>";
        nextProject.metadata["generation"] = "new";
        session.loadBytes(serialize_project(nextProject), {}, true);
        QVERIFY(session.recoveryPath().isEmpty());
        session.requestRecovery();
        const auto current = session.recoveryPath();
        QVERIFY(current != obsolete);
        gate->release();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 3000);
        QCOMPARE(saved.front().front().toString(), current);
        QCOMPARE(failed.size(), 0);
        QVERIFY(!QFileInfo::exists(obsolete));
        QVERIFY(!QFileInfo::exists(obsolete + ".json"));
        QVERIFY(!QFileInfo::exists(obsolete + ".lock"));
        const auto recovered = parse_project(readBytes(current).toStdString());
        QCOMPARE(recovered.library_bytes, nextProject.library_bytes);
        QCOMPARE(recovered.metadata.at("generation"), std::string("new"));
        QVERIFY(!gate->timedOut.load());
        session.discardRecovery();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy(), 5000);
    }

    void savingInvalidatesRecoveryAndCloseDoesNotJoinPreparation() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        ProjectFile project;
        project.library_bytes = fixture();
        auto session = std::make_unique<DocumentSession>();
        session->loadBytes(serialize_project(project), {}, true);
        auto gate = std::make_shared<RecoveryGate>();
        ReleaseRecoveryGate release{gate};
        auto witness = std::make_shared<int>(0);
        std::weak_ptr<int> workLifetime = witness;
        session->requestRecovery(session->projectSnapshot(),
            [gate, witness](ProjectFile &) { gate->wait(); });
        witness.reset();
        const auto recovery = session->recoveryPath();
        QTRY_COMPARE_WITH_TIMEOUT(gate->entered.load(), 1U, 3000);
        QElapsedTimer elapsed;
        elapsed.start();
        const auto destination = folder.filePath("saved.apoproject");
        session->save(destination);
        QVERIFY(session->recoveryPath().isEmpty());
        session.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Saving/closing waited for stale recovery preparation");
        QCOMPARE(gate->finished.load(), 0U);
        gate->release();
        QTRY_VERIFY_WITH_TIMEOUT(workLifetime.expired(), 5000);
        // A stale worker may never recreate its retired path after save/close.
        QVERIFY(!QFileInfo::exists(recovery));
        QVERIFY(!QFileInfo::exists(recovery + ".json"));
        QVERIFY(!QFileInfo::exists(recovery + ".lock"));
        QCOMPARE(parse_project(readBytes(destination).toStdString()).library_bytes, project.library_bytes);
        QVERIFY(!gate->timedOut.load());
    }

    void recoveryCompletionCanEnqueueAgainAndDeleteSession() {
        auto session = std::make_unique<DocumentSession>();
        session->loadBytes(fixture(), {}, true);
        QString recovery;
        unsigned completions = 0;
        connect(session.get(), &DocumentSession::recoverySaved, this, [&](const QString &path) {
            recovery = path;
            if (++completions == 1) {
                renameFirst(*session, "Enqueued by completion");
                session->requestRecovery();
            } else {
                session.reset();
            }
        });
        session->requestRecovery();
        QTRY_VERIFY_WITH_TIMEOUT(!session, 10000);
        QCOMPARE(completions, 2U);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(recovery + ".lock"), 5000);
        QVERIFY(readBytes(recovery).contains("Enqueued by completion"));
        QVERIFY(QFile::remove(recovery));
        QVERIFY(QFile::remove(recovery + ".json"));
    }

    void failedRecoveryCompletionCanDeleteSession() {
        auto session = std::make_unique<DocumentSession>();
        ProjectFile project;
        project.library_bytes = fixture();
        session->loadBytes(serialize_project(project), {}, true);
        QString error;
        connect(session.get(), &DocumentSession::recoveryFailed, this, [&](const QString &message) {
            error = message;
            session.reset();
        });
        session->requestRecovery(session->projectSnapshot(), [](ProjectFile &) {
            throw std::runtime_error("Injected preparation failure");
        });
        QTRY_VERIFY_WITH_TIMEOUT(!session, 5000);
        QCOMPARE(error, QString("Injected preparation failure"));
    }

    void recoverySnapshotCaptureIsBoundedAndPreservesRetainedData() {
        ProjectFile project;
        project.library_bytes = fixture();
        project.library_bytes.insert(project.library_bytes.find("</Flames>"),
            "<!--" + std::string(8 * 1024 * 1024, 'p') + "-->\r\n");
        project.metadata["retained"] = "metadata";
        project.animation_data = "opaque timeline";
        DocumentSession session;
        session.loadBytes(serialize_project(project), {}, true);
        QElapsedTimer elapsed;
        elapsed.start();
        auto snapshot = session.projectSnapshot();
        const auto captureNs = elapsed.nsecsElapsed();
        qInfo("Recovery capture: %llu library bytes, %.3f ms (before encoding/writing)",
              static_cast<unsigned long long>(snapshot.library_bytes.size()),
              double(captureNs) / 1000000.0);
        QVERIFY2(captureNs < 500000000, "An 8 MiB immutable snapshot took more than 500 ms to capture");
        QCOMPARE(snapshot.library_bytes, project.library_bytes);
        QCOMPARE(snapshot.metadata, project.metadata);
        QCOMPARE(snapshot.animation_data, project.animation_data);
        QVERIFY(snapshot.retained_source != nullptr);
    }

    void pluginReferencesUseRetainedIdentityWithoutLoadingCode() {
        const auto document = LegacyDocument::parse(
            "<?xml version='1.0' encoding='ISO-8859-1'?>"
            "<Flames><flame name='Caf\xe9' vendor='unsupported'>"
            "<xform linear='1' reborn_wave='0.2' reborn_plugin_id_reborn_wave='wave-id' "
            "reborn_plugin_version_reborn_wave='2'/></flame>"
            "<flame name='Final'><xform linear='1'/><finalxform old_wave='0'/>"
            "<history><xform history_only='1'/></history></flame></Flames>");
        QVERIFY(document.references_plugin("reborn_wave", "wave-id", "2"));
        QVERIFY(document.references_plugin("reborn_wave", "wave-id", "2", 0));
        QVERIFY(!document.references_plugin("reborn_wave", "other-id", "2"));
        QVERIFY(!document.references_plugin("reborn_wave", "wave-id", "1"));
        QVERIFY(!document.references_plugin("reborn_wave", "wave-id", "2", 1));
        QVERIFY(!document.references_plugin("wave", "wave-id", "2"));
        QVERIFY(document.references_plugin("old_wave", "legacy-id", "1", 1));
        QVERIFY(!document.references_plugin("history_only", "legacy-id", "1"));
        QCOMPARE(document.entries().front().name, std::string("Caf\xc3\xa9"));
    }
};

QTEST_GUILESS_MAIN(DocumentSessionTests)
#include "document_session_tests.moc"
