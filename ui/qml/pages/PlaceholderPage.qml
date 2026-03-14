import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 20

        Label {
            Layout.alignment: Qt.AlignHCenter
            font.bold: true
            font.pixelSize: 24
            text: "🚧 Страница в разработке"
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            color: "#666666"
            font.pixelSize: 16
            text: "Скоро здесь появится новый интерфейс"
        }
    }
}
