---
name: Bugfixes and Improvements
overview: План включает исправление путей шаблонов, доработку парсера постов, внедрение кодека aac_at с проверкой наличия, использование mp4box, а также масштабное улучшение системы логирования с цветовой индикацией и группировкой прогресса. Ветка и коммит будут созданы сразу после утверждения плана.
todos:
  - id: git-init
    content: Сделать коммит локальных изменений и создать новую ветку
    status: completed
  - id: fix-templates
    content: Вернуть папку шаблонов и черновиков к экзешнику, починить ручную сборку
    status: completed
  - id: fix-post-parser
    content: "Починить парсер: добавить 'постер' в якори и исправить парсинг редактора перевода"
    status: completed
  - id: fix-aac-at
    content: Написать проверку кодека aac_at, использовать его с -b:a 256k или делать фоллбэк на aac
    status: completed
  - id: fix-audio-muxing
    content: Проверить и доработать логику вывода mp4box и извлечения m4a/mka
    status: completed
  - id: improve-logging
    content: Добавить LogLevel, обновить сигнатуры logMessage, добавить цвета в UI
    status: completed
  - id: logging-args
    content: Экранировать аргументы программ в логах для удобного копирования в консоль
    status: completed
  - id: logging-results
    content: Добавить вывод результатов важных команд (ffprobe и т.п.)
    status: completed
  - id: logging-throttle
    content: Реализовать in-place обновление логов прогресса с сохранением бэктрека раз в 10 секунд
    status: completed
isProject: false
---

# Bugfixes and Improvements

## 1. Git & Workflow
* Будет сделан коммит текущих локальных изменений.
* Будет создана новая ветка `fix-bugs-and-improvements`.
*(Выполняется в первую очередь после перехода в Agent mode, так как Plan mode не позволяет вносить изменения)*

## 2. Шаблоны и Черновики
* В файле `src/ui/mainwindow.cpp` изменим `templatesStorageDir()`: вернем путь сохранения в папку `templates` рядом с исполняемым файлом (вместо `appdata/`).
* Вернем туда же сохранение черновиков (`.new_template_draft.json`).
* Это автоматически починит чтение шаблона в `src/processing/manualassembler.cpp`, так как он уже ищет их по относительному пути `templates/`.

## 3. Парсер постов (`PostGenerator`)
* В `src/processing/postgenerator.cpp` в массив `allAnchors` добавим `постер`, `редактор перевода` и другие потенциальные лишние строки, чтобы `findNextSectionHeaderStart` не захватывал их.
* Исправим дублирование "перевода": будем искать `"редактор перевода"` отдельно и маппить его в `%TRANSLATION_EDITOR%`, а `"перевод"` только в `%SUB_AUTHOR%`.

## 4. Поддержка `aac_at` и `m4a/mka`
* Добавим функцию (например, в `WorkflowManager` или `AppSettings`), которая при старте делает `ffmpeg -encoders | findstr aac_at` (или программный парсинг) и кэширует наличие кодека `aac_at`.
* Там, где мы указывали `-c:a aac_at`, добавим условие: если кодек есть — используем `aac_at -b:a 256k`, если нет — фоллбэк на обычный `aac`.
* Убедимся, что форматы `m4a/mka` корректно прокидываются на этап сборки через `mp4box` (`src/core/workflowmanager.cpp`, `src/processing/manualrenderer.cpp`).

## 5. Система логирования (`LogCategory` -> `LogLevel`)
* В `src/core/appsettings.h` добавим `enum class LogLevel { Info, Warning, Error, Success };`.
* Изменим сигнатуру: `void logMessage(const QString& msg, LogCategory cat = LogCategory::APP, LogLevel level = LogLevel::Info)`.
* Обновим UI (например, `MainWindow::logMessage`), оборачивая текст в HTML-теги: `<span style="color: orange;">` для `Warning`, `red` для `Error` и т.д.
* При логгировании старта процессов (`ProcessManager`) добавим экранирование: `QString("\"%1\"").arg(arg)` для аргументов с пробелами, чтобы лог можно было просто вставить в консоль.
* В местах вызова `ffprobe` (например, получение длительности) выведем реальный результат команды в лог (например: "Определена длительность: 24.5 секунд из ffprobe").
* Для логов прогресса (`ffmpeg`, `mkvmerge`, `mp4box`) реализуем in-place обновление: последняя строка прогресса будет постоянно перезаписываться (чтобы визуально было плавно), но каждые 10 секунд мы будем оставлять строку в истории лога для бэктрека. Ошибки из `stderr` будут выводиться отдельными строками без задержек.