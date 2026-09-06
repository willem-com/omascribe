#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QVector>

class Document;

struct NoteRecord {
    QString id;
    QString title;
    QString path;
    QDateTime created;
    QDateTime modified;
    int strokeCount = 0;
};

class NoteStore : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ModifiedRole,
        ModifiedTextRole,
        CreatedRole,
        StrokeCountRole
    };

    explicit NoteStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString notesDir() const { return m_dir; }
    QString dataDir() const;
    void reload();
    void upsert(const Document *doc);
    void removeById(const QString &id);
    int indexOfId(const QString &id) const;
    NoteRecord recordAt(int index) const;
    QString pathForId(const QString &id) const;

signals:
    void countChanged();

private:
    static QString relativeTime(const QDateTime &when);
    void sortNotes();

    QString m_dir;
    QVector<NoteRecord> m_notes;
};
