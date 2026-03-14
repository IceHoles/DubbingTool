import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import DubbingTool

Dialog {
    id: missingDialog
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 800
    modal: true
    title: "Требуется ввод данных"
    standardButtons: Dialog.Ok | Dialog.Cancel

    background: Rectangle {
        border.color: "#2d2d2d"
        color: "#252525"
        radius: 8
    }

    // Слушаем сигнал из C++ для показа окна и подготовки данных
    Connections {
        target: AppController.missingFiles
        function onOpenDialogRequested() {
            // Подготавливаем список шрифтов
            fontModel.clear()
            var fonts = AppController.missingFiles.missingFonts
            for (var i = 0; i < fonts.length; ++i) {
                fontModel.append({ "fontName": fonts[i], "resolvedPath": "" })
            }
            missingDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // --- БЛОК 1: АУДИО ---
        GroupBox {
            title: "Аудиодорожка"
            Layout.fillWidth: true
            visible: AppController.missingFiles.audioRequired

            background: Rectangle { color: "transparent"; border.color: "#2d2d2d"; radius: 8 }

            ColumnLayout {
                anchors.fill: parent
                Label { 
                    text: AppController.missingFiles.audioPrompt 
                    color: "#ffffff"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        text: AppController.missingFiles.audioPath
                        readOnly: true
                        Layout.fillWidth: true
                        color: "#aaaaaa"
                    }
                    Button {
                        text: "Обзор..."
                        onClicked: audioDialog.open()
                    }
                }
            }
        }

        // --- БЛОК 2: ШРИФТЫ ---
        GroupBox {
            title: "Недостающие шрифты"
            Layout.fillWidth: true
            visible: AppController.missingFiles.fontsRequired

            background: Rectangle { color: "transparent"; border.color: "#2d2d2d"; radius: 8 }

            ColumnLayout {
                anchors.fill: parent
                Label { 
                    text: "Кликните по шрифту в списке, чтобы указать к нему путь вручную:" 
                    color: "#ffffff"
                }
                
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    color: "#1e1e1e"
                    border.color: "#2d2d2d"
                    radius: 4

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 5
                        clip: true
                        model: ListModel { id: fontModel }
                        
                        delegate: ItemDelegate {
                            width: ListView.view.width
                            text: model.resolvedPath === "" ? model.fontName : (model.fontName + " -> " + model.resolvedPath)
                            
                            contentItem: Text {
                                text: parent.text
                                // Красный - если не указан, зеленый - если путь выбран
                                color: model.resolvedPath === "" ? "#ff6666" : "#66ff66" 
                                font.bold: true
                            }
                            
                            onClicked: {
                                fontDialog.currentIndex = index
                                fontDialog.currentName = model.fontName
                                fontDialog.open()
                            }
                        }
                    }
                }
            }
        }

        // --- БЛОК 3: ВИДЕО И ВРЕМЯ (ТБ) ---
        GroupBox {
            title: "Время эндинга (Начала ТБ)"
            Layout.fillWidth: true
            visible: AppController.missingFiles.timeRequired
            
            background: Rectangle { color: "transparent"; border.color: "#2d2d2d"; radius: 8 }

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Label { 
                    text: AppController.missingFiles.timePrompt 
                    color: "#ffffff"
                }

                // Видеоплеер
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 360
                    color: "#000000"
                    
                    VideoOutput {
                        id: videoOut
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectFit
                        
                        // При создании передаем ссылку на экран (Sink) в C++
                        Component.onCompleted: {
                            AppController.missingFiles.setVideoSink(videoOut.videoSink)
                        }
                    }
                }

                // Ползунок времени
                Slider {
                    id: timeSlider
                    Layout.fillWidth: true
                    from: 0
                    to: AppController.missingFiles.sliderMaximum
                    value: AppController.missingFiles.currentSliderValue
                    
                    // Когда юзер тянет ползунок, передаем значение в C++
                    onMoved: AppController.missingFiles.setCurrentSliderValue(value)
                }

                // Кнопки управления плеером
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    
                    Button { 
                        text: "◀|" 
                        font.bold: true
                        onClicked: AppController.missingFiles.prevFrame() 
                    }
                    Button { 
                        text: AppController.missingFiles.isPlaying ? "⏸" : "▶"
                        font.bold: true
                        onClicked: AppController.missingFiles.togglePlayPause() 
                    }
                    Button { 
                        text: "|▶" 
                        font.bold: true
                        onClicked: AppController.missingFiles.nextFrame() 
                    }
                    
                    TextField {
                        text: AppController.missingFiles.currentTimeStr
                        readOnly: true
                        Layout.preferredWidth: 100
                        horizontalAlignment: Text.AlignHCenter
                    }
                    
                    Item { Layout.fillWidth: true } // Spacer
                    
                    Button { 
                        text: "Открыть в плеере" 
                        onClicked: AppController.missingFiles.openVideoInExternalPlayer()
                    }
                }
            }
        }
    }

    // Обработка кнопок ОК / Отмена
    onAccepted: AppController.missingFiles.acceptDialog()
    onRejected: AppController.missingFiles.rejectDialog()

    // --- ДИАЛОГИ ВЫБОРА ФАЙЛОВ ---
    function urlToPath(urlString) {
        var path = urlString.toString();
        path = path.replace(/^(file:\/{2})|(qrc:\/{2})|(http:\/{2})/, "");
        if (Qt.platform.os === "windows") path = path.replace(/^\//, "");
        return decodeURIComponent(path);
    }

    FileDialog {
        id: audioDialog
        title: "Выберите аудиофайл"
        nameFilters: ["Аудиофайлы (*.wav *.flac *.aac *.eac3)", "Все файлы (*)"]
        onAccepted: AppController.missingFiles.audioPath = urlToPath(selectedFile)
    }

    FileDialog {
        id: fontDialog
        title: "Выберите файл шрифта"
        nameFilters: ["Файлы шрифтов (*.ttf *.otf *.ttc)", "Все файлы (*)"]
        property int currentIndex: -1
        property string currentName: ""
        
        onAccepted: {
            var path = urlToPath(selectedFile)
            fontModel.setProperty(currentIndex, "resolvedPath", path)
            AppController.missingFiles.resolveFont(currentName, path)
        }
    }
}