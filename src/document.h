#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

struct InkPoint {
    float x = 0;
    float y = 0;
    float pressure = 1;
};

struct Stroke {
    QString id;
    QString tool = QStringLiteral("fineliner");
    QString colorId = QStringLiteral("ink");
    float width = 2.4f;
    QVector<InkPoint> points;
    QRectF bounds;

    void recomputeBounds();
    QJsonObject toJson() const;
    static Stroke fromJson(const QJsonObject &obj);
    bool hits(QPointF p, float radius) const;
    bool intersectsPolygon(const QVector<QPointF> &poly) const;
    void translate(QPointF delta);
};

class Document : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY geometryChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY contentsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    explicit Document(QObject *parent = nullptr);

    QString id() const { return m_id; }
    QString title() const { return m_title; }
    void setTitle(const QString &title);

    QDateTime created() const { return m_created; }
    QDateTime modifiedTime() const { return m_modifiedTime; }

    qreal contentHeight() const;
    bool modified() const { return m_modified; }
    int strokeCount() const { return m_strokes.size(); }
    int selectedCount() const { return m_selected.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    const QVector<Stroke> &strokes() const { return m_strokes; }
    const QSet<QString> &selectedIds() const { return m_selected; }
    QRectF selectionBounds() const;

    void addStroke(Stroke stroke);
    void beginErase();
    void eraseAt(QPointF p, float radius);
    void endErase();
    void selectLasso(const QVector<QPointF> &poly);
    Q_INVOKABLE void clearSelection();
    void beginTranslate();
    void translateSelection(QPointF delta);
    void endTranslate();
    Q_INVOKABLE void deleteSelection();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    QJsonObject toJson() const;
    static Document *fromJson(const QJsonObject &obj, QObject *parent = nullptr);
    static Document *createNew(QObject *parent = nullptr);

    void markSaved();
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }

signals:
    void titleChanged();
    void geometryChanged();
    void modifiedChanged();
    void contentsChanged();
    void selectionChanged();
    void historyChanged();

private:
    enum class EditKind { Add, Remove, Translate, Title };

    struct Edit {
        EditKind kind = EditKind::Add;
        QVector<Stroke> strokes;
        QVector<int> indices;
        QPointF delta;
        QString beforeTitle;
        QString afterTitle;
    };

    void setModified(bool modified);
    void pushUndo(Edit edit);
    QString newStrokeId() const;
    void touchModifiedTime();

    QString m_id;
    QString m_title;
    QString m_filePath;
    QDateTime m_created;
    QDateTime m_modifiedTime;
    QVector<Stroke> m_strokes;
    QSet<QString> m_selected;
    QVector<Edit> m_undo;
    QVector<Edit> m_redo;
    QVector<Stroke> m_eraseBuffer;
    QPointF m_translateAccum;
    bool m_modified = false;
    bool m_erasing = false;
    bool m_translating = false;
};
