#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QEventLoop>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QUrl>
#include <cmath>
#include <cstdio>

#include "backend.h"
#include "document.h"
#include "exporter.h"
#include "ink.h"
#include "inkcanvas.h"
#include "store.h"
#include "systemtheme.h"

static int runSelfTest()
{
    Stroke s;
    s.id = QStringLiteral("stroke-1");
    s.tool = QStringLiteral("fineliner");
    s.colorId = QStringLiteral("#222324");
    s.width = 2.4f;
    s.points = {
        {10.f, 20.f, 0.4f},
        {14.5f, 28.25f, 0.9f},
        {40.f, 80.f, 0.6f},
    };
    s.recomputeBounds();

    Document *doc = Document::createNew();
    doc->setTitle(QStringLiteral("Test note"));
    doc->addStroke(s);
    const QJsonObject json = doc->toJson();
    Document *round = Document::fromJson(json);
    bool ok = round
        && round->strokeCount() == 1
        && round->title() == QStringLiteral("Test note")
        && qFuzzyCompare(round->strokes().at(0).points.at(1).x, 14.5f)
        && round->strokes().at(0).hits(QPointF(14.5, 28.25), 2.f)
        && round->strokes().at(0).colorId == QStringLiteral("ink")
        && json.value(QStringLiteral("format")).toString() == QStringLiteral("omascribe");

    // Ribbon geometry: one outline per stroke, triangles that stay inside the bounds.
    {
        const Stroke &st = doc->strokes().at(0);
        const QPainterPath outline = strokeOutline(st);
        QVector<QPointF> tris;
        appendStrokeTriangles(st, tris);
        const QRectF pad = st.bounds.adjusted(-1, -1, 1, 1);
        ok = ok && outline.elementCount() > 8 && pad.contains(outline.boundingRect())
            && tris.size() >= 12 && tris.size() % 3 == 0;
        for (const QPointF &v : tris)
            ok = ok && pad.contains(v);

        Stroke hairpin = st;
        hairpin.id = QStringLiteral("stroke-2");
        hairpin.points = {{0.f, 0.f, 0.8f}, {30.f, 0.f, 0.8f}, {0.f, 3.f, 0.8f}};
        hairpin.recomputeBounds();
        QVector<QPointF> hp;
        appendStrokeTriangles(hairpin, hp);
        ok = ok && !hp.isEmpty() && strokeOutline(hairpin).elementCount() > 8;

        Stroke dot = st;
        dot.id = QStringLiteral("stroke-3");
        dot.points = {{5.f, 5.f, 0.5f}};
        dot.recomputeBounds();
        QVector<QPointF> dp;
        appendStrokeTriangles(dot, dp);
        ok = ok && dp.size() == 16 * 3 && !strokeOutline(dot).isEmpty();
    }

    // Title: keystrokes share one undo step; trailing whitespace never reaches disk.
    {
        Document *t = Document::createNew();
        t->setTitle(QStringLiteral("R"));
        t->setTitle(QStringLiteral("Re"));
        t->setTitle(QStringLiteral("Ref   "));
        ok = ok && t->toJson().value(QStringLiteral("title")).toString() == QStringLiteral("Ref");
        t->undo();
        ok = ok && t->title().isEmpty() && !t->canUndo() && t->canRedo();
        t->redo();
        ok = ok && t->title() == QStringLiteral("Ref   ");
        delete t;
    }

    // The text font must resolve to the Regular face (the Bold file lies about its weight).
    {
        const QFontInfo fi(TextBlock().font());
        ok = ok && fi.styleName() == QStringLiteral("Regular");
        if (fi.styleName() != QStringLiteral("Regular"))
            std::fprintf(stderr, "text font resolved to %s %s\n",
                         qUtf8Printable(fi.family()), qUtf8Printable(fi.styleName()));
    }

    // Typed text: margins from wide ink, one undo step per block of typing,
    // empty blocks vanish without a trace, JSON roundtrip.
    {
        Document *t = Document::createNew();
        Stroke wide = s;
        wide.id = QStringLiteral("wide");
        wide.points = {{12.f, 100.f, 0.8f}, {700.f, 104.f, 0.8f}};
        wide.recomputeBounds();
        t->addStroke(wide);
        const QString id = t->addTextAt(300, 140, 800);
        ok = ok && t->textCount() == 1 && t->textIndex(id) == 0;
        const TextBlock &b = t->textBlocks().at(0);
        ok = ok && std::abs(b.x - wide.bounds.left()) < 0.01
            && std::abs(b.width - wide.bounds.width()) < 0.01 && b.y < 140 && b.y > 120;
        t->setTextContent(id, QStringLiteral("H"));
        t->setTextContent(id, QStringLiteral("Hello"));
        t->setTextContent(id, QStringLiteral("Hello page"));
        ok = ok && t->textAt(b.x + 5, b.y + 5) == id && t->textAt(b.x - 40, b.y).isEmpty();
        t->undo();
        ok = ok && t->textBlocks().at(0).text.isEmpty();
        t->redo();
        ok = ok && t->textBlocks().at(0).text == QStringLiteral("Hello page");
        Document *rt = Document::fromJson(t->toJson());
        ok = ok && rt && rt->textCount() == 1
            && rt->textBlocks().at(0).text == QStringLiteral("Hello page")
            && std::abs(rt->textBlocks().at(0).width - b.width) < 0.01;
        ok = ok && rt->contentHeight() >= rt->textBlocks().at(0).rect().bottom();
        delete rt;

        // Narrow ink: text starts at the tap and flows at reading width.
        Document *n = Document::createNew();
        n->addStroke(s);
        const QString nid = n->addTextAt(100, 300, 1000);
        ok = ok && std::abs(n->textBlocks().at(0).x - 100) < 0.01
            && std::abs(n->textBlocks().at(0).width - 620) < 0.01;
        // An untouched empty block removed leaves no undo step behind.
        n->removeText(nid);
        ok = ok && n->textCount() == 0 && n->canUndo();  // the stroke's Add remains
        n->undo();
        ok = ok && n->strokeCount() == 0;
        delete n;

        // Removing a block with text is undoable.
        t->removeText(id);
        ok = ok && t->textCount() == 0;
        t->undo();
        ok = ok && t->textCount() == 1 && t->textBlocks().at(0).text == QStringLiteral("Hello page");

        QTemporaryDir tdir;
        ok = ok && exportNotePdf(t, tdir.filePath(QStringLiteral("t.pdf")))
            && writeAgentReadout(t, tdir.path());
        QFile tr(tdir.filePath(QStringLiteral("current.json")));
        ok = ok && tr.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(tr.readAll()).object().value(QStringLiteral("typedText")).toString()
                == QStringLiteral("Hello page");
        delete t;
    }

    ok = ok && doc->canUndo();
    doc->undo();
    ok = ok && doc->strokeCount() == 0 && doc->canRedo();
    doc->redo();
    ok = ok && doc->strokeCount() == 1;

    QTemporaryDir tmp;
    const QString pdfPath = tmp.filePath(QStringLiteral("note.pdf"));
    const QString svgPath = tmp.filePath(QStringLiteral("note.svg"));
    ok = ok && exportNotePdf(doc, pdfPath) && exportNoteSvg(doc, svgPath);
    QFile pdf(pdfPath);
    QFile svg(svgPath);
    ok = ok && pdf.open(QIODevice::ReadOnly) && svg.open(QIODevice::ReadOnly)
        && pdf.read(4) == QByteArray("%PDF")
        && svg.readAll().contains("<svg");
    ok = ok && writeAgentReadout(doc, tmp.path());
    QFile readout(tmp.filePath(QStringLiteral("current.json")));
    QFile preview(tmp.filePath(QStringLiteral("current.png")));
    ok = ok && readout.open(QIODevice::ReadOnly) && preview.open(QIODevice::ReadOnly)
        && QJsonDocument::fromJson(readout.readAll()).object().value(QStringLiteral("format")).toString()
            == QStringLiteral("omascribe-readout")
        && preview.read(8).startsWith("\x89PNG");

    // Deleting a note moves it to trash/ instead of removing it.
    {
        QTemporaryDir dataHome;
        qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
        NoteStore store;
        const QString notePath = dataHome.path() + QStringLiteral("/omascribe/notes/")
            + doc->id() + QStringLiteral(".omascribe");
        QDir().mkpath(QFileInfo(notePath).absolutePath());
        QFile noteFile(notePath);
        ok = ok && noteFile.open(QIODevice::WriteOnly)
            && noteFile.write(QJsonDocument(doc->toJson()).toJson()) > 0;
        noteFile.close();
        store.reload();
        ok = ok && store.indexOfId(doc->id()) >= 0;
        store.removeById(doc->id());
        ok = ok && store.indexOfId(doc->id()) < 0 && !QFile::exists(notePath)
            && QFile::exists(store.trashDir() + QLatin1Char('/') + doc->id()
                             + QStringLiteral(".omascribe"));
        qunsetenv("XDG_DATA_HOME");
    }

    delete round;
    delete doc;
    if (!ok) {
        std::fprintf(stderr, "omascribe self-test failed\n");
        return 1;
    }
    std::fprintf(stdout, "omascribe self-test ok\n");
    return 0;
}

