import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1320
    height: 850
    minimumWidth: 1050
    minimumHeight: 700
    visible: true
    title: "XIN Power · Studio 6.2.3"
    font.family: "Microsoft YaHei"
    color: "#0A1018"
    property int activePage: 0
    property var pid: bridge.pidController
    property color mint: "#3DE2B4"
    property color textMain: "#F1F6FA"
    property color textDim: "#8C9BAC"
    property color cardColor: "#151E29"
    property color borderColor: "#273644"

    component Panel: Rectangle {
        color: root.cardColor
        radius: 20
        border.width: 1
        border.color: root.borderColor
    }

    component ActionButton: Button {
        id: action
        property color fillColor: root.mint
        property color inkColor: "#081B19"
        implicitHeight: 44
        implicitWidth: 130
        font.pixelSize: 15
        font.weight: Font.DemiBold
        background: Rectangle {
            radius: 11
            color: action.enabled ? (action.down ? Qt.darker(action.fillColor, 1.18) : action.fillColor) : "#263544"
            border.color: action.enabled ? action.fillColor : "#425366"
            border.width: 1
        }
        contentItem: Text {
            text: action.text
            color: action.enabled ? action.inkColor : "#B5C2CE"
            font: action.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    component Field: TextField {
        implicitHeight: 47
        color: root.textMain
        placeholderTextColor: "#8191A2"
        selectionColor: "#276D62"
        selectedTextColor: "#FFFFFF"
        font.pixelSize: 18
        leftPadding: 14
        rightPadding: 14
        verticalAlignment: TextInput.AlignVCenter
        selectByMouse: true
        background: Rectangle {
            radius: 11
            color: "#101B27"
            border.width: 1
            border.color: parent.activeFocus ? root.mint : "#344555"
        }
    }

    component StudioScrollBar: ScrollBar {
        id: bar
        minimumSize: 0.10
        padding: 2
        policy: ScrollBar.AlwaysOn
        contentItem: Rectangle {
            implicitWidth: 14
            implicitHeight: 14
            radius: 6
            color: bar.pressed ? root.mint : (bar.hovered ? "#7BA596" : "#5E7C91")
        }
        background: Rectangle { color: "#0D1722"; radius: 7; border.color: "#304252" }
    }

    component StudioCombo: ComboBox {
        id: combo
        implicitHeight: 42
        font.pixelSize: 14
        contentItem: Text {
            text: combo.displayText
            color: combo.enabled ? root.textMain : root.textDim
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 12
            rightPadding: 28
        }
        indicator: Text { text: "⌄"; color: root.mint; font.pixelSize: 18; anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter }
        background: Rectangle { radius: 10; color: "#101B27"; border.color: combo.activeFocus ? root.mint : "#344555" }
        delegate: ItemDelegate {
            width: combo.popup.width - 10
            height: 38
            highlighted: combo.highlightedIndex === index
            contentItem: Text { text: modelData; color: root.textMain; font.pixelSize: 14; verticalAlignment: Text.AlignVCenter; leftPadding: 8; elide: Text.ElideRight }
            background: Rectangle { radius: 7; color: parent.highlighted ? "#1C5652" : (parent.hovered ? "#223241" : "transparent") }
        }
        popup: Popup {
            y: combo.height + 4
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight + 10, 340)
            padding: 5
            contentItem: ListView { clip: true; implicitHeight: contentHeight; model: combo.popup.visible ? combo.delegateModel : null; currentIndex: combo.highlightedIndex; ScrollBar.vertical: StudioScrollBar { } }
            background: Rectangle { radius: 11; color: "#172330"; border.color: root.borderColor }
        }
    }

    component NavButton: Rectangle {
        id: nav
        property int page: 0
        property string caption: ""
        property string glyph: "•"
        Layout.fillWidth: true
        height: 49
        radius: 11
        color: root.activePage === page ? "#193E3E" : (mouse.containsMouse ? "#1A2734" : "transparent")
        border.color: root.activePage === page ? "#2B8A78" : "transparent"
        border.width: 1
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 19
            spacing: 16
            Text { text: nav.glyph; color: root.activePage === nav.page ? root.mint : root.textDim; font.pixelSize: 20; width: 24 }
            Text { text: nav.caption; color: root.activePage === nav.page ? root.textMain : root.textDim; font.pixelSize: 15; font.weight: root.activePage === nav.page ? Font.DemiBold : Font.Normal }
        }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.activePage = nav.page }
    }

    Connections {
        target: bridge
        function onTelemetryChanged() {
            if (!voltageField.activeFocus && !voltageField.dirty) voltageField.text = bridge.setVoltage
            if (!currentField.activeFocus && !currentField.dirty) currentField.text = bridge.setCurrent
        }
    }

    header: Rectangle {
        height: 78
        color: "#0F1822"
        border.color: root.borderColor
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 30
            anchors.rightMargin: 28
            spacing: 16
            Rectangle { width: 37; height: 37; radius: 10; color: "#1B5D54"; Text { anchors.centerIn: parent; text: "X"; color: root.mint; font.pixelSize: 27; font.bold: true } }
            Column {
                spacing: 1
                Text { text: "XIN Power"; color: root.textMain; font.pixelSize: 21; font.bold: true }
                Text { text: "可调电源控制台"; color: root.textDim; font.pixelSize: 10; font.letterSpacing: 2 }
            }
            Item { Layout.fillWidth: true }
            Text { text: "3.2–32 V  /  8 A"; color: root.textDim; font.pixelSize: 13 }

        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 302
            Layout.fillHeight: true
            color: "#101A25"
            border.color: root.borderColor
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 17
                spacing: 8
                Text { text: "工作区"; color: "#64798B"; font.pixelSize: 12; font.letterSpacing: 2; Layout.leftMargin: 14; Layout.topMargin: 19; Layout.bottomMargin: 10 }
                NavButton { page: 0; glyph: "◉"; caption: "设备概览" }
                NavButton { page: 1; glyph: "⌁"; caption: "实时趋势" }
                NavButton { page: 2; glyph: "◇"; caption: "精度校准" }
                NavButton { page: 3; glyph: "↑"; caption: "固件升级" }
                NavButton { page: 4; glyph: "≡"; caption: "通讯日志" }

                Item { Layout.fillHeight: true }
                Panel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 324
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 10
                        Text { text: "连接设备"; color: root.textMain; font.pixelSize: 15; font.weight: Font.DemiBold }
                        RowLayout {
                            Layout.fillWidth: true;spacing: 6
                            ActionButton { text: "有线 USB"; Layout.fillWidth: true; implicitHeight: 34; implicitWidth: 90
                                enabled: !bridge.connected && !bridge.connecting && !bridge.updating
                                fillColor: bridge.transportMode === "wired" ? root.mint : "#273B4D"
                                inkColor: bridge.transportMode === "wired" ? "#081B19" : root.textMain
                                onClicked: bridge.selectTransport("wired") }
                            ActionButton { text: "无线蓝牙"; Layout.fillWidth: true; implicitHeight: 34; implicitWidth: 90
                                enabled: !bridge.connected && !bridge.connecting && !bridge.updating
                                fillColor: bridge.transportMode === "wireless" ? root.mint : "#273B4D"
                                inkColor: bridge.transportMode === "wireless" ? "#081B19" : root.textMain
                                onClicked: bridge.selectTransport("wireless") }
                        }
                        ComboBox {
                            id: portCombo
                            Layout.fillWidth: true
                            implicitHeight: 44
                            model: bridge.ports
                            enabled: !bridge.connected && !bridge.updating && !bridge.connecting
                            font.pixelSize: 13
                            onActivated: bridge.selectPort(currentText)
                            function syncSelection() {
                                if (count > 0) {
                                    let choice = 0
                                    for (let i = 0; i < count; i++) {
                                        if (textAt(i).indexOf(bridge.selectedPort + "  ·") === 0) {
                                            choice = i
                                            break
                                        }
                                    }
                                    currentIndex = choice
                                    bridge.selectPort(currentText)
                                }
                            }
                            onCountChanged: Qt.callLater(syncSelection)
                            onModelChanged: Qt.callLater(syncSelection)
                            contentItem: Text {
                                leftPadding: 12
                                rightPadding: 31
                                text: portCombo.displayText || "扫描后选择系统串口"
                                color: portCombo.count ? root.textMain : root.textDim
                                font.pixelSize: 13
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            indicator: Text {
                                text: "⌄"
                                color: root.mint
                                font.pixelSize: 19
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            background: Rectangle {
                                radius: 10
                                color: "#101B27"
                                border.width: 1
                                border.color: portCombo.activeFocus ? root.mint : "#344555"
                            }
                            delegate: ItemDelegate {
                                width: portCombo.popup.width - 10
                                height: 42
                                text: modelData
                                contentItem: Text {
                                    text: modelData
                                    color: root.textMain
                                    font.pixelSize: 13
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    leftPadding: 11
                                    rightPadding: 11
                                }
                                background: Rectangle {
                                    radius: 7
                                    color: highlighted ? "#1C5652" : (hovered ? "#223241" : "transparent")
                                }
                            }
                            popup: Popup {
                                y: portCombo.height + 4
                                width: Math.max(portCombo.width, 420)
                                implicitHeight: Math.min(contentItem.implicitHeight + 10, 240)
                                padding: 5
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: portCombo.popup.visible ? portCombo.delegateModel : null
                                    currentIndex: portCombo.highlightedIndex
                                    ScrollIndicator.vertical: ScrollIndicator { }
                                }
                                background: Rectangle {
                                    radius: 11
                                    color: "#172330"
                                    border.width: 1
                                    border.color: root.borderColor
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ActionButton { text: bridge.scanning ? "扫描中" : "扫描端口"; enabled: !bridge.scanning && !bridge.updating; Layout.fillWidth: true; fillColor: "#273B4D"; inkColor: root.textMain; onClicked: bridge.scanPorts() }
                            ActionButton { text: bridge.connected ? "断开" : bridge.connecting ? "取消" : "连接"; enabled: !bridge.updating && bridge.selectedPort !== ""; Layout.fillWidth: true; onClicked: (bridge.connected || bridge.connecting) ? bridge.disconnectDevice() : bridge.connectDevice() }
                        }
                        Text { text: bridge.scanText; color: root.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { text: bridge.connectionText; color: bridge.connected ? root.mint : root.textDim; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Text { text: "固件 " + bridge.firmware + " · " + bridge.selectedTransport; color: root.textDim; font.pixelSize: 12 }

                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.activePage

            // Dashboard
            Flickable {
                contentWidth: width
                contentHeight: overviewColumn.implicitHeight + 60
                clip: true
                ScrollBar.vertical: ScrollBar { }
                ColumnLayout {
                    id: overviewColumn
                    width: parent.width - 72
                    x: 36; y: 30
                    spacing: 21
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout { spacing: 3
                            Text { text: "设备概览"; color: root.textMain; font.pixelSize: 29; font.bold: true }
                            Text { text: "实时监测和输出控制集中在一处"; color: root.textDim; font.pixelSize: 14 }
                        }
                        Item { Layout.fillWidth: true }
                        Text { text: "遥测  " + bridge.telemetryRateText; color: root.textDim; font.pixelSize: 13 }
                        Text { text: bridge.hardware; color: root.mint; font.pixelSize: 13 }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 18
                        Panel { Layout.fillWidth: true; Layout.preferredHeight: 220
                            Column { anchors.fill: parent; anchors.margins: 24; spacing: 17
                                Text { text: "OUTPUT VOLTAGE"; color: root.textDim; font.pixelSize: 12; font.letterSpacing: 2 }
                                Row { spacing: 9
                                    Text { text: bridge.voltage; color: root.mint; font.pixelSize: 71; font.weight: Font.Medium }
                                    Text { text: "V"; color: root.mint; font.pixelSize: 25; anchors.bottom: parent.bottom; anchors.bottomMargin: 11 }
                                }
                                Text { text: "设定  " + bridge.setVoltage + " V"; color: root.textDim; font.pixelSize: 15 }
                            }
                        }
                        Panel { Layout.fillWidth: true; Layout.preferredHeight: 220
                            Column { anchors.fill: parent; anchors.margins: 24; spacing: 17
                                Text { text: "OUTPUT CURRENT"; color: root.textDim; font.pixelSize: 12; font.letterSpacing: 2 }
                                Row { spacing: 9
                                    Text { text: bridge.current; color: "#87B8FF"; font.pixelSize: 71; font.weight: Font.Medium }
                                    Text { text: "A"; color: "#87B8FF"; font.pixelSize: 25; anchors.bottom: parent.bottom; anchors.bottomMargin: 11 }
                                }
                                Text { text: "过流关断  " + bridge.setCurrent + " A"; color: root.textDim; font.pixelSize: 15 }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 18
                        Panel { Layout.fillWidth: true; Layout.preferredHeight: 112
                            Column { anchors.fill: parent; anchors.margins: 20; spacing: 9
                                Text { text: "输入电压"; color: root.textDim; font.pixelSize: 13 }
                                Text { text: bridge.inputVoltage + " V"; color: root.textMain; font.pixelSize: 25; font.weight: Font.DemiBold }
                            }
                        }
                        Panel { Layout.fillWidth: true; Layout.preferredHeight: 112
                            Column { anchors.fill: parent; anchors.margins: 20; spacing: 9
                                Text { text: "输出功率"; color: root.textDim; font.pixelSize: 13 }
                                Text { text: bridge.power + " W"; color: root.textMain; font.pixelSize: 25; font.weight: Font.DemiBold }
                            }
                        }
                        Panel { Layout.fillWidth: true; Layout.preferredHeight: 112
                            Column { anchors.fill: parent; anchors.margins: 20; spacing: 9
                                Text { text: "温度"; color: root.textDim; font.pixelSize: 13 }
                                Text { text: bridge.temperature + " °C"; color: root.textMain; font.pixelSize: 25; font.weight: Font.DemiBold }
                            }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredHeight: 200
                        RowLayout { anchors.fill: parent; anchors.margins: 24; spacing: 20
                            ColumnLayout { Layout.fillWidth: true; spacing: 10
                                Text { text: "输出设定"; color: root.textMain; font.pixelSize: 19; font.weight: Font.DemiBold }
                                RowLayout { spacing: 11
                                    Field { id: voltageField; property bool dirty: false; onTextEdited: dirty = true; Layout.preferredWidth: 146; placeholderText: "电压 V"; Component.onCompleted: text = bridge.setVoltage }
                                    Text { text: "V"; color: root.textDim; font.pixelSize: 17 }
                                    Field { id: currentField; property bool dirty: false; onTextEdited: dirty = true; Layout.preferredWidth: 146; placeholderText: "电流 A"; Component.onCompleted: text = bridge.setCurrent }
                                    Text { text: "A"; color: root.textDim; font.pixelSize: 17 }
                                    ActionButton { text: "应用设定"; enabled: bridge.connected; onClicked: { bridge.applySetpoints(voltageField.text, currentField.text); voltageField.dirty = false; currentField.dirty = false } }
                                }
                                Text { text: "电压 3.2–32 V  ·  电流 0.1–8 A  ·  断开后设定由设备保存"; color: root.textDim; font.pixelSize: 12 }
                            }
                            Rectangle { width: 1; Layout.fillHeight: true; color: root.borderColor }
                            ColumnLayout { Layout.preferredWidth: 175; spacing: 12
                                Text { text: bridge.fault === "NONE" ? "保护状态正常" : bridge.faultText; color: bridge.fault === "NONE" ? root.textDim : "#FF7A80"; font.pixelSize: 12 }
                                ActionButton { Layout.fillWidth: true; text: bridge.outputOn ? "关闭输出" : "开启输出"; fillColor: bridge.outputOn ? "#DA4C58" : root.mint; inkColor: "#0A1720"; enabled: bridge.connected; onClicked: bridge.toggleOutput() }
                                ActionButton { Layout.fillWidth: true; text: "清除故障"; fillColor: "#263747"; inkColor: root.textMain; enabled: bridge.connected; onClicked: bridge.clearFault() }
                            }
                        }
                    }
                }
            }

            // Trends
            Item {
                ColumnLayout { anchors.fill: parent; anchors.margins: 36; spacing: 21
                    Text { text: "实时趋势"; color: root.textMain; font.pixelSize: 29; font.bold: true }
                    Text { text: "当前设定最近 140 帧 · 实际回传 " + bridge.telemetryRateText + "；完整记录可导出为 CSV"; color: root.textDim; font.pixelSize: 14 }
                    Text { Layout.fillWidth: true; text: bridge.controlDiagnostic; color: root.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    Panel { Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 12
                            Text { text: "输出电压  ·  V"; color: root.mint; font.pixelSize: 16; font.bold: true }
                            Text { Layout.fillWidth: true; text: bridge.voltageWindowText; color: root.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap }
                            Canvas { id: voltageChart; Layout.fillWidth: true; Layout.fillHeight: true
                                onPaint: drawSeries(getContext("2d"), bridge.historyVoltage, root.mint, width, height)
                            }
                        }
                    }
                    Panel { Layout.fillWidth: true; Layout.fillHeight: true
                        ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 12
                            Text { text: "输出电流  ·  A"; color: "#87B8FF"; font.pixelSize: 16; font.bold: true }
                            Canvas { id: currentChart; Layout.fillWidth: true; Layout.fillHeight: true
                                onPaint: drawSeries(getContext("2d"), bridge.historyCurrent, "#87B8FF", width, height)
                            }
                        }
                    }
                    ActionButton { text: "导出 CSV"; fillColor: "#263747"; inkColor: root.textMain; onClicked: bridge.exportCsv() }
                }
            }

            // Calibration
            Flickable { objectName: "calibrationScroll"; contentWidth: width; contentHeight: calibrationColumn.implicitHeight + 70; clip: true; ScrollBar.vertical: StudioScrollBar { }
                ColumnLayout { id: calibrationColumn; width: parent.width - 72; x: 36; y: 30; spacing: 18
                    Text { text: "输出调压校准 · " + curve.total + " 点"; color: root.textMain; font.pixelSize: 29; font.bold: true }
                    Text { text: "选点 → 手动调整 DAC → 输入万用表实测值 → 采集并保存。\n保存实测电压与 DAC 的对应关系，其他电压按相邻实测点做区间映射。"; color: root.textDim; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Panel { Layout.fillWidth: true; Layout.preferredHeight: 98
                        RowLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
                            ColumnLayout { Layout.fillWidth: true; spacing: 7
                                Text { text: "设备记录  " + curve.completed + " / " + curve.total; color: root.mint; font.pixelSize: 18; font.bold: true }
                                Text { objectName: "curveStatus"; text: curve.status; color: root.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                            ActionButton { text: "读取校准表"; enabled: curve.supported && !curve.active && !curve.busy && !bridge.updating; onClicked: curve.refresh() }
                        }
                    }
                    GridLayout { objectName: "curvePointGrid"; Layout.fillWidth: true; columns: 2; rowSpacing: 10; columnSpacing: 14
                        Repeater { model: curve.pointRows
                            delegate: Panel { required property var modelData; Layout.fillWidth: true; Layout.preferredHeight: 76
                                RowLayout { anchors.fill: parent; anchors.margins: 14; spacing: 8
                                    ColumnLayout { Layout.fillWidth: true; spacing: 5
                                        Text { text: modelData.label + "  ·  " + modelData.state; color: root.textMain; font.pixelSize: 15; font.bold: true }
                                        Text { text: modelData.actual + "   DAC " + modelData.dac; color: root.textDim; font.pixelSize: 12 }
                                    }
                                    ActionButton { text: modelData.state === "待校准" ? "校准" : "重校"; implicitWidth: 72; implicitHeight: 38
                                        enabled: curve.supported && !curve.active && !curve.busy && !bridge.updating
                                        onClicked: { meterReading.text = ""; curve.start(modelData.index) }
                                    }
                                }
                            }
                        }
                    }
                    Panel { Layout.fillWidth: true; Layout.preferredHeight: 210
                        ColumnLayout { anchors.fill: parent; anchors.margins: 20; spacing: 12
                            Text { text: curve.liveText; color: root.mint; font.pixelSize: 16; Layout.fillWidth: true }
                            RowLayout { Layout.fillWidth: true; spacing: 10
                                ActionButton { text: "降压 · 5 码"; Layout.fillWidth: true; implicitWidth: 90; enabled: curve.ready && !bridge.updating; onClicked: curve.step(5) }
                                ActionButton { text: "降压 · 1 码"; Layout.fillWidth: true; implicitWidth: 90; enabled: curve.ready && !bridge.updating; onClicked: curve.step(1) }
                                ActionButton { text: "升压 · 1 码"; Layout.fillWidth: true; implicitWidth: 90; enabled: curve.ready && !bridge.updating; onClicked: curve.step(-1) }
                                ActionButton { text: "升压 · 5 码"; Layout.fillWidth: true; implicitWidth: 90; enabled: curve.ready && !bridge.updating; onClicked: curve.step(-5) }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 12
                                Text { text: "万用表实测"; color: root.textDim; font.pixelSize: 14 }
                                Field { id: meterReading; objectName: "curveMeterReading"; text: ""; Layout.fillWidth: true; enabled: curve.active && !curve.busy }
                                Text { text: "V"; color: root.textDim }
                                ActionButton { text: "采集并保存"; enabled: curve.ready && !bridge.updating; onClicked: curve.capture(meterReading.text) }
                                ActionButton { text: "停止 / 关闭"; fillColor: "#703B43"; inkColor: root.textMain; enabled: curve.supported && (curve.active || curve.busy) && !bridge.updating; onClicked: curve.stop() }
                            }
                            Text { text: "±1 / ±5 直接写入 DAC，不等待软件稳定判定。填写万用表实际读数；采集保存后关闭输出。过流（含短路大电流）、过压、过温、采样失联和输入欠压触发锁存关断。"; color: root.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                    Text { text: "已有电压／电流测量系数继续保留。调压表保存的是实际 DAC 码，不通过修改显示值假装调准。电流一键零点校准仍在电源设置页。"; color: root.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Panel { Layout.fillWidth: true; Layout.preferredHeight: 118
                        RowLayout { anchors.fill: parent; anchors.margins: 22; spacing: 18
                            Text { text: bridge.voltageDiagnostic; color: root.textDim; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            ActionButton { text: "读取校准参数"; enabled: bridge.connected && !bridge.updating; onClicked: bridge.runQuickCommand("CAL GET") }
                        }
                    }
                }
            }

            // Firmware update
            Flickable { contentWidth: width; contentHeight: updateColumn.implicitHeight + 70; clip: true; ScrollBar.vertical: ScrollBar { }
                ColumnLayout { id: updateColumn; width: parent.width - 72; x: 36; y: 30; spacing: 20
                    Text { text: "固件升级"; color: root.textMain; font.pixelSize: 29; font.bold: true }
                    Text { text: "USB / 蓝牙 SPP 上传至 W25Q256 · BL 校验刷写 · 失败自动回退"; color: root.textDim; font.pixelSize: 14 }
                    Panel { Layout.fillWidth: true; Layout.preferredHeight: 110
                        RowLayout { anchors.fill: parent; anchors.margins: 22; spacing: 18
                            Rectangle { width: 43; height: 43; radius: 11; color: "#1B5D54"; Text { anchors.centerIn: parent; text: "✓"; color: root.mint; font.pixelSize: 23 } }
                            ColumnLayout { Layout.fillWidth: true; spacing: 4
                                Text { text: "安全检查"; color: root.textMain; font.pixelSize: 17; font.bold: true }
                            Text { text: "请先扫描识别并连接 XIN Power。上传使用当前连接；只有执行刷写重启时才暂时断开，随后自动恢复。"; color: root.textDim; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            }
                        }
                    }
                    Panel { Layout.fillWidth: true; Layout.preferredHeight: 225
                        ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 16
                            Text { text: "01  选择应用固件"; color: root.textMain; font.pixelSize: 18; font.bold: true }
                            RowLayout { Layout.fillWidth: true; spacing: 14
                                Field { text: bridge.imagePath; readOnly: true; placeholderText: "选择 Keil 生成的 .hex 文件"; Layout.fillWidth: true }
                                ActionButton { text: "浏览文件"; fillColor: "#263747"; inkColor: root.textMain; onClicked: bridge.chooseFirmware() }
                            }
                            RowLayout { spacing: 14
                                Text { text: "目标版本"; color: root.textDim; font.pixelSize: 14 }
                                Field { id: targetVersion; text: "6.2.3"; Layout.preferredWidth: 130 }
                                Text { text: "设备版本  V" + bridge.firmware + "  ·  " + bridge.hardware; color: root.textDim; font.pixelSize: 13 }
                                Text { text: "当前链路  " + bridge.selectedTransport; color: root.mint; font.pixelSize: 13 }
                            }
                        }
                    }
                    Panel { Layout.fillWidth: true; Layout.preferredHeight: 206
                        ColumnLayout { anchors.fill: parent; anchors.margins: 24; spacing: 18
                            Text { text: "02  传输和刷写"; color: root.textMain; font.pixelSize: 18; font.bold: true }
                            Text { text: bridge.updateStatus; color: root.textDim; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            ProgressBar { from: 0; to: 100; value: bridge.updateProgress; Layout.fillWidth: true; implicitHeight: 14 }
                            RowLayout { Layout.fillWidth: true
                                Text { text: bridge.updateProgress + "%"; color: root.mint; font.pixelSize: 15; font.bold: true }
                                Item { Layout.fillWidth: true }
                                ActionButton { text: "取消上传"; fillColor: "#263747"; inkColor: root.textMain; enabled: bridge.updating && bridge.updateProgress < 82; onClicked: bridge.cancelUpgrade() }
                                ActionButton { text: bridge.updating ? "升级进行中" : "开始升级"; enabled: !bridge.updating && bridge.connected && bridge.imagePath !== ""; onClicked: bridge.startUpgrade(targetVersion.text) }
                            }
                        }
                    }
                    Text { text: "在 BL 刷写阶段取消不可用。若升级失败，请勿带载，先查看设备版本或使用 SWD 恢复。"; color: "#F3B783"; font.pixelSize: 12 }
                }
            }

            // Logs
            Item {
                ColumnLayout { anchors.fill: parent; anchors.margins: 36; spacing: 17
                    RowLayout { Layout.fillWidth: true
                        ColumnLayout { spacing: 3
                            Text { text: "通讯日志"; color: root.textMain; font.pixelSize: 29; font.bold: true }
                            Text { text: "设备响应、告警和操作记录"; color: root.textDim; font.pixelSize: 14 }
                        }
                        Item { Layout.fillWidth: true }
                        Text { text: "实际回传  " + bridge.telemetryRateText; color: root.mint; font.pixelSize: 13 }
                    }
                    Flow { Layout.fillWidth: true; Layout.preferredHeight: implicitHeight; spacing: 8
                        ActionButton { text: bridge.logPaused ? "继续刷新" : "暂停刷新"; implicitWidth: 110; fillColor: bridge.logPaused ? root.mint : "#263747"; inkColor: bridge.logPaused ? "#081B19" : root.textMain; onClicked: bridge.toggleLogPause() }
                        ActionButton { text: "回到最新"; implicitWidth: 110; fillColor: "#263747"; inkColor: root.textMain; onClicked: { if (bridge.logPaused) bridge.toggleLogPause(); logScroll.followTail = true; Qt.callLater(logScroll.scrollToEnd) } }
                        ActionButton { text: "清空日志"; implicitWidth: 110; fillColor: "#263747"; inkColor: root.textMain; onClicked: { bridge.clearLog(); logScroll.followTail = true; Qt.callLater(logScroll.scrollToEnd) } }
                        ActionButton { text: "导出日志"; implicitWidth: 110; fillColor: "#263747"; inkColor: root.textMain; onClicked: bridge.exportLog() }
                        ActionButton { text: "导出 CSV"; implicitWidth: 110; fillColor: "#263747"; inkColor: root.textMain; onClicked: bridge.exportCsv() }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 12
                        text: bridge.logPaused ? "显示已暂停 · 新到 " + bridge.pendingLogCount + " 条；接收和 CSV 记录继续，日志保留最近 1000 条。" : "显示最近 1000 条 · 状态摘要每秒 5 条，后台完整记录接收数据，默认 25 Hz；清空显示不删除记录。"
                        color: bridge.logPaused ? "#F3B783" : root.textDim
                    }
                    RowLayout { Layout.fillWidth: true; spacing: 9
                        StudioCombo { id: quickCommand; Layout.fillWidth: true
                            model: ["连接测试 · PING", "设备信息 · INFO", "当前状态 · STATUS", "校准参数 · CAL GET", "原始测量 · CAL RAW", "通讯诊断 · DIAG", "升级状态 · FW STATUS", "命令帮助 · HELP", "开启设备回传 · LOG ON", "停止设备回传 · LOG OFF"]
                            property var commands: ["PING", "INFO", "STATUS", "CAL GET", "CAL RAW", "DIAG", "FW STATUS", "HELP", "LOG ON", "LOG OFF"]
                        }
                        ActionButton { text: "执行"; implicitWidth: 70; enabled: bridge.connected && !bridge.updating; onClicked: bridge.runQuickCommand(quickCommand.commands[quickCommand.currentIndex]) }
                        StudioCombo { id: rateChoice; Layout.preferredWidth: 157; model: ["25 Hz / 40 ms", "10 Hz / 100 ms", "5 Hz / 200 ms", "50 Hz / 20 ms"]; property var intervals: [40,100,200,20] }
                        ActionButton { text: "应用速率"; implicitWidth: 100; enabled: bridge.connected && !bridge.updating; onClicked: bridge.setTelemetryInterval(rateChoice.intervals[rateChoice.currentIndex]) }
                    }
                    Panel { Layout.fillWidth: true; Layout.fillHeight: true
                        ScrollView { id: logScroll; objectName: "logScroll"; anchors.fill: parent; anchors.margins: 14
                            rightPadding: 18
                            bottomPadding: 18
                            property bool followTail: true
                            property real preservedY: 0
                            function scrollToEnd() {
                                contentItem.contentY = Math.max(0, contentItem.contentHeight - contentItem.height)
                            }
                            ScrollBar.vertical: StudioScrollBar {
                                objectName: "logVerticalBar"
                                parent: logScroll
                                x: logScroll.width - width
                                y: 0
                                height: logScroll.availableHeight
                                onPressedChanged: {
                                    if (pressed) logScroll.followTail = false
                                    else if (position + size >= 0.98) logScroll.followTail = true
                                }
                            }
                            ScrollBar.horizontal: StudioScrollBar {
                                objectName: "logHorizontalBar"
                                parent: logScroll
                                x: 0
                                y: logScroll.height - height
                                width: logScroll.availableWidth
                            }
                            TextArea { id: logArea; objectName: "logArea"; readOnly: true; wrapMode: TextEdit.NoWrap; color: "#CAD7E3"; font.family: "Microsoft YaHei"; font.pixelSize: 13; selectionColor: "#276D62"; selectedTextColor: "#FFFFFF"; background: Rectangle { color: "transparent" }
                                Component.onCompleted: {
                                    text = bridge.logText
                                    Qt.callLater(logScroll.scrollToEnd)
                                }
                            }
                            Connections {
                                target: bridge
                                function onLogsChanged() {
                                    if (!logScroll.followTail) logScroll.preservedY = logScroll.contentItem.contentY
                                    logArea.text = bridge.logText
                                    Qt.callLater(function() {
                                        if (logScroll.followTail) logScroll.scrollToEnd()
                                        else logScroll.contentItem.contentY = Math.min(
                                            logScroll.preservedY,
                                            Math.max(0, logScroll.contentItem.contentHeight - logScroll.contentItem.height))
                                    })
                                }
                            }
                            Connections {
                                target: logScroll.contentItem
                                function onMovementStarted() { logScroll.followTail = false }
                                function onMovementEnded() {
                                    if (logScroll.contentItem.contentY >=
                                            logScroll.contentItem.contentHeight - logScroll.contentItem.height - 8)
                                        logScroll.followTail = true
                                }
                            }
                        }
                    }
                    RowLayout { Layout.fillWidth: true; spacing: 12
                        Field { id: commandField; Layout.fillWidth: true; enabled: bridge.connected && !bridge.updating; placeholderText: "高级命令：确认含义后发送（HELP 查看命令）"; onAccepted: { bridge.sendCommand(text); text = "" } }
                        ActionButton { text: "发送命令"; enabled: bridge.connected && !bridge.updating; onClicked: { bridge.sendCommand(commandField.text); commandField.text = "" } }
                    }
                }
            }
            // PID tuning
            Flickable { objectName: "pidScroll"; clip: true; contentWidth: width
                contentHeight: pidColumn.implicitHeight + 60
                ScrollBar.vertical: StudioScrollBar { }
                ColumnLayout { id: pidColumn; width: parent.width - 72; x: 36; y: 30; spacing: 18
                    Text { text: "持续电压 PID 调参"; color: root.textMain; font.pixelSize: 29; font.bold: true }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "每帧新 INA226 采样计算一次 PID。周期同时用于采样读取和 PID；参数只保存在 RAM，重启恢复试验默认值。"; color: root.textDim; font.pixelSize: 14 }
                    Panel { Layout.fillWidth: true; implicitHeight: pidGrid.implicitHeight + 40
                        GridLayout { id: pidGrid; anchors.fill: parent; anchors.margins: 20; columns: 4; columnSpacing: 16; rowSpacing: 12
                            Text { text: "Kp（0–3）"; color: root.textMain }
                            Field { id: pidKp; objectName: "pidKp"; text: "0.150"; Layout.fillWidth: true; Layout.preferredWidth: 130 }
                            Text { text: "Ki / 秒（0–10）"; color: root.textMain }
                            Field { id: pidKi; objectName: "pidKi"; text: "1.000"; Layout.fillWidth: true; Layout.preferredWidth: 130 }
                            Text { text: "Kd · 秒（0–1）"; color: root.textMain }
                            Field { id: pidKd; objectName: "pidKd"; text: "0.001"; Layout.fillWidth: true }
                            Text { text: "采样 / PID 周期 ms"; color: root.textMain }
                            Field { id: pidPeriod; objectName: "pidPeriod"; text: "20"; Layout.fillWidth: true }
                            Text { text: "误差死区 mV"; color: root.textMain }
                            Field { id: pidDeadband; objectName: "pidDeadband"; text: "10"; Layout.fillWidth: true }
                            Text { text: "近目标范围 mV"; color: root.textMain }
                            Field { id: pidBand; objectName: "pidBand"; text: "500"; Layout.fillWidth: true }
                            Text { text: "近目标每次最多 DAC 码"; color: root.textMain }
                            Field { id: pidNear; objectName: "pidNear"; text: "1"; Layout.fillWidth: true }
                            Text { text: "远目标每次最多 DAC 码"; color: root.textMain }
                            Field { id: pidFar; objectName: "pidFar"; text: "32"; Layout.fillWidth: true }
                        }
                    }
                    RowLayout { spacing: 12
                        ActionButton { objectName: "pidRead"; text: "读取设备参数"; enabled: root.pid.supported; onClicked: root.pid.read() }
                        ActionButton { objectName: "pidApply"; text: "应用全部参数"; enabled: root.pid.supported; onClicked: root.pid.apply(pidKp.text, pidKi.text, pidKd.text, pidPeriod.text, pidDeadband.text, pidNear.text, pidFar.text, pidBand.text) }
                        ActionButton { objectName: "pidReset"; text: "恢复试验默认"; enabled: root.pid.supported; fillColor: "#263747"; inkColor: root.textMain; onClicked: root.pid.reset() }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.pid.status; color: root.mint; font.pixelSize: 14 }
                    Panel { Layout.fillWidth: true; implicitHeight: pidLive.implicitHeight + 40
                        Text { id: pidLive; anchors.fill: parent; anchors.margins: 20; text: root.pid.liveText; color: root.textMain; font.pixelSize: 16; wrapMode: Text.WordWrap }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "应用时保留当前 DAC，重建误差历史，等待 ACK 和参数回读确认。手动 DAC 校准期间 PID 暂停；基本保护保持有效。"; color: root.textDim; font.pixelSize: 13 }
                    RowLayout {
                        ActionButton { text: "导出调试 CSV"; onClicked: bridge.exportCsv() }
                        ActionButton { text: "导出完整日志"; fillColor: "#263747"; inkColor: root.textMain; onClicked: bridge.exportLog() }
                        ActionButton { text: "回传 10 Hz"; enabled: bridge.connected && !bridge.updating; fillColor: "#263747"; inkColor: root.textMain; onClicked: bridge.setTelemetryInterval(100) }
                    }
                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "回传速率只影响上位机显示和记录，不改变 INA226 / PID 共用周期。"; color: root.textDim; font.pixelSize: 12 }
                }
                Connections { target: root.pid
                    function onConfigChanged() {
                        // Readback updates editors; telemetry never overwrites a draft.
                        var p = root.pid.parameters
                        pidKp.text=p.kp; pidKi.text=p.ki; pidKd.text=p.kd; pidPeriod.text=p.period
                        pidDeadband.text=p.deadband; pidNear.text=p.nearstep; pidFar.text=p.farstep; pidBand.text=p.nearband
                    }
                }
            }

        }
    }

    function drawSeries(ctx, series, stroke, w, h) {
        ctx.clearRect(0, 0, w, h)
        if (series.length < 1) return
        var lowest = Math.min.apply(null, series)
        var highest = Math.max.apply(null, series)
        var span = Math.max(0.1, highest - lowest)
        var bottom = Math.max(0, lowest - span * 0.1)
        var top = Math.max(highest + span * 0.1, bottom + 0.1)
        var left = 62, right = w - 8, upper = 10, lower = h - 10
        if (right <= left || lower <= upper) return
        ctx.font = "11px sans-serif"
        ctx.textAlign = "right"
        ctx.textBaseline = "middle"
        ctx.fillStyle = "#92A6B9"
        ctx.strokeStyle = "#2A3948"
        ctx.lineWidth = 1
        for (var grid = 0; grid <= 4; grid++) {
            var y = upper + (lower - upper) * grid / 4
            ctx.beginPath(); ctx.moveTo(left, y); ctx.lineTo(right, y); ctx.stroke()
            ctx.fillText((top - (top-bottom) * grid / 4).toFixed(3), left-8, y)
        }
        ctx.strokeStyle = stroke
        ctx.lineWidth = 2.5
        ctx.beginPath()
        for (var i = 0; i < series.length; i++) {
            var px = left + i * (right-left) / Math.max(1, series.length-1)
            var py = lower - ((series[i]-bottom) / (top-bottom)) * (lower-upper)
            if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
        }
        ctx.stroke()
    }
    Connections {
        target: bridge
        function onTelemetryChanged() {
            voltageChart.requestPaint()
            currentChart.requestPaint()

        }
    }
}
