#pragma once

#include "document.h"
#include "store.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(NoteStore *notes READ notes CONSTANT)
    Q_PROPERTY(Document *document READ document NOTIFY documentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString themeBackground READ themeBackground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeForeground READ themeForeground NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeColorsChanged)
    Q_PROPERTY(QString themeSelection READ themeSelection NOTIFY themeColorsChanged)
    Q_PROPERTY(QString paperColor READ paperColor NOTIFY themeColorsChanged)
    Q_PROPERTY(QString gridColor READ gridColor NOTIFY themeColorsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    NoteStore *notes() const { return m_notes; }
    Document *document() const { return m_document; }
    int currentIndex() const { return m_currentIndex; }

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool darkMode);
    qreal textScale() const { return m_textScale; }
    void setTextScale(qreal textScale);
    QString themeBackground() const { return m_themeBackground; }
    QString themeForeground() const { return m_themeForeground; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeSelection() const { return m_themeSelection; }
    QString paperColor() const { return m_paperColor; }
    QString gridColor() const { return m_gridColor; }
    QString status() const { return m_status; }

    Q_INVOKABLE void newNote();
    Q_INVOKABLE void openIndex(int index);
    Q_INVOKABLE void openId(const QString &id);
    Q_INVOKABLE void deleteCurrent();
    Q_INVOKABLE void saveNow();
    Q_INVOKABLE void exportNote(const QUrl &url);
    Q_INVOKABLE QUrl suggestedExportUrl() const;
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int x, int y, int width, int height, bool maximized);

signals:
    void documentChanged();
    void currentIndexChanged();
    void darkModeChanged();
    void textScaleChanged();
    void themeColorsChanged();
    void statusChanged();

private:
    void loadOmarchyTheme();
    void watchOmarchyTheme();
    bool loadFromPath(const QString &path);
    bool writeDocument();
    void writeReadout();
    void scheduleReadout();
    void setStatus(const QString &status);
    void scheduleSave();
    QString defaultNotePath(const QString &id) const;

    NoteStore *m_notes = nullptr;
    Document *m_document = nullptr;
    int m_currentIndex = -1;
    bool m_darkMode = true;
    qreal m_textScale = 1.0;
    QString m_themeBackground;
    QString m_themeForeground;
    QString m_themeAccent;
    QString m_themeSelection;
    QString m_paperColor;
    QString m_gridColor;
    QString m_status;
    QTimer m_saveTimer;
    QTimer m_readoutTimer;
    QTimer m_themeDebounce;
};
