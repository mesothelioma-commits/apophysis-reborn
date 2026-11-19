#include "flame_favorites.hpp"
#include "background_jobs.hpp"
#include "document_session.hpp"
#include "preview_controller.hpp"

#include <QBuffer>
#include <QCache>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QSignalBlocker>
#include <QHash>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace apo::editor {
namespace {
constexpr auto suffix = ".apofavorite";
constexpr qint64 headerLimit = 512 * 1024;
const QSize thumbnailSize(144, 96);

[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

QStringList cleanTags(const QStringList &input) {
    QStringList result;
    for (const auto &part : input) {
        const auto tag = part.simplified();
        if (!tag.isEmpty() && !result.contains(tag, Qt::CaseInsensitive))
            result.append(tag);
    }
    return result;
}

QByteArray encodeThumbnail(const QImage &image) {
    if (image.isNull()) return {};
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (!image.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
             .convertToFormat(QImage::Format_RGB888).save(&buffer, "PNG"))
        throw std::runtime_error("Could not prepare the favorite thumbnail");
    return buffer.data();
}

QImage decodeThumbnail(QByteArray bytes) {
    if (bytes.isEmpty()) return {};
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > thumbnailSize.width() || size.height() > thumbnailSize.height())
        return {};
    return reader.read();
}

FavoriteEntry readHeader(QFile &file) {
    if (!file.open(QIODevice::ReadOnly)) fail(file.errorString());
    const auto line = file.readLine(headerLimit + 1);
    if (!line.endsWith('\n') || line.size() > headerLimit)
        throw std::runtime_error("Invalid favorite header");
    const auto json = QJsonDocument::fromJson(line).object();
    if (json.value("version").toInt() != 1 || !json.value("name").isString() ||
        !json.value("tags").isArray() || !json.value("thumbnail").isString())
        throw std::runtime_error("Unrecognized favorite file");
    FavoriteEntry entry;
    entry.id = QFileInfo(file).completeBaseName();
    entry.name = json.value("name").toString();
    for (const auto &tag : json.value("tags").toArray()) {
        if (!tag.isString()) throw std::runtime_error("Invalid favorite tag");
        entry.tags.append(tag.toString());
    }
    entry.thumbnail = QByteArray::fromBase64(json.value("thumbnail").toString().toLatin1());
    return entry;
}

QByteArray readSnapshot(QFile &file) {
    const auto length = file.size() - file.pos();
    if (length <= 0 || length > qint64(project_library_limit))
        throw std::runtime_error("Invalid favorite snapshot size");
    const auto bytes = file.read(length);
    if (bytes.size() != length || !file.atEnd()) fail("Cannot read the complete favorite snapshot");
    return bytes;
}

void writeRecord(const QString &path, const FavoriteEntry &entry, QByteArrayView bytes) {
    const QJsonObject json{{"version", 1}, {"name", entry.name},
                           {"tags", QJsonArray::fromStringList(entry.tags)},
                           {"thumbnail", QString::fromLatin1(entry.thumbnail.toBase64())}};
    auto header = QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
    if (header.size() > headerLimit) throw std::runtime_error("Favorite name or tags are too long");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size() ||
        file.write(bytes.data(), bytes.size()) != bytes.size() || !file.commit())
        fail("Could not save favorite: " + file.errorString());
}

FavoriteEntry checkedEntry(QFile &file, const FavoriteEntry &expected) {
    auto current = readHeader(file);
    if (current.tags != expected.tags)
        throw std::runtime_error("This favorite changed in another window. Refresh Favorites and try again.");
    return current;
}

void lockRecord(QLockFile &lock) {
    if (!lock.tryLock(0))
        throw std::runtime_error("This favorite is in use by another window. Try again in a moment.");
}

