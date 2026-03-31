import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 760
    title: "Sony Photo Editor"

    property bool openingForMain: false
    property int  imageRotation: 0

    property int  lvFrameSeq: 0
    property bool lvRunning:  false

    property real zoomScale:   1.0
    property real panX:        0.0
    property real panY:        0.0

    property bool fillScreen: false

    property bool showHistogram: false

    property bool compareMode: false
    property string compareImageA: ""
    property string compareImageB: ""
    property real   compareSplit: 0.5
    function log(msg) {
        var ts = Qt.formatTime(new Date(), "hh:mm:ss")
        logModel.append({ line: "[" + ts + "] " + msg })
        logView.positionViewAtEnd()
    }

    function resetView() {
        root.zoomScale = 1.0
        root.panX      = 0.0
        root.panY      = 0.0
    }

    Connections {
        target: sonyCamera

        function onConnectionChanged(connected) {
            log(connected ? "Camera connected successfully!" : "Camera disconnected.")
            if (!connected) root.lvRunning = false
        }

        function onPhotoTaken(filePath) {
            log("Photo saved: " + filePath)
            var url = "file:///" + filePath.replace(/\\/g, "/")
            mainImg.source = ""
            mainImg.source = url
            imageModel.append({ path: url })
            resetView()
        }

        function onErrorOccurred(message) {
            log("Error: " + message)
            errorText.text = message
            errorTimer.restart()
        }

        function onLogMessage(message)  { log(message) }
        function onSettingsChanged()    { }

        function onLiveViewActiveChanged(active) {
            root.lvRunning = active
        }

        function onLiveViewFrameReady(frame) {
            root.lvFrameSeq++
        }

        function onFocusRangeReady() {
            if (sonyCamera.focusRangeValid)
                log("MF range: " + sonyCamera.focusMin + " – " + sonyCamera.focusMax)
        }

        function onFocusPositionChanged(pos) { }

        function onExifReady() {
            exifDialog.model_    = sonyCamera.exifModel()
            exifDialog.lens_     = sonyCamera.exifLens()
            exifDialog.focal_    = sonyCamera.exifFocalLength()
            exifDialog.aperture_ = sonyCamera.exifAperture()
            exifDialog.iso_      = sonyCamera.exifISO()
            exifDialog.shutter_  = sonyCamera.exifShutter()
            exifDialog.expMode_  = sonyCamera.exifExposureMode()
            exifDialog.wb_       = sonyCamera.exifWhiteBalance()
            exifDialog.battery_  = sonyCamera.exifBatteryLevel()
            exifDialog.open()
        }
    }

    Timer { id: errorTimer; interval: 5000; onTriggered: errorText.text = "" }
    ListModel { id: imageModel }
    ListModel { id: logModel  }

    function deleteImage(removedPath) {
        var realIndex = -1
        for (var i = 0; i < imageModel.count; i++) {
            if (imageModel.get(i).path === removedPath) { realIndex = i; break }
        }
        if (realIndex === -1) return
        var wasCurrent = (mainImg.source.toString() === removedPath.toString())
        imageModel.remove(realIndex)
        if (wasCurrent) {
            mainImg.source = imageModel.count > 0
                ? imageModel.get(Math.min(realIndex, imageModel.count - 1)).path : ""
            resetView()
        }
    }


    property var histR: []
    property var histG: []
    property var histB: []

    function computeHistogram(imgObj) {
        histCanvas.imgSource = imgObj.source
        histCanvas.requestPaint()
    }

    Canvas {
        id: histCanvas
        width: 256; height: 256
        visible: false
        property url imgSource: ""

        onPaint: {
            if (imgSource === "") return
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.drawImage(imgSource.toString(), 0, 0, 256, 256)
            var data = ctx.getImageData(0, 0, 256, 256).data
            var r = new Array(256).fill(0)
            var g = new Array(256).fill(0)
            var b = new Array(256).fill(0)
            for (var i = 0; i < data.length; i += 4) {
                r[data[i]]++
                g[data[i+1]]++
                b[data[i+2]]++
            }
            root.histR = r; root.histG = g; root.histB = b
        }
    }

    property var wbLabels: ["Auto", "Daylight", "Shade", "Cloudy",
                            "Incandescent", "Fluorescent", "Flash", "Custom"]
    property var wbValues: [0x0001, 0x0002, 0x0003, 0x0004,
                            0x0006, 0x0007, 0x0008, 0x8001]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 36; color: "#141414"

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left; anchors.leftMargin: 12; spacing: 10

                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: sonyCamera.connected ? "#44FF44" : "#FF4444"
                    anchors.verticalCenter: parent.verticalCenter
                    SequentialAnimation on opacity {
                        running: !sonyCamera.connected; loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }
                Text {
                    text: sonyCamera.connected ? "Camera Connected" : "Camera Disconnected"
                    color: sonyCamera.connected ? "#44FF44" : "#FF4444"
                    font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right; anchors.rightMargin: 12; spacing: 8

                Text {
                    id: errorText; color: "#FF6666"
                    font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter
                }

                Button {
                    id: connectBtn; text: "Connect"
                    implicitWidth: 90; implicitHeight: 26
                    enabled: !sonyCamera.connected; opacity: enabled ? 1.0 : 0.35
                    background: Rectangle { radius: 4; color: connectBtn.pressed ? "#1a7a1a" : "#226622" }
                    contentItem: Text { text: connectBtn.text; color: "white"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: { log("Connecting to camera..."); sonyCamera.connectCamera() }
                }
                Button {
                    id: disconnectBtn; text: "Disconnect"
                    implicitWidth: 100; implicitHeight: 26
                    enabled: sonyCamera.connected; opacity: enabled ? 1.0 : 0.35
                    background: Rectangle { radius: 4; color: disconnectBtn.pressed ? "#7a1a1a" : "#662222" }
                    contentItem: Text { text: disconnectBtn.text; color: "white"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        if (root.lvRunning) sonyCamera.stopLiveView()
                        log("Disconnecting...")
                        sonyCamera.disconnectCamera()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                width: 150; color: "#2E2E2E"; Layout.fillHeight: true
                visible: !root.fillScreen

                Column {
                    anchors.fill: parent; anchors.margins: 16; spacing: 10

                    Button {
                        text: "Open Image"; implicitWidth: 118; implicitHeight: 34
                        onClicked: { openingForMain = true; fileDialog.open() }
                    }

                    Button {
                        id: takePhotoBtn; text: "Take Photo"
                        implicitWidth: 118; implicitHeight: 34
                        enabled: sonyCamera.connected
                        background: Rectangle {
                            radius: 4
                            color: takePhotoBtn.enabled ? (takePhotoBtn.pressed ? "#1a7a1a" : "#226622") : "#444444"
                        }
                        contentItem: Text { text: takePhotoBtn.text; color: "white"; font.bold: true; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        onClicked: { log("Taking photo..."); sonyCamera.takePhoto() }
                    }

                    Button {
                        id: lvBtn
                        text: root.lvRunning ? "■ Stop LV" : "▶ Live View"
                        implicitWidth: 118; implicitHeight: 34
                        enabled: sonyCamera.connected
                        background: Rectangle {
                            radius: 4
                            color: {
                                if (!lvBtn.enabled)      return "#444444"
                                if (root.lvRunning)      return lvBtn.pressed ? "#7a1a1a" : "#992222"
                                return lvBtn.pressed     ? "#1a4a7a" : "#224466"
                            }
                        }
                        contentItem: Text { text: lvBtn.text; color: "white"; font.bold: root.lvRunning; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        onClicked: {
                            if (root.lvRunning) sonyCamera.stopLiveView()
                            else                sonyCamera.startLiveView()
                        }
                    }

                    Button {
                        text: "Rotate"
                        implicitWidth: 118; implicitHeight: 30
                        enabled: mainImg.source.toString() !== "" && !root.lvRunning
                        opacity: enabled ? 1.0 : 0.4
                        onClicked: root.imageRotation = (root.imageRotation + 90) % 360
                    }

                    Button {
                        id: fillBtn
                        text: root.fillScreen ? "⊡ Exit Fill" : "⛶ Full Screen"
                        implicitWidth: 118; implicitHeight: 30
                        background: Rectangle {
                            radius: 4
                            color: root.fillScreen ? (fillBtn.pressed ? "#4a3a00" : "#6a5500") : (fillBtn.pressed ? "#2a2a2a" : "#3a3a3a")
                        }
                        contentItem: Text { text: fillBtn.text; color: "#FFD700"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        onClicked: {
                            root.fillScreen = !root.fillScreen
                            if (root.fillScreen) resetView()
                        }
                    }
                }
            }

            Rectangle {
                id: canvasArea
                color: "#1E1E1E"
                Layout.fillWidth: true
                Layout.fillHeight: true

                Item {
                    id: imgContainer
                    anchors.fill: parent
                    visible: !root.lvRunning && !root.compareMode
                    clip: true

                    Image {
                        id: mainImg
                        width:  root.fillScreen ? parent.width  * root.zoomScale : parent.width
                        height: root.fillScreen ? parent.height * root.zoomScale : parent.height
                        x: root.fillScreen ? (parent.width  - width)  / 2 + root.panX : 0
                        y: root.fillScreen ? (parent.height - height) / 2 + root.panY : 0
                        fillMode: root.fillScreen ? Image.Stretch : Image.PreserveAspectFit
                        cache: false; asynchronous: true
                        rotation: root.imageRotation
                        smooth: true
                        onSourceChanged: { root.imageRotation = 0; resetView() }
                        onStatusChanged: if (status === Image.Ready && root.showHistogram) computeHistogram(mainImg)
                        BusyIndicator { anchors.centerIn: parent; running: mainImg.status === Image.Loading }
                    }

                    WheelHandler {
                        target: null
                        onWheel: function(event) {
                            var delta = event.angleDelta.y
                            var factor = delta > 0 ? 1.12 : 0.89
                            var newScale = Math.max(0.1, Math.min(10.0, root.zoomScale * factor))
                            root.zoomScale = newScale
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.fillScreen && root.zoomScale > 1.0
                        cursorShape: Qt.OpenHandCursor

                        property real startX: 0; property real startY: 0
                        property real startPanX: 0; property real startPanY: 0

                        onPressed: function(mouse) {
                            startX = mouse.x; startY = mouse.y
                            startPanX = root.panX; startPanY = root.panY
                            cursorShape = Qt.ClosedHandCursor
                        }
                        onReleased: cursorShape = Qt.OpenHandCursor
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                root.panX = startPanX + (mouse.x - startX)
                                root.panY = startPanY + (mouse.y - startY)
                            }
                        }
                    }

                    PinchHandler {
                        id: pinch
                        target: null
                        onActiveScaleChanged: {
                            root.zoomScale = Math.max(0.1, Math.min(10.0, root.zoomScale * pinch.activeScale))
                        }
                    }
                }

                Image {
                    id: lvImg
                    anchors.fill: parent
                    fillMode: root.fillScreen ? Image.Stretch : Image.PreserveAspectFit
                    cache: false
                    visible: root.lvRunning
                    source: root.lvRunning
                            ? "image://liveview/frame?seq=" + root.lvFrameSeq
                            : ""
                    smooth: true; mipmap: false

                    Rectangle {
                        anchors.fill: parent; color: "transparent"
                        border.color: "#CC2222"; border.width: 2
                        opacity: lvPulse.pulseOpacity
                    }
                    SequentialAnimation {
                        id: lvPulse; running: root.lvRunning; loops: Animation.Infinite
                        property real pulseOpacity: 1.0
                        NumberAnimation { target: lvPulse; property: "pulseOpacity"; to: 0.3; duration: 700 }
                        NumberAnimation { target: lvPulse; property: "pulseOpacity"; to: 1.0; duration: 700 }
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: root.compareMode
                    clip: true

                    Item {
                        id: compareLeft
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        width: parent.width * root.compareSplit
                        clip: true

                        Image {
                            id: cmpImgA
                            width: compareLeft.parent.width; height: parent.height
                            fillMode: root.fillScreen ? Image.Stretch : Image.PreserveAspectFit
                            source: root.compareImageA; cache: false; smooth: true
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.left: parent.left
                            color: "#AA000000"; width: 60; height: 20; radius: 3
                            anchors.margins: 8
                            Text { anchors.centerIn: parent; text: "BEFORE"; color: "#FFD700"; font.pixelSize: 10; font.bold: true }
                        }
                    }

                    Item {
                        id: compareRight
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        width: parent.width * (1.0 - root.compareSplit)
                        clip: true

                        Image {
                            id: cmpImgB
                            x: -(parent.parent.width * root.compareSplit)
                            width: compareRight.parent.width; height: parent.height
                            fillMode: root.fillScreen ? Image.Stretch : Image.PreserveAspectFit
                            source: root.compareImageB; cache: false; smooth: true
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom; anchors.right: parent.right
                            color: "#AA000000"; width: 60; height: 20; radius: 3
                            anchors.margins: 8
                            Text { anchors.centerIn: parent; text: "AFTER"; color: "#44FFAA"; font.pixelSize: 10; font.bold: true }
                        }
                    }

                    Rectangle {
                        id: splitHandle
                        x: parent.width * root.compareSplit - 1
                        anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 3; color: "#FFFFFF"; opacity: 0.85

                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: "#FFFFFF"
                            Text { anchors.centerIn: parent; text: "⇔"; font.pixelSize: 13; color: "#222222" }
                        }

                        MouseArea {
                            anchors.fill: parent; anchors.margins: -12
                            cursorShape: Qt.SizeHorCursor
                            drag.target: splitHandle
                            drag.axis: Drag.XAxis
                            drag.minimumX: 20
                            drag.maximumX: canvasArea.width - 20
                            onPositionChanged: {
                                if (drag.active)
                                    root.compareSplit = Math.max(0.05, Math.min(0.95,
                                        (splitHandle.x + 1) / canvasArea.width))
                            }
                        }
                    }
                }

                Rectangle {
                    id: histogramPanel
                    visible: root.showHistogram
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 12
                    width: 200; height: 120; radius: 6
                    color: "#CC000000"
                    border.color: "#444444"; border.width: 1

                    Canvas {
                        id: histDisplay
                        anchors.fill: parent; anchors.margins: 8

                        property var rData: root.histR
                        property var gData: root.histG
                        property var bData: root.histB

                        onRDataChanged: requestPaint()
                        onGDataChanged: requestPaint()
                        onBDataChanged: requestPaint()

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            if (!root.histR.length) return

                            var maxVal = 1
                            for (var i = 0; i < 256; i++) {
                                maxVal = Math.max(maxVal, root.histR[i], root.histG[i], root.histB[i])
                            }

                            function drawChannel(data, color) {
                                ctx.globalAlpha = 0.55
                                ctx.fillStyle = color
                                ctx.beginPath()
                                ctx.moveTo(0, height)
                                for (var k = 0; k < 256; k++) {
                                    var bx = k * width / 256
                                    var bh = (data[k] / maxVal) * height
                                    ctx.lineTo(bx, height - bh)
                                }
                                ctx.lineTo(width, height)
                                ctx.closePath()
                                ctx.fill()
                            }

                            drawChannel(root.histB, "#4488FF")
                            drawChannel(root.histG, "#44CC44")
                            drawChannel(root.histR, "#FF4444")

                            ctx.globalAlpha = 0.18
                            ctx.strokeStyle = "#FFFFFF"
                            ctx.lineWidth = 0.5
                            for (var g = 1; g < 4; g++) {
                                ctx.beginPath()
                                ctx.moveTo(g * width / 4, 0)
                                ctx.lineTo(g * width / 4, height)
                                ctx.stroke()
                            }
                            ctx.globalAlpha = 1.0
                        }
                    }

                    Text {
                        anchors.top: parent.top; anchors.left: parent.left
                        anchors.margins: 4
                        text: "Histogram"; color: "#AAAAAA"; font.pixelSize: 9
                    }
                }

                Item {
                    anchors.fill: parent
                    visible: root.lvRunning
                    opacity: root.lvRunning ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    Row {
                        anchors.top: parent.top; anchors.left: parent.left
                        anchors.margins: 10; spacing: 6

                        Rectangle {
                            width: 10; height: 10; radius: 5; color: "#FF3333"
                            anchors.verticalCenter: parent.verticalCenter
                            SequentialAnimation on opacity {
                                running: root.lvRunning; loops: Animation.Infinite
                                NumberAnimation { to: 0.2; duration: 500 }
                                NumberAnimation { to: 1.0; duration: 500 }
                            }
                        }
                        Text { text: "LIVE"; color: "#FF3333"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
                        height: 32; color: "#AA000000"
                        Row {
                            anchors.centerIn: parent; spacing: 20
                            HudLabel { label: "ISO";   value: sonyCamera.connected ? sonyCamera.formatISO(sonyCamera.currentISO)         : "—" }
                            HudLabel { label: "SS";    value: sonyCamera.connected ? sonyCamera.formatShutter(sonyCamera.currentShutter) : "—" }
                            HudLabel { label: "EV";    value: sonyCamera.connected ? sonyCamera.formatExposure(sonyCamera.currentExposure): "—" }
                            HudLabel { label: "ZOOM";  value: Math.round(root.zoomScale * 100) + "%" }
                        }
                    }
                }

                Rectangle {
                    visible: root.fillScreen && !root.lvRunning
                    anchors.top: parent.top; anchors.right: parent.right
                    anchors.margins: 10
                    color: "#AA000000"; radius: 4
                    width: 90; height: 22
                    Text {
                        anchors.centerIn: parent
                        text: "🔍  " + Math.round(root.zoomScale * 100) + "%"
                        color: "#FFD700"; font.pixelSize: 11; font.bold: true
                    }
                }

                Column {
                    anchors.centerIn: parent; spacing: 8
                    visible: mainImg.source.toString() === "" && mainImg.status !== Image.Loading
                             && !root.lvRunning && !root.compareMode
                    Text { text: "📷"; font.pixelSize: 48; anchors.horizontalCenter: parent.horizontalCenter }
                    Text {
                        text: sonyCamera.connected
                              ? "Press 'Take Photo' or '▶ Live View'"
                              : "Open an image or connect camera"
                        color: "#555555"; font.pixelSize: 13
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }

            Rectangle {
                width: 220; color: "#161616"; Layout.fillHeight: true
                visible: !root.fillScreen

                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 0; spacing: 0

                    Rectangle {
                        Layout.fillWidth: true; height: 30; color: "#1E1E1E"
                        Text {
                            text: "Camera Settings"
                            color: "#AAAAAA"; font.pixelSize: 11; font.bold: true
                            anchors.verticalCenter: parent.verticalCenter; leftPadding: 10
                        }
                        Text {
                            text: "Refresh"
                            color: sonyCamera.connected ? "#5588FF" : "#444"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right; anchors.rightMargin: 10
                            MouseArea {
                                anchors.fill: parent; anchors.margins: -6
                                enabled: sonyCamera.connected
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { sonyCamera.fetchAllSettings_(); log("Settings refreshed.") }
                            }
                        }
                    }

                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        Column {
                            width: 220; spacing: 0

                            SettingRow {
                                label: "ISO"
                                currentText: sonyCamera.connected ? sonyCamera.formatISO(sonyCamera.currentISO) : "—"
                                model: sonyCamera.isoValues
                                currentValue: sonyCamera.currentISO
                                enabled: sonyCamera.connected && sonyCamera.isoValues.length > 0
                                formatFn: function(v) { return sonyCamera.formatISO(v) }
                                onValueSelected: function(v) { sonyCamera.setISO(v) }
                            }

                            SettingRow {
                                label: "Shutter"
                                currentText: sonyCamera.connected ? sonyCamera.formatShutter(sonyCamera.currentShutter) : "—"
                                model: sonyCamera.shutterValues
                                currentValue: sonyCamera.currentShutter
                                enabled: sonyCamera.connected && sonyCamera.shutterValues.length > 0
                                formatFn: function(v) { return sonyCamera.formatShutter(v) }
                                onValueSelected: function(v) { sonyCamera.setShutterSpeed(v) }
                            }

                            SettingRow {
                                label: "Exposure"
                                currentText: sonyCamera.connected ? sonyCamera.formatExposure(sonyCamera.currentExposure) : "—"
                                model: sonyCamera.exposureValues
                                currentValue: sonyCamera.currentExposure
                                enabled: sonyCamera.connected
                                formatFn: function(v) { return sonyCamera.formatExposure(v) }
                                onValueSelected: function(v) { sonyCamera.setExposure(v) }
                            }

                            SettingRow {
                                label: "Sharpness"
                                currentText: sonyCamera.connected ? String(sonyCamera.currentSharpness) : "—"
                                model: sonyCamera.sharpnessValues
                                currentValue: sonyCamera.currentSharpness
                                enabled: sonyCamera.connected
                                formatFn: function(v) { return String(v) }
                                onValueSelected: function(v) { sonyCamera.setSharpness(v) }
                            }

                            SettingRow {
                                label: "Brightness"
                                currentText: sonyCamera.connected ? sonyCamera.formatBrightness(sonyCamera.currentBrightness) : "—"
                                model: sonyCamera.brightnessValues
                                currentValue: sonyCamera.currentBrightness
                                enabled: sonyCamera.connected
                                formatFn: function(v) { return sonyCamera.formatBrightness(v) }
                                onValueSelected: function(v) { sonyCamera.setBrightness(v) }
                            }

                            SettingRow {
                                label: "Img Size"
                                currentText: sonyCamera.connected ? sonyCamera.formatImageSize(sonyCamera.currentImageSize) : "—"
                                model: sonyCamera.imageSizeValues
                                currentValue: sonyCamera.currentImageSize
                                enabled: sonyCamera.connected && sonyCamera.imageSizeValues.length > 0
                                formatFn: function(v) { return sonyCamera.formatImageSize(v) }
                                onValueSelected: function(v) { sonyCamera.setImageSize(v) }
                            }

                            SettingRow {
                                label: "Img Qual"
                                currentText: sonyCamera.connected ? sonyCamera.formatImageQual(sonyCamera.currentImageQual) : "—"
                                model: sonyCamera.imageQualValues
                                currentValue: sonyCamera.currentImageQual
                                enabled: sonyCamera.connected && sonyCamera.imageQualValues.length > 0
                                formatFn: function(v) { return sonyCamera.formatImageQual(v) }
                                onValueSelected: function(v) { sonyCamera.setImageQual(v) }
                            }

                            Rectangle {
                                id: wbPanel
                                width: 220
                                height: sonyCamera.connected ? 38 + (wbExpanded ? wbList.height : 0) : 0
                                clip: true
                                color: "transparent"
                                Behavior on height { NumberAnimation { duration: 120 } }

                                property bool wbExpanded: false

                                Rectangle {
                                    width: parent.width; height: 38
                                    color: wbMouse.containsMouse ? "#222222" : "#1A1A1A"

                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left; anchors.leftMargin: 10; spacing: 0
                                        Text {
                                            text: "White Bal."
                                            color: sonyCamera.connected ? "#CCCCCC" : "#555555"
                                            font.pixelSize: 12; width: 80; anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Text {
                                            text: {
                                                if (!sonyCamera.connected) return "—"
                                                for (var i = 0; i < root.wbValues.length; i++) {
                                                    if (root.wbValues[i] === wbPanel.selectedCode) return root.wbLabels[i]
                                                }
                                                return "Auto"
                                            }
                                            color: sonyCamera.connected ? "#FFFFFF" : "#444444"
                                            font.pixelSize: 12; font.bold: true; anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                    Text {
                                        text: wbPanel.wbExpanded ? "▲" : "▼"; color: "#888888"; font.pixelSize: 9
                                        anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 10
                                    }
                                    Rectangle { width: parent.width; height: 1; color: "#2A2A2A"; anchors.bottom: parent.bottom }
                                    MouseArea {
                                        id: wbMouse; anchors.fill: parent; hoverEnabled: true
                                        enabled: sonyCamera.connected; cursorShape: Qt.PointingHandCursor
                                        onClicked: wbPanel.wbExpanded = !wbPanel.wbExpanded
                                    }
                                }

                                property int selectedCode: root.wbValues[0]

                                ListView {
                                    id: wbList
                                    x: 0; y: 38; width: parent.width
                                    height: wbPanel.wbExpanded ? Math.min(root.wbLabels.length * 32, 160) : 0
                                    clip: true; model: root.wbLabels
                                    Behavior on height { NumberAnimation { duration: 120 } }
                                    delegate: Rectangle {
                                        width: wbList.width; height: 32
                                        color: (root.wbValues[index] === wbPanel.selectedCode) ? "#2A3A5A"
                                             : wbItemMouse.containsMouse ? "#222233" : "#181818"
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter; leftPadding: 16
                                            text: modelData
                                            color: (root.wbValues[index] === wbPanel.selectedCode) ? "#88BBFF" : "#AAAAAA"
                                            font.pixelSize: 11
                                        }
                                        Rectangle { width: parent.width; height: 1; color: "#1E1E1E"; anchors.bottom: parent.bottom }
                                        MouseArea {
                                            id: wbItemMouse; anchors.fill: parent; hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                wbPanel.selectedCode = root.wbValues[index]
                                                wbPanel.wbExpanded = false
                                                sonyCamera.setWhiteBalance(root.wbValues[index])
                                                log("White Balance → " + root.wbLabels[index])
                                            }
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                id: focusPanel
                                width: 220
                                height: sonyCamera.connected && sonyCamera.focusRangeValid ? 86 : 0
                                clip: true; color: "transparent"
                                Behavior on height { NumberAnimation { duration: 150 } }

                                Rectangle { width: parent.width; height: 1; color: "#2A2A2A"; anchors.top: parent.top }

                                Column {
                                    anchors.fill: parent; anchors.topMargin: 4
                                    anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 4

                                    Row {
                                        width: parent.width; height: 20; spacing: 0
                                        Text { text: "MF Position"; color: "#CCCCCC"; font.pixelSize: 12; width: 80; anchors.verticalCenter: parent.verticalCenter }
                                        Text {
                                            text: {
                                                if (!sonyCamera.focusRangeValid) return "—"
                                                var span = sonyCamera.focusMax - sonyCamera.focusMin
                                                if (span <= 0) return String(sonyCamera.focusPosition)
                                                var pct = (sonyCamera.focusPosition - sonyCamera.focusMin) / span
                                                if (pct < 0.12) return "Near"
                                                if (pct > 0.88) return "∞ Far"
                                                return Math.round(pct * 100) + "%"
                                            }
                                            color: "#FFFFFF"; font.pixelSize: 12; font.bold: true; anchors.verticalCenter: parent.verticalCenter
                                        }
                                        Item { width: 1; height: 1; Layout.fillWidth: true }
                                        Text {
                                            text: "⟳"; color: sonyCamera.connected ? "#5588FF" : "#444"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter
                                            MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: sonyCamera.fetchFocusRange() }
                                        }
                                    }

                                    Row {
                                        width: parent.width; spacing: 0
                                        Text { text: "Near"; color: "#555"; font.pixelSize: 9; width: parent.width / 2 }
                                        Text { text: "Far ∞"; color: "#555"; font.pixelSize: 9; width: parent.width / 2; horizontalAlignment: Text.AlignRight }
                                    }

                                    Item {
                                        id: focusSliderItem; width: parent.width; height: 24
                                        Rectangle {
                                            id: focusTrack
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width; height: 4; radius: 2; color: "#333333"
                                            Rectangle {
                                                width: {
                                                    var span = sonyCamera.focusMax - sonyCamera.focusMin
                                                    if (span <= 0) return 0
                                                    return (sonyCamera.focusPosition - sonyCamera.focusMin) / span * focusTrack.width
                                                }
                                                height: parent.height; radius: parent.radius; color: "#3366CC"
                                                Behavior on width { NumberAnimation { duration: 80 } }
                                            }
                                        }

                                        Rectangle {
                                            id: focusThumb
                                            width: 18; height: 18; radius: 9
                                            color: focusDragArea.pressed ? "#88BBFF" : "#5588FF"
                                            border.color: "#AACCFF"; border.width: 1
                                            anchors.verticalCenter: parent.verticalCenter
                                            x: {
                                                var span = sonyCamera.focusMax - sonyCamera.focusMin
                                                if (span <= 0) return 0
                                                var frac = (sonyCamera.focusPosition - sonyCamera.focusMin) / span
                                                return frac * (focusSliderItem.width - focusThumb.width)
                                            }
                                            Behavior on x { enabled: !focusDragArea.pressed; NumberAnimation { duration: 80 } }

                                            MouseArea {
                                                id: focusDragArea; anchors.fill: parent; anchors.margins: -6
                                                cursorShape: Qt.SizeHorCursor
                                                drag.target: focusThumb; drag.axis: Drag.XAxis
                                                drag.minimumX: 0; drag.maximumX: focusSliderItem.width - focusThumb.width
                                                onPressed:  sonyCamera.setFocusDragging(true)
                                                onReleased: {
                                                    sonyCamera.setFocusDragging(false)
                                                    var frac = focusThumb.x / (focusSliderItem.width - focusThumb.width)
                                                    var raw  = sonyCamera.focusMin + Math.round(frac * (sonyCamera.focusMax - sonyCamera.focusMin))
                                                    sonyCamera.setFocusPosition(raw)
                                                }
                                            }
                                        }

                                        MouseArea {
                                            anchors.fill: parent; anchors.margins: -4
                                            onClicked: function(mouse) {
                                                var frac = Math.max(0, Math.min(1, (mouse.x - focusThumb.width / 2) / (focusSliderItem.width - focusThumb.width)))
                                                var raw = sonyCamera.focusMin + Math.round(frac * (sonyCamera.focusMax - sonyCamera.focusMin))
                                                sonyCamera.setFocusPosition(raw)
                                            }
                                        }
                                    }
                                }
                            }

                            Item { width: 220; height: 20; visible: !sonyCamera.connected }
                            Text {
                                visible: !sonyCamera.connected
                                width: 200; leftPadding: 10; rightPadding: 10
                                text: "Connect camera to\nadjust settings"
                                color: "#444444"; font.pixelSize: 11
                                wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: 230; color: "#111111"; Layout.fillHeight: true
                visible: !root.fillScreen

                ColumnLayout {
                    anchors.fill: parent; spacing: 0

                    Rectangle {
                        Layout.fillWidth: true; height: 30; color: "#1E1E1E"
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left; anchors.leftMargin: 10; spacing: 6
                            Rectangle { width: 8; height: 8; radius: 4; color: "#FF5F57" }
                            Rectangle { width: 8; height: 8; radius: 4; color: "#FEBC2E" }
                            Rectangle { width: 8; height: 8; radius: 4; color: "#28C840" }
                            Text { text: "Log"; color: "#888888"; font.pixelSize: 11; leftPadding: 4; anchors.verticalCenter: parent.verticalCenter }
                        }
                        Text {
                            text: "Clear"; color: "#555555"; font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right; anchors.rightMargin: 10
                            MouseArea { anchors.fill: parent; onClicked: logModel.clear(); cursorShape: Qt.PointingHandCursor }
                        }
                    }

                    ListView {
                        id: logView
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; model: logModel; spacing: 2
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Text {
                            width: logView.width - 16
                            leftPadding: 8; rightPadding: 8; topPadding: 2
                            text: model.line
                            color: {
                                if (model.line.indexOf("Error")                  >= 0) return "#FF6666";
                                if (model.line.indexOf("disconnected")           >= 0) return "#FF4444";
                                if (model.line.indexOf("connected successfully") >= 0) return "#44FF44";
                                if (model.line.indexOf("Connecting")             >= 0) return "#88CCFF";
                                if (model.line.indexOf("set to")                 >= 0) return "#AADDFF";
                                if (model.line.indexOf("Photo saved")            >= 0) return "#FFD700";
                                if (model.line.indexOf("Save folder")            >= 0) return "#88FFAA";
                                if (model.line.indexOf("Live view")              >= 0) return "#FF8844";
                                if (model.line.indexOf("White Balance")          >= 0) return "#AA88FF";
                                return "#AAAAAA";
                            }
                            font.pixelSize: 11; font.family: "Consolas, monospace"
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; height: 46; color: "#2B2B2B"

            Row {
                anchors.centerIn: parent; spacing: 8

                ToolbarButton { label: "EXIF"; accent: false; enabled_: sonyCamera.connected; onAction: sonyCamera.fetchExifInfo() }

                ToolbarButton { label: "−"; width_: 34; onAction: { root.zoomScale = Math.max(0.1, root.zoomScale - 0.1) } }

                Rectangle {
                    width: 54; height: 36; color: "#1A1A1A"; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        anchors.centerIn: parent
                        text: Math.round(root.zoomScale * 100) + "%"
                        color: "#FFD700"; font.pixelSize: 12; font.bold: true
                    }
                }

                ToolbarButton { label: "+"; width_: 34; onAction: { root.zoomScale = Math.min(10.0, root.zoomScale + 0.1) } }
                ToolbarButton { label: "1:1"; width_: 42; onAction: root.zoomScale = 1.0 }
                ToolbarButton { label: "Fit"; width_: 40; onAction: resetView() }

                Rectangle { width: 1; height: 28; color: "#444444"; anchors.verticalCenter: parent.verticalCenter }

                ToolbarButton {
                    label: "Histogram"
                    accent: root.showHistogram
                    width_: 90
                    onAction: {
                        root.showHistogram = !root.showHistogram
                        if (root.showHistogram && mainImg.source.toString() !== "")
                            computeHistogram(mainImg)
                    }
                }

                Rectangle { width: 1; height: 28; color: "#444444"; anchors.verticalCenter: parent.verticalCenter }

                ToolbarButton {
                    label: root.compareMode ? "✕ Compare" : "Compare"
                    accent: root.compareMode
                    width_: 82
                    onAction: {
                        if (root.compareMode) {
                            root.compareMode = false
                        } else {
                            if (imageModel.count < 2) {
                                log("Compare: need at least 2 images in film strip.")
                                return
                            }
                            comparePicker.open()
                        }
                    }
                }

                ToolbarButton {
                    label: root.fillScreen ? "⊡ Exit Full" : "⛶ Full"
                    accent: root.fillScreen
                    width_: 76
                    onAction: {
                        root.fillScreen = !root.fillScreen
                        if (root.fillScreen) resetView()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; height: 150; color: "#1A1A1A"
            visible: !root.fillScreen

            Rectangle {
                id: addBtn; width: 100; height: parent.height; color: "#2E2E2E"; anchors.left: parent.left
                Column {
                    anchors.centerIn: parent; spacing: 4
                    Text { text: "+"; color: "white"; font.pixelSize: 28; anchors.horizontalCenter: parent.horizontalCenter }
                    Text { text: "Add Image"; color: "#AAAAAA"; font.pixelSize: 11; anchors.horizontalCenter: parent.horizontalCenter }
                }
                MouseArea {
                    anchors.fill: parent; hoverEnabled: true
                    onClicked: { openingForMain = false; fileDialog.open() }
                    onEntered: addBtn.color = "#3E3E3E"; onExited: addBtn.color = "#2E2E2E"
                }
            }

            ScrollView {
                anchors.left: addBtn.right; anchors.right: parent.right
                anchors.top: parent.top; anchors.bottom: parent.bottom
                clip: true; ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                Row {
                    spacing: 6; leftPadding: 6; rightPadding: 6; height: 150
                    Repeater {
                        model: imageModel
                        delegate: Rectangle {
                            width: 130; height: 138; radius: 4
                            color: mainImg.source.toString() === model.path.toString() ? "#3A3A5A" : "#2A2A2A"
                            Image { anchors.fill: parent; anchors.margins: 4; source: model.path; fillMode: Image.PreserveAspectFit; cache: false }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    mainImg.source = model.path
                                    root.lvRunning && sonyCamera.stopLiveView()
                                    resetView()
                                }
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 3
                                width: 18; height: 18; radius: 3; color: "#AA000000"
                                Text { anchors.centerIn: parent; text: index + 1; color: "#AAAAAA"; font.pixelSize: 9 }
                            }
                            Rectangle {
                                width: 20; height: 20; radius: 10; color: "#CC3333"
                                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 2
                                Text { anchors.centerIn: parent; text: "×"; color: "white" }
                                MouseArea { anchors.fill: parent; onClicked: root.deleteImage(model.path) }
                            }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: comparePicker
        anchors.centerIn: parent
        width: 460; height: 300
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0

        background: Rectangle { color: "#1E1E1E"; radius: 8; border.color: "#444"; border.width: 1 }

        ColumnLayout {
            anchors.fill: parent; spacing: 0

            Rectangle {
                Layout.fillWidth: true; height: 40; color: "#222222"; radius: 8
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 8; color: "#222222" }
                Text { text: "Select Images to Compare"; color: "#CCCCCC"; font.pixelSize: 13; font.bold: true; anchors.verticalCenter: parent.verticalCenter; leftPadding: 16 }
                Text {
                    text: "✕"; color: "#666666"; font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 14
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: comparePicker.close() }
                }
            }

            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 16; spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "Before (Left)"; color: "#888888"; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; color: "#111111"; radius: 4
                        clip: true
                        ListView {
                            anchors.fill: parent; model: imageModel; spacing: 2
                            delegate: Rectangle {
                                width: parent ? parent.width : 0; height: 44
                                color: root.compareImageA === model.path ? "#1A3A5A"
                                     : (aPickMouse.containsMouse ? "#222233" : "#1A1A1A")
                                Row {
                                    anchors.fill: parent; anchors.margins: 6; spacing: 8
                                    Image { width: 36; height: 36; source: model.path; fillMode: Image.PreserveAspectFit; cache: false; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: "Image " + (index+1); color: "#AAAAAA"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                }
                                MouseArea {
                                    id: aPickMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.compareImageA = model.path
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: "After (Right)"; color: "#888888"; font.pixelSize: 11 }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; color: "#111111"; radius: 4
                        clip: true
                        ListView {
                            anchors.fill: parent; model: imageModel; spacing: 2
                            delegate: Rectangle {
                                width: parent ? parent.width : 0; height: 44
                                color: root.compareImageB === model.path ? "#1A5A3A"
                                     : (bPickMouse.containsMouse ? "#1A2A1A" : "#1A1A1A")
                                Row {
                                    anchors.fill: parent; anchors.margins: 6; spacing: 8
                                    Image { width: 36; height: 36; source: model.path; fillMode: Image.PreserveAspectFit; cache: false; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: "Image " + (index+1); color: "#AAAAAA"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                }
                                MouseArea {
                                    id: bPickMouse; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.compareImageB = model.path
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true; height: 50; color: "transparent"
                Button {
                    anchors.centerIn: parent; implicitWidth: 140; implicitHeight: 34
                    enabled: root.compareImageA !== "" && root.compareImageB !== ""
                    opacity: enabled ? 1.0 : 0.4
                    background: Rectangle { radius: 4; color: "#226622" }
                    contentItem: Text { text: "Start Compare"; color: "white"; font.bold: true; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    onClicked: {
                        root.compareMode = true
                        root.compareSplit = 0.5
                        comparePicker.close()
                        log("Compare: Before=" + root.compareImageA + " | After=" + root.compareImageB)
                    }
                }
            }
        }
    }

    Popup {
        id: exifDialog
        anchors.centerIn: parent
        width: 380; height: 340
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0

        property string model_:    "—"
        property string lens_:     "—"
        property string focal_:    "—"
        property string aperture_: "—"
        property string iso_:      "—"
        property string shutter_:  "—"
        property string expMode_:  "—"
        property string wb_:       "—"
        property string battery_:  "—"

        background: Rectangle { color: "#1A1A1A"; radius: 8; border.color: "#333333"; border.width: 1 }

        ColumnLayout {
            anchors.fill: parent; spacing: 0

            Rectangle {
                Layout.fillWidth: true; height: 40; color: "#222222"; radius: 8
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 8; color: "#222222" }
                Text { text: "📷  Camera Info & EXIF"; color: "#CCCCCC"; font.pixelSize: 13; font.bold: true; anchors.verticalCenter: parent.verticalCenter; leftPadding: 16 }
                Text {
                    text: "✕"; color: "#666666"; font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 14
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: exifDialog.close() }
                }
            }

            GridLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 16
                columns: 2; rowSpacing: 10; columnSpacing: 12

                component ExifLabel: Text { color: "#666666"; font.pixelSize: 12 }
                component ExifValue: Text { color: "#EEEEEE"; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }

                ExifLabel { text: "Model" }       ExifValue { text: exifDialog.model_ }
                ExifLabel { text: "Lens" }        ExifValue { text: exifDialog.lens_; wrapMode: Text.NoWrap; elide: Text.ElideRight }
                ExifLabel { text: "Focal Length" }ExifValue { text: exifDialog.focal_ }
                ExifLabel { text: "Aperture" }    ExifValue { text: exifDialog.aperture_ }
                ExifLabel { text: "ISO" }         ExifValue { text: exifDialog.iso_ }
                ExifLabel { text: "Shutter" }     ExifValue { text: exifDialog.shutter_ }
                ExifLabel { text: "Exposure Mode"}ExifValue { text: exifDialog.expMode_ }
                ExifLabel { text: "White Balance"}ExifValue { text: exifDialog.wb_ }
                ExifLabel { text: "Battery" }     ExifValue { text: exifDialog.battery_ }
            }

            Text {
                Layout.fillWidth: true; Layout.bottomMargin: 12
                horizontalAlignment: Text.AlignHCenter
                text: "Note: Exposure/Sharpness locked in Manual mode"
                color: "#444444"; font.pixelSize: 10; font.italic: true
            }
        }
    }

    FileDialog {
        id: fileDialog; title: "Select an Image"
        nameFilters: ["Image files (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"]
        onAccepted: {
            var path = fileDialog.selectedFile || fileDialog.fileUrl
            if (path) {
                mainImg.source = path
                imageModel.append({ path: path })
                log("Opened: " + path)
                resetView()
            }
        }
    }


    component HudLabel: Row {
        spacing: 4
        property string label: ""
        property string value: "—"
        Text { text: label + ":"; color: "#888888"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
        Text { text: value;        color: "#FFFFFF";  font.pixelSize: 11; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
    }

    component ToolbarButton: Rectangle {
        id: tbRoot
        property string label:    ""
        property bool   accent:   false
        property bool   enabled_: true
        property int    width_:   0
        signal action()

        width:  width_ > 0 ? width_ : label.length * 8 + 24
        height: 36
        radius: 4
        color:  accent     ? (tbMouse.containsMouse ? "#4a6a00" : "#3a5500")
              : enabled_   ? (tbMouse.containsMouse ? "#3A3A3A" : "#2E2E2E")
              :               "#222222"
        opacity: enabled_ ? 1.0 : 0.4
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined

        Text {
            anchors.centerIn: parent
            text:  tbRoot.label
            color: tbRoot.accent ? "#AAFF44" : "#DDDDDD"
            font.pixelSize: 12; font.bold: tbRoot.accent
        }

        MouseArea {
            id: tbMouse; anchors.fill: parent; hoverEnabled: true
            enabled: tbRoot.enabled_; cursorShape: Qt.PointingHandCursor
            onClicked: tbRoot.action()
        }
    }

    component SettingRow: Rectangle {
        id: settingRow
        width: 220; height: expanded ? 38 + dropList.height : 38
        color: "transparent"; clip: true

        property string  label:        ""
        property string  currentText:  "—"
        property var     model:        []
        property var     currentValue: 0
        property bool    enabled:      false
        property var     formatFn:     function(v) { return String(v) }
        property bool    expanded:     false

        signal valueSelected(var value)
        Behavior on height { NumberAnimation { duration: 120 } }

        Rectangle {
            width: parent.width; height: 38
            color: settingRowMouse.containsMouse ? "#222222" : "#1A1A1A"

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left; anchors.leftMargin: 10; spacing: 0
                Text { text: settingRow.label; color: settingRow.enabled ? "#CCCCCC" : "#555555"; font.pixelSize: 12; width: 80; anchors.verticalCenter: parent.verticalCenter }
                Text { text: settingRow.currentText; color: settingRow.enabled ? "#FFFFFF" : "#444444"; font.pixelSize: 12; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
            }
            Text { text: settingRow.expanded ? "▲" : "▼"; color: settingRow.enabled ? "#888888" : "#333333"; font.pixelSize: 9; anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 10 }
            Rectangle { width: parent.width; height: 1; color: "#2A2A2A"; anchors.bottom: parent.bottom }

            MouseArea {
                id: settingRowMouse; anchors.fill: parent; hoverEnabled: true
                enabled: settingRow.enabled; cursorShape: Qt.PointingHandCursor
                onClicked: settingRow.expanded = !settingRow.expanded
            }
        }

        ListView {
            id: dropList; x: 0; y: 38; width: parent.width
            height: settingRow.expanded ? Math.min(Math.max(settingRow.model.length, 1) * 32, 160) : 0
            clip: true; model: settingRow.model.length > 0 ? settingRow.model : ["__empty__"]
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Behavior on height { NumberAnimation { duration: 120 } }

            delegate: Rectangle {
                width: dropList.width; height: 32
                color: (modelData !== "__empty__" && modelData == settingRow.currentValue) ? "#2A3A5A"
                     : dropMouse.containsMouse ? "#222233" : "#181818"

                Text {
                    anchors.verticalCenter: parent.verticalCenter; leftPadding: 16
                    text: modelData === "__empty__" ? "Not available in this mode" : settingRow.formatFn(modelData)
                    color: modelData === "__empty__" ? "#555555" : (modelData == settingRow.currentValue ? "#88BBFF" : "#AAAAAA")
                    font.pixelSize: 11; font.italic: modelData === "__empty__"
                }
                Rectangle { width: parent.width; height: 1; color: "#1E1E1E"; anchors.bottom: parent.bottom }

                MouseArea {
                    id: dropMouse; anchors.fill: parent; hoverEnabled: true
                    cursorShape: modelData === "__empty__" ? Qt.ArrowCursor : Qt.PointingHandCursor
                    onClicked: {
                        if (modelData === "__empty__") return
                        settingRow.valueSelected(modelData)
                        settingRow.expanded = false
                    }
                }
            }
        }
    }
}