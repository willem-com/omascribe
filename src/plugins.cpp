#include "plugins.h"

#include "document.h"
#include "exporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

PluginModel::PluginModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

PluginModel::~PluginModel()
{
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }
}

QString PluginModel::pluginsDir()
{
    const QByteArray override = qgetenv("OMASCRIBE_PLUGINS_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/omascribe/plugins");
}

Plugin PluginModel::load(const QString &dir)
{
    Plugin p;
    QFile file(dir + QStringLiteral("/plugin.json"));
    if (!file.open(QIODevice::ReadOnly))
        return p;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    p.name = o.value(QStringLiteral("name")).toString().trimmed();
    p.hint = o.value(QStringLiteral("hint")).toString().trimmed();
    p.input = o.value(QStringLiteral("input")).toString(QStringLiteral("pdf")).trimmed().toLower();
    p.success = o.value(QStringLiteral("success")).toString().trimmed();
    QString exec = o.value(QStringLiteral("exec")).toString().trimmed();
    if (p.name.isEmpty() || exec.isEmpty())
        return {};
    if (!QFileInfo(exec).isAbsolute())
        exec = dir + QLatin1Char('/') + exec;
    if (!QFileInfo(exec).isExecutable())
        return {};
    static const QStringList kinds{QStringLiteral("pdf"), QStringLiteral("svg"), QStringLiteral("png"),
                                   QStringLiteral("json"), QStringLiteral("none")};
    if (!kinds.contains(p.input))
        p.input = QStringLiteral("pdf");
    p.exec = exec;
    p.dir = dir;
    p.id = QFileInfo(dir).fileName();
    return p;
}

void PluginModel::reload()
{
    beginResetModel();
    m_plugins.clear();
    const QDir root(pluginsDir());
    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : dirs) {
        const Plugin p = load(info.absoluteFilePath());
        if (!p.id.isEmpty())
            m_plugins.append(p);
    }
    endResetModel();
    emit countChanged();
}

int PluginModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_plugins.size();
}

QVariant PluginModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_plugins.size())
        return {};
    const Plugin &p = m_plugins.at(index.row());
    switch (role) {
    case IdRole: return p.id;
    case NameRole: return p.name;
    case HintRole: return p.hint;
    default: return {};
    }
}

QHash<int, QByteArray> PluginModel::roleNames() const
{
    return {{IdRole, "pluginId"}, {NameRole, "name"}, {HintRole, "hint"}};
}

const Plugin *PluginModel::find(const QString &id) const
{
    for (const Plugin &p : m_plugins) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

void PluginModel::run(const QString &id, const Document *doc, const QString &version)
{
    const Plugin *p = find(id);
    if (!p || !doc) {
        emit finished(false, QStringLiteral("Unknown plugin"));
        return;
    }
    if (m_process) {
        emit finished(false, QStringLiteral("%1 is still running").arg(m_running));
        return;
    }

    m_work = std::make_unique<QTemporaryDir>();
    QString file;
    if (p->input != QStringLiteral("none")) {
        file = m_work->filePath(QStringLiteral("note.") + p->input);
        bool ok = false;
        if (p->input == QStringLiteral("pdf"))
            ok = exportNotePdf(doc, file);
        else if (p->input == QStringLiteral("svg"))
            ok = exportNoteSvg(doc, file);
        else if (p->input == QStringLiteral("png"))
            ok = exportNotePng(doc, file);
        else
            ok = QFile::copy(doc->filePath(), file);
        if (!ok) {
            m_work.reset();
            emit finished(false, QStringLiteral("%1: export failed").arg(p->name));
            return;
        }
    }

    QStringList typed;
    for (const TextBlock &t : doc->textBlocks()) {
        if (!t.text.trimmed().isEmpty())
            typed.append(t.text.trimmed());
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMASCRIBE_FILE"), file);
    env.insert(QStringLiteral("OMASCRIBE_INPUT"), p->input);
    env.insert(QStringLiteral("OMASCRIBE_TITLE"), doc->title().trimmed());
    env.insert(QStringLiteral("OMASCRIBE_NOTE_ID"), doc->id());
    env.insert(QStringLiteral("OMASCRIBE_NOTE_PATH"), doc->filePath());
    env.insert(QStringLiteral("OMASCRIBE_CREATED"), doc->created().toLocalTime().toString(Qt::ISODate));
    env.insert(QStringLiteral("OMASCRIBE_MODIFIED"), doc->modifiedTime().toLocalTime().toString(Qt::ISODate));
    env.insert(QStringLiteral("OMASCRIBE_STROKES"), QString::number(doc->strokeCount()));
    env.insert(QStringLiteral("OMASCRIBE_TEXTS"), QString::number(doc->textCount()));
    env.insert(QStringLiteral("OMASCRIBE_TYPED_TEXT"), typed.join(QStringLiteral("\n\n")));
    env.insert(QStringLiteral("OMASCRIBE_HOST"), QHostInfo::localHostName());
    env.insert(QStringLiteral("OMASCRIBE_VERSION"), version);
    env.insert(QStringLiteral("OMASCRIBE_PLUGIN_DIR"), p->dir);

    m_process = std::make_unique<QProcess>();
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(p->dir);
    m_process->setProgram(p->exec);
    m_process->setArguments(file.isEmpty() ? QStringList{} : QStringList{file});
    m_running = p->name;
    emit runningChanged();

    const QString name = p->name;
    const QString success = p->success;
    auto done = [this, name, success](int code, QProcess::ExitStatus status) {
        const auto lastLine = [](const QByteArray &raw) {
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (int i = lines.size() - 1; i >= 0; --i) {
                const QString t = lines[i].trimmed();
                if (!t.isEmpty())
                    return t;
            }
            return QString();
        };
        const QString out = lastLine(m_process->readAllStandardOutput());
        const QString err = lastLine(m_process->readAllStandardError());
        const bool ok = status == QProcess::NormalExit && code == 0;
        QString message;
        if (ok)
            message = !out.isEmpty() ? out : (!success.isEmpty() ? success : name + QStringLiteral(": done"));
        else
            message = name + QStringLiteral(" failed") + (err.isEmpty() ? QString() : QStringLiteral(": ") + err);
        m_process->deleteLater();
        m_process.release();
        m_work.reset();
        m_running.clear();
        emit runningChanged();
        emit finished(ok, message);
    };
    connect(m_process.get(), &QProcess::finished, this, done);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this, name](QProcess::ProcessError) {
        if (m_process->state() == QProcess::NotRunning && m_process->exitStatus() != QProcess::CrashExit
            && m_process->error() == QProcess::FailedToStart) {
            const QString why = m_process->errorString();
            m_process->deleteLater();
            m_process.release();
            m_work.reset();
            m_running.clear();
            emit runningChanged();
            emit finished(false, name + QStringLiteral(" failed to start: ") + why);
        }
    });
    m_process->start();
}
