QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = omascribe
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/document.h \
    src/inkcanvas.h \
    src/store.h \
    src/systemtheme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/document.cpp \
    src/inkcanvas.cpp \
    src/store.cpp \
    src/systemtheme.cpp

RESOURCES += src/resources.qrc
