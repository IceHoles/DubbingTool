import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import DubbingTool

Item {
    id: root

    // Текущее состояние
    property string currentFilePath: ""
    property double durationSec: 0

    function urlToPath(urlString) {
        var path = urlString ? urlString.toString() : "";
        path = path.replace(/^(file:\/{2})|(qrc:\/{2})|(http:\/{2})/, "");
        if (Qt.platform.os === "windows") {
            path = path.replace(/^\//, "");
        }
        return decodeURIComponent(path);
    }

    function scanFile(path) {
        currentFilePath = path;
        inputPathField.text = path;
        var json = extractor.identifyFile(currentFilePath);
        parseMkvJson(json);
        extractor.setDurationSeconds(durationSec);
    }

    function getExtensionForMkvCodec(codecId, trackType) {
        var cid = codecId.toUpperCase();

        if (trackType === "video") {
            if (cid.indexOf("HEVC") !== -1 || cid.indexOf("H265") !== -1
                    || cid.indexOf("HVC1") !== -1 || cid.indexOf("MPEGH") !== -1)
                return "h265";
            if (cid.indexOf("AVC") !== -1 || cid.indexOf("H264") !== -1
                    || cid.indexOf("MPEG4") !== -1 || cid.indexOf("AVC1") !== -1)
                return "h264";
            if (cid.indexOf("AV1") !== -1)
                return "ivf";
            if (cid.indexOf("VP9") !== -1 || cid.indexOf("VP8") !== -1)
                return "ivf";
            return "h264";
        }

        if (trackType === "audio") {
            if (cid.indexOf("AAC") !== -1)
                return "aac";
            if (cid.indexOf("EAC3") !== -1)
                return "eac3";
            if (cid.indexOf("AC3") !== -1)
                return "ac3";
            if (cid.indexOf("DTS") !== -1)
                return "dts";
            if (cid.indexOf("FLAC") !== -1)
                return "flac";
            if (cid.indexOf("OPUS") !== -1)
                return "opus";
            if (cid.indexOf("VORBIS") !== -1)
                return "ogg";
            if (cid.indexOf("MP3") !== -1 || cid.indexOf("MPEG/L3") !== -1)
                return "mp3";
            return "aac";
        }

        if (trackType === "subtitles") {
            if (cid.indexOf("ASS") !== -1 || cid.indexOf("SSA") !== -1 || cid.indexOf("S_TEXT/ASS") !== -1)
                return "ass";
            if (cid.indexOf("UTF8") !== -1 || cid.indexOf("SRT") !== -1 || cid.indexOf("TEXT/UTF8") !== -1)
                return "srt";
            if (cid.indexOf("PGS") !== -1 || cid.indexOf("SUP") !== -1 || cid.indexOf("S_HDMV/PGS") !== -1)
                return "sup";
            return "ass";
        }

        return "bin";
    }

    function parseMkvJson(jsonString) {
        clearTracks();
        if (!jsonString || jsonString === "")
            return;

        var rootObj = JSON.parse(jsonString);

        if (rootObj.container && rootObj.container.properties && rootObj.container.properties.duration) {
            var durationNs = Number(rootObj.container.properties.duration);
            durationSec = durationNs / 1000000000.0;
        } else {
            durationSec = 0;
        }

        function findInsertIndexForGroup(groupKey) {
            // Группы идут в порядке: Видео, Аудио, Субтитры, Шрифты / Вложения
            var headerTitle;
            if (groupKey === "video")
                headerTitle = "Видео";
            else if (groupKey === "audio")
                headerTitle = "Аудио";
            else if (groupKey === "subtitles")
                headerTitle = "Субтитры";
            else
                headerTitle = "Шрифты / Вложения";

            var headerIndex = -1;
            for (var i = 0; i < tracksModel.count; ++i) {
                var obj = tracksModel.get(i);
                if (obj.isHeader && obj.headerTitle === headerTitle) {
                    headerIndex = i;
                    break;
                }
            }
            if (headerIndex === -1)
                return tracksModel.count;

            var insert = headerIndex + 1;
            while (insert < tracksModel.count) {
                var o = tracksModel.get(insert);
                if (o.isHeader)
                    break;
                insert++;
            }
            return insert;
        }

        function appendTrack(trackObj) {
            var props = trackObj.properties || {};
            var type = trackObj.type || "";
            var trackId = trackObj.id || 0;
            var codecName = trackObj.codec || "";
            var codecIdProp = props.codec_id || "";
            var combinedInfo = codecName + " " + codecIdProp;
            var ext = getExtensionForMkvCodec(combinedInfo, type);
            var lang = props.language ? props.language : "und";
            var name = props.track_name ? props.track_name : "";
            var displayName = name !== "" ? name : type;

            var groupKey = "other";
            if (type === "video")
                groupKey = "video";
            else if (type === "audio")
                groupKey = "audio";
            else if (type === "subtitles")
                groupKey = "subtitles";

            var insertIndex = findInsertIndexForGroup(groupKey);

            tracksModel.insert(insertIndex, {
                                    "isHeader": false,
                                    "group": groupKey,
                                    "mode": "track",
                                    "id": trackId,
                                    "trackType": type,
                                    "ext": ext,
                                    "lang": lang,
                                    "info": combinedInfo,
                                    "fileName": "",
                                    "displayName": displayName,
                                    "checked": false
                                });
        }

        clearTracks();

        if (rootObj.tracks && rootObj.tracks.length) {
            for (var i = 0; i < rootObj.tracks.length; ++i) {
                appendTrack(rootObj.tracks[i]);
            }
        }

        if (rootObj.attachments && rootObj.attachments.length) {
            for (var j = 0; j < rootObj.attachments.length; ++j) {
                var att = rootObj.attachments[j];
                var idA = att.id || 0;
                var fileName = att.file_name || "";
                var mime = att.content_type || "";
                var isFont = (mime.indexOf("font") !== -1
                              || fileName.toLowerCase().endsWith(".ttf")
                              || fileName.toLowerCase().endsWith(".otf"));
                var checked = isFont && extractFontsCheckBox.checked;

                tracksModel.append({
                                       "isHeader": false,
                                       "group": "attachments",
                                       "mode": "attachment",
                                       "id": idA,
                                       "trackType": "attach",
                                       "ext": "attach",
                                       "lang": "",
                                       "info": mime,
                                       "fileName": fileName,
                                       "displayName": fileName,
                                       "checked": checked
                                   });
            }
        }
    }

    ListModel {
        id: tracksModel
        // Элементы:
        //  - группы: { isHeader: true, headerTitle: "Видео" | "Аудио" | "Субтитры" | "Шрифты / Вложения" }
        //  - дорожки/вложения:
        //    { isHeader: false, group: "video"|"audio"|"subs"|"attachments", mode: "track"|"attachment",
        //      id: int, trackType: string, ext: string, lang: string, info: string,
        //      fileName: string, checked: bool }
    }

    function clearTracks() {
        tracksModel.clear();
        // Заголовки групп добавляются заранее, чтобы всегда был одинаковый порядок.
        tracksModel.append({ "isHeader": true, "headerTitle": "Видео" });
        tracksModel.append({ "isHeader": true, "headerTitle": "Аудио" });
        tracksModel.append({ "isHeader": true, "headerTitle": "Субтитры" });
        tracksModel.append({ "isHeader": true, "headerTitle": "Шрифты / Вложения" });
    }

    ManualExtractionBridge {
        id: extractor

        onLogMessage: function(message) {
            AppController.logMessage(message);
        }

        onProgressUpdated: function(value, status) {
            // Можно дополнительно отобразить локальный прогресс, если потребуется
            void(value);
            void(status);
        }

        onExtractionFinished: function(exitCode) {
            extractButton.enabled = true;
            void(exitCode);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // --- ВЕРХНЯЯ ПАНЕЛЬ: ВЫБОР ИСХОДНОГО ФАЙЛА ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: "Исходный файл:"
                color: "#a0a0a0"
            }

            TextField {
                id: inputPathField
                Layout.fillWidth: true
                placeholderText: "Путь к MKV или MP4..."
            }

            Button {
                id: browseButton
                text: "Обзор..."
                onClicked: fileDialog.open()
            }
        }

        // --- СРЕДНЯЯ ЧАСТЬ: ТАБЛИЦА ДОРОЖЕК ---
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 8
            color: "#1c1c1c"
            border.color: "#2d2d2d"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                // Заголовок таблицы (5 колонок) — гибкие ширины
                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 140
                        text: "Извлечь"
                        color: "#a0a0a0"
                    }
                    Label {
                        Layout.minimumWidth: 40
                        Layout.preferredWidth: 60
                        text: "ID"
                        color: "#a0a0a0"
                    }
                    Label {
                        Layout.minimumWidth: 80
                        Layout.preferredWidth: 120
                        text: "Тип"
                        color: "#a0a0a0"
                    }
                    Label {
                        Layout.minimumWidth: 70
                        Layout.preferredWidth: 100
                        text: "Язык"
                        color: "#a0a0a0"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Информация"
                        color: "#a0a0a0"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: "#1a1a1a"
                    border.color: "#2d2d2d"

                    ListView {
                        id: tracksView
                        anchors.fill: parent
                        anchors.margins: 4
                        model: tracksModel
                        clip: true
                        delegate: Item {
                            width: tracksView.width
                            height: isHeader ? 28 : 32

                            RowLayout {
                                anchors.fill: parent

                                // Колонка «Извлечь» — чекбокс только для дорожек/вложений
                                Item {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 140

                                    CheckBox {
                                        visible: !isHeader
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: !isHeader && model.checked === true
                                        text: isHeader ? headerTitle : (model.mode === "attachment" ? model.fileName : model.displayName)

                                        onToggled: {
                                            tracksModel.setProperty(index, "checked", checked);
                                        }
                                    }

                                    Label {
                                        visible: isHeader
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: headerTitle
                                        font.bold: true
                                        color: "#dddddd"
                                    }
                                }

                                Label {
                                    Layout.minimumWidth: 40
                                    Layout.preferredWidth: 60
                                    text: isHeader ? "" : model.id
                                    color: "#cccccc"
                                }
                                Label {
                                    Layout.minimumWidth: 80
                                    Layout.preferredWidth: 120
                                    text: isHeader ? "" : model.ext
                                    color: "#cccccc"
                                }
                                Label {
                                    Layout.minimumWidth: 70
                                    Layout.preferredWidth: 100
                                    text: isHeader ? "" : model.lang
                                    color: "#cccccc"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: isHeader ? "" : model.info
                                    color: "#cccccc"
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- НИЖНЯЯ ПАНЕЛЬ: ОПЦИИ И КНОПКА ЗАПУСКА ---
        RowLayout {
            Layout.fillWidth: true

            CheckBox {
                id: extractFontsCheckBox
                text: "Извлечь шрифты (вложения)"
                checked: true
                onToggled: {
                    // Массовое включение/выключение для элементов mode == "attachment"
                    for (var i = 0; i < tracksModel.count; ++i) {
                        var obj = tracksModel.get(i);
                        if (!obj.isHeader && obj.mode === "attachment") {
                            tracksModel.setProperty(i, "checked", checked);
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                id: extractButton
                Layout.preferredHeight: 40
                text: "НАЧАТЬ РАЗБОР"
                onClicked: {
                    if (!currentFilePath || currentFilePath === "")
                        return;

                    var selected = [];
                    for (var i = 0; i < tracksModel.count; ++i) {
                        var obj = tracksModel.get(i);
                        if (!obj.isHeader && obj.checked) {
                            selected.push({
                                              "mode": obj.mode,
                                              "id": obj.id,
                                              "ext": obj.ext,
                                              "lang": obj.lang,
                                              "fileName": obj.fileName
                                          });
                        }
                    }
                    if (selected.length === 0) {
                        AppController.logMessage("Ничего не выбрано.");
                        return;
                    }

                    extractButton.enabled = false;
                    extractor.setDurationSeconds(durationSec);
                    extractor.startExtraction(currentFilePath, selected);
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: "Выбор файла"
        nameFilters: ["Видео (*.mkv *.mp4 *.mks)"]

        onAccepted: {
            scanFile(urlToPath(selectedFile));
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (!drop.urls || drop.urls.length === 0)
                return;
            scanFile(urlToPath(drop.urls[0]));
        }
    }
}

