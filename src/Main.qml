import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import Omascribe

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: (backend.document && backend.document.title.length ? backend.document.title : "Note")
           + " - Omascribe"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color accentColor: backend.themeAccent
    readonly property color mutedColor: darkMode ? "#8d8e8e" : "#8a8d91"
    readonly property color sidebarColor: darkMode ? Qt.darker(pageColor, 1.18) : "#efece3"
    readonly property real textScale: backend.textScale
    readonly property bool compact: width < 880
    property bool sidebarOpen: !compact

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: accentColor
    color: pageColor

    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    Component.onCompleted: {
        colorModel.setProperty(0, "value", win.darkMode ? "#eeeeee" : "#222324");
        var geo = backend.windowGeometry();
        if (geo.width > 200 && geo.height > 200) {
            width = geo.width;
            height = geo.height;
            x = geo.x;
            y = geo.y;
        }
        if (geo.maximized)
            win.visibility = Window.Maximized;
    }

    onClosing: backend.saveWindowGeometry(x, y, width, height,
                                          visibility === Window.Maximized)

    onCompactChanged: {
        if (compact)
            sidebarOpen = false;
        else
            sidebarOpen = true;
    }

    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; onActivated: backend.newNote() }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; onActivated: backend.saveNow() }
    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        onActivated: if (backend.document) backend.document.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        context: Qt.WindowShortcut
        onActivated: if (backend.document) backend.document.redo()
    }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        context: Qt.WindowShortcut
        enabled: canvas.tool === "select" && backend.document && backend.document.selectedCount > 0
        onActivated: backend.document.deleteSelection()
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        onActivated: {
            if (backend.document)
                backend.document.clearSelection();
            if (compact)
                sidebarOpen = false;
        }
    }
    Shortcut { sequence: "P"; context: Qt.WindowShortcut; onActivated: canvas.tool = "pen" }
    Shortcut { sequence: "E"; context: Qt.WindowShortcut; onActivated: canvas.tool = "eraser" }
    Shortcut { sequence: "V"; context: Qt.WindowShortcut; onActivated: canvas.tool = "select" }
    Shortcut { sequence: "L"; context: Qt.WindowShortcut; onActivated: canvas.tool = "ruler" }
    Shortcut { sequences: ["Meta+F", "F11"]; context: Qt.ApplicationShortcut; onActivated: toggleFullScreen() }
    Shortcut { sequence: "Ctrl+?"; context: Qt.ApplicationShortcut; onActivated: shortcutsDialog.open() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            Layout.preferredWidth: win.sidebarOpen ? win.scaledSize(280) : 0
            Layout.fillHeight: true
            color: win.sidebarColor
            clip: true
            visible: width > 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: win.scaledSize(16)
                spacing: win.scaledSize(12)

                RowLayout {
                    Label {
                        text: "Notes"
                        color: win.textColor
                        font.family: "iA Writer Quattro S"
                        font.pixelSize: win.scaledSize(22)
                        Layout.fillWidth: true
                    }
                    ToolGlyph {
                        glyph: "plus"
                        ink: win.textColor
                        onClicked: backend.newNote()
                    }
                }

                ListView {
                    id: noteList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: backend.notes
                    clip: true
                    currentIndex: backend.currentIndex
                    spacing: 2
                    delegate: Rectangle {
                        required property string noteId
                        required property string title
                        required property string modifiedText
                        required property int strokeCount
                        required property int index
                        width: noteList.width
                        height: win.scaledSize(58)
                        radius: 8
                        color: index === backend.currentIndex
                               ? (win.darkMode ? "#1c1c1c" : "#ffffff")
                               : "transparent"
                        border.color: index === backend.currentIndex ? win.accentColor : "transparent"
                        border.width: 1

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                backend.openIndex(index);
                                if (win.compact)
                                    win.sidebarOpen = false;
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 2
                            Text {
                                text: title
                                color: win.textColor
                                elide: Text.ElideRight
                                width: parent.width
                                font.pixelSize: win.scaledSize(15)
                                font.family: "iA Writer Quattro S"
                            }
                            Text {
                                text: modifiedText + (strokeCount ? "  ·  " + strokeCount : "")
                                color: win.mutedColor
                                font.pixelSize: win.scaledSize(11)
                                font.family: "iA Writer Quattro S"
                            }
                        }
                    }
                }

                ToolGlyph {
                    glyph: "trash"
                    ink: win.mutedColor
                    Layout.alignment: Qt.AlignLeft
                    onClicked: backend.deleteCurrent()
                }
            }
        }

        Rectangle {
            width: 1
            Layout.fillHeight: true
            color: win.darkMode ? "#1f1f1f" : "#ddd8cc"
            visible: win.sidebarOpen
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            InkCanvas {
                id: canvas
                anchors.fill: parent
                document: backend.document
                paperColor: backend.paperColor
                gridColor: backend.gridColor
                darkMode: win.darkMode
                inkColor: colorModel.get(colorRow.currentIndex).value
                inkWidth: widthModel.get(widthRow.currentIndex).value
                tool: toolModel.get(toolRow.currentIndex).value
            }

            TextInput {
                id: titleField
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: win.scaledSize(18)
                anchors.leftMargin: win.scaledSize(win.compact ? 52 : 28)
                anchors.rightMargin: win.scaledSize(28)
                text: backend.document ? backend.document.title : ""
                color: win.textColor
                font.family: "iA Writer Quattro S"
                font.pixelSize: win.scaledSize(28)
                selectByMouse: true
                onTextChanged: {
                    if (backend.document && backend.document.title !== text)
                        backend.document.title = text;
                }
            }

            Text {
                anchors.fill: titleField
                text: "Title"
                color: win.mutedColor
                font: titleField.font
                visible: titleField.text.length === 0 && !titleField.activeFocus
            }

            Rectangle {
                id: toolPill
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: win.scaledSize(18)
                height: win.scaledSize(48)
                width: toolRow.width + colorRow.width + widthRow.width + win.scaledSize(52)
                radius: height / 2
                color: win.darkMode ? "#cc1a1a1a" : "#e6fffdf8"
                border.color: win.darkMode ? "#333333" : "#ddd6c8"
                z: 20

                Row {
                    id: pillRow
                    anchors.centerIn: parent
                    spacing: win.scaledSize(10)

                    Row {
                        id: toolRow
                        spacing: 4
                        property int currentIndex: 0
                        Repeater {
                            model: toolModel
                            ToolGlyph {
                                required property string name
                                required property string value
                                required property int index
                                glyph: name
                                ink: toolRow.currentIndex === index ? win.accentColor : win.mutedColor
                                onClicked: {
                                    toolRow.currentIndex = index;
                                    canvas.tool = value;
                                }
                            }
                        }
                    }

                    Rectangle { width: 1; height: 22; color: win.darkMode ? "#444" : "#d5cfc2"; anchors.verticalCenter: parent.verticalCenter }

                    Row {
                        id: colorRow
                        spacing: 8
                        property int currentIndex: 0
                        Repeater {
                            model: colorModel
                            Rectangle {
                                required property color value
                                required property int index
                                width: 18
                                height: 18
                                radius: 9
                                color: value
                                border.color: colorRow.currentIndex === index ? win.textColor : (win.darkMode ? "#666" : "#ccc")
                                border.width: colorRow.currentIndex === index ? 2 : 1
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -6
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        colorRow.currentIndex = index;
                                        canvas.inkColor = value;
                                    }
                                }
                            }
                        }
                    }

                    Rectangle { width: 1; height: 22; color: win.darkMode ? "#444" : "#d5cfc2"; anchors.verticalCenter: parent.verticalCenter }

                    Row {
                        id: widthRow
                        spacing: 10
                        property int currentIndex: 1
                        Repeater {
                            model: widthModel
                            Rectangle {
                                required property real value
                                required property int index
                                width: 18
                                height: 18
                                color: "transparent"
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: Math.max(4, value * 2.2)
                                    height: Math.max(4, value * 2.2)
                                    radius: width / 2
                                    color: widthRow.currentIndex === index ? win.textColor : win.mutedColor
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -6
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        widthRow.currentIndex = index;
                                        canvas.inkWidth = value;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ToolGlyph {
                visible: win.compact
                glyph: "menu"
                ink: win.mutedColor
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: win.scaledSize(14)
                z: 21
                onClicked: win.sidebarOpen = !win.sidebarOpen
            }

            Label {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 12
                anchors.bottomMargin: 10
                text: backend.status
                color: win.mutedColor
                opacity: 0.7
                font.family: "iA Writer Quattro S"
                font.pixelSize: win.scaledSize(11)
            }
        }
    }

    ListModel {
        id: toolModel
        ListElement { name: "pen"; value: "pen" }
        ListElement { name: "eraser"; value: "eraser" }
        ListElement { name: "select"; value: "select" }
        ListElement { name: "ruler"; value: "ruler" }
    }

    ListModel {
        id: colorModel
        ListElement { value: "#222324" }
        ListElement { value: "#2077b2" }
        ListElement { value: "#b42318" }
        ListElement { value: "#6b7280" }
    }

    ListModel {
        id: widthModel
        ListElement { value: 1.4 }
        ListElement { value: 2.4 }
        ListElement { value: 4.6 }
    }

    Connections {
        target: backend
        function onDarkModeChanged() {
            colorModel.setProperty(0, "value", win.darkMode ? "#eeeeee" : "#222324");
            if (colorRow.currentIndex === 0)
                canvas.inkColor = win.darkMode ? "#eeeeee" : "#222324";
        }
        function onThemeColorsChanged() {
            colorModel.setProperty(1, "value", win.accentColor);
        }
    }

    Dialog {
        id: shortcutsDialog
        modal: true
        title: "Omascribe"
        standardButtons: Dialog.Close
        anchors.centerIn: parent
        contentItem: Label {
            text: "Pen draws. Finger pans. Wheel pans.\nP  Pen\nE  Eraser\nV  Select\nL  Ruler\nCtrl+N  New note\nCtrl+Z  Undo\nDelete  Delete selection\nF11  Fullscreen"
            lineHeight: 1.45
        }
    }

    component ToolGlyph: Item {
        id: g
        property string glyph
        property color ink: "#666"
        signal clicked()
        width: 28
        height: 28
        Canvas {
            id: cv
            readonly property real dpr: Screen.devicePixelRatio
            width: g.width * dpr
            height: g.height * dpr
            transformOrigin: Item.TopLeft
            scale: 1 / dpr
            onDprChanged: requestPaint()
            onPaint: {
                var c = getContext("2d");
                c.setTransform(dpr, 0, 0, dpr, 0, 0);
                c.clearRect(0, 0, width, height);
                c.strokeStyle = g.ink;
                c.fillStyle = g.ink;
                c.lineWidth = 1.5;
                c.lineCap = "round";
                c.lineJoin = "round";
                c.beginPath();
                if (g.glyph === "pen") {
                    c.moveTo(7, 21); c.lineTo(9, 13); c.lineTo(19, 7); c.lineTo(21, 9); c.lineTo(15, 19); c.lineTo(7, 21);
                    c.stroke();
                } else if (g.glyph === "eraser") {
                    c.rect(7, 9, 14, 10);
                    c.stroke();
                } else if (g.glyph === "select") {
                    c.setLineDash([3, 2]);
                    c.ellipse(7, 7, 14, 14);
                    c.stroke();
                    c.setLineDash([]);
                } else if (g.glyph === "ruler") {
                    c.moveTo(6, 20); c.lineTo(22, 8);
                    c.moveTo(10, 17); c.lineTo(8, 14);
                    c.moveTo(14, 14); c.lineTo(12, 11);
                    c.moveTo(18, 11); c.lineTo(16, 8);
                    c.stroke();
                } else if (g.glyph === "plus") {
                    c.moveTo(14, 7); c.lineTo(14, 21);
                    c.moveTo(7, 14); c.lineTo(21, 14);
                    c.stroke();
                } else if (g.glyph === "trash") {
                    c.moveTo(9, 10); c.lineTo(19, 10); c.lineTo(18, 21); c.lineTo(10, 21); c.closePath();
                    c.moveTo(11, 8); c.lineTo(17, 8);
                    c.stroke();
                } else if (g.glyph === "menu") {
                    c.moveTo(7, 10); c.lineTo(21, 10);
                    c.moveTo(7, 14); c.lineTo(21, 14);
                    c.moveTo(7, 18); c.lineTo(21, 18);
                    c.stroke();
                }
            }
            Connections {
                target: g
                function onInkChanged() { cv.requestPaint(); }
                function onGlyphChanged() { cv.requestPaint(); }
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            cursorShape: Qt.PointingHandCursor
            onClicked: g.clicked()
        }
    }
}
