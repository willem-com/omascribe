#include "backend.h"
#include "document.h"
#include "exporter.h"
#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace {
QFileSystemWatcher *themeWatcher()
{
    static QFileSystemWatcher watcher;
    return &watcher;
}
}

Backend::Backend(QObject *parent)
    : QObject(parent)
    , m_notes(new NoteStore(this))
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &Backend::saveNow);

    // The agent readout (PNG, SVG, JSON) renders the whole page on the GUI
    // thread. Autosave is 350 ms; the readout waits until the pen has rested
    // for 5 s, and is flushed on note switch and quit.
    m_readoutTimer.setSingleShot(true);
    m_readoutTimer.setInterval(5000);
    connect(&m_readoutTimer, &QTimer::timeout, this, &Backend::writeReadout);

    m_themeDebounce.setSingleShot(true);
    m_themeDebounce.setInterval(80);
    connect(&m_themeDebounce, &QTimer::timeout, this, &Backend::loadOmarchyTheme);

    loadOmarchyTheme();
    watchOmarchyTheme();

    if (m_notes->rowCount() > 0)
        openIndex(0);
    else
        newNote();
}

Backend::~Backend()
{
    saveNow();
    if (m_readoutTimer.isActive()) {
        m_readoutTimer.stop();
        writeReadout();
    }
}

void Backend::setDarkMode(bool darkMode)
{
    if (m_darkMode == darkMode)
        return;
    m_darkMode = darkMode;
    loadOmarchyTheme();
    emit darkModeChanged();
}

void Backend::setTextScale(qreal textScale)
{
    textScale = qBound(0.5, textScale, 3.0);
    if (qFuzzyCompare(m_textScale, textScale))
        return;
    m_textScale = textScale;
    emit textScaleChanged();
}

void Backend::newNote()
{
    saveNow();
    m_readoutTimer.stop();
    if (m_document)
        m_document->deleteLater();
    m_document = Document::createNew(this);
    m_document->setFilePath(defaultNotePath(m_document->id()));
    connect(m_document, &Document::contentsChanged, this, &Backend::scheduleSave);
    connect(m_document, &Document::titleChanged, this, &Backend::scheduleSave);
    writeDocument();
    m_notes->upsert(m_document);
    writeReadout();
    m_currentIndex = m_notes->indexOfId(m_document->id());
    emit documentChanged();
    emit currentIndexChanged();
    setStatus(QStringLiteral("New note"));
}

void Backend::openIndex(int index)
{
    if (index < 0 || index >= m_notes->rowCount())
        return;
    const NoteRecord rec = m_notes->recordAt(index);
    if (m_document && rec.id == m_document->id()) {
        m_currentIndex = index;
        emit currentIndexChanged();
        return;
    }
    saveNow();
    m_readoutTimer.stop();
    if (!loadFromPath(rec.path))
        return;
    m_currentIndex = m_notes->indexOfId(m_document->id());
    emit currentIndexChanged();
}

void Backend::openId(const QString &id)
{
    const int index = m_notes->indexOfId(id);
    if (index >= 0)
        openIndex(index);
}

void Backend::deleteCurrent()
{
    if (!m_document)
        return;
    const QString id = m_document->id();
    m_saveTimer.stop();
    m_readoutTimer.stop();
    m_notes->removeById(id);
    m_document->deleteLater();
    m_document = nullptr;
    if (m_notes->rowCount() > 0)
        openIndex(0);
    else
        newNote();
    setStatus(QStringLiteral("Moved to trash"));
}

void Backend::saveNow()
{
    m_saveTimer.stop();
    if (!m_document)
        return;
    if (!m_document->modified() && QFile::exists(m_document->filePath()))
        return;
    if (writeDocument()) {
        m_notes->upsert(m_document);
        scheduleReadout();
        setStatus(QStringLiteral("Saved"));
    }
}

void Backend::scheduleReadout()
{
    m_readoutTimer.start();
}

void Backend::exportNote(const QUrl &url)
{
    if (!m_document)
        return;
    QString path = url.toLocalFile();
    if (path.isEmpty())
        return;
    const bool svg = path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);
    if (!svg && !path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        path += QStringLiteral(".pdf");
    const bool ok = svg ? exportNoteSvg(m_document, path) : exportNotePdf(m_document, path);
    setStatus(ok ? QStringLiteral("Exported") : QStringLiteral("Export failed"));
}

QUrl Backend::suggestedExportUrl() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir().mkpath(dir);
    const QString name = suggestedExportName(m_document) + QStringLiteral(".pdf");
    return QUrl::fromLocalFile(dir + QLatin1Char('/') + name);
}

