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
    minimumWidth: backend.minimumWidth
    minimumHeight: backend.minimumHeight
    visible: true
    title: backend.title
    color: backgroundColor

    readonly property color backgroundColor: systemPalette.window
    readonly property color surfaceColor: systemPalette.base
    readonly property color raisedSurfaceColor: Qt.tint(systemPalette.base, Qt.alpha(systemPalette.highlight, 0.08))
    readonly property color primaryTextColor: systemPalette.text
    readonly property color secondaryTextColor: Qt.alpha(systemPalette.text, 0.82)
    readonly property color borderColor: Qt.alpha(systemPalette.text, 0.18)
    readonly property color accentColor: systemPalette.highlight
    readonly property color accentWash: Qt.alpha(systemPalette.highlight, 0.13)
    readonly property color inactiveControlColor: systemPalette.mid
    readonly property real baseFontSize: Application.font.pixelSize > 0 ? Application.font.pixelSize : 13
    readonly property bool compactNavigation: width < 900
    // Below this width the header keeps only the search field and the menu button.
    readonly property bool narrowHeader: width < 560
    // Short windows drop the page heading and tighten spacing so the launcher grid keeps room.
    readonly property bool shortWindow: height < 480
    // The view switches move into the header menu when they can't sit beside the heading.
    readonly property bool compactViewControls: shortWindow || !viewHeader.sideBySide
    property bool condensedView: false
    property bool hideCategories: false

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
        property alias hideCategories: root.hideCategories
    }

    // Hiding the category list also hides the only way back to "All launchers",
    // so drop any active filter instead of stranding the user in a subset.
    onHideCategoriesChanged: {
        if (root.hideCategories) {
            root.backend.selectedCategory = ""
        }
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
        implicitHeight: root.shortWindow ? 64 : 88
        color: root.surfaceColor
        border.color: root.borderColor
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.narrowHeader ? 12 : 25
            anchors.rightMargin: root.narrowHeader ? 12 : 25
            spacing: 14

            Rectangle {
                visible: !root.narrowHeader
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
                visible: !root.narrowHeader
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

            Item {
                visible: !root.narrowHeader
                Layout.fillWidth: true
            }

            TextField {
                id: searchField
                Layout.fillWidth: root.narrowHeader
                Layout.preferredWidth: Math.min(380, root.width * 0.36)
                Layout.minimumWidth: root.narrowHeader ? 120 : 210
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
                id: helpButton
                visible: root.width >= 820 && !root.narrowHeader
                text: qsTranslate("MainWindow", "Help")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: root.backend.openHelp()
            }
            SecondaryButton {
                visible: !root.narrowHeader
                text: qsTranslate("MainWindow", "About...")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: aboutDialog.open()
            }
            SecondaryButton {
                id: moreButton
                // Also the only route to Help whenever its button doesn't fit.
                visible: root.narrowHeader || root.compactViewControls || !helpButton.visible
                text: "⋮"
                Accessible.name: qsTr("More options")
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
                onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)

                Menu {
                    id: moreMenu
                    MenuItem {
                        text: qsTr("Condensed view")
                        checkable: true
                        checked: root.condensedView
                        onToggled: {
                            root.condensedView = checked
                            checked = Qt.binding(() => root.condensedView)
                        }
                    }
                    MenuItem {
                        text: qsTr("Hide categories")
                        checkable: true
                        checked: root.hideCategories
                        onToggled: {
                            root.hideCategories = checked
                            checked = Qt.binding(() => root.hideCategories)
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTranslate("MainWindow", "Help")
                        onTriggered: root.backend.openHelp()
                    }
                    MenuItem {
                        text: qsTranslate("MainWindow", "About...")
                        onTriggered: aboutDialog.open()
                    }
                }
            }
        }
    }

    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.margins: root.narrowHeader || root.shortWindow ? 12 : 24
        spacing: 24

        Rectangle {
            id: sidebar
            visible: !root.compactNavigation && !root.hideCategories
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
                // Long category lists scroll here, keeping the launch-at-login control in view.
                ListView {
                    id: sidebarCategories
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    model: root.backend.categories

                    // See the comment on the compact category Flickable below: bypass the default
                    // flick-momentum wheel handling so touchpad scrolling can reverse immediately.
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (event) => {
                            const minimumY = sidebarCategories.originY
                            const maximumY = Math.max(minimumY, minimumY + sidebarCategories.contentHeight
                                                                - sidebarCategories.height)
                            sidebarCategories.contentY = Math.max(minimumY, Math.min(
                                maximumY, sidebarCategories.contentY - event.angleDelta.y))
                        }
                    }

                    delegate: CategoryButton {
                        required property string modelData
                        required property int index
                        width: ListView.view.width
                        // The button's fixed 44 px suits the compact strip; the layout this list
                        // replaced sized sidebar rows to the implicit height, so keep that.
                        height: implicitHeight
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
            spacing: root.shortWindow ? 8 : 15

            Flickable {
                id: compactCategoriesFlickable
                visible: root.compactNavigation && !root.hideCategories
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
                id: viewHeader
                visible: !root.shortWindow
                Layout.fillWidth: true
                spacing: 12
                // The view switches sit beside the heading when both fit; otherwise they move
                // into the header menu. Measure against the width the window leaves for this
                // column: the column's own width follows its content, so it would keep a
                // layout that doesn't fit.
                readonly property real availableWidth: contentRow.width
                                                       - (sidebar.visible ? sidebar.width + contentRow.spacing : 0)
                readonly property real controlsWidth: launcherCount.implicitWidth + condensedLabel.implicitWidth
                                                      + hideCategoriesLabel.implicitWidth
                                                      + condensedSwitch.implicitWidth
                                                      + hideCategoriesSwitch.implicitWidth
                                                      + 2 * 11 + 6 * viewControls.spacing
                readonly property bool sideBySide: availableWidth >= heading.implicitWidth + controlsWidth + spacing

                ColumnLayout {
                    id: heading
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: searchField.text.length > 0 ? qsTr("Search results")
                              : (root.backend.selectedCategory.length > 0
                                 ? root.backend.selectedCategory : qsTr("All launchers"))
                        color: root.primaryTextColor
                        font.pixelSize: root.baseFontSize + 12
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: searchField.text.length > 0
                              ? qsTr("Results matching “%1”").arg(searchField.text)
                              : qsTr("Choose a launcher to start an application or task")
                        color: root.secondaryTextColor
                        font.pixelSize: root.baseFontSize
                        elide: Text.ElideRight
                    }
                }

                RowLayout {
                    id: viewControls
                    visible: !root.compactViewControls
                    spacing: 5

                    Text {
                        id: launcherCount
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
                        id: condensedLabel
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
                        onToggled: {
                            root.condensedView = checked
                            checked = Qt.binding(() => root.condensedView)
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 24
                        Layout.leftMargin: 5
                        Layout.rightMargin: 5
                        color: root.borderColor
                    }

                    Text {
                        id: hideCategoriesLabel
                        Layout.maximumWidth: 130
                        text: qsTr("Hide categories")
                        color: root.secondaryTextColor
                        font.pixelSize: Math.max(10, root.baseFontSize - 1)
                        elide: Text.ElideRight
                    }
                    ModernSwitch {
                        id: hideCategoriesSwitch
                        checked: root.hideCategories
                        accentColor: root.accentColor
                        inactiveColor: root.inactiveControlColor
                        Accessible.name: qsTr("Hide the category list")
                        ThemeToolTip {
                            visible: hideCategoriesSwitch.hovered
                            text: qsTr("Use the whole window for launchers")
                        }
                        onToggled: {
                            root.hideCategories = checked
                            checked = Qt.binding(() => root.hideCategories)
                        }
                    }
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
                readonly property int launcherIconSize: root.backend.iconSize > 0
                                                       ? root.backend.iconSize : (root.condensedView ? 30 : 38)
                readonly property int columnCount: root.backend.fixedNumberColumns > 0
                                                   ? root.backend.fixedNumberColumns
                                                   : Math.max(1, Math.floor(width / Math.max(
                                                       root.condensedView ? 230 : 300, launcherIconSize + 180)))
                cellWidth: width / columnCount
                cellHeight: Math.max(root.condensedView ? 112 : 154,
                                     launcherIconSize + (root.condensedView ? 48 : 64))

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
                    iconSize: launcherGrid.launcherIconSize
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
                    visible: (root.compactNavigation || root.hideCategories) && root.backend.startupVisible
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    // Layouts snap to whole pixels; round up so the full label isn't elided.
                    Layout.preferredWidth: Math.ceil(implicitWidth)
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    text: qsTr("Launch at login")
                    color: root.secondaryTextColor
                    font.pixelSize: Math.max(10, root.baseFontSize - 1)
                    elide: Text.ElideRight
                }
                ModernSwitch {
                    visible: (root.compactNavigation || root.hideCategories) && root.backend.startupVisible
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
        anchors.centerIn: Overlay.overlay
        width: Math.min(440, root.width - 24)
        // Fit short windows; the content scrolls when it doesn't.
        height: Math.min(implicitHeight, root.height - 24)
        title: qsTr("About %1").arg(root.backend.title)
        // Set on the Dialog: it pushes its own (empty) standard buttons into the footer box,
        // so buttons set only on the box never appeared.
        standardButtons: Dialog.Close
        background: Rectangle { color: root.surfaceColor; radius: 6; border.color: root.borderColor }
        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
        }
        contentItem: Flickable {
            id: aboutContentArea
            implicitHeight: aboutContent.implicitHeight
            contentWidth: width
            contentHeight: aboutContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: aboutContent
                width: aboutContentArea.width
                spacing: 14
                Image {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    source: "../icons/custom-toolbox.svg"
                    fillMode: Image.PreserveAspectFit
                }
                Text {
                    Layout.fillWidth: true
                    text: root.backend.title
                    color: root.primaryTextColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
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
    }

    Dialog {
        id: errorDialog
        property string message: ""
        modal: true
        anchors.centerIn: Overlay.overlay
        // Size from the message rather than implicitWidth, which follows the footer and loops.
        width: Math.min(Math.max(errorText.implicitWidth, 240) + leftPadding + rightPadding, 560, root.width - 24)
        // Long messages (e.g. a failed command line) scroll above the fixed OK button.
        height: Math.min(implicitHeight, root.height - 24)
        standardButtons: Dialog.Ok
        background: Rectangle {
            color: root.surfaceColor
            radius: 6
            border.color: root.borderColor
        }
        footer: DialogButtonBox {
            alignment: Qt.AlignRight
            padding: 12
            background: Item {}
            delegate: SecondaryButton {
                textColor: root.primaryTextColor
                hoverColor: root.accentWash
                borderColor: root.borderColor
                accentColor: root.accentColor
            }
        }
        contentItem: Flickable {
            id: errorMessageArea
            implicitHeight: errorText.implicitHeight
            contentWidth: width
            contentHeight: errorText.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Text {
                id: errorText
                width: errorMessageArea.width
                text: errorDialog.message
                color: root.primaryTextColor
                wrapMode: Text.Wrap
            }
        }
    }
}
