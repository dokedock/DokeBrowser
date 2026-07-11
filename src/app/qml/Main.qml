import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 820
    title: "DokeBrowser"
    color: "#f5f5f5"

    function fmtTs(ms) {
        if (!ms || ms <= 0)
            return ""
        return Qt.formatDateTime(new Date(ms), "yyyy-MM-dd HH:mm:ss")
    }

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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Frame {
            Layout.preferredWidth: 200
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
                spacing: 12

                Button {
                    Layout.fillWidth: true
                    text: "新建浏览器"
                    onClicked: AppController.createProfile()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: "环境管理"
                        highlighted: true
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "代理池"
                        enabled: false
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "VPN"
                        enabled: false
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "系统设置"
                        enabled: false
                    }
                }

                Item { Layout.fillHeight: true }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: AppController.agentRunning ? "Agent 运行中" : "Agent 未运行"; color: AppController.agentRunning ? "#389e0d" : "#cf1322" }
                    Label { text: AppController.ipcConnected ? "IPC 已连接" : "IPC 未连接"; color: AppController.ipcConnected ? "#389e0d" : "#cf1322" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Button { Layout.fillWidth: true; text: "启动"; onClicked: AppController.startAgent() }
                        Button { Layout.fillWidth: true; text: "停止"; enabled: AppController.agentRunning; onClicked: AppController.stopAgent() }
                    }
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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ComboBox { Layout.preferredWidth: 160; model: ["所有分组"] }
                    TextField { Layout.fillWidth: true; placeholderText: "输入 名称、备注、分组 等..." }
                    Button { text: "搜索" }
                    Button { text: "重置" }
                    Item { Layout.fillWidth: true }
                }

                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Vertical

                    Item {
                        SplitView.fillWidth: true
                        SplitView.preferredHeight: 460

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 36
                                color: "#fafafa"
                                radius: theme.radius
                                border.width: 1
                                border.color: theme.border
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    Label { text: ""; Layout.preferredWidth: 30; color: theme.text2 }
                                    Label { text: "ID"; Layout.preferredWidth: 70; color: theme.text2 }
                                    Label { text: "状态"; Layout.preferredWidth: 60; color: theme.text2 }
                                    Label { text: "名称"; Layout.preferredWidth: 200; color: theme.text2 }
                                    Label { text: "分组"; Layout.preferredWidth: 140; color: theme.text2 }
                                    Label { text: "IP"; Layout.preferredWidth: 140; color: theme.text2 }
                                    Label { text: "备注"; Layout.fillWidth: true; color: theme.text2 }
                                    Label { text: "最近打开"; Layout.preferredWidth: 150; color: theme.text2 }
                                    Label { text: "创建时间"; Layout.preferredWidth: 150; color: theme.text2 }
                                    Label { text: "操作"; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignHCenter; color: theme.text2 }
                                }
                            }

                            ListView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: AppController.profiles
                                delegate: Rectangle {
                                    required property int index
                                    required property string profileName
                                    required property string profileId
                                    required property string profileGroup
                                    required property string profileRemark
                                    required property string profileStatus
                                    required property var profileLastOpenAtMs
                                    required property var profileCreatedAtMs

                                    width: ListView.view.width
                                    height: 42
                                    radius: theme.radius
                                    color: index === AppController.selectedProfileIndex ? "#e6f4ff" : "#ffffff"
                                    border.width: 1
                                    border.color: index === AppController.selectedProfileIndex ? "#91caff" : theme.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 10

                                        CheckBox { Layout.preferredWidth: 30 }
                                        Label { text: profileId.length > 8 ? profileId.slice(0, 8) : profileId; Layout.preferredWidth: 70; color: theme.text2 }
                                        RowLayout {
                                            Layout.preferredWidth: 60
                                            spacing: 6
                                            Rectangle { width: 10; height: 10; radius: 5; color: profileStatus === "running" ? "#52c41a" : "#bfbfbf" }
                                            Label { text: profileStatus === "running" ? "运行" : "停止"; color: theme.text2 }
                                        }
                                        Label { text: profileName; Layout.preferredWidth: 200; color: theme.text }
                                        Label { text: profileGroup; Layout.preferredWidth: 140; color: theme.text2; elide: Text.ElideRight }
                                        Label { text: "-"; Layout.preferredWidth: 140; color: theme.text2 }
                                        Label { text: profileRemark; Layout.fillWidth: true; color: theme.text2; elide: Text.ElideRight }
                                        Label { text: root.fmtTs(profileLastOpenAtMs); Layout.preferredWidth: 150; color: theme.text2 }
                                        Label { text: root.fmtTs(profileCreatedAtMs); Layout.preferredWidth: 150; color: theme.text2 }
                                        Button {
                                            Layout.preferredWidth: 90
                                            text: "更多"
                                            onClicked: AppController.selectedProfileIndex = index
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: AppController.selectedProfileIndex = index
                                    }
                                }
                            }
                        }
                    }

                    Frame {
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                        padding: 10
                        background: Rectangle {
                            color: "#fafafa"
                            radius: theme.radius
                            border.width: 1
                            border.color: theme.border
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Button { text: "运行"; enabled: AppController.selectedProfileIndex >= 0; onClicked: AppController.runSelectedProfile() }
                                Button { text: "停止"; enabled: AppController.selectedProfileIndex >= 0; onClicked: AppController.stopSelectedProfile() }
                                Button { text: "删除"; enabled: AppController.selectedProfileIndex >= 0; onClicked: AppController.deleteSelectedProfile() }
                                Item { Layout.fillWidth: true }
                                Button { text: "清空日志"; onClicked: AppController.clearLogs() }
                            }

                            TabBar {
                                id: bottomTabs
                                Layout.fillWidth: true
                                TabButton { text: "基础信息" }
                                TabButton { text: "代理" }
                                TabButton { text: "VPN" }
                                TabButton { text: "日志" }
                            }

                            StackLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: bottomTabs.currentIndex

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 4
                                        columnSpacing: 10
                                        rowSpacing: 8

                                        Label { text: "ID"; color: theme.text2 }
                                        TextField { Layout.fillWidth: true; readOnly: true; text: AppController.selectedProfileId }
                                        Label { text: "状态"; color: theme.text2 }
                                        TextField { Layout.fillWidth: true; readOnly: true; text: AppController.selectedProfileStatus }

                                        Label { text: "名称"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileName
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileName = text
                                        }
                                        Label { text: "分组"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileGroup
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileGroup = text
                                        }

                                        Label { text: "数据目录"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileDataDir
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileDataDir = text
                                        }
                                        Label { text: "分辨率"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileResolution
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileResolution = text
                                        }

                                        Label { text: "语言"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileLanguage
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileLanguage = text
                                        }
                                        Label { text: "时区"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileTimezone
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileTimezone = text
                                        }

                                        Label { text: "触屏"; color: theme.text2 }
                                        CheckBox {
                                            Layout.alignment: Qt.AlignVCenter
                                            checked: AppController.selectedProfileTouchEnabled
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedProfileTouchEnabled = checked
                                        }
                                        Item { }
                                        Item { }

                                        Label { text: "创建时间"; color: theme.text2 }
                                        TextField { Layout.fillWidth: true; readOnly: true; text: root.fmtTs(AppController.selectedProfileCreatedAtMs) }
                                        Label { text: "最近打开"; color: theme.text2 }
                                        TextField { Layout.fillWidth: true; readOnly: true; text: root.fmtTs(AppController.selectedProfileLastOpenAtMs) }
                                    }

                                    Label { text: "备注"; color: theme.text2 }
                                    TextArea {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        enabled: AppController.selectedProfileIndex >= 0
                                        text: AppController.selectedProfileRemark
                                        onActiveFocusChanged: if (!activeFocus) AppController.selectedProfileRemark = text
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 4
                                        columnSpacing: 10
                                        rowSpacing: 8

                                        Label { text: "启用代理"; color: theme.text2 }
                                        CheckBox {
                                            checked: AppController.selectedProxyEnabled
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedProxyEnabled = checked
                                        }
                                        Label { text: "类型"; color: theme.text2 }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            model: ["direct", "http", "https", "socks5"]
                                            currentIndex: Math.max(0, model.indexOf(AppController.selectedProxyType))
                                            onActivated: AppController.selectedProxyType = currentText
                                        }

                                        Label { text: "Host"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedProxyHost
                                            onEditingFinished: AppController.selectedProxyHost = text
                                        }
                                        Label { text: "Port"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            text: AppController.selectedProxyPort > 0 ? String(AppController.selectedProxyPort) : ""
                                            onEditingFinished: {
                                                var v = parseInt(text.length > 0 ? text : "0")
                                                AppController.selectedProxyPort = isNaN(v) ? 0 : v
                                            }
                                        }

                                        Label { text: "用户名"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedProxyUsername
                                            onEditingFinished: AppController.selectedProxyUsername = text
                                        }
                                        Label { text: "密码"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            echoMode: TextInput.Password
                                            text: AppController.selectedProxyPassword
                                            onEditingFinished: AppController.selectedProxyPassword = text
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Button {
                                            text: "测试代理"
                                            enabled: AppController.selectedProfileIndex >= 0 && AppController.ipcConnected
                                            onClicked: AppController.testSelectedProxy()
                                        }
                                        Label { text: AppController.proxyLastTestSummary; color: theme.text2; Layout.fillWidth: true; elide: Text.ElideRight }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        Label { text: "状态：" + AppController.selectedVpnStatus; color: theme.text2 }
                                        Item { Layout.fillWidth: true }
                                        Button {
                                            text: "启动 OpenVPN"
                                            enabled: AppController.selectedProfileIndex >= 0 && AppController.ipcConnected
                                            onClicked: AppController.startSelectedVpn()
                                        }
                                        Button {
                                            text: "停止 OpenVPN"
                                            enabled: AppController.selectedProfileIndex >= 0 && AppController.ipcConnected
                                            onClicked: AppController.stopSelectedVpn()
                                        }
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: 4
                                        columnSpacing: 10
                                        rowSpacing: 8

                                        Label { text: "启用 VPN"; color: theme.text2 }
                                        CheckBox {
                                            checked: AppController.selectedVpnEnabled
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedVpnEnabled = checked
                                        }
                                        Label { text: "OpenVPN 可执行"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedOpenvpnExe
                                            onEditingFinished: AppController.selectedOpenvpnExe = text
                                        }

                                        Label { text: "OpenVPN 配置"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedOpenvpnConfig
                                            onEditingFinished: AppController.selectedOpenvpnConfig = text
                                        }
                                        Item { }
                                        Item { }

                                        Label { text: "走 SOCKS"; color: theme.text2 }
                                        CheckBox {
                                            checked: AppController.selectedOpenvpnUseSocks
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedOpenvpnUseSocks = checked
                                        }
                                        Label { text: "SOCKS Host"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedOpenvpnSocksHost
                                            onEditingFinished: AppController.selectedOpenvpnSocksHost = text
                                        }

                                        Label { text: "SOCKS Port"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            text: AppController.selectedOpenvpnSocksPort > 0 ? String(AppController.selectedOpenvpnSocksPort) : ""
                                            onEditingFinished: {
                                                var v = parseInt(text.length > 0 ? text : "0")
                                                AppController.selectedOpenvpnSocksPort = isNaN(v) ? 0 : v
                                            }
                                        }
                                        Label { text: "SOCKS 用户名"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedOpenvpnSocksUsername
                                            onEditingFinished: AppController.selectedOpenvpnSocksUsername = text
                                        }

                                        Label { text: "SOCKS 密码"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            echoMode: TextInput.Password
                                            text: AppController.selectedOpenvpnSocksPassword
                                            onEditingFinished: AppController.selectedOpenvpnSocksPassword = text
                                        }
                                        Item { }
                                        Item { }
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 6
                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        model: AppController.logs
                                        delegate: Label {
                                            width: ListView.view.width
                                            text: model.text
                                            color: theme.text2
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
