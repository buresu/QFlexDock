// SPDX-License-Identifier: MIT
import QtQuick
import QFlexDock

// QML content of a dock panel. `dock` is the QmlDockController the application
// installed into the engine.
Rectangle {
    id: root
    color: palette.window

    SystemPalette { id: palette }

    component DockButton: Rectangle {
        id: button
        property alias text: label.text
        signal clicked()
        width: label.implicitWidth + 24
        height: label.implicitHeight + 12
        radius: 4
        color: area.pressed ? palette.highlight : palette.button
        border.color: palette.mid
        Text { id: label; anchors.centerIn: parent; color: palette.buttonText }
        MouseArea { id: area; anchors.fill: parent; onClicked: button.clicked() }
    }

    Column {
        anchors.centerIn: parent
        spacing: 10

        Text {
            color: palette.windowText
            text: "Active panel: " + (dock.activePanel || "none")
        }
        Text {
            color: palette.windowText
            // A panel object: its properties are bindable.
            text: "Console is " + (dock.panel("console").open ? "open" : "closed")
        }
        Row {
            spacing: 8
            DockButton { text: "Toggle console"; onClicked: dock.togglePanel("console") }
            DockButton { text: "Console below me"; onClicked: dock.movePanel("console", "qml", Dock.Bottom) }
        }
        Row {
            spacing: 8
            DockButton { text: "Float me"; onClicked: dock.floatPanel("qml") }
            DockButton { text: "Dock me"; onClicked: dock.dockPanel("qml") }
            DockButton {
                text: dock.maximizedPanel === "qml" ? "Restore" : "Maximize me"
                onClicked: dock.maximizedPanel === "qml" ? dock.restoreMaximizedPanel()
                                                         : dock.maximizePanel("qml")
            }
        }
        Text {
            visible: dock.lastError !== ""
            color: "#c0392b"
            text: dock.lastError
        }
    }
}
