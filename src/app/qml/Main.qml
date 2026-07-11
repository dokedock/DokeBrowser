import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 780
    title: "DokeBrowser"
    color: "#f5f5f5"

    QtObject {
        id: theme
        property color bg: "#f5f5f5"
        property color card: "#ffffff"
        property color border: "#d9d9d9"
        property color text: "#262626"
        property color text2: "#595959"
        property color primary: "#1677ff"
        property int radius: 10
    }

    header: ToolBar {
        background: Rectangle {
            color: "#ffffff"
            border.width: 1
            border.color: theme.border
        }
        RowLayout {
            anchors.fill: parent
            spacing: 10
            Label {
                text: "DokeBrowser"
                font.pixelSize: 16
                font.weight: 600
                color: theme.primary
                leftPadding: 10
            }
            Label { text: "（Qt6 + CEF）"; color: theme.text2 }
            Item { Layout.fillWidth: true }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Frame {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            padding: 12
            background: Rectangle {
                color: theme.card
                radius: theme.radius
                border.width: 1
                border.color: theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                Label { text: "Profiles"; font.pixelSize: 14; font.weight: 600; color: theme.text }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: 0
                    delegate: ItemDelegate { width: ListView.view.width; text: "" }
                }
            }
        }

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 12
            background: Rectangle {
                color: theme.card
                radius: theme.radius
                border.width: 1
                border.color: theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 10
                Label { text: "控制台"; color: theme.text; font.pixelSize: 14; font.weight: 600 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius
                    color: "#fafafa"
                    border.width: 1
                    border.color: theme.border
                    Label {
                        anchors.centerIn: parent
                        text: "下一步：Profile / 代理 / OpenVPN / CEF 浏览器实例"
                        color: theme.text2
                    }
                }
            }
        }
    }
}

