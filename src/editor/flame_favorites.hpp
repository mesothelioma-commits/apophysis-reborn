#pragma once

#include <QByteArray>
#include <QImage>
#include <QStringList>
#include <functional>
#include <string>

class QWidget;

namespace apo::editor {
class DocumentSession;

struct FavoriteEntry {
    QString id, name;
    QStringList tags;
    QByteArray thumbnail;
};

// Each favorite owns a standalone flame. Tags and thumbnails are local catalog data.
class FavoriteStore {
public:
    explicit FavoriteStore(const QString &directory = {});
    QList<FavoriteEntry> entries(QStringList &errors, const std::function<bool()> &cancel = {}) const;
    FavoriteEntry save(const std::string &flame, QStringList tags, const QImage &thumbnail) const;
    std::string snapshot(const QString &id) const;
    void setTags(const FavoriteEntry &original, QStringList tags) const;
    void remove(const FavoriteEntry &original) const;
private:
    QString directory_;
    QString path(const QString &id) const;
};

void saveSelectedFavorite(DocumentSession &, QWidget *parent, const QImage &thumbnail = {},
                          const QString &directory = {});
void browseFlameFavorites(DocumentSession &, QWidget *parent, const QString &directory = {});
} // namespace apo::editor
