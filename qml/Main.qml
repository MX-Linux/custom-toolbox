pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.settings
import "components"

ApplicationWindow {
    id: root

    required property var backend
    required property string version

    SystemPalette {
        id: systemPalette
        colorGroup: root.active ? SystemPalette.Active : SystemPalette.Inactive
    }

    width: 1080
    height: 720
    minimumWidth: 720
    minimumHeight: 560
    visible: true
    title: backend.title
    color: backgroundColor

    readonly property color backgroundColor: systemPalette.window
    readonly property color surfaceColor: systemPalette.base
    readonly property color raisedSurfaceColor: Qt.tint(systemPalette.base, Qt.alpha(systemPalette.highlight, 0.08))
    readonly property color primaryTextColor: systemPalette.text
    readonly property color secondaryTextColor: Qt.alpha(systemPalette.text, 0.68)
    readonly property color borderColor: Qt.alpha(systemPalette.text, 0.18)
    readonly property color accentColor: systemPalette.highlight
    readonly property color accentWash: Qt.alpha(systemPalette.highlight, 0.13)
    readonly property color inactiveControlColor: systemPalette.mid
    readonly property real baseFontSize: Application.font.pixelSize > 0 ? Application.font.pixelSize : 13
    readonly property bool compactNavigation: width < 900
    property bool condensedView: false

    Component.onCompleted: {
        if (backend.reloadMessage.length > 0) {
            errorDialog.title = qsTr("Configuration error")
            errorDialog.message = backend.reloadMessage
            errorDialog.open()
        }
    }

    Settings {
        category: "Launcher_" + root.backend.customName
        property alias windowX: root.x
        property alias windowY: root.y
        property alias windowWidth: root.width
        property alias windowHeight: root.height
        property alias condensedView: root.condensedView
    }

    Connections {
        target: root.backend
        function onErrorOccurred(title, message) {
            errorDialog.title = title
            errorDialog.message = message
            errorDialog.open()
        }
        function onHideRequested() { root.hide() }
        function onShowRequested() { root.show() }
    }

    header: Rectangle {
        implicitHeight: 88
        color: root.surfaceColor
        border.color: root.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 25
            anchors.rightMargin: 25
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 6
                color: root.accentWash
                Image {
                    anchors.centerIn: parent
                    width: 38
                    height: 38
                    source: "../icons/custom-toolbox.svg"
                    fillMode: Image.PreserveAspectFit
                }
            }

            ColumnLayout {
                Layout.preferredWidth: root.compactNavigation ? 170 : 230
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: root.backend.title
                    color: root.primaryTextColor
                    font.pixelSize: root.baseFontSize + 7
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: !root.compactNavigation
                    text: root.backend.description
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                    elide: Text.ElideRight
                }
            }

            Item { Layout.fillWidth: true }

            TextField {
                id: searchField
                Layout.preferredWidth: Math.min(380, root.width * 0.36)
                Layout.minimumWidth: 210
                Layout.preferredHeight: 44
                leftPadding: 42
                rightPadding: 38
                placeholderText: ""
                color: root.primaryTextColor
                placeholderTextColor: root.secondaryTextColor
                selectByMouse: true
                focus: true
                onTextChanged: root.backend.search = text
                Accessible.name: qsTr("Search launchers")

                background: Rectangle {
                    radius: 6
                    color: root.backgroundColor
                    border.width: searchField.activeFocus ? 2 : 1
                    border.color: searchField.activeFocus ? root.accentColor : root.borderColor
                }
                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 15
                    anchors.verticalCenter: parent.verticalCenter
                    text: "⌕"
                    color: root.secondaryTextColor
                    font.pixelSize: root.baseFontSize + 11
                }
                Text {
                    visible: searchField.text.length === 0
                    anchors.left: parent.left
                    anchors.leftMargin: searchField.leftPadding
                    anchors.right: parent.right
                    anchors.rightMargin: searchField.rightPadding
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Search launchers and tasks…")
                    color: root.secondaryTextColor
                    font: searchField.font
                    elide: Text.ElideRight
                }
                ToolButton {
                    visible: searchField.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.verticalCenter: parent.verticalCenter
                    text: "×"
                    font.pixelSize: root.baseFontSize + 7
                    Accessible.name: qsTr("Clear search")
                    onClicked: searchField.clear()
                    background: Item {}

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }

            SecondaryButton {
                visible: root.width >= 820
                text: qsTranslate("MainWindow", "Help")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: root.backend.openHelp()
            }
            SecondaryButton {
                text: qsTranslate("MainWindow", "About...")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: aboutDialog.open()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 24

        Rectangle {
            visible: !root.compactNavigation
            Layout.preferredWidth: 218
            Layout.fillHeight: true
            radius: 6
            color: root.surfaceColor
            border.color: root.borderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 13
                spacing: 6
                Text {
                    Layout.leftMargin: 12
                    Layout.topMargin: 7
                    Layout.bottomMargin: 5
                    text: qsTr("CATEGORIES")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(9, root.baseFontSize - 3)
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.1
                }
                Repeater {
                    model: root.backend.categories
                    CategoryButton {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        text: modelData
                        selected: searchField.text.length === 0
                                  && (root.backend.selectedCategory === modelData
                                      || (index === 0 && root.backend.selectedCategory === ""))
                        accentColor: root.accentColor
                        mutedTextColor: root.secondaryTextColor
                        hoverColor: root.accentWash
                        onClicked: {
                            searchField.clear()
                            root.backend.selectedCategory = modelData
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }
                RowLayout {
                    visible: root.backend.startupVisible
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 7
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text {
                            text: qsTr("Launch at login")
                            color: root.primaryTextColor
                            font.pixelSize: root.baseFontSize
                            font.weight: Font.Medium
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Open this toolbox automatically")
                            color: root.secondaryTextColor
                            font.pixelSize: Math.max(10, root.baseFontSize - 2)
                            wrapMode: Text.Wrap
                        }
                    }
                    ModernSwitch {
                        checked: root.backend.startupEnabled
                        accentColor: root.accentColor
                        inactiveColor: root.inactiveControlColor
                        Accessible.name: qsTr("Launch this toolbox at login")
                        onToggled: root.backend.startupEnabled = checked
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 15

            Flickable {
                id: compactCategoriesFlickable
                visible: root.compactNavigation
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                contentWidth: compactCategories.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                // Qt's default wheel handling on Flickable applies flick momentum, which on
                // touchpads keeps decelerating in the old direction after the fingers reverse
                // (you have to lift off and let it stop before it will scroll the other way).
                // Move contentX directly instead so reversing direction is immediate.
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                        compactCategoriesFlickable.contentX = Math.max(0, Math.min(
                            Math.max(0, compactCategoriesFlickable.contentWidth - compactCategoriesFlickable.width),
                            compactCategoriesFlickable.contentX - delta))
                    }
                }

                Row {
                    id: compactCategories
                    spacing: 7
                    Repeater {
                        model: root.backend.categories
                        CategoryButton {
                            required property string modelData
                            required property int index
                            width: Math.max(92, implicitWidth)
                            text: modelData
                            selected: searchField.text.length === 0
                                      && (root.backend.selectedCategory === modelData
                                          || (index === 0 && root.backend.selectedCategory === ""))
                            accentColor: root.accentColor
                            mutedTextColor: root.secondaryTextColor
                            hoverColor: root.accentWash
                            onClicked: {
                                searchField.clear()
                                root.backend.selectedCategory = modelData
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 2
                    Text {
                        text: searchField.text.length > 0 ? qsTr("Search results")
                              : (root.backend.selectedCategory.length > 0
                                 ? root.backend.selectedCategory : qsTr("All launchers"))
                        color: root.primaryTextColor
                        font.pixelSize: root.baseFontSize + 12
                        font.weight: Font.Bold
                    }
                    Text {
                        text: searchField.text.length > 0
                              ? qsTr("Results matching “%1”").arg(searchField.text)
                              : qsTr("Choose a launcher to start an application or task")
                        color: root.secondaryTextColor
                        font.pixelSize: root.baseFontSize
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("%n launcher(s)", "", launcherGrid.count)
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 24
                    Layout.leftMargin: 5
                    Layout.rightMargin: 5
                    color: root.borderColor
                }

                Text {
                    Layout.maximumWidth: 130
                    text: qsTr("Condensed view")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                    elide: Text.ElideRight
                }
                ModernSwitch {
                    id: condensedSwitch
                    checked: root.condensedView
                    accentColor: root.accentColor
                    inactiveColor: root.inactiveControlColor
                    Accessible.name: qsTr("Use condensed launcher view")
                    ThemeToolTip {
                        visible: condensedSwitch.hovered
                        text: qsTr("Show more launchers at once")
                    }
                    onToggled: root.condensedView = checked
                }
            }

            Rectangle {
                visible: root.backend.reloadMessage.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: reloadText.implicitHeight + 20
                radius: 6
                color: root.accentWash
                Text {
                    id: reloadText
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.backend.reloadMessage
                    color: root.primaryTextColor
                    wrapMode: Text.Wrap
                }
            }

            GridView {
                id: launcherGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
                model: root.backend
                cellWidth: width / Math.max(1, Math.floor(width / (root.condensedView ? 230 : 300)))
                cellHeight: root.condensedView ? 112 : 154

                Timer {
                    id: returnToBoundsTimer
                    interval: 0
                    onTriggered: launcherGrid.returnToBounds()
                }

                onCellWidthChanged: returnToBoundsTimer.restart()
                onCellHeightChanged: returnToBoundsTimer.restart()

                // See the comment on the compact category Flickable above: bypass the default
                // flick-momentum wheel handling so touchpad scrolling can reverse direction
                // immediately instead of needing a full stop first.
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (event) => {
                        const minimumY = launcherGrid.originY
                        const maximumY = Math.max(minimumY,
                                                  minimumY + launcherGrid.contentHeight - launcherGrid.height)
                        launcherGrid.contentY = Math.max(minimumY, Math.min(
                            maximumY,
                            launcherGrid.contentY - event.angleDelta.y))
                    }
                }

                delegate: LauncherCard {
                    required property string name
                    required property string comment
                    required property string category
                    required property int sourceIndex
                    width: launcherGrid.cellWidth - 12
                    height: launcherGrid.cellHeight - 12
                    launcherName: name
                    description: comment
                    categoryName: category
                    condensed: root.condensedView
                    surfaceColor: root.surfaceColor
                    hoverSurfaceColor: root.raisedSurfaceColor
                    primaryTextColor: root.primaryTextColor
                    secondaryTextColor: root.secondaryTextColor
                    accentColor: root.accentColor
                    borderColor: root.borderColor
                    onClicked: root.backend.launch(sourceIndex)
                }

                displaced: Transition {
                    NumberAnimation { properties: "x,y"; duration: 160; easing.type: Easing.OutCubic }
                }
                Text {
                    visible: launcherGrid.count === 0
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 40, 420)
                    text: qsTr("No launchers found\nTry a different search or category.")
                    color: root.secondaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: root.baseFontSize + 3
                    lineHeight: 1.5
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    visible: root.compactNavigation && root.backend.startupVisible
                    text: qsTr("Launch at login")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                }
                ModernSwitch {
                    visible: root.compactNavigation && root.backend.startupVisible
                    checked: root.backend.startupEnabled
                    accentColor: root.accentColor
                    inactiveColor: root.inactiveControlColor
                    Accessible.name: qsTr("Launch this toolbox at login")
                    onToggled: root.backend.startupEnabled = checked
                }
                Item { Layout.fillWidth: true }
                SecondaryButton {
                    text: qsTranslate("MainWindow", "Edit")
                    textColor: root.primaryTextColor
                    hoverColor: root.accentWash
                    borderColor: root.borderColor
                    accentColor: root.accentColor
                    onClicked: root.backend.edit()
                }
                SecondaryButton {
                    text: qsTranslate("MainWindow", "Close")
                    textColor: root.primaryTextColor
                    hoverColor: root.accentWash
                    borderColor: root.borderColor
                    accentColor: root.accentColor
                    onClicked: root.close()
                }
            }
        }
    }

    Dialog {
        id: aboutDialog
        modal: true
        width: 440
        x: (root.width - width) / 2
        y: (root.height - height) / 2
        title: qsTr("About %1").arg(root.backend.title)
        background: Rectangle { color: root.surfaceColor; radius: 6; border.color: root.borderColor }
        footer: DialogButtonBox {
            standardButtons: Dialog.Close
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
            onRejected: aboutDialog.reject()
        }
        contentItem: ColumnLayout {
            spacing: 14
            Image {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                source: "../icons/custom-toolbox.svg"
                fillMode: Image.PreserveAspectFit
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.backend.title
                color: root.primaryTextColor
                font.pixelSize: root.baseFontSize + 11
                font.weight: Font.Bold
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Version %1").arg(root.version)
                color: root.secondaryTextColor
                font.pixelSize: root.baseFontSize
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Custom Toolbox creates focused collections of application launchers and system tasks.")
                color: root.primaryTextColor
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: root.baseFontSize + 1
            }
            SecondaryButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("License")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: root.backend.openLicense()
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Copyright © MX Linux")
                color: root.secondaryTextColor
                font.pixelSize: Math.max(10, root.baseFontSize - 2)
            }
        }
    }

    Dialog {
        id: errorDialog
        property string message: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        background: Rectangle {
            color: root.surfaceColor
            radius: 6
            border.color: root.borderColor
        }
        footer: DialogButtonBox {
            standardButtons: Dialog.Ok
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
            onAccepted: errorDialog.accept()
        }
        contentItem: Text {
            text: errorDialog.message
            color: root.primaryTextColor
            wrapMode: Text.Wrap
        }
    }
}
