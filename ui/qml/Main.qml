import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DubbingTool // Импорт нашего C++ модуля (AppController)

ApplicationWindow {
    id: window

    height: 750
    title: qsTr("DubbingTool 2.0")
    visible: true
    width: 1200

    // Fluent dark base (see .cursor/rules/qml-fluent-style.mdc)
    background: Rectangle {
        color: "#1a1a1a"
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- БОКОВОЕ МЕНЮ (Сайдбар) ---
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 220
            color: "#161616"

            Rectangle {
                anchors.right: parent.right
                color: "#2d2d2d"
                height: parent.height
                width: 1
            }
            ListView {
                id: navMenu

                anchors.fill: parent
                anchors.margins: 16

                delegate: ItemDelegate {
                    height: 44
                    highlighted: ListView.isCurrentItem
                    width: parent.width - 16

                    background: Rectangle {
                        color: parent.highlighted ? "#2d2d2d" : (parent.hovered ? "#252525" : "transparent")
                        radius: 4
                    }

                    contentItem: Text {
                        color: "#ffffff"
                        font.pixelSize: 14
                        leftPadding: 8
                        text: model.icon + "   " + model.name
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        navMenu.currentIndex = index;
                    }
                }
                model: ListModel {
                    ListElement {
                        icon: "⚡"
                        name: "Автоматический режим"
                        page: "pages/AutoModePage.qml"
                    }
                    ListElement {
                        icon: "📦"
                        name: "Ручная разборка"
                        page: "pages/ManualExtractionPage.qml"
                    }
                    ListElement {
                        icon: "�"
                        name: "Ручная сборка"
                        page: "pages/PlaceholderPage.qml"
                    }
                    ListElement {
                        icon: "🎬"
                        name: "Ручной рендер"
                        page: "pages/PlaceholderPage.qml"
                    }
                    ListElement {
                        icon: "📢"
                        name: "Публикация"
                        page: "pages/PlaceholderPage.qml"
                    }
                    ListElement {
                        icon: "⚙️"
                        name: "Настройки"
                        page: "pages/PlaceholderPage.qml"
                    }
                }
            }
        }
        // --- ОБЛАСТЬ КОНТЕНТА (карточка с общим стилем) ---
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.margins: 20
            color: "#1e1e1e"
            radius: 8
            border.color: "#2d2d2d"
            border.width: 1

            StackLayout {
                anchors.fill: parent
                anchors.margins: 20
                currentIndex: navMenu.currentIndex

                AutoModePage {
                }

                // Индекс 1: Ручная разборка
                ManualExtractionPage {
                }

                // Индексы 2-5: пока заглушки
            PlaceholderPage {
            } // Сборка
            PlaceholderPage {
            } // Рендер
            PlaceholderPage {
            } // Публикация
            PlaceholderPage {
            } // Настройки
            }
        }
    }
}
