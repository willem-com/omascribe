#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFont>
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

// Typed text on the page: a block at (x, y) that wraps at `width`, in the
// interface font at `size` px. Laid out identically on screen (QML TextEdit)
// and in export (QPainter), both in document units.
struct TextBlock {
    QString id;
    qreal x = 0;
    qreal y = 0;
    qreal width = 320;
    qreal size = 17;
    QString text;

    QJsonObject toJson() const;
    static TextBlock fromJson(const QJsonObject &obj);
    QFont font() const;
    QRectF rect() const;   // laid-out bounds; needs a QGuiApplication for font metrics
};

class Document;

class TextModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        XRole,
        YRole,
        WidthRole,
        TextRole,
        SizeRole
    };
    explicit TextModel(Document *doc);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    friend class Document;   // Document drives begin/endInsertRows around its own vector
    Document *m_doc;
};

class Document : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY geometryChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY contentsChanged)
    Q_PROPERTY(int textCount READ textCount NOTIFY contentsChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(TextModel *texts READ texts CONSTANT)

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
    int textCount() const { return m_texts.size(); }
    int selectedCount() const { return m_selected.size(); }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    const QVector<Stroke> &strokes() const { return m_strokes; }
    const QVector<TextBlock> &textBlocks() const { return m_texts; }
    TextModel *texts() const { return m_textModel; }
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

    // Text. addTextAt picks the block's left edge and width from the
    // handwriting around y: if the ink spans most of the page width the text
    // takes the ink's margins, otherwise it starts at x and flows at a normal
    // reading width. Returns the new block's id.
    Q_INVOKABLE QString addTextAt(qreal x, qreal y, qreal pageWidth);
    QString addText(TextBlock block);
    Q_INVOKABLE QString textAt(qreal x, qreal y) const;
    Q_INVOKABLE void setTextContent(const QString &id, const QString &text);
    Q_INVOKABLE void removeText(const QString &id);
    int textIndex(const QString &id) const;

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
    friend class TextModel;
    enum class EditKind { Add, Remove, Translate, Title, AddText, RemoveText, EditText };

    struct Edit {
        EditKind kind = EditKind::Add;
        QVector<Stroke> strokes;
        QVector<int> indices;
        QPointF delta;
        QString beforeTitle;
        QString afterTitle;
        TextBlock block;       // AddText / RemoveText: the block; EditText: before
        QString textAfter;     // EditText
        int textIndex = 0;     // RemoveText: where it sat
    };

    void setModified(bool modified);
    void pushUndo(Edit edit);
    QString newStrokeId() const;
    void touchModifiedTime();
    void insertBlock(const TextBlock &block, int index);
    TextBlock takeBlock(const QString &id, int *index);
    void applyText(const QString &id, const QString &text);
    void afterTextChange();

    QString m_id;
    QString m_title;
    QString m_filePath;
    QDateTime m_created;
    QDateTime m_modifiedTime;
    QVector<Stroke> m_strokes;
    QVector<TextBlock> m_texts;
    TextModel *m_textModel = nullptr;
    QSet<QString> m_selected;
    QVector<Edit> m_undo;
    QVector<Edit> m_redo;
    QVector<Stroke> m_eraseBuffer;
    QPointF m_translateAccum;
    bool m_modified = false;
    bool m_erasing = false;
    bool m_translating = false;
};
