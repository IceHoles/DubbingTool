import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import DubbingTool

Dialog {
    id: editorDialog

    // Внутренний объект (JSON) с данными шаблона
    property var currentTemplate: ({})
    // Режим работы: true - редактируем существующий, false - создаем новый
    property bool isEditMode: false

    // Функция сохранения данных из UI в JSON
    function gatherTemplateData() {
        // --- Вкладка 1 ---
        currentTemplate.templateName = nameEdit.text.trim();
        currentTemplate.seriesTitle = seriesEdit.text.trim();
        currentTemplate.rssUrl = rssEdit.text.trim();

        var tagsStr = tagsEdit.text.trim();
        currentTemplate.releaseTags = tagsStr === "" ? [] : tagsStr.split(",").map(t => t.trim());

        currentTemplate.animationStudio = studioEdit.text.trim();
        currentTemplate.subAuthor = subAuthorEdit.text.trim();
        currentTemplate.originalLanguage = origLangEdit.text.trim();
        currentTemplate.targetAudioFormat = audioFormatCombo.currentValue;
        currentTemplate.voiceoverType = voiceTypeCombo.currentValue;
        currentTemplate.renderPresetName = presetCombo.currentValue;

        currentTemplate.useOriginalAudio = origAudioCheck.checked;
        currentTemplate.createSrtMaster = srtMasterCheck.checked;
        currentTemplate.isCustomTranslation = customTransCheck.checked;
        currentTemplate.useConcatRender = concatCheck.checked;

        // --- Вкладка 2 ---
        currentTemplate.sourceHasSubtitles = srcSubsCheck.checked;
        currentTemplate.forceSignStyleRequest = forceSignCheck.checked;
        currentTemplate.pauseForSubEdit = pauseSubCheck.checked;

        var stylesStr = signStylesArea.text.trim();
        currentTemplate.signStyles = stylesStr === "" ? [] : stylesStr.split("\n").map(s => s.trim());

        // Собираем таблицу замен обратно в объект JSON
        var subObj = {};
        for (var i = 0; i < subsModel.count; i++) {
            var item = subsModel.get(i);
            if (item.findText.trim() !== "") {
                subObj[item.findText.trim()] = item.replaceText.trim();
            }
        }
        currentTemplate.substitutions = subObj;

        // --- Вкладка 3 ---
        currentTemplate.generateTb = genTbCheck.checked;
        currentTemplate.endingChapterName = endChapterEdit.text.trim();
        currentTemplate.endingStartTime = endTimeEdit.text.trim();
        currentTemplate.useManualTime = manualTimeCheck.checked;
        currentTemplate.defaultTbStyleName = tbStyleCombo.currentValue;

        currentTemplate.director = dirEdit.text.trim();
        currentTemplate.assistantDirector = assistDirEdit.text.trim();
        currentTemplate.soundEngineer = sndEngEdit.text.trim();
        currentTemplate.songsSoundEngineer = songEngEdit.text.trim();
        currentTemplate.episodeSoundEngineer = epEngEdit.text.trim();
        currentTemplate.recordingSoundEngineer = recEngEdit.text.trim();
        currentTemplate.videoLocalizationAuthor = vidLocEdit.text.trim();
        currentTemplate.timingAuthor = timAuthEdit.text.trim();
        currentTemplate.signsAuthor = signAuthEdit.text.trim();
        currentTemplate.translationEditor = transEdEdit.text.trim();
        currentTemplate.releaseBuilder = relBldEdit.text.trim();

        var castStr = castArea.text.trim();
        currentTemplate.cast = castStr === "" ? [] : castStr.split(/\n|,/).map(c => c.trim()).filter(c => c !== "");

        // --- Вкладка 4 ---
        currentTemplate.postTemplates = {
            "tg_mp4": tgMp4Area.text.trim(),
            "tg_mkv": tgMkvArea.text.trim(),
            "vk": vkArea.text.trim(),
            "vk_comment": vkCommentArea.text.trim()
        };

        // --- Вкладка 5 ---
        currentTemplate.seriesTitleForPost = postTitleEdit.text.trim();
        currentTemplate.totalEpisodes = totalEpSpin.value;
        currentTemplate.posterPath = posterPathEdit.text.trim();

        var urlsStr = urlsArea.text.trim();
        currentTemplate.uploadUrls = urlsStr === "" ? [] : urlsStr.split("\n").map(u => u.trim());

        return currentTemplate;
    }

    // Функция загрузки данных в UI
    function loadTemplate(templateData) {
        currentTemplate = templateData;

        // Загружаем списки пресетов из С++
        presetCombo.model = AppController.getRenderPresets();
        tbStyleCombo.model = AppController.getTbStyles();

        // --- Функция-помощник для поиска индекса ---
        function findIndex(combo, value, defaultIndex) {
            var idx = combo.indexOfValue(value);
            return idx >= 0 ? idx : defaultIndex;
        }

        // --- Вкладка 1: Основные ---
        nameEdit.text = currentTemplate.templateName || "";
        seriesEdit.text = currentTemplate.seriesTitle || "";
        rssEdit.text = currentTemplate.rssUrl || "";
        tagsEdit.text = (currentTemplate.releaseTags || []).join(", ");
        studioEdit.text = currentTemplate.animationStudio || "";
        subAuthorEdit.text = currentTemplate.subAuthor || "";
        origLangEdit.text = currentTemplate.originalLanguage || "ja";

        audioFormatCombo.currentIndex = findIndex(audioFormatCombo, currentTemplate.targetAudioFormat, 0);
        voiceTypeCombo.currentIndex = findIndex(voiceTypeCombo, currentTemplate.voiceoverType || "Dubbing", 0);

        // Если в шаблоне пусто, пробуем найти "NVIDIA (HEVC NVENC)", иначе ставим первый элемент (0)
        presetCombo.currentIndex = findIndex(presetCombo, currentTemplate.renderPresetName || "NVIDIA (HEVC NVENC)", 0);

        origAudioCheck.checked = currentTemplate.useOriginalAudio || false;
        srtMasterCheck.checked = currentTemplate.createSrtMaster || false;
        customTransCheck.checked = currentTemplate.isCustomTranslation || false;
        concatCheck.checked = currentTemplate.useConcatRender || false;

        // --- Вкладка 2: Субтитры ---
        srcSubsCheck.checked = currentTemplate.sourceHasSubtitles !== undefined ? currentTemplate.sourceHasSubtitles : true;
        forceSignCheck.checked = currentTemplate.forceSignStyleRequest || false;
        pauseSubCheck.checked = currentTemplate.pauseForSubEdit || false;

        signStylesArea.text = (currentTemplate.signStyles || []).join("\n");

        subsModel.clear();
        if (currentTemplate.substitutions) {
            for (var key in currentTemplate.substitutions) {
                subsModel.append({
                    "findText": key,
                    "replaceText": currentTemplate.substitutions[key]
                });
            }
        }

        // --- Вкладка 3: Создание ТБ ---
        genTbCheck.checked = currentTemplate.generateTb !== undefined ? currentTemplate.generateTb : true;
        endChapterEdit.text = currentTemplate.endingChapterName || "";
        endTimeEdit.text = currentTemplate.endingStartTime || "00:00:00.000";
        manualTimeCheck.checked = currentTemplate.useManualTime || false;

        // Если в шаблоне пусто, пробуем найти "default_1080p", иначе ставим первый элемент (0)
        tbStyleCombo.currentIndex = findIndex(tbStyleCombo, currentTemplate.defaultTbStyleName || "default_1080p", 0);

        dirEdit.text = currentTemplate.director || "";
        assistDirEdit.text = currentTemplate.assistantDirector || "";
        sndEngEdit.text = currentTemplate.soundEngineer || "";
        songEngEdit.text = currentTemplate.songsSoundEngineer || "";
        epEngEdit.text = currentTemplate.episodeSoundEngineer || "";
        recEngEdit.text = currentTemplate.recordingSoundEngineer || "";
        vidLocEdit.text = currentTemplate.videoLocalizationAuthor || "";
        timAuthEdit.text = currentTemplate.timingAuthor || "";
        signAuthEdit.text = currentTemplate.signsAuthor || "";
        transEdEdit.text = currentTemplate.translationEditor || "";
        relBldEdit.text = currentTemplate.releaseBuilder || "";

        castArea.text = (currentTemplate.cast || []).join("\n");

        // --- Вкладка 4: Посты ---
        var posts = currentTemplate.postTemplates || {};
        tgMp4Area.text = posts["tg_mp4"] || "";
        tgMkvArea.text = posts["tg_mkv"] || "";
        vkArea.text = posts["vk"] || "";
        vkCommentArea.text = posts["vk_comment"] || "";

        // --- Вкладка 5: Публикация ---
        postTitleEdit.text = currentTemplate.seriesTitleForPost || "";
        totalEpSpin.value = currentTemplate.totalEpisodes || 0;
        posterPathEdit.text = currentTemplate.posterPath || "";

        urlsArea.text = (currentTemplate.uploadUrls || []).join("\n");
    }

    height: 700
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    title: "Редактор шаблона"
    width: 850
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)

    background: Rectangle {
        border.color: "#2d2d2d"
        color: "#252525"
        radius: 8
    }

    onAccepted: {
        var finalJson = gatherTemplateData();
        AppController.saveTemplateJson(finalJson);
    }

    ColumnLayout {
        anchors.fill: parent

        // --- ВКЛАДКИ (TabBar) ---
        TabBar {
            id: tabBar

            Layout.fillWidth: true

            TabButton {
                text: "Основные"
            }
            TabButton {
                text: "Субтитры"
            }
            TabButton {
                text: "Создание ТБ"
            }
            TabButton {
                text: "Посты"
            }
            TabButton {
                text: "Публикация"
            }
        }

        // --- СОДЕРЖИМОЕ ВКЛАДОК (StackLayout) ---
        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            currentIndex: tabBar.currentIndex

            // 1. ВКЛАДКА "ОСНОВНЫЕ"
            ScrollView {
                clip: true

                GridLayout {
                    columnSpacing: 15
                    columns: 2
                    rowSpacing: 10
                    width: parent.width - 20

                    Label {
                        text: "Имя шаблона:"
                    }
                    TextField {
                        id: nameEdit

                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Название сериала:"
                    }
                    TextField {
                        id: seriesEdit

                        Layout.fillWidth: true
                    }
                    Label {
                        text: "URL RSS-фида:"
                    }
                    TextField {
                        id: rssEdit

                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Теги релиза (для поиска):"
                    }
                    TextField {
                        id: tagsEdit

                        Layout.fillWidth: true
                        placeholderText: "например, [Erai-raws], [SubsPlease]"
                    }
                    Label {
                        text: "Студия анимации:"
                    }
                    TextField {
                        id: studioEdit

                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Автор перевода:"
                    }
                    TextField {
                        id: subAuthorEdit

                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Язык оригинала (код):"
                    }
                    TextField {
                        id: origLangEdit

                        Layout.fillWidth: true
                        text: "ja"
                    }
                    Label {
                        text: "Формат аудио для сборки:"
                    }
                    ComboBox {
                        id: audioFormatCombo

                        Layout.fillWidth: true
                        model: ["aac", "flac"]
                    }
                    Label {
                        text: "Тип озвучки:"
                    }
                    ComboBox {
                        id: voiceTypeCombo

                        Layout.fillWidth: true
                        model: [
                            {
                                text: "Дубляж",
                                val: "Dubbing"
                            },
                            {
                                text: "Закадр",
                                val: "Voiceover"
                            }
                        ]
                        textRole: "text"
                        valueRole: "val"
                    }
                    Label {
                        text: "Пресет рендера MP4:"
                    }
                    ComboBox {
                        id: presetCombo

                        Layout.fillWidth: true
                    }

                    // Чекбоксы (на всю ширину 2 колонок)
                    CheckBox {
                        id: origAudioCheck

                        Layout.columnSpan: 2
                        text: "Использовать оригинальную аудиодорожку при сборке MKV"
                    }
                    CheckBox {
                        id: srtMasterCheck

                        Layout.columnSpan: 2
                        text: "Создать дополнительную сборку с SRT (мастер-копия)"
                    }
                    CheckBox {
                        id: customTransCheck

                        Layout.columnSpan: 2
                        text: "Собственный перевод (дорожка назовется '[Дубляжная]')"
                    }
                    CheckBox {
                        id: concatCheck

                        Layout.columnSpan: 2
                        text: "Умный рендер (concat) — перекодировать только ТБ"
                    }
                }
            }

            // 2. ВКЛАДКА "СУБТИТРЫ"
            ScrollView {
                clip: true

                ColumnLayout {
                    spacing: 15
                    width: parent.width - 20

                    CheckBox {
                        id: srcSubsCheck

                        text: "В исходном MKV есть дорожка русских субтитров"
                    }
                    CheckBox {
                        id: forceSignCheck

                        text: "Всегда запрашивать стили/актеров для надписей"
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        color: "#2d2d2d"
                        height: 1
                    } // Разделитель

                    Label {
                        text: "Стили/актеры для надписей (каждый с новой строки):"
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150

                        ScrollView {
                            Layout.fillHeight: true
                            Layout.fillWidth: true

                            TextArea {
                                id: signStylesArea

                                color: "#ffffff"

                                background: Rectangle {
                                    border.color: "#2d2d2d"
                                    color: "#1e1e1e"
                                    radius: 4
                                }
                            }
                        }
                        Button {
                            Layout.alignment: Qt.AlignTop
                            // TODO: В будущем добавим вызов твоего диалога из файла
                            text: "Выбрать из файла..."
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        color: "#2d2d2d"
                        height: 1
                    } // Разделитель

                    CheckBox {
                        id: pauseSubCheck

                        text: "Приостанавливать процесс для ручной правки субтитров"
                    }

                    // Таблица замен
                    GroupBox {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        title: "Автоматические замены в тексте субтитров"

                        background: Rectangle {
                            border.color: "#2d2d2d"
                            color: "transparent"
                            radius: 4
                        }

                        ColumnLayout {
                            anchors.fill: parent

                            // Заголовки таблицы
                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    font.bold: true
                                    text: "Найти"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    font.bold: true
                                    text: "Заменить на"
                                }
                            }

                            // Сама таблица (ListView + TextField)
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 150
                                border.color: "#2d2d2d"
                                color: "#1e1e1e"

                                ListView {
                                    anchors.fill: parent
                                    anchors.margins: 5
                                    clip: true

                                    delegate: RowLayout {
                                        width: ListView.view.width

                                        TextField {
                                            Layout.fillWidth: true
                                            text: model.findText

                                            onTextEdited: model.findText = text
                                        }
                                        TextField {
                                            Layout.fillWidth: true
                                            text: model.replaceText

                                            onTextEdited: model.replaceText = text
                                        }
                                    }
                                    model: ListModel {
                                        id: subsModel

                                    }
                                }
                            }

                            // Кнопки управления таблицей
                            RowLayout {
                                Layout.alignment: Qt.AlignRight

                                Button {
                                    text: "Добавить"

                                    onClicked: subsModel.append({
                                        "findText": "Текст для поиска",
                                        "replaceText": "Текст для замены"
                                    })
                                }
                                Button {
                                    text: "Удалить"

                                    onClicked: {
                                        if (subsModel.count > 0)
                                            subsModel.remove(subsModel.count - 1);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 3. ВКЛАДКА "СОЗДАНИЕ ТБ"
            ScrollView {
                clip: true

                RowLayout {
                    spacing: 20
                    width: parent.width - 20

                    // ЛЕВАЯ КОЛОНКА: Настройки эндинга и должности
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        spacing: 10

                        CheckBox {
                            id: genTbCheck

                            text: "Генерировать Титры Благодарности (ТБ)"
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            color: "#2d2d2d"
                            height: 1
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columnSpacing: 10
                            columns: 2
                            rowSpacing: 10

                            Label {
                                text: "Название главы эндинга:"
                            }
                            TextField {
                                id: endChapterEdit

                                Layout.fillWidth: true
                                placeholderText: "например, Ending"
                            }
                            Label {
                                text: "Время начала эндинга:"
                            }
                            RowLayout {
                                TextField {
                                    id: endTimeEdit

                                    Layout.fillWidth: true
                                    placeholderText: "00:00:00.000"
                                }
                                CheckBox {
                                    id: manualTimeCheck

                                    text: "Вручную"
                                }
                            }
                            Label {
                                text: "Стиль ТБ по умолчанию:"
                            }
                            ComboBox {
                                id: tbStyleCombo

                                Layout.fillWidth: true
                            }
                            Rectangle {
                                Layout.bottomMargin: 5
                                Layout.columnSpan: 2
                                Layout.fillWidth: true
                                Layout.topMargin: 5
                                color: "#2d2d2d"
                                height: 1
                            }
                            Label {
                                text: "Режиссер дубляжа:"
                            }
                            TextField {
                                id: dirEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Помощник режиссера:"
                            }
                            TextField {
                                id: assistDirEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Звукорежиссер:"
                            }
                            TextField {
                                id: sndEngEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Звукореж. песен:"
                            }
                            TextField {
                                id: songEngEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Звукореж. эпизода:"
                            }
                            TextField {
                                id: epEngEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Звукореж. записи:"
                            }
                            TextField {
                                id: recEngEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Локализация видео:"
                            }
                            TextField {
                                id: vidLocEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Разметка:"
                            }
                            TextField {
                                id: timAuthEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Локал. надписей:"
                            }
                            TextField {
                                id: signAuthEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Редактор перевода:"
                            }
                            TextField {
                                id: transEdEdit

                                Layout.fillWidth: true
                            }
                            Label {
                                text: "Сборка релиза:"
                            }
                            TextField {
                                id: relBldEdit

                                Layout.fillWidth: true
                            }
                        }
                    }

                    // ПРАВАЯ КОЛОНКА: Список актеров
                    ColumnLayout {
                        Layout.alignment: Qt.AlignTop
                        Layout.fillHeight: true
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: "Список актеров (каждый с новой строки или через запятую):"
                            wrapMode: Text.WordWrap
                        }
                        ScrollView {
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            Layout.minimumHeight: 300

                            TextArea {
                                id: castArea

                                color: "#ffffff"
                                wrapMode: TextEdit.Wrap

                                background: Rectangle {
                                    border.color: "#2d2d2d"
                                    color: "#1e1e1e"
                                    radius: 4
                                }
                            }
                        }
                    }
                }
            }
            // 4. ВКЛАДКА "ПОСТЫ"
            ScrollView {
                clip: true

                ColumnLayout {
                    spacing: 15
                    width: parent.width - 20

                    RowLayout {
                        Layout.fillWidth: true

                        Item {
                            Layout.fillWidth: true
                        }
                        Button {
                            text: "? Справка"
                        }
                    }
                    GridLayout {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        columnSpacing: 15
                        columns: 2
                        rowSpacing: 15

                        // Telegram MP4
                        ColumnLayout {
                            Layout.fillHeight: true
                            Layout.fillWidth: true

                            Label {
                                font.bold: true
                                text: "Telegram (MP4 пост):"
                            }
                            ScrollView {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                Layout.minimumHeight: 200

                                TextArea {
                                    id: tgMp4Area

                                    wrapMode: TextEdit.Wrap

                                    background: Rectangle {
                                        border.color: "#2d2d2d"
                                        color: "#1e1e1e"
                                        radius: 4
                                    }
                                }
                            }
                        }

                        // Telegram MKV
                        ColumnLayout {
                            Layout.fillHeight: true
                            Layout.fillWidth: true

                            Label {
                                font.bold: true
                                text: "Telegram (MKV пост):"
                            }
                            ScrollView {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                Layout.minimumHeight: 200

                                TextArea {
                                    id: tgMkvArea

                                    wrapMode: TextEdit.Wrap

                                    background: Rectangle {
                                        border.color: "#2d2d2d"
                                        color: "#1e1e1e"
                                        radius: 4
                                    }
                                }
                            }
                        }

                        // VK Основной
                        ColumnLayout {
                            Layout.fillHeight: true
                            Layout.fillWidth: true

                            Label {
                                font.bold: true
                                text: "VK (Основной пост):"
                            }
                            ScrollView {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                Layout.minimumHeight: 200

                                TextArea {
                                    id: vkArea

                                    wrapMode: TextEdit.Wrap

                                    background: Rectangle {
                                        border.color: "#2d2d2d"
                                        color: "#1e1e1e"
                                        radius: 4
                                    }
                                }
                            }
                        }

                        // VK Комментарий
                        ColumnLayout {
                            Layout.fillHeight: true
                            Layout.fillWidth: true

                            Label {
                                font.bold: true
                                text: "VK (Комментарий):"
                            }
                            ScrollView {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                Layout.minimumHeight: 200

                                TextArea {
                                    id: vkCommentArea

                                    wrapMode: TextEdit.Wrap

                                    background: Rectangle {
                                        border.color: "#2d2d2d"
                                        color: "#1e1e1e"
                                        radius: 4
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // 5. ВКЛАДКА "ПУБЛИКАЦИЯ"
            ScrollView {
                clip: true

                GridLayout {
                    Layout.alignment: Qt.AlignTop
                    columnSpacing: 15
                    columns: 2
                    rowSpacing: 15
                    width: parent.width - 20

                    Label {
                        text: "Название для постов:"
                    }
                    TextField {
                        id: postTitleEdit

                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Всего серий в сезоне:"
                    }
                    SpinBox {
                        id: totalEpSpin

                        editable: true
                        from: 0
                        to: 9999
                    }
                    Label {
                        text: "Путь к постеру:"
                    }
                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            id: posterPathEdit

                            Layout.fillWidth: true
                        }
                        Button {
                            text: "Обзор..."

                            onClicked: posterDialog.open()
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignTop
                        text: "Ссылки для загрузки видео\n(каждая с новой строки):"
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150

                        TextArea {
                            id: urlsArea

                            placeholderText: "https://v4.anilib.me/upload...\https://smotret-anime.app/upload..."
                            wrapMode: TextEdit.Wrap

                            background: Rectangle {
                                border.color: "#2d2d2d"
                                color: "#1e1e1e"
                                radius: 4
                            }
                        }
                    }
                }
            }
        }
    }
    // --- Диалог выбора постера ---
    FileDialog {
        id: posterDialog

        nameFilters: ["Изображения (*.png *.jpg *.jpeg *.webp)", "Все файлы (*)"]
        title: "Выберите файл постера"

        onAccepted: {
            var path = selectedFile.toString();
            path = path.replace(/^(file:\/{2})|(qrc:\/{2})|(http:\/{2})/, "");
            if (Qt.platform.os === "windows") {
                path = path.replace(/^\//, "");
            }
            posterPathEdit.text = decodeURIComponent(path);
        }
    }
}
