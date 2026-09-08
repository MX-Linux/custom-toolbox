import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    SystemPalette { id: systemPalette }

    required property string launcherName
    required property string description
    required property string categoryName
    required property string iconSource
    property bool condensed: false
    property int iconSize: condensed ? 30 : 38
    property color surfaceColor: systemPalette.base
    property color hoverSurfaceColor: Qt.tint(systemPalette.base, Qt.alpha(systemPalette.highlight, 0.08))
    property color primaryTextColor: systemPalette.text
    property color secondaryTextColor: Qt.alpha(systemPalette.text, 0.68)
    property color accentColor: systemPalette.highlight
    property color borderColor: Qt.alpha(systemPalette.text, 0.18)

    hoverEnabled: true
    padding: condensed ? 12 : 18
    Accessible.name: launcherName
    Accessible.description: description

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    scale: down ? 0.985 : 1
    Behavior on scale { NumberAnimation { duration: 90 } }

    background: Rectangle {
        color: control.hovered ? control.hoverSurfaceColor : control.surfaceColor
        radius: 6
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.hovered ? control.accentColor : control.borderColor
        Behavior on color { ColorAnimation { duration: 130 } }
        Behavior on border.color { ColorAnimation { duration: 130 } }
    }

    // The child IDs intentionally make this deferred control property immediate.
    // qmllint disable deferred-property-id
    contentItem: RowLayout {
        // qmllint enable deferred-property-id
        spacing: control.condensed ? 10 : 15
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: control.iconSize + (control.condensed ? 12 : 16)
            Layout.preferredHeight: control.iconSize + (control.condensed ? 12 : 16)
            radius: 6
            color: Qt.alpha(control.accentColor, control.hovered ? 0.16 : 0.10)
            Image {
                anchors.centerIn: parent
                width: control.iconSize
                height: control.iconSize
                source: control.iconSource
                sourceSize.width: control.iconSize
                sourceSize.height: control.iconSize
                fillMode: Image.PreserveAspectFit
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: control.condensed ? 2 : 5
            Text {
                Layout.fillWidth: true
                text: control.launcherName
                color: control.primaryTextColor
                font.pixelSize: control.font.pixelSize + (control.condensed ? 1 : 3)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                id: descriptionText
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: control.description.length > 0 ? control.description : qsTr("Open this launcher")
                color: control.secondaryTextColor
                font.pixelSize: control.font.pixelSize
                lineHeight: 1.15
                wrapMode: Text.Wrap
                maximumLineCount: control.condensed ? 2 : 3
                elide: Text.ElideRight
            }
            Text {
                text: control.categoryName.toUpperCase()
                color: control.accentColor
                font.pixelSize: Math.max(9, control.font.pixelSize - 3)
                font.weight: Font.DemiBold
                font.letterSpacing: 0.7
            }
        }
    }
    ThemeToolTip {
        visible: control.hovered && descriptionText.truncated
        text: descriptionText.text
        delay: 600
    }
}