// --probe-grid [dir]: open the real window on a scratch data dir, grab it
// before and after a user scroll, and check that the dot grid is hidden at rest
// and visible during the scroll. Writes before.png / after.png into dir.
static int runGridProbe(int argc, char *argv[])
{
    QTemporaryDir dataHome;
    QTemporaryDir configHome;
    qputenv("XDG_DATA_HOME", dataHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("willem.com"));
    QQuickStyle::setStyle(QStringLiteral("Material"));
    qmlRegisterType<InkCanvas>("Omascribe", 1, 0, "InkCanvas");
    qmlRegisterUncreatableType<Document>("Omascribe", 1, 0, "Document",
                                         QStringLiteral("Created by Backend"));
    Backend backend(&app);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    auto *window = engine.rootObjects().isEmpty()
        ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *canvas = window ? window->findChild<InkCanvas *>(QStringLiteral("canvas")) : nullptr;
    if (!window || !canvas) {
        std::fprintf(stderr, "probe: no window or canvas\n");
        return 1;
    }
    window->resize(1280, 820);
    const auto settle = [&](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    // Non-paper pixels inside a window-space rect (logical px).
    const auto countInkIn = [&](const QImage &img, const QRectF &area) {
        const qreal dpr = window->devicePixelRatio();
        const QColor paper = canvas->paperColor();
        int n = 0;
        for (int y = int(area.top() * dpr); y < int(area.bottom() * dpr) && y < img.height(); ++y)
            for (int x = int(area.left() * dpr); x < int(area.right() * dpr) && x < img.width(); ++x)
                if (y >= 0 && x >= 0 && img.pixelColor(x, y) != paper)
                    ++n;
        return n;
    };
    const auto countInk = [&](const QImage &img) {
        return countInkIn(img, QRectF(500, 250, 400, 200));
    };
    const QString dir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    settle(700);
    const QImage before = window->grabWindow();
    const int atRest = countInk(before);
    canvas->scrollBy(60);
    settle(120);
    const QImage after = window->grabWindow();
    const int scrolling = countInk(after);
    settle(1300);
    const QImage later = window->grabWindow();
    const int faded = countInk(later);
    if (!dir.isEmpty()) {
        before.save(dir + QStringLiteral("/before.png"));
        after.save(dir + QStringLiteral("/after.png"));
        later.save(dir + QStringLiteral("/later.png"));
    }
    std::fprintf(stdout, "grid pixels: at rest %d, scrolling %d, 1.3 s later %d; viewY %g\n",
                 atRest, scrolling, faded, canvas->viewY());
    const bool gridOk = atRest == 0 && scrolling > 50 && faded == 0 && canvas->viewY() > 0;

    // Typed text: a tap on empty paper opens a block with focus, keys land in
    // the document, and the glyphs render on the page.
    Document *doc = backend.document();
    emit canvas->textTapped(600, 360);
    settle(150);
    QQuickItem *focus = window->activeFocusItem();
    const bool opened = doc && doc->textCount() == 1 && focus
        && QByteArray(focus->metaObject()->className()).contains("TextEdit");
    if (opened) {
        for (const QChar ch : QStringLiteral("Hi there")) {
            const Qt::Key key = ch == QLatin1Char(' ') ? Qt::Key_Space : Qt::Key(ch.toUpper().unicode());
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, QString(ch));
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, QString(ch));
            QCoreApplication::sendEvent(focus, &press);
            QCoreApplication::sendEvent(focus, &release);
        }
    }
    settle(150);
    const QImage typed = window->grabWindow();
    int typedPixels = 0;
    if (opened) {
        const QRectF r = doc->textBlocks().at(0).rect();
        const QPointF origin = canvas->mapToScene(QPointF(r.x(), r.y() - canvas->viewY()));
        typedPixels = countInkIn(typed, QRectF(origin, r.size()));
    }
    const bool textOk = opened && doc->textBlocks().at(0).text == QStringLiteral("Hi there")
        && typedPixels > 20;
    if (!dir.isEmpty())
        typed.save(dir + QStringLiteral("/typed.png"));
    std::fprintf(stdout, "text: blocks %d, body \"%s\", pixels %d\n",
                 doc ? doc->textCount() : -1,
                 doc && doc->textCount() ? qUtf8Printable(doc->textBlocks().at(0).text) : "",
                 typedPixels);

    // A plain mouse click (pen tool, no drag) opens a block too.
    {
        if (focus)
            focus->setFocus(false);
        canvas->forceActiveFocus();
        settle(80);
        const QPointF local(canvas->width() * 0.5, canvas->height() * 0.8);
        const QPointF scene = canvas->mapToScene(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, scene, scene, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, scene, scene, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &press);
        QCoreApplication::sendEvent(canvas, &release);
        settle(150);
    }
    const bool clickOk = doc && doc->textCount() == 2 && window->activeFocusItem()
        && QByteArray(window->activeFocusItem()->metaObject()->className()).contains("TextEdit");
    std::fprintf(stdout, "mouse click: blocks %d, focus %s\n", doc ? doc->textCount() : -1,
                 clickOk ? "TextEdit" : "elsewhere");

    const bool ok = gridOk && textOk && clickOk;
    std::fprintf(stdout, ok ? "window probe ok\n" : "window probe FAILED\n");
    return ok ? 0 : 1;
}

