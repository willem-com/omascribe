#include "store.h"
#include "document.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>

NoteStore::NoteStore(QObject *parent)
    : QAbstractListModel(parent)
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/omascribe/notes");
    QDir().mkpath(m_dir);

    const QString legacy = QDir::homePath()
        + QStringLiteral("/.local/share/willem.com/omascribe/notes");
    if (QDir(m_dir).entryList({QStringLiteral("*.omascribe")}, QDir::Files).isEmpty()
        && QDir(legacy).exists()) {
        const QFileInfoList files = QDir(legacy).entryInfoList({QStringLiteral("*.omascribe")},
                                                               QDir::Files);
        for (const QFileInfo &info : files)
            QFile::copy(info.absoluteFilePath(), m_dir + QLatin1Char('/') + info.fileName());
    }

    reload();
}

QString NoteStore::dataDir() const
{
    return QFileInfo(m_dir).dir().absolutePath();
}

int NoteStore::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_notes.size();
}

QVariant NoteStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_notes.size())
        return {};
    const NoteRecord &n = m_notes.at(index.row());
    switch (role) {
    case IdRole:
        return n.id;
    case TitleRole:
        return n.title.isEmpty() ? QStringLiteral("Note") : n.title;
    case ModifiedRole:
        return n.modified;
    case ModifiedTextRole:
        return relativeTime(n.modified);
    case CreatedRole:
        return n.created;
    case StrokeCountRole:
        return n.strokeCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> NoteStore::roleNames() const
{
    return {
        {IdRole, "noteId"},
        {TitleRole, "title"},
        {ModifiedRole, "modified"},
        {ModifiedTextRole, "modifiedText"},
        {CreatedRole, "created"},
        {StrokeCountRole, "strokeCount"},
    };
}

void NoteStore::reload()
{
    beginResetModel();
    m_notes.clear();
    const QDir dir(m_dir);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.omascribe")},
                                                  QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
        if (!json.isObject())
            continue;
        const QJsonObject o = json.object();
        if (o.value(QStringLiteral("format")).toString() != QStringLiteral("omascribe"))
            continue;
        NoteRecord rec;
        rec.id = o.value(QStringLiteral("id")).toString();
        rec.title = o.value(QStringLiteral("title")).toString();
        rec.path = info.absoluteFilePath();
        rec.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(),
                                            Qt::ISODateWithMs);
        rec.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(),
                                             Qt::ISODateWithMs);
        if (!rec.modified.isValid())
            rec.modified = info.lastModified().toUTC();
        rec.strokeCount = o.value(QStringLiteral("strokes")).toArray().size();
        if (rec.id.isEmpty())
            rec.id = info.completeBaseName();
        m_notes.append(rec);
    }
    sortNotes();
    endResetModel();
    emit countChanged();
}

void NoteStore::upsert(const Document *doc)
{
    if (!doc)
        return;
    const int existing = indexOfId(doc->id());
    NoteRecord rec;
    rec.id = doc->id();
    rec.title = doc->title();
    rec.path = doc->filePath();
    rec.created = doc->created();
    rec.modified = doc->modifiedTime();
    rec.strokeCount = doc->strokeCount();
    beginResetModel();
    if (existing >= 0)
        m_notes[existing] = rec;
    else
        m_notes.append(rec);
    sortNotes();
    endResetModel();
    emit countChanged();
}

QString NoteStore::trashDir() const
{
    return dataDir() + QStringLiteral("/trash");
}

// Delete is one tap in the UI, so the file goes to trash/ instead of away.
void NoteStore::removeById(const QString &id)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    const QString path = m_notes[i].path;
    const QString trash = trashDir();
    QDir().mkpath(trash);
    QString dest = trash + QLatin1Char('/') + QFileInfo(path).fileName();
    if (QFile::exists(dest)) {
        dest = trash + QLatin1Char('/') + QFileInfo(path).completeBaseName() + QLatin1Char('-')
            + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddHHmmss"))
            + QStringLiteral(".omascribe");
    }
    if (!QFile::rename(path, dest)) {
        if (QFile::copy(path, dest))
            QFile::remove(path);
    }
    beginRemoveRows(QModelIndex(), i, i);
    m_notes.removeAt(i);
    endRemoveRows();
    emit countChanged();
}

int NoteStore::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].id == id)
            return i;
    }
    return -1;
}

NoteRecord NoteStore::recordAt(int index) const
{
    if (index < 0 || index >= m_notes.size())
        return {};
    return m_notes.at(index);
}

QString NoteStore::pathForId(const QString &id) const
{
    const int i = indexOfId(id);
    if (i < 0)
        return {};
    return m_notes[i].path;
}

QString NoteStore::relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return {};
    const QDateTime local = when.toLocalTime();
    const QDate today = QDate::currentDate();
    const qint64 days = local.date().daysTo(today);
    if (days == 0)
        return local.time().toString(QStringLiteral("HH:mm"));
    if (days == 1)
        return QStringLiteral("Yesterday");
    if (days < 7)
        return local.toString(QStringLiteral("dddd"));
    return local.toString(QStringLiteral("d MMM"));
}

void NoteStore::sortNotes()
{
    std::sort(m_notes.begin(), m_notes.end(), [](const NoteRecord &a, const NoteRecord &b) {
        return a.modified > b.modified;
    });
}
