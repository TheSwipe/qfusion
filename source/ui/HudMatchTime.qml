import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import net.warsow 2.6

Item {
    implicitWidth: separator.width + 2 * (sideMargin + Math.max(minutesLabel.width, secondsLabel.width)) + 16
    implicitHeight: separator.height + matchStateLabel.anchors.topMargin + matchStateLabel.height

    width: implicitWidth
    height: implicitHeight

    readonly property Scale scaleTransform: Scale {
        xScale: 1.00
        yScale: 0.85
    }

    readonly property real sideMargin: 8
    readonly property string matchState: hudDataModel.matchState

    Label {
        id: minutesLabel
        anchors.right: separator.left
        anchors.rightMargin: sideMargin
        anchors.baseline: separator.baseline
        transform: scaleTransform
        font.family: wsw.numbersFontFamily
        font.weight: Font.Black
        font.pointSize: 40
        font.letterSpacing: 3
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchTimeMinutes
    }

    Label {
        id: separator
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        // TextMetrics didn't turn to be really useful
        anchors.topMargin: -12
        transform: scaleTransform
        font.family: wsw.numbersFontFamily
        font.weight: Font.Black
        font.pointSize: 48
        style: Text.Raised
        textFormat: Text.PlainText
        text: ":"
    }

    Label {
        id: secondsLabel
        anchors.left: separator.right
        anchors.leftMargin: sideMargin
        anchors.baseline: separator.baseline
        transform: scaleTransform
        font.family: wsw.numbersFontFamily
        font.weight: Font.Black
        font.pointSize: 40
        font.letterSpacing: 1.75
        style: Text.Raised
        textFormat: Text.PlainText
        text: hudDataModel.matchTimeSeconds
    }

    Label {
        id: matchStateLabel
        visible: matchState.length > 0
        height: visible ? implicitHeight : 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: separator.bottom
        anchors.topMargin: -20
        font.family: wsw.headingFontFamily
        font.weight: Font.Black
        font.pointSize: 13
        font.letterSpacing: 1.75
        style: Text.Raised
        textFormat: Text.PlainText
        text: matchState
    }
}