PreviewSettings thumbSettings() {
    PreviewSettings settings;
    settings.debounce_ms = 0;
    settings.sample_limit = 150000;
    settings.threads = 1;
    return settings;
}
// Decode only visible gallery tiles and bound the decoded-pixel cache.
class FavoriteDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
        return QSize(172, thumbnailSize.height() + 2 * option.fontMetrics.height() + 14);
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const QRect cell = option.rect.adjusted(3, 2, -3, -2);
        const QPalette &palette = option.palette;
        painter->save();
        painter->setClipRect(cell);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected) {
            auto highlight = palette.highlight().color();
            auto fill = highlight;
            fill.setAlpha(65);
            painter->fillRect(cell, fill);
            painter->setPen(highlight);
            painter->drawRect(cell.adjusted(0, 0, -1, -1));
        }
        const QRect imageRect(cell.left() + 4, cell.top() + 3, cell.width() - 8, thumbnailSize.height());
        auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (icon.isNull()) {
            const auto id = index.data(Qt::UserRole).toString();
            if (auto cached = cache_.object(id)) icon = *cached;
            else {
                const auto bytes = index.data(Qt::UserRole + 1).toByteArray();
                if (!bytes.isEmpty()) {
                    icon = QIcon(QPixmap::fromImage(decodeThumbnail(bytes)));
                    cache_.insert(id, new QIcon(icon));
                }
            }
        }
        if (!icon.isNull()) icon.paint(painter, imageRect, Qt::AlignCenter);
        else {
            painter->setPen(palette.color(QPalette::PlaceholderText));
            painter->drawText(imageRect, Qt::AlignCenter, "No preview");
        }
        const auto font = option.fontMetrics;
        QRect caption(cell.left() + 4, imageRect.bottom() + 3, cell.width() - 8, font.height());
        painter->setPen(palette.color(QPalette::Text));
        painter->drawText(caption, Qt::AlignCenter,
                          font.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, caption.width()));
        caption.translate(0, font.height());
        painter->setPen(palette.color(QPalette::PlaceholderText));
        painter->drawText(caption, Qt::AlignCenter,
                          font.elidedText(index.data(Qt::UserRole + 2).toString(), Qt::ElideRight, caption.width()));
        painter->restore();
    }
private:
    mutable QCache<QString, QIcon> cache_{64};
};
Q_APPLICATION_STATIC(ApplicationJobs, favoriteListingJobs)
} // namespace

FavoriteStore::FavoriteStore(const QString &directory) : directory_(directory) {
    if (directory_.isEmpty()) {
        const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (root.isEmpty()) fail("The personal data folder is unavailable");
        directory_ = QDir(root).filePath("favorites");
    }
}

QString FavoriteStore::path(const QString &id) const {
    if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id)
        throw std::invalid_argument("Invalid favorite identifier");
    return QDir(directory_).filePath(id + suffix);
}

QList<FavoriteEntry> FavoriteStore::entries(QStringList &errors, const std::function<bool()> &cancel) const {
    QList<FavoriteEntry> entries;
    QDir dir(directory_);
    if (dir.exists() && !dir.isReadable()) fail("Cannot read the Favorites folder");
    for (const auto &name : dir.entryList({QString("*") + suffix}, QDir::Files | QDir::NoSymLinks, QDir::Time)) {
        if (cancel && cancel()) break;
        try {
            QFile file(path(QFileInfo(name).completeBaseName()));
            entries.append(readHeader(file));
        } catch (const std::exception &e) {
            errors.append(name + " — " + QString::fromUtf8(e.what()));
        }
    }
    return entries;
}

FavoriteEntry FavoriteStore::save(const std::string &flame, QStringList tags, const QImage &thumbnail) const {
    if (flame.empty() || flame.size() > project_library_limit)
        throw std::invalid_argument("Favorite exceeds the project flame size limit");
    const auto doc = LegacyDocument::parse(flame);
    if (doc.entries().size() != 1) throw std::invalid_argument("A favorite must contain one flame");
    FavoriteEntry entry{QUuid::createUuid().toString(QUuid::WithoutBraces),
                        QString::fromStdString(doc.entries().front().name), cleanTags(tags), encodeThumbnail(thumbnail)};
    if (entry.name.isEmpty()) entry.name = "Untitled flame";
    if (!QDir().mkpath(directory_)) fail("Could not create the Favorites folder");
    writeRecord(path(entry.id), entry, QByteArrayView(flame.data(), qsizetype(flame.size())));
    return entry;
}

