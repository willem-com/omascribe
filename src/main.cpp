#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "document.h"
#include "inkcanvas.h"
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
    const bool ok = round
        && round->strokeCount() == 1
        && round->title() == QStringLiteral("Test note")
        && qFuzzyCompare(round->strokes().at(0).points.at(1).x, 14.5f)
        && round->strokes().at(0).hits(QPointF(14.5, 28.25), 2.f)
        && round->strokes().at(0).colorId == QStringLiteral("ink")
        && json.value(QStringLiteral("format")).toString() == QStringLiteral("omascribe");
    delete round;
    delete doc;
    if (!ok) {
        std::fprintf(stderr, "omascribe self-test failed\n");
        return 1;
    }
    std::fprintf(stdout, "omascribe self-test ok\n");
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && QByteArray(argv[1]) == QByteArrayLiteral("--self-test"))
        return runSelfTest();

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
