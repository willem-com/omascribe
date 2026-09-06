import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
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

    // Keep in step with src/palette.h
    function resolvedInk(name) {
        if (name === "ink")
            return win.darkMode ? "#f2f0ea" : "#1a1a1a";
        if (name === "blue")
            return win.darkMode ? "#6db3e0" : "#1d6fa8";
        if (name === "red")
            return win.darkMode ? "#e07070" : "#c0392b";
        if (name === "gray")
            return win.darkMode ? "#a8adb4" : "#5c6370";
        return name;
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    Component.onCompleted: {
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
            titleField.focus = false;
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
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; onActivated: win.openExport() }
    Shortcut { sequence: "Ctrl+?"; context: Qt.ApplicationShortcut; onActivated: shortcutsDialog.open() }

    function openExport() {
        exportDialog.selectedFile = backend.suggestedExportUrl();
        exportDialog.open();
    }

    Dialogs.FileDialog {
        id: exportDialog
        title: "Export note"
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF (*.pdf)", "SVG (*.svg)"]
        onAccepted: backend.exportNote(selectedFile)
    }

    Item {
        anchors.fill: parent

        InkCanvas {
            id: canvas
            anchors.fill: parent
            anchors.leftMargin: (!win.compact && win.sidebarOpen) ? win.scaledSize(280) : 0
            document: backend.document
            paperColor: backend.paperColor
            gridColor: backend.gridColor
            darkMode: win.darkMode
            colorId: colorModel.get(colorRow.currentIndex).name
            inkWidth: widthModel.get(widthRow.currentIndex).value
            tool: toolModel.get(toolRow.currentIndex).value
            onEngaged: {
                titleField.focus = false;
                forceActiveFocus();
            }
        }

        TextInput {
            id: titleField
            anchors.top: parent.top
            anchors.left: canvas.left
            anchors.right: parent.right
            anchors.topMargin: win.scaledSize(18)
            anchors.leftMargin: win.scaledSize(win.compact ? 88 : 28)
            anchors.rightMargin: win.scaledSize(100)
            text: backend.document ? backend.document.title : ""
            color: win.textColor
            font.family: "iA Writer Quattro S"
            font.pixelSize: win.scaledSize(28)
            selectByMouse: true
            onTextChanged: {
                if (backend.document && backend.document.title !== text)
                    backend.document.title = text;
            }
            Keys.onReturnPressed: function(event) {
                focus = false;
                canvas.forceActiveFocus();
                event.accepted = true;
            }
            Keys.onEscapePressed: function(event) {
                focus = false;
                canvas.forceActiveFocus();
                event.accepted = true;
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
            id: notesChip
            visible: win.compact
            z: 21
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: win.scaledSize(12)
            height: win.scaledSize(32)
            width: notesChipLabel.width + win.scaledSize(22)
            radius: height / 2
            color: win.darkMode ? "#cc1a1a1a" : "#e6fffdf8"
            border.color: win.darkMode ? "#333333" : "#ddd6c8"
            Label {
                id: notesChipLabel
                anchors.centerIn: parent
                text: "Notes"
                color: win.textColor
                font.family: "iA Writer Quattro S"
                font.pixelSize: win.scaledSize(13)
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                onClicked: win.sidebarOpen = !win.sidebarOpen
            }
        }

        Rectangle {
            id: exportChip
            z: 21
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: win.scaledSize(12)
            height: win.scaledSize(32)
            width: exportChipLabel.width + win.scaledSize(22)
            radius: height / 2
            color: win.darkMode ? "#cc1a1a1a" : "#e6fffdf8"
            border.color: win.darkMode ? "#333333" : "#ddd6c8"
            Label {
                id: exportChipLabel
                anchors.centerIn: parent
                text: "Export"
                color: win.textColor
                font.family: "iA Writer Quattro S"
                font.pixelSize: win.scaledSize(13)
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -8
                cursorShape: Qt.PointingHandCursor
                onClicked: win.openExport()
            }
        }

        Rectangle {
            id: toolPill
            anchors.horizontalCenter: canvas.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: win.scaledSize(18)
            height: win.scaledSize(48)
            width: toolRow.width + colorRow.width + widthRow.width + win.scaledSize(56)
            radius: height / 2
            color: win.darkMode ? "#cc1a1a1a" : "#e6fffdf8"
            border.color: win.darkMode ? "#333333" : "#ddd6c8"
            z: 20

            Row {
                id: pillRow
                height: parent.height
                anchors.centerIn: parent
                spacing: win.scaledSize(10)

                Row {
                    id: toolRow
                    height: parent.height
                    spacing: 2
                    property int currentIndex: 0
                    Repeater {
                        model: toolModel
                        Item {
                            required property string name
                            required property string value
                            required property string hint
                            required property int index
                            width: win.scaledSize(28)
                            height: toolRow.height
                            ToolGlyph {
                                anchors.centerIn: parent
                                glyph: name
                                tip: hint
                                ink: toolRow.currentIndex === index ? win.accentColor : win.mutedColor
                                onClicked: {
                                    toolRow.currentIndex = index;
                                    canvas.tool = value;
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: 1
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    color: win.darkMode ? "#444" : "#d5cfc2"
                }

                Row {
                    id: colorRow
                    height: parent.height
                    spacing: 2
                    property int currentIndex: 0
                    Repeater {
                        model: colorModel
                        Item {
                            required property string name
                            required property int index
                            width: win.scaledSize(28)
                            height: colorRow.height
                            Rectangle {
                                width: 16
                                height: 16
                                radius: 8
                                anchors.centerIn: parent
                                color: win.resolvedInk(name)
                                border.color: colorRow.currentIndex === index ? win.textColor : (win.darkMode ? "#666" : "#ccc")
                                border.width: colorRow.currentIndex === index ? 2 : 1
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    colorRow.currentIndex = index;
                                    canvas.colorId = name;
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: 1
                    height: 18
                    anchors.verticalCenter: parent.verticalCenter
                    color: win.darkMode ? "#444" : "#d5cfc2"
                }

                Row {
                    id: widthRow
                    height: parent.height
                    spacing: 2
                    property int currentIndex: 1
                    Repeater {
                        model: widthModel
                        Item {
                            required property real value
                            required property int index
                            width: win.scaledSize(28)
                            height: widthRow.height
                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.max(5, value * 2.4)
                                height: Math.max(5, value * 2.4)
                                radius: width / 2
                                color: widthRow.currentIndex === index ? win.resolvedInk("ink") : win.mutedColor
                            }
                            MouseArea {
                                anchors.fill: parent
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

        Label {
            id: statusLabel
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 12
            anchors.bottomMargin: 10
            text: backend.status
            color: win.mutedColor
            opacity: 0
            font.family: "iA Writer Quattro S"
            font.pixelSize: win.scaledSize(11)
            Behavior on opacity { NumberAnimation { duration: 280 } }
        }

        Timer {
            id: statusHide
            interval: 1400
            onTriggered: statusLabel.opacity = 0
        }

        Connections {
            target: backend
            function onStatusChanged() {
                if (backend.status.length === 0)
                    return;
                statusLabel.opacity = 0.75;
                statusHide.restart();
            }
        }

        Rectangle {
            visible: win.compact && win.sidebarOpen
            anchors.fill: parent
            anchors.leftMargin: win.scaledSize(280)
            z: 35
            color: "#66000000"
            MouseArea {
                anchors.fill: parent
                onClicked: win.sidebarOpen = false
            }
        }

        Rectangle {
            id: sidebar
            width: win.scaledSize(280)
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            color: win.sidebarColor
            visible: win.sidebarOpen
            z: win.compact ? 40 : 10
            clip: true

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
    }

    ListModel {
        id: toolModel
        ListElement { name: "pen"; value: "pen"; hint: "Pen (P)" }
        ListElement { name: "eraser"; value: "eraser"; hint: "Eraser (E)" }
        ListElement { name: "select"; value: "select"; hint: "Select (V)" }
        ListElement { name: "ruler"; value: "ruler"; hint: "Ruler (L)" }
    }

    ListModel {
        id: colorModel
        ListElement { name: "ink" }
        ListElement { name: "blue" }
        ListElement { name: "red" }
        ListElement { name: "gray" }
    }

    ListModel {
        id: widthModel
        ListElement { value: 1.4 }
        ListElement { value: 2.4 }
        ListElement { value: 4.6 }
    }

    Dialog {
        id: shortcutsDialog
        modal: true
        title: "Omascribe"
        standardButtons: Dialog.Close
        anchors.centerIn: parent
        contentItem: Label {
            text: "Pen draws. Finger pans. Wheel pans.\nP  Pen\nE  Eraser (tilted block in the toolbar)\nV  Select\nL  Ruler\nCtrl+N  New note\nCtrl+E  Export PDF or SVG\nCtrl+Z  Undo\nDelete  Delete selection\nF11  Fullscreen\nNotes  (narrow window) opens the note list"
            lineHeight: 1.45
        }
    }

    component ToolGlyph: Item {
        id: g
        property string glyph
        property string tip
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
                    c.moveTo(6, 17);
                    c.lineTo(12, 7);
                    c.lineTo(22, 11);
                    c.lineTo(16, 21);
                    c.closePath();
                    c.stroke();
                    c.beginPath();
                    c.moveTo(9, 16);
                    c.lineTo(19, 12);
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
                }
            }
            Connections {
                target: g
                function onInkChanged() { cv.requestPaint(); }
                function onGlyphChanged() { cv.requestPaint(); }
            }
        }
        MouseArea {
            id: hit
            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: g.clicked()
        }
        ToolTip.visible: hit.containsMouse && g.tip.length > 0
        ToolTip.text: g.tip
        ToolTip.delay: 350
    }
}
