import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import DubbingTool

Item {
    id: root

    // --- НЕВИДИМЫЕ ДИАЛОГИ ВЫБОРА ФАЙЛОВ ---
    // QML FileDialog возвращает пути в формате URL (file:///C:/path...),
    // поэтому мы используем функцию urlToPath для обрезки префикса "file:///"

    function urlToPath(urlString) {
        var path = urlString.toString();
        path = path.replace(/^(file:\/{2})|(qrc:\/{2})|(http:\/{2})/, "");
        if (Qt.platform.os === "windows") {
            path = path.replace(/^\//, ""); // Убираем ведущий слеш в Windows (например, /C:/ -> C:/)
        }
        return decodeURIComponent(path);
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // --- ВЕРХНЯЯ ЧАСТЬ: ФОРМА ВВОДА ---
        GridLayout {
            Layout.fillWidth: true
            columnSpacing: 12
            columns: 3
            rowSpacing: 12

            // 1. Шаблон
            Label {
                color: "#a0a0a0"
                text: "Шаблон:"
            }
            ComboBox {
                id: templateCombo

                Layout.fillWidth: true
                model: AppController.templateList
            }
            RowLayout {
                spacing: 5

                Button {
                    text: "Создать"

                    onClicked: {
                        var defaultJson = AppController.getDefaultTemplateJson();
                        templateEditorDialog.isEditMode = false;
                        templateEditorDialog.loadTemplate(defaultJson);
                        templateEditorDialog.open();
                    }
                }
                Button {
                    text: "Редактировать"

                    onClicked: {
                        var currentName = templateCombo.currentText;
                        if (currentName !== "") {
                            var tJson = AppController.getTemplateJson(currentName);
                            templateEditorDialog.isEditMode = true;
                            templateEditorDialog.loadTemplate(tJson);
                            templateEditorDialog.open();
                        }
                    }
                }
                Button {
                    text: "Удалить"

                    onClicked: deleteConfirmDialog.open()
                }
            }

            // 2. Исходный MKV
            Label {
                color: "#a0a0a0"
                text: "Исходный MKV:"
            }
            TextField {
                id: mkvField

                Layout.fillWidth: true
                placeholderText: "Выберите .mkv или .mp4 файл"
            }
            Button {
                text: "Обзор..."

                onClicked: mkvDialog.open()
            }

            // 3. Русское аудио
            Label {
                color: "#a0a0a0"
                text: "Русское аудио:"
            }
            TextField {
                id: audioField

                Layout.fillWidth: true
                placeholderText: "Выберите .wav, .flac, .aac или .eac3 файл"
            }
            Button {
                text: "Обзор..."

                onClicked: audioDialog.open()
            }

            // 4. Номер серии
            Label {
                color: "#a0a0a0"
                text: "Номер серии:"
            }
            TextField {
                id: episodeField

                Layout.fillWidth: true
                placeholderText: "Можно оставить пустым при работе с .mkv"
            }
            ColumnLayout {
                Layout.columnSpan: 1
                spacing: 2

                CheckBox {
                    id: normalizeCheckBox

                    enabled: AppController.isNugenAmbAvailable()
                    text: "Нормализовать аудио"
                    ToolTip.text: "Укажите путь к NUGEN Audio AMB в Настройках"
                    ToolTip.visible: !enabled && hovered
                }
                Label {
                    Layout.leftMargin: normalizeCheckBox.indicator.width + 8
                    color: "#888888"
                    font.pixelSize: 11
                    text: "NUGEN Audio AMB не найден — укажите путь в Настройках"
                    visible: !AppController.isNugenAmbAvailable()
                    wrapMode: Text.WordWrap
                }
            }

            // 5. Свои субтитры
            Label {
                color: "#a0a0a0"
                text: "Свои субтитры:"
            }
            TextField {
                id: subsField

                Layout.fillWidth: true
                placeholderText: "Субтитры из MKV будут проигнорированы"
            }
            Button {
                text: "Обзор..."

                onClicked: subsDialog.open()
            }

            // 6. Свои надписи
            Label {
                color: "#a0a0a0"
                text: "Свои надписи:"
            }
            TextField {
                id: signsField

                Layout.fillWidth: true
                placeholderText: "Надписи из MKV будут проигнорированы"
            }
            Button {
                text: "Обзор..."

                onClicked: signsDialog.open()
            }
        }
        CheckBox {
            id: decoupleCheckBox

            text: "Использовать «Свои субтитры» только для SRT-мастера"
            visible: AppController.canDecoupleSubs(templateCombo.currentText, subsField.text)
        }

        // --- СРЕДНЯЯ ЧАСТЬ: ЛОГИ ---
        Label {
            Layout.fillWidth: true
            color: "#a0a0a0"
            font.pixelSize: 12
            text: "Журнал"
        }
        ScrollView {
            Layout.fillHeight: true
            Layout.fillWidth: true

            background: Rectangle {
                border.color: "#2d2d2d"
                color: "#1c1c1c"
                radius: 8
            }

            Component.onCompleted: {
                AppController.loadTemplates();
            }

            TextArea {
                id: logArea

                font.family: "Consolas"
                font.pixelSize: 13
                padding: 12
                readOnly: true
                text: "--- DubbingTool 2.0 ---"
                wrapMode: TextEdit.Wrap

                // --- МАГИЯ АВТОСКРОЛЛА ---
                onTextChanged: {
                    logArea.cursorPosition = logArea.text.length;
                }

                // --- КОНТЕКСТНОЕ МЕНЮ (ПКМ) ---
                MouseArea {
                    acceptedButtons: Qt.RightButton
                    anchors.fill: parent

                    onClicked: logMenu.popup()
                }
            }
            Connections {
                function onLogMessage(msg) {
                    logArea.text += "\n" + msg;
                }
                function onPauseForSubEditRequested(subFilePath) {
                    subEditDialog.subPath = subFilePath;
                    subEditDialog.open();
                }
                function onSignStylesRequested(styles, actors) {
                    styleModel.clear();
                    actorModel.clear();
                    for (var i = 0; i < styles.length; i++)
                        styleModel.append({
                            "name": styles[i],
                            "isChecked": false
                        });
                    for (var j = 0; j < actors.length; j++)
                        actorModel.append({
                            "name": actors[j],
                            "isChecked": false
                        });
                    styleDialog.open();
                }

                target: AppController
            }
        }

        // --- НИЖНЯЯ ЧАСТЬ: ПРОГРЕСС ПО ЭТАПАМ И СТАРТ ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            // Полоса этапов: кружки + заполнение между ними; подпись этапа — одна строка внизу
            Item {
                id: stageProgressStrip
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                visible: AppController.isBusy

                property int stageCount: AppController.autoWorkflowStageNames().length
                property int currentIdx: AppController.currentPipelineStageIndex()
                property real progress: AppController.currentProgress / 100.0

                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    spacing: 0

                    Repeater {
                        model: stageProgressStrip.stageCount

                        RowLayout {
                            spacing: 0
                            Layout.fillWidth: index < stageProgressStrip.stageCount - 1

                            // Кружок этапа (с подсказкой — название этапа при наведении)
                            Rectangle {
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                Layout.alignment: Qt.AlignVCenter
                                radius: 12
                                border.width: 2
                                border.color: {
                                    var idx = stageProgressStrip.currentIdx
                                    if (idx > index) return "#4caf50"
                                    if (idx === index) return "#8bc34a"
                                    return "#404040"
                                }
                                color: {
                                    var idx = stageProgressStrip.currentIdx
                                    if (idx > index) return "#4caf50"
                                    if (idx === index) return "#6b9b37"
                                    return "transparent"
                                }

                                property bool hovered: false
                                ToolTip.visible: hovered
                                ToolTip.text: {
                                    var names = AppController.autoWorkflowStageNames()
                                    return index < names.length ? names[index] : ""
                                }

                                Label {
                                    anchors.centerIn: parent
                                    color: stageProgressStrip.currentIdx >= index ? "#1a1a1a" : "#666666"
                                    font.bold: stageProgressStrip.currentIdx >= index
                                    font.pixelSize: 11
                                    text: index + 1
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onContainsMouseChanged: parent.hovered = containsMouse
                                }
                            }

                            // Сегмент полоски между кружками (кроме последнего)
                            Item {
                                visible: index < stageProgressStrip.stageCount - 1
                                Layout.fillWidth: true
                                Layout.preferredHeight: 6
                                Layout.leftMargin: 4
                                Layout.rightMargin: 4

                                Rectangle {
                                    anchors.fill: parent
                                    color: "#404040"
                                    radius: 3
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    width: {
                                        var idx = stageProgressStrip.currentIdx
                                        var prog = stageProgressStrip.progress
                                        var segW = parent.width
                                        if (idx > index) return segW
                                        if (idx === index) return segW * prog
                                        return 0
                                    }
                                    color: "#4caf50"
                                    radius: 3
                                }
                            }
                        }
                    }
                }
            }
            Label {
                font.bold: true
                text: "Текущий этап: " + AppController.currentStage
                visible: AppController.isBusy
            }
            Button {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                font.bold: true
                font.pixelSize: 15
                text: AppController.isBusy ? "ОТМЕНА" : "СТАРТ"

                onClicked: {
                    if (AppController.isBusy) {
                        AppController.cancelWorkflow();
                    } else {
                        AppController.startAutoWorkflow(templateCombo.currentText, episodeField.text, mkvField.text, audioField.text, subsField.text, signsField.text, normalizeCheckBox.checked, decoupleCheckBox.checked);
                    }
                }
            }
        }
    }
    FileDialog {
        id: mkvDialog

        nameFilters: ["Видеофайлы (*.mkv *.mp4)", "Все файлы (*)"]
        title: "Выберите видеофайл"

        onAccepted: {
            var path = urlToPath(selectedFile);
            mkvField.text = path;
            if (episodeField.text === "") {
                episodeField.text = AppController.extractEpisodeNumber(path);
            }
        }
    }
    FileDialog {
        id: audioDialog

        nameFilters: ["Аудиофайлы (*.wav *.flac *.aac *.eac3)", "Все файлы (*)"]
        title: "Выберите аудиофайл"

        onAccepted: audioField.text = urlToPath(selectedFile)
    }
    FileDialog {
        id: subsDialog

        nameFilters: ["ASS Subtitles (*.ass)", "Все файлы (*)"]
        title: "Выберите ASS файл субтитров"

        onAccepted: subsField.text = urlToPath(selectedFile)
    }
    FileDialog {
        id: signsDialog

        nameFilters: ["ASS Subtitles (*.ass)", "Все файлы (*)"]
        title: "Выберите ASS файл надписей"

        onAccepted: signsField.text = urlToPath(selectedFile)
    }

    // --- ВСПЛЫВАЮЩЕЕ ОКНО ДЛЯ РЕДАКТИРОВАНИЯ СУБТИТРОВ ---
    Dialog {
        id: subEditDialog

        property string subPath: ""

        modal: true
        standardButtons: Dialog.Ok
        title: "Ручное редактирование"
        width: 500
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        background: Rectangle {
            border.color: "#3f3f3f"
            color: "#2b2b2b"
            radius: 8
        }

        onAccepted: {
            AppController.resumeAfterSubEdit();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 15

            Label {
                Layout.fillWidth: true
                color: "#ffffff"
                text: "Вы можете отредактировать файл субтитров:\n" + subEditDialog.subPath
                wrapMode: Text.WrapAnywhere
            }

            // --- КЛИКАБЕЛЬНАЯ ССЫЛКА ---
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 10
                Layout.topMargin: 10
                font.bold: true
                font.pixelSize: 15
                linkColor: "#4da6ff" // Приятный синий цвет для темной темы
                text: "<a href=\"file:///" + subEditDialog.subPath + "\">Открыть в редакторе (Aegisub)</a>"
                textFormat: Text.RichText

                // Обработчик клика по ссылке
                onLinkActivated: function (link) {
                    Qt.openUrlExternally(link);
                }

                // Меняем курсор на "руку" при наведении
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
            Label {
                color: "#ffffff"
                font.bold: true
                text: "После сохранения файла нажмите 'ОК' для продолжения сборки."
            }
        }
    }

    // --- ДИАЛОГ ВЫБОРА СТИЛЕЙ НАДПИСЕЙ (ДВЕ КОЛОНКИ) ---
    Dialog {
        id: styleDialog

        height: 550
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: "Выберите стили надписей"
        width: 650 // Сделали шире для двух колонок
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)

        background: Rectangle {
            border.color: "#3f3f3f"
            color: "#2b2b2b"
            radius: 8
        }

        // Собираем результаты из обеих колонок
        onAccepted: {
            var selected = [];
            for (var i = 0; i < styleModel.count; i++) {
                if (styleModel.get(i).isChecked)
                    selected.push(styleModel.get(i).name);
            }
            for (var j = 0; j < actorModel.count; j++) {
                if (actorModel.get(j).isChecked)
                    selected.push(actorModel.get(j).name);
            }
            AppController.submitSignStyles(selected);
        }
        onRejected: {
            AppController.submitSignStyles([]);
        }

        // --- Делегат для чекбоксов (чтобы не писать код дважды) ---
        Component {
            id: checkDelegate

            ItemDelegate {
                height: 40
                width: ListView.view.width

                contentItem: CheckBox {
                    checked: model.isChecked
                    text: model.name

                    contentItem: Text {
                        color: "#ffffff"
                        leftPadding: parent.indicator.width + parent.spacing
                        text: parent.text
                        verticalAlignment: Text.AlignVCenter
                    }

                    onCheckedChanged: model.isChecked = checked
                }

                onClicked: model.isChecked = !model.isChecked
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                color: "#ffffff"
                text: "Отметьте стили или актёров, которые нужно перенести в финальный MKV/MP4 как надписи:"
                wrapMode: Text.WordWrap
            }

            // Две колонки
            RowLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                spacing: 15

                // Левая колонка: Стили
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    Label {
                        color: "#ffffff"
                        font.bold: true
                        text: "Стили"
                    }
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        border.color: "#3f3f3f"
                        color: "#1e1e1e"
                        radius: 4

                        ListView {
                            anchors.fill: parent
                            anchors.margins: 5
                            clip: true
                            delegate: checkDelegate

                            ScrollBar.vertical: ScrollBar {
                            }
                            model: ListModel {
                                id: styleModel

                            }
                        }
                    }
                }

                // Правая колонка: Актеры
                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    Label {
                        color: "#ffffff"
                        font.bold: true
                        text: "Актёры"
                    }
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        border.color: "#3f3f3f"
                        color: "#1e1e1e"
                        radius: 4

                        ListView {
                            anchors.fill: parent
                            anchors.margins: 5
                            clip: true
                            delegate: checkDelegate

                            ScrollBar.vertical: ScrollBar {
                            }
                            model: ListModel {
                                id: actorModel

                            }
                        }
                    }
                }
            }
        }
    }
    MessageDialog {
        id: deleteConfirmDialog

        buttons: MessageDialog.Yes | MessageDialog.No
        text: "Вы уверены, что хотите удалить шаблон '" + templateCombo.currentText + "'?"
        title: "Подтверждение удаления"

        onButtonClicked: function (button, role) {
            if (button === MessageDialog.Yes) {
                AppController.deleteTemplate(templateCombo.currentText);
            }
        }
    }
    TemplateEditorDialog {
        id: templateEditorDialog

    }
    MissingFilesDialog {
        id: missingFilesDialog
    }

    // --- КОНТЕКСТНОЕ МЕНЮ ЛОГОВ ---
    Menu {
        id: logMenu

        MenuItem {
            text: "Копировать всё"

            onClicked: {
                logArea.selectAll();
                logArea.copy();
                logArea.deselect();
            }
        }
        MenuItem {
            text: "Очистить логи"

            onClicked: logArea.text = "--- DubbingTool 2.0 ---"
        }
    }
}