QVariantMap Backend::windowGeometry() const
{
    QSettings settings;
    return {
        {QStringLiteral("x"), settings.value(QStringLiteral("window/x"), 120)},
        {QStringLiteral("y"), settings.value(QStringLiteral("window/y"), 80)},
        {QStringLiteral("width"), settings.value(QStringLiteral("window/width"), 1280)},
        {QStringLiteral("height"), settings.value(QStringLiteral("window/height"), 820)},
        {QStringLiteral("maximized"), settings.value(QStringLiteral("window/maximized"), false)},
    };
}

void Backend::saveWindowGeometry(int x, int y, int width, int height, bool maximized)
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/x"), x);
    settings.setValue(QStringLiteral("window/y"), y);
    settings.setValue(QStringLiteral("window/width"), width);
    settings.setValue(QStringLiteral("window/height"), height);
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}

void Backend::loadOmarchyTheme()
{
    m_themeBackground = m_darkMode ? QStringLiteral("#101010") : QStringLiteral("#ffffff");
    m_themeForeground = m_darkMode ? QStringLiteral("#eeeeee") : QStringLiteral("#222324");
    m_themeAccent = m_darkMode ? QStringLiteral("#5584aa") : QStringLiteral("#2077b2");
    m_themeSelection = m_darkMode ? QStringLiteral("#186a9a") : QStringLiteral("#2077b2");

    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QString themeMode;
    QFile file(colorsPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0)
                continue;
            const QString key = line.left(equals).trimmed();
            QString value = line.mid(equals + 1).trimmed();
            if (value.size() >= 2
                && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                    || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
                value = value.mid(1, value.size() - 2);
            if (key == QStringLiteral("mode"))
                themeMode = value;
            else if (key == QStringLiteral("background"))
                m_themeBackground = value;
            else if (key == QStringLiteral("foreground"))
                m_themeForeground = value;
            else if (key == QStringLiteral("accent"))
                m_themeAccent = value;
            else if (key == QStringLiteral("selection"))
                m_themeSelection = value;
        }
    }

    bool themeModeKnown = false;
    bool themeIsDark = m_darkMode;
    if (themeMode == QStringLiteral("dark")) {
        themeIsDark = true;
        themeModeKnown = true;
    } else if (themeMode == QStringLiteral("light")) {
        themeIsDark = false;
        themeModeKnown = true;
    } else {
        const QColor background(m_themeBackground);
        if (background.isValid()) {
            const double luminance = 0.299 * background.redF()
                + 0.587 * background.greenF() + 0.114 * background.blueF();
            themeIsDark = luminance < 0.5;
            themeModeKnown = true;
        }
    }
    if (themeModeKnown && themeIsDark != m_darkMode) {
        m_darkMode = themeIsDark;
        emit darkModeChanged();
    }

    if (m_darkMode) {
        m_paperColor = m_themeBackground;
        m_gridColor = QStringLiteral("#28ffffff");
    } else {
        m_paperColor = QStringLiteral("#f7f4ec");
        m_gridColor = QStringLiteral("#1c000000");
    }

    emit themeColorsChanged();
}

void Backend::watchOmarchyTheme()
{
    const QString colorsPath = QDir::homePath()
        + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    const QString themeDir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme");
    auto *watcher = themeWatcher();
    if (!watcher->files().contains(colorsPath) && QFile::exists(colorsPath))
        watcher->addPath(colorsPath);
    if (!watcher->directories().contains(themeDir) && QDir(themeDir).exists())
        watcher->addPath(themeDir);
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [this]() { m_themeDebounce.start(); },
            Qt::UniqueConnection);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { m_themeDebounce.start(); }, Qt::UniqueConnection);
}

bool Backend::loadFromPath(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("Could not open note"));
        return false;
    }
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject()) {
        setStatus(QStringLiteral("Note is not valid"));
        return false;
    }
    if (m_document)
        m_document->deleteLater();
    m_document = Document::fromJson(json.object(), this);
    m_document->setFilePath(path);
    connect(m_document, &Document::contentsChanged, this, &Backend::scheduleSave);
    connect(m_document, &Document::titleChanged, this, &Backend::scheduleSave);
    emit documentChanged();
    writeReadout();
    return true;
}

void Backend::writeReadout()
{
    if (!m_document)
        return;
    writeAgentReadout(m_document, m_notes->dataDir());
}

bool Backend::writeDocument()
{
    if (!m_document)
        return false;
    QString path = m_document->filePath();
    if (path.isEmpty()) {
        path = defaultNotePath(m_document->id());
        m_document->setFilePath(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("Could not save"));
        return false;
    }
    const QJsonDocument json(m_document->toJson());
    file.write(json.toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        setStatus(QStringLiteral("Could not save"));
        return false;
    }
    m_document->markSaved();
    return true;
}

void Backend::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::scheduleSave()
{
    m_saveTimer.start();
}

QString Backend::defaultNotePath(const QString &id) const
{
    return m_notes->notesDir() + QLatin1Char('/') + id + QStringLiteral(".omascribe");
}
