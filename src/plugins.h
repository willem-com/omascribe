#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <memory>

class Document;

// A per-install button: a directory under ~/.config/omascribe/plugins/<id>/
// with a plugin.json and a program. See plugins/README.md for the contract.
struct Plugin {
    QString id;
    QString name;
    QString hint;
    QString exec;      // absolute path once loaded
    QString input;     // pdf | svg | png | json | none
    QString success;
    QString dir;
};

class PluginModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString running READ running NOTIFY runningChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, HintRole };

    explicit PluginModel(QObject *parent = nullptr);
    ~PluginModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    static QString pluginsDir();
    void reload();
    const Plugin *find(const QString &id) const;
    QString running() const { return m_running; }

    // Export the note as the plugin asks and run it. Emits finished(ok, toast).
    void run(const QString &id, const Document *doc, const QString &version);

signals:
    void countChanged();
    void runningChanged();
    void finished(bool ok, const QString &message);

private:
    static Plugin load(const QString &dir);

    QVector<Plugin> m_plugins;
    QString m_running;
    // m_work is declared before m_process so it outlives it: the process
    // destructor may still deliver finished() into the handler that resets m_work.
    std::unique_ptr<QTemporaryDir> m_work;
    std::unique_ptr<QProcess> m_process;
};