std::string FavoriteStore::snapshot(const QString &id) const {
    QFile file(path(id));
    readHeader(file);
    return readSnapshot(file).toStdString();
}

void FavoriteStore::setTags(const FavoriteEntry &original, QStringList tags) const {
    const auto destination = path(original.id);
    QLockFile lock(destination + ".lock");
    lockRecord(lock);
    QFile file(destination);
    auto entry = checkedEntry(file, original);
    auto bytes = readSnapshot(file);
    file.close();
    entry.tags = cleanTags(tags);
    writeRecord(destination, entry, bytes);
}

void FavoriteStore::remove(const FavoriteEntry &original) const {
    const auto destination = path(original.id);
    QLockFile lock(destination + ".lock");
    lockRecord(lock);
    QFile file(destination);
    checkedEntry(file, original);
    file.close();
    if (!file.remove()) fail("Could not remove the favorite: " + file.errorString());
}

void saveSelectedFavorite(DocumentSession &session, QWidget *parent, const QImage &thumbnail,
                          const QString &directory) {
    if (!session.hasDocument()) return;
    auto index = std::size_t(session.selectedFlame());
    const auto &document = session.document();
    auto bytes = document.standalone_flame(index);
    QDialog dialog(parent);
    dialog.setObjectName("saveFavoriteDialog");
    dialog.setWindowTitle("Save to Favorites");
    auto layout = new QVBoxLayout(&dialog);
    auto preview = new QLabel(&dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumSize(thumbnailSize);
    layout->addWidget(preview);
    QImage image = thumbnail;
    auto show = [&] { preview->setPixmap(QPixmap::fromImage(image.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation))); };
    PreviewController renderer(nullptr, PreviewController::Purpose::thumbnail);
    if (image.isNull()) {
        renderer.setSettings(thumbSettings());
        QObject::connect(&renderer, &PreviewController::imageReady, &dialog, [&](const QImage &result) {
            image = result;
            show();
        });
        try { renderer.request(document.render_model(index), thumbnailSize); }
        catch (const std::exception &) { preview->setText("Preview unavailable"); }
    } else show();
    auto form = new QFormLayout;
    layout->addLayout(form);
    auto name = QString::fromStdString(document.entries().at(index).name);
    auto label = new QLabel(name.isEmpty() ? QString("Untitled flame") : name, &dialog);
    label->setTextFormat(Qt::PlainText);
    form->addRow("Flame", label);
    auto tags = new QLineEdit(&dialog);
    tags->setObjectName("favoriteTags");
    tags->setPlaceholderText("e.g. spiral, copper");
    form->addRow("Tags (comma-separated)", tags);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setObjectName("favoriteSave");
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    tags->setFocus();
    if (dialog.exec() == QDialog::Accepted)
        FavoriteStore(directory).save(bytes, tags->text().split(','), image);
}

