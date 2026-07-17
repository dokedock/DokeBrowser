import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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

    FileDialog {
        id: dokeExecutableDialog
        title: "选择 Doke Chromium"
        onAccepted: AppController.setSelectedProfileDokeExecutableFromUrl(selectedFile)
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
                    onClicked: {
                        searchInput.text = ""
                        groupFilter.currentIndex = 0
                        bottomTabs.currentIndex = 0
                        AppController.resetFilters()
                        AppController.createProfile()
                        Qt.callLater(function() { profileListView.positionViewAtEnd() })
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Button {
                        Layout.fillWidth: true
                        text: "环境管理"
                        highlighted: true
                        onClicked: envDialog.open()
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "代理池"
                        onClicked: {
                            AppController.refreshProxyPool()
                            proxyPoolDialog.open()
                        }
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

        Dialog {
            id: proxyPoolDialog
            modal: true
            title: "代理池"
            width: 980
            height: 640
            anchors.centerIn: parent

            background: Rectangle {
                color: theme.card
                radius: theme.radius
                border.width: 1
                border.color: theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Button { text: "刷新"; onClicked: AppController.refreshProxyPool() }
                    Button {
                        text: "导入"
                        onClicked: {
                            AppController.importProxyPool(proxyImportText.text)
                            proxyImportText.text = ""
                        }
                    }
                    Button { text: "一键分配（勾选）"; onClicked: AppController.assignProxyPoolToCheckedProfiles() }
                    Button { text: "一键释放（勾选）"; onClicked: AppController.releaseProxyPoolFromCheckedProfiles() }
                    Button { text: "换一个（当前）"; onClicked: AppController.rotateProxyForSelectedProfile() }
                    Button { text: "批量自检"; onClicked: AppController.testProxyPoolAll() }
                    Button { text: "取消自检"; onClicked: AppController.cancelProxyPoolTestBatch() }
                    Item { Layout.fillWidth: true }
                    Button { text: "关闭"; onClicked: proxyPoolDialog.close() }
                }

                TextArea {
                    id: proxyImportText
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    placeholderText: "每行一个代理：\nhttp://user:pass@host:port\nhttps://host:port\nsocks5://user:pass@host:port\n或：host:port:user:pass（默认 http）"
                    wrapMode: TextArea.Wrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: theme.radius
                    color: "#fafafa"
                    border.width: 1
                    border.color: theme.border

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Label { text: "状态"; Layout.preferredWidth: 80; color: theme.text2 }
                            Label { text: "类型"; Layout.preferredWidth: 80; color: theme.text2 }
                            Label { text: "地址"; Layout.preferredWidth: 260; color: theme.text2 }
                            Label { text: "账号"; Layout.preferredWidth: 160; color: theme.text2 }
                            Label { text: "最近IP"; Layout.preferredWidth: 160; color: theme.text2 }
                            Label { text: "占用"; Layout.fillWidth: true; color: theme.text2 }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: AppController.proxyPool
                            delegate: Rectangle {
                                required property string proxyId
                                required property string proxyType
                                required property string proxyHost
                                required property int proxyPort
                                required property string proxyUsername
                                required property bool proxyDisabled
                                required property bool proxyLastOk
                                required property string proxyLastIp
                                required property string proxyAssignedProfileId

                                width: ListView.view.width
                                height: 40
                                radius: theme.radius
                                color: "#ffffff"
                                border.width: 1
                                border.color: theme.border

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10

                                    Label {
                                        Layout.preferredWidth: 80
                                        text: proxyDisabled ? "禁用" : (proxyAssignedProfileId.length > 0 ? "占用" : "空闲")
                                        color: proxyDisabled ? "#cf1322" : (proxyAssignedProfileId.length > 0 ? "#fa8c16" : "#389e0d")
                                    }
                                    Label { Layout.preferredWidth: 80; text: proxyType; color: theme.text2 }
                                    Label { Layout.preferredWidth: 260; text: proxyHost + ":" + proxyPort; elide: Text.ElideRight; color: theme.text }
                                    Label { Layout.preferredWidth: 160; text: proxyUsername.length > 0 ? proxyUsername : "-"; elide: Text.ElideRight; color: theme.text2 }
                                    Label { Layout.preferredWidth: 160; text: proxyLastIp.length > 0 ? proxyLastIp : (proxyLastOk ? "-" : "-"); elide: Text.ElideRight; color: proxyLastOk ? "#52c41a" : theme.text2 }
                                    Label {
                                        Layout.fillWidth: true
                                        text: proxyAssignedProfileId.length > 0 ? proxyAssignedProfileId : "-"
                                        elide: Text.ElideRight
                                        color: theme.text2
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Dialog {
            id: envDialog
            modal: true
            title: "环境管理"
            implicitWidth: 560
            standardButtons: Dialog.Close
            anchors.centerIn: parent

            background: Rectangle {
                color: theme.card
                radius: theme.radius
                border.width: 1
                border.color: theme.border
            }

            contentItem: Item {
                implicitWidth: 520
                implicitHeight: content.implicitHeight + 24
                ColumnLayout {
                    id: content
                    x: 12
                    y: 12
                    width: parent.implicitWidth - 24
                    spacing: 12

                    Label {
                        text: AppController.selectedProfileIndex >= 0 ? ("当前 Profile：" + AppController.selectedProfileName) : "请先选择一个 Profile"
                        color: theme.text
                        font.pixelSize: 14
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 10

                        Label { text: "语言"; color: theme.text2 }
                        TextField { Layout.fillWidth: true; text: AppController.selectedProfileLanguage; enabled: false }

                        Label { text: "时区"; color: theme.text2 }
                        TextField { Layout.fillWidth: true; text: AppController.selectedProfileTimezone; enabled: false }

                        Label { text: "分辨率"; color: theme.text2 }
                        TextField { Layout.fillWidth: true; text: AppController.selectedProfileResolution; enabled: false }

                        Label { text: "触屏"; color: theme.text2 }
                        CheckBox { checked: AppController.selectedProfileTouchEnabled; enabled: false }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "一键随机指纹"
                            enabled: AppController.selectedProfileIndex >= 0
                            onClicked: AppController.randomizeSelectedFingerprint()
                        }
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
                    ComboBox {
                        id: groupFilter
                        Layout.preferredWidth: 160
                        model: AppController.groups
                        onActivated: {
                            AppController.setGroupFilter(currentText)
                            AppController.applyFilters()
                        }
                    }
                    TextField { id: searchInput; Layout.fillWidth: true; placeholderText: "输入 名称、备注、分组 等..." }
                    Button {
                        text: "搜索"
                        onClicked: {
                            AppController.setSearchKeyword(searchInput.text)
                            AppController.applyFilters()
                        }
                    }
                    Button {
                        text: "重置"
                        onClicked: {
                            searchInput.text = ""
                            groupFilter.currentIndex = 0
                            AppController.resetFilters()
                        }
                    }
                    Button {
                        id: batchBtn
                        text: "批量操作"
                        onClicked: {
                            var p = batchBtn.mapToItem(root.contentItem, 0, batchBtn.height)
                            batchMenu.popup(p.x, p.y)
                        }
                        Menu {
                            id: batchMenu
                            MenuItem { text: "全选（可见）"; onTriggered: AppController.checkAllVisibleProfiles() }
                            MenuItem { text: "反选（可见）"; onTriggered: AppController.invertCheckedVisibleProfiles() }
                            MenuItem { text: "清空（可见）"; onTriggered: AppController.uncheckAllVisibleProfiles() }
                            MenuSeparator { }
                            MenuItem { text: "勾选该组"; onTriggered: AppController.checkGroupProfiles(groupFilter.currentText) }
                            MenuItem { text: "取消该组"; onTriggered: AppController.uncheckGroupProfiles(groupFilter.currentText) }
                            MenuItem { text: "反选该组"; onTriggered: AppController.invertCheckedGroupProfiles(groupFilter.currentText) }
                            MenuSeparator { }
                            MenuItem {
                                text: "仅看勾选"
                                checkable: true
                                checked: AppController.showOnlyChecked
                                onTriggered: AppController.setShowOnlyChecked(!AppController.showOnlyChecked)
                            }
                        }
                    }
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
                                id: profileListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: AppController.filteredProfiles
                                delegate: Rectangle {
                                    required property int index
                                    required property string profileName
                                    required property string profileId
                                    required property string profileGroup
                                    required property string profileRemark
                                    required property string profileStatus
                                    required property string profileProxyLastIp
                                    required property bool profileProxyLastOk
                                    required property var profileLastOpenAtMs
                                    required property var profileCreatedAtMs

                                    width: ListView.view.width
                                    height: 42
                                    radius: theme.radius
                                    color: profileId === AppController.selectedProfileId ? "#e6f4ff" : "#ffffff"
                                    border.width: 1
                                    border.color: profileId === AppController.selectedProfileId ? "#91caff" : theme.border

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 10

                                        CheckBox {
                                            Layout.preferredWidth: 30
                                            checked: AppController.isProfileChecked(profileId)
                                            onClicked: AppController.setProfileChecked(profileId, checked)
                                        }
                                        Label { text: profileId.length > 8 ? profileId.slice(0, 8) : profileId; Layout.preferredWidth: 70; color: theme.text2 }
                                        RowLayout {
                                            Layout.preferredWidth: 60
                                            spacing: 6
                                            Rectangle {
                                                width: 10
                                                height: 10
                                                radius: 5
                                                color: profileStatus === "running" ? "#52c41a"
                                                    : profileStatus === "starting" ? theme.primary
                                                    : profileStatus === "stopping" ? "#fa8c16"
                                                    : (profileStatus === "crashed" || profileStatus === "error") ? "#cf1322"
                                                    : "#bfbfbf"
                                            }
                                            Label {
                                                text: profileStatus === "running" ? "运行"
                                                    : profileStatus === "starting" ? "启动中"
                                                    : profileStatus === "stopping" ? "停止中"
                                                    : profileStatus === "crashed" ? "崩溃"
                                                    : profileStatus === "error" ? "错误"
                                                    : "停止"
                                                color: theme.text2
                                            }
                                        }
                                        Label { text: profileName; Layout.preferredWidth: 200; color: theme.text }
                                        Label { text: profileGroup; Layout.preferredWidth: 140; color: theme.text2; elide: Text.ElideRight }
                                        Label {
                                            text: profileProxyLastIp.length > 0 ? profileProxyLastIp : "-"
                                            Layout.preferredWidth: 140
                                            color: profileProxyLastOk ? "#52c41a" : theme.text2
                                            elide: Text.ElideRight
                                        }
                                        Label { text: profileRemark; Layout.fillWidth: true; color: theme.text2; elide: Text.ElideRight }
                                        Label { text: root.fmtTs(profileLastOpenAtMs); Layout.preferredWidth: 150; color: theme.text2 }
                                        Label { text: root.fmtTs(profileCreatedAtMs); Layout.preferredWidth: 150; color: theme.text2 }
                                        Button {
                                            Layout.preferredWidth: 90
                                            text: "更多"
                                            onClicked: AppController.selectProfileById(profileId)
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        z: -1
                                        onClicked: AppController.selectProfileById(profileId)
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
                                Button {
                                    text: "运行"
                                    enabled: AppController.selectedProfileIndex >= 0 || AppController.checkedProfileIds.length > 0
                                    onClicked: AppController.runCheckedProfiles()
                                }
                                Button {
                                    text: "停止"
                                    enabled: AppController.selectedProfileIndex >= 0 || AppController.checkedProfileIds.length > 0
                                    onClicked: AppController.stopCheckedProfiles()
                                }
                                Button {
                                    text: "删除"
                                    enabled: AppController.selectedProfileIndex >= 0 || AppController.checkedProfileIds.length > 0
                                    onClicked: {
                                        if (AppController.checkedProfileIds.length > 0) {
                                            batchDeleteDialog.open()
                                        } else {
                                            AppController.deleteSelectedProfile()
                                        }
                                    }
                                }
                                Button {
                                    text: "设分组"
                                    visible: AppController.checkedProfileIds.length > 0 || AppController.selectedProfileIndex >= 0
                                    enabled: AppController.checkedProfileIds.length > 0 || AppController.selectedProfileIndex >= 0
                                    onClicked: groupDialog.open()
                                }
                                Button {
                                    text: "清空勾选"
                                    visible: AppController.checkedProfileIds.length > 0
                                    onClicked: AppController.clearCheckedProfiles()
                                }
                                Label {
                                    text: AppController.checkedProfileIds.length > 0 ? ("已选 " + AppController.checkedProfileIds.length) : ""
                                    color: theme.text2
                                }
                                Item { Layout.fillWidth: true }
                                Button { text: "清空日志"; onClicked: AppController.clearLogs() }
                            }

                            Dialog {
                                id: batchDeleteDialog
                                modal: true
                                title: "批量删除"
                                implicitWidth: 420
                                standardButtons: Dialog.Ok | Dialog.Cancel
                                contentItem: Label {
                                    padding: 12
                                    text: "确认删除已勾选的 " + AppController.checkedProfileIds.length + " 个 Profile？"
                                    color: theme.text
                                }
                                onAccepted: AppController.deleteCheckedProfiles()
                            }

                            Dialog {
                                id: groupDialog
                                modal: true
                                title: "设置分组"
                                implicitWidth: 420
                                standardButtons: Dialog.Ok | Dialog.Cancel
                                contentItem: Item {
                                    implicitWidth: 360
                                    implicitHeight: form.implicitHeight + 24
                                    ColumnLayout {
                                        id: form
                                        x: 12
                                        y: 12
                                        width: parent.implicitWidth - 24
                                        spacing: 10
                                        Label { text: "分组名"; color: theme.text2 }
                                        TextField { id: groupInput; Layout.preferredWidth: 320; placeholderText: "例如：A组"; text: "" }
                                    }
                                }
                                onAccepted: {
                                    AppController.setGroupForCheckedProfiles(groupInput.text)
                                    groupInput.text = ""
                                }
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
                                            Layout.columnSpan: 3
                                            text: AppController.selectedProfileDataDir
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileDataDir = text
                                        }

                                        Label { text: "浏览器内核"; color: theme.text2 }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            model: ["system_chrome", "doke_chromium"]
                                            currentIndex: Math.max(0, model.indexOf(AppController.selectedProfileBrowserEngine))
                                            onActivated: AppController.selectedProfileBrowserEngine = currentText
                                        }
                                        Label { text: "GeoIP"; color: theme.text2 }
                                        CheckBox {
                                            Layout.alignment: Qt.AlignVCenter
                                            checked: AppController.selectedProfileGeoipEnabled
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedProfileGeoipEnabled = checked
                                        }

                                        Label { text: "内核状态"; color: theme.text2 }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            spacing: 8
                                            TextField {
                                                Layout.fillWidth: true
                                                readOnly: true
                                                text: AppController.selectedProfileBrowserEngineStatus
                                            }
                                            Button {
                                                text: "刷新"
                                                enabled: AppController.ipcConnected
                                                onClicked: AppController.refreshEngineStatus()
                                            }
                                            Button {
                                                text: "检测"
                                                enabled: AppController.ipcConnected && AppController.selectedProfileIndex >= 0
                                                onClicked: AppController.probeSelectedBrowserEngine()
                                            }
                                        }

                                        Label {
                                            text: "Doke路径"
                                            color: theme.text2
                                            visible: AppController.selectedProfileBrowserEngine === "doke_chromium"
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            visible: AppController.selectedProfileBrowserEngine === "doke_chromium"
                                            spacing: 8
                                            TextField {
                                                Layout.fillWidth: true
                                                text: AppController.selectedProfileDokeExecutable
                                                enabled: AppController.selectedProfileIndex >= 0
                                                placeholderText: "/path/to/doke_chromium"
                                                onEditingFinished: AppController.selectedProfileDokeExecutable = text
                                            }
                                            Button {
                                                text: "选择"
                                                enabled: AppController.selectedProfileIndex >= 0
                                                onClicked: dokeExecutableDialog.open()
                                            }
                                        }

                                        Label {
                                            text: "Doke参数"
                                            color: theme.text2
                                            visible: AppController.selectedProfileBrowserEngine === "doke_chromium"
                                        }
                                        TextArea {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            Layout.preferredHeight: 54
                                            visible: AppController.selectedProfileBrowserEngine === "doke_chromium"
                                            text: AppController.selectedProfileDokeExtraArgs
                                            enabled: AppController.selectedProfileIndex >= 0
                                            wrapMode: TextArea.NoWrap
                                            placeholderText: "--flag=value"
                                            onActiveFocusChanged: if (!activeFocus) AppController.selectedProfileDokeExtraArgs = text
                                        }

                                        Label {
                                            text: "原生补丁"
                                            color: theme.text2
                                            visible: AppController.selectedProfileBrowserEngine === "doke_chromium"
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            visible: AppController.selectedProfileBrowserEngine === "doke_chromium"
                                            CheckBox {
                                                text: "指纹"
                                                checked: AppController.selectedProfileDokeNativeFingerprint
                                                enabled: AppController.selectedProfileIndex >= 0
                                                onToggled: AppController.selectedProfileDokeNativeFingerprint = checked
                                            }
                                            CheckBox {
                                                text: "代理"
                                                checked: AppController.selectedProfileDokeNativeProxy
                                                enabled: AppController.selectedProfileIndex >= 0
                                                onToggled: AppController.selectedProfileDokeNativeProxy = checked
                                            }
                                            CheckBox {
                                                text: "GeoIP"
                                                checked: AppController.selectedProfileDokeNativeGeoip
                                                enabled: AppController.selectedProfileIndex >= 0
                                                onToggled: AppController.selectedProfileDokeNativeGeoip = checked
                                            }
                                            CheckBox {
                                                text: "拟真"
                                                checked: AppController.selectedProfileDokeNativeHumanize
                                                enabled: AppController.selectedProfileIndex >= 0
                                                onToggled: AppController.selectedProfileDokeNativeHumanize = checked
                                            }
                                        }

                                        Label { text: "指纹Seed"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileFingerprintSeed
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileFingerprintSeed = text
                                        }
                                        Label { text: "拟真行为"; color: theme.text2 }
                                        CheckBox {
                                            Layout.alignment: Qt.AlignVCenter
                                            checked: AppController.selectedProfileHumanizeEnabled
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedProfileHumanizeEnabled = checked
                                        }

                                        Label { text: "启动URL"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            text: AppController.selectedProfileStartUrl
                                            enabled: AppController.selectedProfileIndex >= 0
                                            placeholderText: "about:blank"
                                            onEditingFinished: AppController.selectedProfileStartUrl = text
                                        }

                                        Label { text: "指纹策略"; color: theme.text2 }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            model: ["follow_ip", "random"]
                                            currentIndex: Math.max(0, model.indexOf(AppController.selectedProfileFingerprintMode))
                                            onActivated: AppController.selectedProfileFingerprintMode = currentText
                                        }
                                        Label { text: "生成随机"; color: theme.text2 }
                                        Button {
                                            text: "生成"
                                            enabled: AppController.selectedProfileIndex >= 0 && AppController.selectedProfileFingerprintMode === "random"
                                            onClicked: AppController.randomizeSelectedFingerprint()
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

                                        Label { text: "UA"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            text: AppController.selectedProfileUserAgent
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileUserAgent = text
                                        }

                                        Label { text: "平台"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfilePlatform
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfilePlatform = text
                                        }
                                        Label { text: "分辨率"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: AppController.selectedProfileResolution
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onEditingFinished: AppController.selectedProfileResolution = text
                                        }

                                        Label { text: "CPU 线程"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            text: AppController.selectedProfileHardwareConcurrency > 0 ? String(AppController.selectedProfileHardwareConcurrency) : ""
                                            onEditingFinished: {
                                                var v = parseInt(text.length > 0 ? text : "0")
                                                AppController.selectedProfileHardwareConcurrency = isNaN(v) ? 0 : v
                                            }
                                        }
                                        Label { text: "内存GB"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            text: AppController.selectedProfileDeviceMemoryGb > 0 ? String(AppController.selectedProfileDeviceMemoryGb) : ""
                                            onEditingFinished: {
                                                var v = parseInt(text.length > 0 ? text : "0")
                                                AppController.selectedProfileDeviceMemoryGb = isNaN(v) ? 0 : v
                                            }
                                        }

                                        Label { text: "DPR"; color: theme.text2 }
                                        TextField {
                                            Layout.fillWidth: true
                                            enabled: AppController.selectedProfileIndex >= 0
                                            text: AppController.selectedProfileDeviceScaleFactor > 0 ? String(AppController.selectedProfileDeviceScaleFactor) : ""
                                            onEditingFinished: {
                                                var v = parseFloat(text.length > 0 ? text : "0")
                                                AppController.selectedProfileDeviceScaleFactor = isNaN(v) ? 0 : v
                                            }
                                        }
                                        Label { text: "触屏"; color: theme.text2 }
                                        CheckBox {
                                            Layout.alignment: Qt.AlignVCenter
                                            checked: AppController.selectedProfileTouchEnabled
                                            enabled: AppController.selectedProfileIndex >= 0
                                            onToggled: AppController.selectedProfileTouchEnabled = checked
                                        }
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
                                            enabled: AppController.ipcConnected && (AppController.selectedProfileIndex >= 0 || AppController.checkedProfileIds.length > 0)
                                            onClicked: AppController.testCheckedProxies()
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
                                            enabled: AppController.ipcConnected && (AppController.selectedProfileIndex >= 0 || AppController.checkedProfileIds.length > 0)
                                            onClicked: AppController.startCheckedVpns()
                                        }
                                        Button {
                                            text: "停止 OpenVPN"
                                            enabled: AppController.ipcConnected && (AppController.selectedProfileIndex >= 0 || AppController.checkedProfileIds.length > 0)
                                            onClicked: AppController.stopCheckedVpns()
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
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        ComboBox {
                                            id: logMode
                                            Layout.preferredWidth: 140
                                            model: ["实时日志", "历史日志", "运行记录", "代理自检"]
                                            onActivated: AppController.setLogViewMode(currentText)
                                        }

                                        ComboBox {
                                            id: logScope
                                            Layout.preferredWidth: 110
                                            visible: logMode.currentText === "历史日志"
                                            model: ["当前Profile", "全局"]
                                        }

                                        TextField {
                                            id: historyKeyword
                                            Layout.fillWidth: true
                                            placeholderText: "关键字（可选）"
                                        }

                                        TextField {
                                            id: historyFrom
                                            Layout.preferredWidth: 130
                                            placeholderText: "开始 YYYY-MM-DD"
                                        }

                                        TextField {
                                            id: historyTo
                                            Layout.preferredWidth: 130
                                            placeholderText: "结束 YYYY-MM-DD"
                                        }

                                        Button {
                                            text: "加载"
                                            enabled: logMode.currentText !== "实时日志"
                                            onClicked: AppController.loadHistory(logMode.currentText, historyKeyword.text, historyFrom.text, historyTo.text, logScope.currentText)
                                        }

                                        Button {
                                            text: "导出"
                                            enabled: logMode.currentText !== "实时日志"
                                            onClicked: AppController.exportHistory(logMode.currentText, historyKeyword.text, historyFrom.text, historyTo.text, logScope.currentText)
                                        }

                                        Button {
                                            text: "复制"
                                            onClicked: {
                                                logCopyText.text = AppController.logs.dump()
                                                logCopyDialog.open()
                                            }
                                        }

                                        Button {
                                            text: "复制到剪贴板"
                                            onClicked: AppController.copyLogsToClipboard()
                                        }
                                    }

                                    ListView {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        model: AppController.logs
                                        delegate: Item {
                                            width: ListView.view.width
                                            height: logText.implicitHeight + 4

                                            Label {
                                                id: logText
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: model.text
                                                color: theme.text2
                                                font.pixelSize: 12
                                                wrapMode: Text.WordWrap
                                            }

                                            MouseArea {
                                                enabled: logMode.currentText === "历史日志" && logScope.currentText === "全局"
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    var t = model.text
                                                    if (!t || t.length < 10)
                                                        return
                                                    if (t[0] !== "[")
                                                        return
                                                    var r = /^\[([0-9a-fA-F]{8}|global)\]\s+/.exec(t)
                                                    if (!r)
                                                        return
                                                    var p = r[1]
                                                    if (p === "global") {
                                                        logMode.currentIndex = 0
                                                        AppController.setLogViewMode("实时日志")
                                                        return
                                                    }
                                                    if (AppController.selectProfileByIdPrefix(p)) {
                                                        logScope.currentIndex = 0
                                                        AppController.loadHistory("历史日志", historyKeyword.text, historyFrom.text, historyTo.text, "当前Profile")
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
        }

        Dialog {
            id: logCopyDialog
            modal: true
            title: "复制日志"
            width: 980
            height: 640
            anchors.centerIn: parent

            background: Rectangle {
                color: theme.card
                radius: theme.radius
                border.width: 1
                border.color: theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label { text: "提示：鼠标拖拽选择后 Cmd+C 复制"; color: theme.text2 }
                    Item { Layout.fillWidth: true }
                    Button { text: "关闭"; onClicked: logCopyDialog.close() }
                }

                TextArea {
                    id: logCopyText
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextArea.Wrap
                    text: ""
                }
            }
        }
    }
}
