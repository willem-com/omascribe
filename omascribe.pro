QT += core gui network qml quick quickcontrols2 quickdialogs2 dbus svg

CONFIG += c++17 release
TARGET = omascribe
VERSION = 0.1
DEFINES += OMASCRIBE_VERSION=\\\"$$VERSION\\\"
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/document.h \
    src/exporter.h \
    src/ink.h \
    src/inkcanvas.h \
    src/palette.h \
    src/plugins.h \
    src/store.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/document.cpp \
    src/exporter.cpp \
    src/ink.cpp \
    src/inkcanvas.cpp \
    src/plugins.cpp \
    src/store.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
