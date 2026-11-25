---
name: qml-manual-extraction-tab
overview: Перенести существующую вкладку ручной разборки с QWidget-интерфейса на QML, сохранив полностью текущее поведение и взаимодействие с mkvmerge/mkvextract/ffmpeg, при этом основная бизнес-логика будет реализована в QML/JavaScript.
todos:
  - id: analyze-widget-logic
    content: Уточнить и зафиксировать все сценарии работы вкладки ManualExtractionWidget (выбор файла, drag&drop, выбор дорожек, извлечение, ошибки).
    status: completed
  - id: design-qml-page
    content: Нарисовать и реализовать каркас ManualExtractionPage.qml с полем пути, таблицей дорожек, чекбоксом и кнопкой запуска.
    status: completed
  - id: implement-tracks-model-qml
    content: Реализовать в QML модель дорожек и вложений (ListModel с группировкой и чекбоксами), соответствующую структуре QTreeWidget из ManualExtractionWidget.
    status: completed
  - id: port-json-logic-to-js
    content: Перенести parseMkvJson и getExtensionForMkvCodec в JavaScript-функции QML с идентичной логикой.
    status: completed
  - id: create-extraction-bridge
    content: Создать C++-бридж (ManualExtractionBridge или методы AppController) для вызова mkvmerge/mkvextract/ffmpeg и проброса логов/прогресса в QML.
    status: completed
  - id: wire-qml-to-bridge
    content: "Связать ManualExtractionPage.qml с бриджем: сканирование файла, массовый выбор шрифтов, запуск извлечения, обработка сигналов."
    status: completed
  - id: integrate-into-main-qml
    content: Включить ManualExtractionPage в навигацию Main.qml и общую систему логов/прогресса.
    status: completed
  - id: manual-extraction-testing
    content: Протестировать QML-вкладку ручной разборки на реальных файлах и сравнить поведение с исходным виджетным интерфейсом.
    status: completed
isProject: false
---

## Цели

- **Сохранить поведение** текущей вкладки ручной разборки (`ManualExtractionWidget`) при выборе файла, анализе дорожек и извлечении.
- **Перенести UI и большую часть бизнес-логики** на QML/JavaScript, используя C++ только как мост к внешним утилитам (`mkvmerge`, `mkvextract`, `ffmpeg`) и настройкам (`AppSettings`).
- **Вписать страницу** в существующую QML-архитектуру (`Main.qml`, модуль `DubbingTool` с `AppController`).

## Обзор текущей логики

- **UI-состав (`ui/manualextractionwidget.ui`):**
  - Поле выбора исходного файла: `QLineEdit inputPathEdit` + `QPushButton browseButton`.
  - Таблица дорожек: `QTreeWidget tracksTree` с 5 колонками: «Извлечь», `ID`, `Тип`, `Язык`, `Информация`.
  - Чекбокс `extractFontsCheckBox` — массово включает/выключает чекбоксы элементов, помеченных как "attachment".
  - Кнопка запуска `extractButton` (40 px высотой).
