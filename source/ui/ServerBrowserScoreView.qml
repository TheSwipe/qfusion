import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

Item {
    visible: height > 0
    width: root.width
    anchors.top: matchTimeView.bottom
    implicitHeight: (!!alphaTeamList || !!betaTeamList) ? 36 : 0

    readonly property real scoreMargin: 25

    property var alphaTeamList
    property var betaTeamList
    property var alphaTeamScore
    property var betaTeamScore

    Label {
        visible: !!alphaTeamList
        anchors.left: parent.left
        anchors.right: alphaScoreLabel.left
        anchors.leftMargin: 24
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Qt.AlignLeft
        textFormat: Text.StyledText
        text: alphaTeamName || ""
        maximumLineCount: 1
        elide: Text.ElideRight
        font.family: wsw.numbersFontFamily
        font.letterSpacing: 4
        font.weight: Font.Black
        font.pointSize: 16
    }

    Label {
        id: alphaScoreLabel
        width: implicitWidth
        anchors.right: parent.horizontalCenter
        anchors.rightMargin: scoreMargin
        anchors.verticalCenter: parent.verticalCenter
        font.family: wsw.headingFontFamily
        font.weight: Font.Black
        font.pointSize: 24
        text: typeof(alphaTeamList) !== "undefined" && typeof(alphaTeamScore) !== "undefined" ? alphaTeamScore : "-"
        transform: Scale { xScale: 1.0; yScale: 0.9 }
    }

    Label {
        id: betaScoreLabel
        width: implicitWidth
        anchors.left: parent.horizontalCenter
        anchors.leftMargin: scoreMargin - 8 // WTF?
        anchors.verticalCenter: parent.verticalCenter
        font.family: wsw.numbersFontFamily
        font.weight: Font.Black
        font.pointSize: 24
        text: typeof(betaTeamList) !== "undefined" && typeof(betaTeamScore) !== "undefined" ? betaTeamScore : "-"
        transform: Scale { xScale: 1.0; yScale: 0.9 }
    }

    Label {
        visible: !!betaTeamList
        anchors.left: betaScoreLabel.left
        anchors.right: parent.right
        anchors.leftMargin: 12 + 20 // WTF?
        anchors.rightMargin: 24 + 8 // WTF?
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Qt.AlignRight
        textFormat: Text.StyledText
        text: betaTeamName || ""
        maximumLineCount: 1
        elide: Text.ElideLeft
        font.family: wsw.headingFontFamily
        font.letterSpacing: 4
        font.weight: Font.Black
        font.pointSize: 16
    }
}