int main(int argc, char *argv[])
{
    // Ink is drawn as scene-graph triangles; MSAA gives them their smooth edge.
    // OMASCRIBE_MSAA=0 turns it off (or picks 2/8) for a latency comparison.
    {
        bool given = false;
        int samples = qEnvironmentVariableIntValue("OMASCRIBE_MSAA", &given);
        if (!given)
            samples = 4;
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setSamples(samples > 1 ? samples : 0);
        QSurfaceFormat::setDefaultFormat(format);
    }

    if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--self-test")) {
        QGuiApplication app(argc, argv);
        return runSelfTest();
    }

    if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--probe-grid"))
        return runGridProbe(argc, argv);

    if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--readout")) {
        const QString path = QDir::homePath() + QStringLiteral("/.local/share/omascribe/current.json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "no live readout at %s\n", qUtf8Printable(path));
            return 1;
        }
        const QByteArray body = file.readAll();
        std::fwrite(body.constData(), 1, size_t(body.size()), stdout);
        return 0;
    }

    if (argc >= 4 && (QByteArray(argv[1]) == QByteArrayLiteral("--export-pdf")
                      || QByteArray(argv[1]) == QByteArrayLiteral("--export-svg"))) {
        QGuiApplication app(argc, argv);
        QFile file(QString::fromLocal8Bit(argv[2]));
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "could not read %s\n", argv[2]);
            return 1;
        }
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
        Document *doc = Document::fromJson(json.object());
        const QString out = QString::fromLocal8Bit(argv[3]);
        const bool ok = QByteArray(argv[1]) == QByteArrayLiteral("--export-svg")
            ? exportNoteSvg(doc, out)
            : exportNotePdf(doc, out);
        delete doc;
        if (!ok) {
            std::fprintf(stderr, "export failed\n");
            return 1;
        }
        return 0;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omascribe"));
    app.setApplicationDisplayName(QStringLiteral("Omascribe"));
    app.setDesktopFileName(QStringLiteral("omascribe"));
    app.setOrganizationName(QStringLiteral("willem.com"));
    app.setOrganizationDomain(QStringLiteral("willem.com"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omascribe")));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    qmlRegisterType<InkCanvas>("Omascribe", 1, 0, "InkCanvas");
    qmlRegisterUncreatableType<Document>("Omascribe", 1, 0, "Document",
                                         QStringLiteral("Created by Backend"));

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend, &Backend::setDarkMode);

    QFont interfaceFont(QStringLiteral("iA Writer Quattro S"));
    if (interfaceFont.family() != QStringLiteral("iA Writer Quattro S"))
        interfaceFont = QFont(QStringLiteral("iA Writer Mono S"));
    const qreal basePointSize = interfaceFont.pointSizeF() > 0
        ? interfaceFont.pointSizeF()
        : app.font().pointSizeF();
    const auto applyInterfaceFont = [&app, interfaceFont, basePointSize](qreal textScale) {
        QFont scaled = interfaceFont;
        scaled.setPointSizeF(basePointSize * textScale);
        app.setFont(scaled);
    };
    applyInterfaceFont(systemTheme.textScale());
    backend.setTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     [&backend, applyInterfaceFont](qreal textScale) {
                         applyInterfaceFont(textScale);
                         backend.setTextScale(textScale);
                     });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
                         for (const QQmlError &warning : warnings)
                             qWarning().noquote() << warning.toString();
                     });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omascribe interface";
        return -1;
    }
    return app.exec();
}
