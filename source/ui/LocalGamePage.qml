import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.12
import net.warsow 2.6

Item {
    Label {
        id: titleLabel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 32
        anchors.bottomMargin: 32
        horizontalAlignment: Qt.AlignHCenter
        font.weight: Font.Medium
        font.pointSize: 12
        font.letterSpacing: 1
        text: swipeView.currentItem.subpageTitle
    }

    Label {
        id: summaryLabel
        anchors.top: titleLabel.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        horizontalAlignment: Qt.AlignHCenter
        width: parent.width
        maximumLineCount: 1
        elide: Qt.ElideRight
        font.pointSize: 12
        font.letterSpacing: 1
        text: {
            if (swipeView.currentIndex === 2) {
                "You have selected map <b>" + selectedMapName +
                "</b> for gametype <b>" + selectedGametypeName + "</b>"
            } else if (swipeView.currentIndex === 1) {
                "You have selected the gametype <b>" + selectedGametypeName + "</b>"
            } else {
                ""
            }
        }
    }

    property var selectedGametypeTitle
    property var selectedGametypeName
    property var selectedGametypeIndex
    property var selectedGametypeDesc
    property var gametypeMapsList
    property var selectedMapName
    property var selectedMapTitle
    property var selectedMapMinPlayers
    property var selectedMapMaxPlayers
    property var selectedMapIndex
    property int selectedNumBots
    property bool selectedInsta
    property bool selectedPublic

    SwipeView {
        id: swipeView
        clip: true
        interactive: false
        width: 0.75 * parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: summaryLabel.bottom
        anchors.topMargin: 32
        anchors.bottom: buttonsBar.top
        anchors.bottomMargin: 32

        LocalGameDetailArrangement {
            id: gametypePage
            readonly property string subpageTitle: "Step <b>1/3</b> - Select a gametype"
            readonly property bool canGoPrev: false
            readonly property bool canGoNext: detailed

            desiredWidth: swipeView.width
            desiredHeight: swipeView.height

            listComponent: ListView {
                id: gametypesListView
                model: gametypesModel
                interactive: false
                delegate: LocalGameListDelegate {
                    detailed: gametypePage.detailed
                    width: gametypePage.expectedListItemWidth
                    text: title
                    selected: index === gametypePage.selectedIndex
                    onClicked: gametypePage.selectedIndex = index
                    // Handles external selectedIndex changes as well
                    onSelectedChanged: {
                        if (selected) {
                            selectProps()
                        }
                    }
                    function selectProps() {
                        gametypePage.detailed = true
                        selectedGametypeTitle = title
                        selectedGametypeName = name
                        selectedGametypeIndex = index
                        selectedGametypeDesc = desc
                        gametypeMapsList = maps
                    }
                }
            }

            detailComponent: ColumnLayout {
                spacing: 12
                Label {
                    Layout.preferredWidth: gametypePage.expectedDetailsWidth
                    horizontalAlignment: Qt.AlignHCenter
                    font.weight: Font.Medium
                    font.pointSize: 16
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 2
                    text: selectedGametypeTitle || ""
                }
                SimpleVideoDecoration {
                    Layout.preferredWidth: gametypePage.expectedDetailsWidth
                    Layout.preferredHeight: gametypePage.expectedDetailsWidth * (9 / 16.0)
                    filePath: "videos/gametypes/" + selectedGametypeName + ".mjpeg"
                }
                Label {
                    Layout.preferredWidth: gametypePage.expectedDetailsWidth - 16
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Qt.AlignJustify
                    maximumLineCount: 99
                    wrapMode: Text.WordWrap
                    elide: Qt.ElideRight
                    font.weight: Font.Normal
                    font.pointSize: 12
                    font.letterSpacing: 1
                    text: selectedGametypeDesc || ""
                }
            }
        }

        LocalGameDetailArrangement {
            id: mapPage
            readonly property string subpageTitle: "Step <b>2/3</b> - Select a map"
            readonly property bool canGoPrev: true
            readonly property bool canGoNext: detailed

            desiredWidth: swipeView.width
            desiredHeight: swipeView.height

            listComponent: ListView {
                interactive: false
                model: gametypeMapsList
                delegate: LocalGameListDelegate {
                    detailed: mapPage.detailed
                    width: mapPage.expectedListItemWidth
                    text: modelData["title"]
                    selected: index === mapPage.selectedIndex
                    onClicked: mapPage.selectedIndex = index
                    // Handles external selectedIndex changes as well
                    onSelectedChanged: {
                        if (selected) {
                            selectProps()
                        }
                    }
                    function selectProps() {
                        mapPage.detailed = true
                        selectedMapName = modelData["name"]
                        selectedMapTitle = modelData["title"]
                        selectedMapIndex = index
                        selectedMapMinPlayers = modelData["minPlayers"]
                        selectedMapMaxPlayers = modelData["maxPlayers"]
                    }
                }
            }

            detailComponent: ColumnLayout {
                spacing: 8
                Label {
                    Layout.preferredWidth: gametypePage.expectedDetailsWidth
                    horizontalAlignment: Qt.AlignHCenter
                    font.weight: Font.Medium
                    font.pointSize: 16
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 2
                    text: selectedMapTitle || ""
                }
                Label {
                    Layout.preferredWidth: gametypePage.expectedDetailsWidth
                    horizontalAlignment: Qt.AlignHCenter
                    font.weight: Font.Medium
                    font.pointSize: 12
                    font.letterSpacing: 1
                    text: selectedMapName || ""
                }
                SimpleVideoDecoration {
                    Layout.preferredWidth: gametypePage.expectedDetailsWidth
                    Layout.preferredHeight: gametypePage.expectedDetailsWidth * (9 / 16.0)
                    filePath: "videos/maps/" + selectedMapName + ".mjpeg"
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!(selectedMapMinPlayers || selectedMapMaxPlayers)
                    horizontalAlignment: Qt.AlignHCenter
                    font.letterSpacing: 1
                    font.weight: Font.Medium
                    font.pointSize: 12
                    text: {
                        if (selectedMapMinPlayers != selectedMapMaxPlayers) {
                            "Optimal for <b>" + selectedMapMinPlayers + "-" + selectedMapMaxPlayers + "</b> players"
                        } else {
                            "Optimal for <b>" + selectedMapMaxPlayers + "</b> players"
                        }
                    }
                }
            }
        }

        Item {
            id: rulesPage
            readonly property string subpageTitle: "Step <b>3/3</b> - Set rules"
            readonly property bool canGoPrev: true
            readonly property bool canGoNext: true

            property bool isNumBotsDefined
            property bool isNumBotsFixed
            property int numBots

            SwipeView.onIsCurrentItemChanged: {
                if (SwipeView.isCurrentItem) {
                    const botConfig  = gametypesModel.getBotConfig(selectedGametypeIndex, selectedMapIndex)
                    isNumBotsDefined = !!(botConfig["defined"])
                    if (isNumBotsDefined) {
                        isNumBotsFixed       = !!(botConfig["fixed"])
                        numBots              = botConfig["number"] || 0
                        numBotsSpinBox.value = numBots
                    }
                }
            }

            GridLayout {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                columns: 2
                columnSpacing: 32

                Label {
                    font.weight: Font.Medium
                    font.pointSize: 12
                    font.letterSpacing: 0.75
                    text: "Instagib"
                }
                CheckBox {
                    id: instaCheckBox
                    Material.theme: checked ? Material.Light : Material.Dark
                    onCheckedChanged: selectedInsta = checked
                }

                Label {
                    font.weight: Font.Medium
                    font.pointSize: 12
                    font.letterSpacing: 0.75
                    text: "Public"
                }
                CheckBox {
                    id: publicCheckBox
                    Material.theme: checked ? Material.Light : Material.Dark
                    onCheckedChanged: selectedPublic = checked
                }

                Label {
                    visible: rulesPage.isNumBotsDefined
                    font.weight: Font.Medium
                    font.pointSize: 12
                    font.letterSpacing: 0.75
                    text: "Number of bots"
                }
                SpinBox {
                    id: numBotsSpinBox
                    visible: rulesPage.isNumBotsDefined
                    enabled: !rulesPage.isNumBotsFixed
                    Layout.preferredWidth: 112
                    Material.theme: activeFocus ? Material.Light : Material.Dark
                    from: 0; to: 9
                    onValueChanged: {
                        if (selectedNumBots != value) {
                            selectedNumBots = value
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.centerIn: pageIndicator
        width: pageIndicator.width + 48
        height: pageIndicator.height + 20
        radius: 5
        color: Qt.rgba(0, 0, 0, 0.1)
    }

    PageIndicator {
        id: pageIndicator
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: buttonsBar.anchors.bottomMargin + 16
        count: 3
        currentIndex: swipeView.currentIndex
        interactive: false
    }

    RowLayout {
        id: buttonsBar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32
        width: 0.67 * parent.width

        Button {
            id: prevButton
            flat: true
            text: "back"
            Layout.preferredWidth: 120
            visible: swipeView.currentItem.canGoPrev
            onClicked: swipeView.currentIndex = swipeView.currentIndex - 1
        }

        Item {
            Layout.preferredWidth: prevButton.Layout.preferredWidth
            visible: !prevButton.visible
        }

        Item { Layout.fillWidth: true }

        Button {
            id: nextButton
            highlighted: true
            text: swipeView.currentIndex === 2 ? "start" : "next"
            Layout.preferredWidth: 120
            visible: swipeView.currentItem.canGoNext
            onClicked: {
                if (swipeView.currentIndex !== 2) {
                    swipeView.currentIndex = swipeView.currentIndex + 1
                } else {
                    let flags = 0
                    if (selectedInsta) {
                        flags |= Wsw.LocalServerInsta
                    }
                    if (selectedPublic) {
                        flags |= Wsw.LocalServerPublic
                    }
                    wsw.launchLocalServer(selectedGametypeName, selectedMapName, flags, selectedNumBots);
                }
            }
        }

        Item {
            Layout.preferredWidth: prevButton.Layout.preferredWidth
            visible: !nextButton.visible
        }
    }

    function handleBackEvent(event) {
        const index = swipeView.currentIndex
        if (index === 0) {
            if (gametypePage.detailed) {
                gametypePage.detailed = false
                gametypePage.selectedIndex = -1
                event.accepted = true
                return true
            }
            return false
        }
        if (index === 1) {
            if (mapPage.detailed) {
                mapPage.detailed = false
                mapPage.selectedIndex = -1
            } else {
                swipeView.currentIndex = 0
            }
            event.accepted = true
            return true
        }
        swipeView.currentIndex = 1
        event.accepted = true
        return true
    }

    function handleCycleList(event, step) {
        const index = swipeView.currentIndex
        if (index === 0 && gametypePage.detailed) {
            gametypePage.selectPrevOrNext(step)
        }
        if (index === 1 && mapPage.detailed) {
            mapPage.selectPrevOrNext(step)
        }
        // Always consider it handled
        event.accepted = true
        return true
    }

    function handleKeyEvent(event) {
        const key = event.key
        if (key === Qt.Key_Escape || key == Qt.Key_Back) {
            return handleBackEvent(event)
        }
        if (key === Qt.Key_Left && swipeView.currentItem.canGoPrev) {
            return handleBackEvent(event)
        }
        if (key === Qt.Key_Right && swipeView.currentItem.canGoNext) {
            swipeView.currentIndex = swipeView.currentIndex + 1
            event.accepted = true
            return
        }
        if (key === Qt.Key_Up) {
            return handleCycleList(event, -1)
        }
        if (key === Qt.Key_Down) {
            return handleCycleList(event, +1)
        }
        // TODO: Accept ENTER key on the last page
        // TODO: Accept ENTER keys to continue
        // TODO: Accept TAB keys
        return false
    }
}