namespace {
class FavoritesDialog : public QDialog {
public:
    FavoritesDialog(FavoriteStore store, QWidget *parent)
        : QDialog(parent), store_(std::move(store)), jobs_(favoriteListingJobs->jobs),
          receiver_(std::make_shared<BackgroundReceiver<FavoritesDialog>>(this)) {
        setWindowTitle("Favorites");
        setObjectName("favoritesDialog");
        resize(630, 460);
        auto layout = new QVBoxLayout(this);
        auto filters = new QHBoxLayout;
        search_ = new QLineEdit(this);
        search_->setPlaceholderText("Search flame names or tags");
        search_->setObjectName("favoritesSearch");
        search_->setClearButtonEnabled(true);
        filters->addWidget(search_, 1);
        auto refresh = new QPushButton("Refresh", this);
        filters->addWidget(refresh);
        layout->addLayout(filters);
        list_ = new QListWidget(this);
        list_->setObjectName("favoritesList");
        list_->setItemDelegate(new FavoriteDelegate(list_));
        list_->setViewMode(QListView::IconMode);
        list_->setResizeMode(QListView::Adjust);
        list_->setMovement(QListView::Static);
        list_->setWordWrap(true);
        list_->setIconSize(thumbnailSize);
        list_->setGridSize({172, thumbnailSize.height() + 2 * list_->fontMetrics().height() + 14});
        layout->addWidget(list_, 1);
        status_ = new QLabel(this);
        status_->setTextFormat(Qt::PlainText);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        insert_ = buttons->addButton("Add to current project", QDialogButtonBox::AcceptRole);
        insert_->setObjectName("favoriteInsert");
        edit_ = buttons->addButton("Edit tags…", QDialogButtonBox::ActionRole);
        edit_->setObjectName("favoriteEditTags");
        remove_ = buttons->addButton("Remove…", QDialogButtonBox::ActionRole);
        remove_->setObjectName("favoriteRemove");
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(insert_, &QPushButton::clicked, this, &QDialog::accept);
        connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
        connect(list_, &QListWidget::currentItemChanged, this, [this] { selectionChanged(); });
        connect(edit_, &QPushButton::clicked, this, [this] { editTags(); });
        connect(remove_, &QPushButton::clicked, this, [this] { removeFavorite(); });
        connect(search_, &QLineEdit::textChanged, this, [this] { filter(); });
        connect(refresh, &QPushButton::clicked, this, [this] { load(); });
        preview_.setSettings(thumbSettings());
        connect(&preview_, &PreviewController::imageReady, this, [this](const QImage &image) {
            auto item = list_->currentItem();
            if (item && item->icon().isNull()) item->setIcon(QIcon(QPixmap::fromImage(image)));
        });
        search_->setFocus();
        load();
    }
    ~FavoritesDialog() override {
        receiver_->close();
        jobs_->cancel(this);
        jobs_->cancel(&preview_);
    }
    QString selectedId() const {
        auto item = list_->currentItem();
        return item && !item->isHidden() ? item->data(Qt::UserRole).toString() : QString{};
    }
private:
    FavoriteStore store_;
    std::shared_ptr<BackgroundJobs> jobs_;
    std::shared_ptr<BackgroundReceiver<FavoritesDialog>> receiver_;
    PreviewController preview_{nullptr, PreviewController::Purpose::thumbnail};
    QHash<QString, FavoriteEntry> entries_;
    QLineEdit *search_;
    QListWidget *list_;
    QLabel *status_;
    QPushButton *insert_, *edit_, *remove_;
    int generation_ = 0;
    quint64 selectionGeneration_ = 0;
    QStringList errors_;

    void load() {
        const auto generation = ++generation_;
        status_->setText("Loading favorites…");
        insert_->setEnabled(false);
        edit_->setEnabled(false);
        remove_->setEnabled(false);
        jobs_->submit(this, [generation, store = store_, receiver = receiver_](std::stop_token stop) {
            QStringList errors;
            QList<FavoriteEntry> result;
            try { result = store.entries(errors, [&] { return stop.stop_requested(); }); }
            catch (const std::exception &e) { errors.append(QString::fromUtf8(e.what())); }
            if (!stop.stop_requested())
                receiver->post([generation, result = std::move(result), errors = std::move(errors)](FavoritesDialog *self) {
                    if (generation == self->generation_) self->populate(result, errors);
                });
        });
    }

    void populate(const QList<FavoriteEntry> &result, const QStringList &errors) {
        const QString previous = selectedId();
        const QSignalBlocker block(list_);
        list_->clear();
        entries_.clear();
        errors_ = errors;
        for (const auto &entry : result) {
            auto item = new QListWidgetItem(entry.name, list_);
            item->setData(Qt::UserRole, entry.id);
            item->setToolTip(entry.name + (entry.tags.isEmpty() ? QString{} : "\n" + entry.tags.join(", ")));
            item->setData(Qt::UserRole + 2, entry.tags.join(", "));
            item->setData(Qt::UserRole + 1, entry.thumbnail);
            entries_.insert(entry.id, entry);
            if (entry.id == previous) list_->setCurrentItem(item);
        }
        filter();
    }