- **Взаимодействия и состояния (`src/ui/manualextractionwidget.cpp`):**
  - **Drag & drop** виде файла (`dragEnterEvent`/`dropEvent`) с фильтром по расширениям (`mkv`, `mp4`, `mks`), при дропе вызывается `scanFile(path)`.
  - **Выбор файла через диалог** (`onBrowseClicked` → `QFileDialog::getOpenFileName` → `scanFile(path)`).
  - **Сканирование файла (`scanFile`)**:
    - Сохраняет `m_currentFile`, заполняет `inputPathEdit`, очищает `tracksTree`, отключает `extractButton`.
    - Берет путь к `mkvmerge` из `AppSettings::instance().mkvmergePath()` и синхронно вызывает `m_processManager->executeAndWait(mkvmergeExe, {"--identify", "--identification-format", "json", m_currentFile}, output)`.
    - При успехе вызывает `parseMkvJson(output)`, включает `extractButton`, логирует прогресс.
  - **Парсинг JSON (`parseMkvJson`)**:
    - Извлекает длительность для прогресса (`m_durationSec`).
    - Создает 4 корневых узла в `tracksTree`: «Видео», «Аудио», «Субтитры», «Шрифты / Вложения», разворачивает первые три.
    - Для каждого объекта из `tracks`:
      - Определяет `type`, `id`, `codec`, `codec_id`, язык `language`, имя дорожки `track_name`.
      - Строит `combinedInfo = codec + " " + codec_id`, вычисляет расширение через `getExtensionForMkvCodec(combinedInfo, type)`.
      - Кладет элемент в нужный корень, заполняет текстовые колонки и `UserRole`-данные (`mode = "track"`, `id`, `ext`), ставит `Unchecked`.
    - Для `attachments`:
      - Строит элементы под корнем «Шрифты / Вложения» с `mode = "attachment"`, `id`, `fileName`; для шрифтов (mime содержит `font` или `.ttf/.otf`) и при включенном `extractFontsCheckBox` сразу ставит `Checked`.
  - **Чекбокс шрифтов (`extractFontsCheckBox`)**:
    - При переключении обходит все элементы `tracksTree` и для `mode == "attachment"` проставляет `Checked/Unchecked`.
  - **Запуск извлечения (`onExtractClicked`)**:
    - Проверяет наличие `m_currentFile`, вычисляет `isMkv` из расширения.
    - Обходит `tracksTree` через `QTreeWidgetItemIterator` и собирает:
      - Для `mode == "track"`: формирует имя `"<basename>_track<id>_<lang>.<ext>"`, путь в ту же папку, добавляет:
        - либо в `mkvextractTracks` (`id:path`) для MKV,
        - либо в `ffmpegArgs` (`-map 0:id -c copy outPath`) для не-MKV.
      - Для `mode == "attachment"` и `isMkv == true`: создает папку `attached_fonts` рядом с видео и добавляет `id:path` в `mkvextractAttach`.
    - Если ничего не выбрано — логирует "Ничего не выбрано." и выходит.
    - Отключает `extractButton`, эмитит `progressUpdated(0, "Начало извлечения...")` и логирует старт.
    - Для MKV:
      - При наличии дорожек: запускает `mkvextract tracks m_currentFile <id:path...>`; если вложения тоже есть — делает это синхронно через `executeAndWait`, иначе — асинхронно через `startProcess`.
      - При наличии вложений: запускает `mkvextract attachments m_currentFile <id:path...>` асинхронно.
    - Для не-MKV: собирает полный список `ffmpegArgs` и запускает `ffmpeg` через `startProcess`.
  - **Мониторинг процессов (`onProcessFinished`, `onProcessStdOut`, `onProcessStdErr`)**:
    - Включает `extractButton`, логирует успех/ошибку, эмитит финальный прогресс.
    - Из `stdout`/`stderr` парсит прогресс (`Progress: N%` для mkvextract, `time=HH:MM:SS.xx` для ffmpeg) и эмитит `progressUpdated(percentage, "Извлечение...")`.
    - Все текстовые строки перенаправляет через `logMessage` с нужной категорией (`MKVTOOLNIX`/`FFMPEG`).

## План переноса на QML

### 1. Спроектировать QML-страницу для ручной разборки

- **Создать новый файл** `ui/qml/pages/ManualExtractionPage.qml` по образцу `AutoModePage.qml`, следуя флюент-стилю из правила `qml-fluent-style.mdc`.
- **Структура страницы:**
  - Верхний блок с полем пути к файлу и кнопкой "Обзор...": `RowLayout` с `Label`, `TextField`, `Button`.
  - Центральная область с таблицей дорожек: `TreeView` или комбинация `ListView` + группировка по типам («Видео», «Аудио», «Субтитры», «Шрифты / Вложения»).
  - Нижняя панель с `CheckBox` "Извлечь шрифты (вложения)" и кнопкой "НАЧАТЬ РАЗБОР" (primary action).
- **Интеграция в `Main.qml`:**
  - В `ListModel` сайдбара заменить `page: "pages/PlaceholderPage.qml"` для пункта "Ручная разборка" на `"pages/ManualExtractionPage.qml"`.
  - В `StackLayout` под индексом 1 вместо `PlaceholderPage {}` разместить `ManualExtractionPage {}`.

### 2. Смоделировать данные дорожек в QML

- **Решение по модели:**
  - Использовать **один плоский `ListModel`** с полями:
    - `group` (`"video"`, `"audio"`, `"subs"`, `"attachments"`).
    - `isHeader` (bool) и `headerTitle` ("Видео", "Аудио", "Субтитры", "Шрифты / Вложения").
    - Для дорожек: `id`, `trackType`, `ext`, `lang`, `info`, `mode` (`"track"`/`"attachment"`), `checked` (bool).
  - Представление в UI:
    - Использовать `ListView` с кастомным `delegate`, который:
      - Для `isHeader == true` отрисовывает заголовок группы без чекбокса.
      - Для остальных элементов — строку с чекбоксом в первой "колонке" и текстовыми полями для ID/типа/языка/информации.
- **Синхронизация с чекбоксом шрифтов:**
  - В обработчике `onToggled` для `extractFontsCheckBox` проходить по `ListModel` и массово обновлять `checked` для элементов с `mode == "attachment"` (точный аналог цикла `QTreeWidgetItemIterator`).
