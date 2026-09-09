import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Qt5Compat.GraphicalEffects
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
    // Id of the typed-text block being edited, "" when none.
    property string editingId: ""

    function focusText(id) {
        editingId = id;
        for (var i = 0; i < textLayer.count; ++i) {
            var item = textLayer.itemAt(i);
            if (item && item.textId === id) {
                item.forceActiveFocus();
                return;
            }
        }
    }

    function leaveText() {
        editingId = "";
        canvas.forceActiveFocus();
    }

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
            win.leaveText();
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
    Shortcut { sequence: "T"; context: Qt.WindowShortcut; onActivated: canvas.tool = "text" }
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
            objectName: "canvas"
            anchors.fill: parent
            anchors.leftMargin: (!win.compact && win.sidebarOpen) ? win.scaledSize(280) : 0
            document: backend.document
            paperColor: backend.paperColor
            gridColor: backend.gridColor
            darkMode: win.darkMode
            colorId: colorModel.get(colorRow.currentIndex).name
            inkWidth: widthModel.get(widthRow.currentIndex).value
            onEngaged: {
                titleField.focus = false;
                forceActiveFocus();
            }
            onToolChanged: {
                for (var i = 0; i < toolModel.count; ++i) {
                    if (toolModel.get(i).value === tool)
                        toolRow.currentIndex = i;
                }
            }
            Component.onCompleted: tool = "pen"

            // Tap: pick the block under the finger, else leave the one being
            // edited, else start a new block whose margins follow the ink.
            onTextTapped: function(x, y) {
                var doc = backend.document;
                if (!doc)
                    return;
                var hit = doc.textAt(x, y);
                if (hit.length) {
                    win.focusText(hit);
                    return;
                }
                if (win.editingId.length) {
                    win.leaveText();
                    return;
                }
                var id = doc.addTextAt(x, y, canvas.width);
                Qt.callLater(function() { win.focusText(id); });
            }
        }

        // Typed text lives in document space, translated by the canvas scroll.
        Item {
            anchors.fill: canvas
            clip: true
            Repeater {
                id: textLayer
                model: backend.document ? backend.document.texts : null
                delegate: TextEdit {
                    id: block
                    required property string textId
                    required property real bx
                    required property real by
                    required property real bw
                    required property string body
                    required property real size
                    x: bx
                    y: by - canvas.viewY
                    width: bw
                    text: body
                    wrapMode: TextEdit.Wrap
                    font.family: "iA Writer Quattro S"
                    font.styleName: "Regular"
                    font.pixelSize: size
                    color: win.resolvedInk("ink")
                    selectionColor: win.accentColor
                    selectByMouse: true
                    // Inert under the pen unless it is being edited or the text
                    // tool is active, so ink can go over text.
                    enabled: activeFocus || textId === win.editingId || canvas.tool === "text"
                    onTextChanged: {
                        if (backend.document && text !== body)
                            backend.document.setTextContent(textId, text);
                    }
                    onActiveFocusChanged: {
                        if (activeFocus)
                            win.editingId = textId;
                        else if (win.editingId === textId)
                            win.editingId = "";
                        if (!activeFocus && backend.document && text.trim().length === 0)
                            backend.document.removeText(textId);
                    }
                    Keys.onEscapePressed: function(event) {
                        win.leaveText();
                        event.accepted = true;
                    }
                }
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
            clip: true
            // Only follow the caret while editing. Otherwise a title wider than
            // the field (compact window) scrolls to its tail and looks empty.
            autoScroll: activeFocus
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
            width: toolRow.width + historyRow.width + colorRow.width + widthRow.width + win.scaledSize(68)
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
                    id: historyRow
                    height: parent.height
                    spacing: 2
                    Item {
                        width: win.scaledSize(28)
                        height: historyRow.height
                        ToolGlyph {
                            anchors.centerIn: parent
                            glyph: "undo"
                            tip: "Undo (Ctrl+Z, two-finger tap)"
                            ink: (backend.document && backend.document.canUndo) ? win.textColor : win.mutedColor
                            onClicked: if (backend.document) backend.document.undo()
                        }
                    }
                    Item {
                        width: win.scaledSize(28)
                        height: historyRow.height
                        ToolGlyph {
                            anchors.centerIn: parent
                            glyph: "redo"
                            tip: "Redo (Ctrl+Shift+Z, three-finger tap)"
                            ink: (backend.document && backend.document.canRedo) ? win.textColor : win.mutedColor
                            onClicked: if (backend.document) backend.document.redo()
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
        ListElement { name: "text"; value: "text"; hint: "Text (T): tap to type" }
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
            text: "Pen draws. Two fingers scroll. Wheel scrolls.\nOne finger does nothing.\nLower stylus button  Eraser (hold, or tap to toggle)\nTwo-finger tap  Undo\nThree-finger tap  Redo\nOne-finger tap  Type text there (tap a block to edit it)\nP  Pen\nE  Eraser\nV  Select\nL  Ruler\nT  Text tool (click to type)\nCtrl+N  New note\nCtrl+E  Export PDF or SVG\nCtrl+Z  Undo\nCtrl+Shift+Z  Redo\nDelete  Delete selection\nF11  Fullscreen"
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

        Image {
            id: ico
            anchors.centerIn: parent
            width: 18
            height: 18
            source: "qrc:/icons/" + g.glyph + ".svg"
            sourceSize.width: 36
            sourceSize.height: 36
            visible: false
        }
        ColorOverlay {
            anchors.fill: ico
            source: ico
            color: g.ink
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