    void filter() {
        const auto words = search_->text().simplified().split(' ', Qt::SkipEmptyParts);
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < list_->count(); ++i) {
            auto item = list_->item(i);
            const auto searchable = item->text() + " " + item->data(Qt::UserRole + 2).toString();
            const bool match = std::all_of(words.begin(), words.end(), [&](const auto &word) {
                return searchable.contains(word, Qt::CaseInsensitive);
            });
            item->setHidden(!match);
            if (!first && match) first = item;
        }
        {
            const QSignalBlocker block(list_);
            if (selectedId().isEmpty()) list_->setCurrentItem(first);
        }
        selectionChanged();
        const QString message = first ? QString{} : entries_.isEmpty() ?
            "Your Favorites library is empty. Use Save to Favorites to keep an editable flame." : "No matching favorites.";
        status_->setText(errors_.isEmpty() ? message : message + " Some favorites could not be read. " + errors_.front());
    }

    void selectionChanged() {
        const auto generation = ++selectionGeneration_;
        jobs_->cancel(&preview_);
        preview_.stop();
        const auto id = selectedId();
        insert_->setEnabled(!id.isEmpty());
        edit_->setEnabled(!id.isEmpty());
        remove_->setEnabled(!id.isEmpty());
        if (id.isEmpty() || !list_->currentItem()->icon().isNull() ||
            !list_->currentItem()->data(Qt::UserRole + 1).toByteArray().isEmpty()) return;
        // An immediate save may precede the first live preview. Read and parse the
        // detached snapshot off the GUI thread, then request its thumbnail.
        jobs_->submit(&preview_, [id, generation, store = store_, receiver = receiver_](std::stop_token stop) {
            try {
                auto bytes = store.snapshot(id);
                if (stop.stop_requested()) return;
                auto model = LegacyDocument::parse(std::move(bytes)).render_model(0);
                if (!stop.stop_requested())
                    receiver->post([generation, model = std::move(model)](FavoritesDialog *self) {
                        if (generation == self->selectionGeneration_)
                            self->preview_.request(model, thumbnailSize);
                    });
            } catch (const std::exception &e) {
                receiver->post([generation, error = QString::fromUtf8(e.what())](FavoritesDialog *self) {
                    auto item = self->list_->currentItem();
                    if (item && generation == self->selectionGeneration_)
                        item->setToolTip(item->text() + "\n" + error);
                });
            }
        });
    }

    void editTags() {
        const auto entry = entries_.value(selectedId());
        if (entry.id.isEmpty()) return;
        bool ok = false;
        const auto text = QInputDialog::getText(this, "Edit favorite tags", "Tags (comma-separated)",
                                                QLineEdit::Normal, entry.tags.join(", "), &ok);
        if (!ok) return;
        try { store_.setTags(entry, text.split(',')); load(); }
        catch (const std::exception &e) { status_->setText(QString::fromUtf8(e.what())); }
    }

    void removeFavorite() {
        const auto entry = entries_.value(selectedId());
        if (entry.id.isEmpty() || QMessageBox::question(this, "Remove favorite",
            QString("Remove “%1” from Favorites?").arg(entry.name), QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No) != QMessageBox::Yes) return;
        try { store_.remove(entry); load(); }
        catch (const std::exception &e) { status_->setText(QString::fromUtf8(e.what())); }
    }
};
} // namespace

void browseFlameFavorites(DocumentSession &session, QWidget *parent, const QString &directory) {
    FavoriteStore store(directory);
    FavoritesDialog dialog(store, parent);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedId().isEmpty()) return;
    auto bytes = store.snapshot(dialog.selectedId());
    if (!session.hasDocument()) { session.loadBytes(std::move(bytes), {}, true); return; }
    auto snapshot = LegacyDocument::parse(std::move(bytes));
    if (snapshot.entries().size() != 1) throw std::runtime_error("Invalid favorite snapshot");
    const auto next = int(session.document().entries().size());
    session.change("Add favorite", [&](LegacyDocument &doc) { doc.append_entry(snapshot, 0); },
                   0, false, next);
}
} // namespace apo::editor