- **Отображение прогресса/состояния:**
  - Использовать уже существующие свойства `AppController.currentProgress` и `AppController.currentStage` в общем лог/прогресс-блоке (если он есть на странице) или добавить локальный индикатор прогресса для операций ручной разборки.

### 3. Перенести в QML логику анализа файла и парсинга JSON

- **Функция `scanFile(path)` в QML:**
  - Хранить `currentFilePath` (string) и `durationSec` (number) в `ManualExtractionPage`.
  - При вызове:
    - Сохранить путь и отобразить его в `TextField`.
    - Очистить `tracksModel.clear()`.
    - Отключить кнопку "НАЧАТЬ РАЗБОР" (состояние `isScanning = true`).
    - Через C++-бридж (см. раздел 4) вызвать **синхронный или псевдо-синхронный** метод, который вернет JSON-строку `mkvmerge -J` (или объект).
- **Разбор JSON в JavaScript (аналог `parseMkvJson`):**
  - Использовать `JSON.parse` для полученной строки.
  - Из `root.container.properties.duration` вычислить `durationSec` по формуле `durationNs / 1e9` (как в C++).
  - Сформировать 4 заголовка-группы (элементы с `isHeader = true`).
  - Для каждого трека из `tracks`:
    - Собрать поля `type`, `id`, `codec`, `codec_id`, `language`, `track_name`.
    - Вызвать **JS-порт функции `getExtensionForMkvCodec`** (один-в-один переписать `if/contains`-логику из C++).
    - Добавить элемент в модель с теми же вычисленными полями (`mode = "track"`, `checked = false`, `group` по `type`).
  - Для `attachments`:
    - Добавить элементы с `mode = "attachment"`, `group = "attachments"`, `checked` в зависимости от того, активен ли чекбокс шрифтов и является ли это шрифтом.
  - По завершении:
    - Включить кнопку "НАЧАТЬ РАЗБОР" (`isScanning = false`).
    - Через C++-сигнал/метод логировать успех "Сканирование завершено. Выберите дорожки...".

### 4. Добавить C++-бридж для вызова внешних процессов из QML

- **Новый помощник C++ (например, `ManualExtractionBridge`):**
  - Реализовать как `QObject` с `QML_ELEMENT` (или методами в `AppController`), примерно с API:
    - `Q_INVOKABLE QString identifyFile(const QString &path)` — запускает `mkvmerge --identify --identification-format json path` и возвращает JSON-строку (использует `AppSettings::mkvmergePath()` и `ProcessManager::executeAndWait`).
    - `Q_INVOKABLE void startExtraction(const QString &filePath, const QVariantList &selectedItems)` — запускает `mkvextract` или `ffmpeg`, принимает список выбранных дорожек/вложений (QVariantMap с полями `mode`, `id`, `ext`, `lang`, `fileName`, `group`).
    - Сигналы: `logMessage(QString)`, `progressUpdated(int, QString)`, `extractionFinished(int exitCode)`.
  - Внутри `startExtraction` перенести логику из `onExtractClicked`/`onProcessFinished`/`onProcessStdOut`/`onProcessStdErr`:
    - Построение `mkvextractTracks`/`mkvextractAttach` и `ffmpegArgs` **на основе данных, пришедших из QML** (чтобы не дублировать вычисление пути/расширения).
    - Запуск процессов через существующий `ProcessManager`.
    - Парсинг `stdout`/`stderr` и эмит прогресса/логов в сигналы бриджа.
  - Зарегистрировать класс в QML-модуле `DubbingTool` так, чтобы в QML можно было создать `ManualExtractionBridge { id: extractor }`.
- **Интеграция с логом и прогресс-баром:**
  - В QML-странице пробросить сигналы бриджа в глобальный контроллер, например:
    - `onLogMessage: AppController.logMessage(message)` (при необходимости добавить соответствующий `Q_INVOKABLE`/сигнал в `AppController`).
    - `onProgressUpdated: { /* обновить локальный прогресс или AppController.setProgress */ }`.

### 5. Реализовать в QML сбор событий и запуск извлечения

- **Выбор файла:**
  - Использовать `FileDialog` (как в `AutoModePage.qml`) и функцию вспомогательной нормализации пути `urlToPath`.
  - При выборе файла — вызвать `scanFile(urlToPath(fileUrl))`.
  - Реализовать `DropArea` над всей страницей, фильтруя `drop.urls` по расширениям (`mkv/mp4/mks`) аналогично `dragEnterEvent`.
- **Кнопка "НАЧАТЬ РАЗБОР":**
  - В обработчике `onClicked`:
    - Собрать `selectedItems` из `tracksModel` (все элементы с `checked == true` и `mode in ("track","attachment")`).
    - Если список пуст — через бридж/`AppController` залогировать "Ничего не выбрано." и выйти.
    - Заблокировать кнопку `extractButton.enabled = false` и, при необходимости, ввести флаг `isExtracting = true`.
    - Вызвать `extractor.startExtraction(currentFilePath, selectedItems)`.
- **Обработка завершения:**
  - Подписаться на `extractor.extractionFinished(exitCode)` и восстановить `extractButton.enabled = true`.
  - Логировать успех/ошибку через те же сообщения, что и в C++.

### 6. Согласовать прогресс и уведомления с существующим QML-потоком

- **Прогресс:**
  - Либо использовать **отдельный прогресс-индикатор** на странице ручной разборки (связан с сигналами бриджа),
  - Либо (предпочтительнее для единообразия) расширить `AppController` простыми методами/сигналами для логов/прогресса ручных операций и в `ManualExtractionBridge` вызывать их, чтобы единый прогресс/лог на главном окне отображал все процессы.
- **Логи:**
  - Повторить формат логов, как в `MainWindow::logMessage` (при необходимости вынести форматирование в общий helper) или оставить текущий QML-формат, если уже задан.

### 7. Точное сохранение поведения и валидаций

- **Проверка путей к утилитам:**
  - В `identifyFile` и `startExtraction` обязательно использовать `AppSettings::mkvmergePath()/mkvextractPath()/ffmpegPath()` и проверять `QFileInfo::exists`, логируя те же сообщения об ошибках, что и сейчас.
- **Маппинг кодеков → расширений:**
  - JS-функция `getExtensionForMkvCodec(codecId, trackType)` должна **дословно повторять** условия и приоритеты из C++ (включая fallback-значения).
- **Имена выходных файлов:**
  - Формировать имена в QML или в C++-бридже по той же формуле `baseName_track<id>_<lang>.<ext>` и директорию `attached_fonts` для вложений.
- **Состояния кнопок:**
  - Обеспечить, чтобы `extractButton` был выключен:
    - во время сканирования,
    - при отсутствии проанализированного файла,
    - во время активного извлечения.

### 8. Встраивание и тестирование

- **Интеграционные шаги:**
  - Обновить `Main.qml` так, чтобы при выборе пункта "Ручная разборка" подгружалась новая страница.
  - Убедиться, что QML-модуль `DubbingTool` экспортирует `ManualExtractionBridge` и он доступен без дополнительных импортов.
- **Тест-кейсы:**
  - MKV с несколькими видео/аудио/субтитровыми дорожками и вложенными шрифтами:
    - Проверить, что группы и элементы отображаются как раньше, с корректными расширениями и языками.
    - Переключение чекбокса "Извлечь шрифты" массово меняет состояние шрифтов.
    - Извлечение только дорожек, только вложений и обеих групп.
  - MP4 или другие контейнеры:
    - Убедиться, что используется `ffmpeg` и формируются корректные `-map`-аргументы.
  - Ошибочные сценарии:
    - Пустой выбор дорожек.
    - Отсутствие `mkvmerge`/`mkvextract`/`ffmpeg` в настройках.
    - Неподдерживаемые расширения при dnd.

## Todos

- **analyze-widget-logic**: Детально сопоставить все пользовательские сценарии `ManualExtractionWidget` (drag&drop, выбор дорожек, ошибки) и зафиксировать их как чек-лист для тестирования QML-страницы.
- **design-qml-page**: Спроектировать и набросать каркас `ManualExtractionPage.qml` (без логики, только разметка и стили под флюент-тему).
- **implement-tracks-model-qml**: Реализовать в QML модель дорожек/вложений (ListModel + делегаты) с группировкой и чекбоксами.
- **port-json-logic-to-js**: Перенести логику `parseMkvJson` и `getExtensionForMkvCodec` в JavaScript-функции QML с идентичным поведением.
- **create-extraction-bridge**: Создать C++-класс-б/ридж `ManualExtractionBridge` (или эквивалент в `AppController`) для вызова mkvmerge/mkvextract/ffmpeg и проброса логов/прогресса в QML.
- **wire-qml-to-bridge**: Подключить QML-страницу к бриджу: `scanFile`, массовый выбор шрифтов, запуск извлечения и обработка сигналов завершения.
- **integrate-into-main-qml**: Включить `ManualExtractionPage` в навигацию `Main.qml` и общий лог/прогресс-индикатор.
- **manual-extraction-testing**: Прогнать тесты на разных типах файлов и сценариях, сравнив поведение QML-вкладки с текущим виджетным интерфейсом.

