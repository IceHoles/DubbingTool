---
name: telegram-clipboard-to-template-markdown
overview: Добавить в редактор шаблонов поддержку обратного преобразования из Telegram clipboard (application/x-td-field-text + application/x-td-field-tags) в внутренний псевдо‑markdown формат постов.
todos:
  - id: extract-tag-parser
    content: Вынести парсер бинарного формата application/x-td-field-tags в отдельную функцию/структуру и интегрировать в TelegramFormatter
    status: pending
  - id: implement-reverse-markdown
    content: Реализовать преобразование из cleanText + TagRecord в псевдо‑markdown строку (обратное к текущему TelegramFormatter)
    status: pending
  - id: hook-template-editor-paste
    content: Перехватить вставку в полях Telegram-постов редактора шаблонов и вызывать обратный парсер только при наличии Telegram MIME-типов
    status: pending
  - id: test-on-real-telegram-data
    content: Протестировать вставку постов с custom emoji, ссылками и стилями из реального клиента Telegram и убедиться в восстановимости формата
    status: pending
isProject: false
---

### Цели

- **Считать Telegram-специфичный буфер обмена** (`application/x-td-field-text` и `application/x-td-field-tags`) при вставке в поля шаблонов постов.
- **Преобразовывать формат Telegram обратно в внутренний псевдо‑markdown** (тот же, который сейчас `TelegramFormatter` превращает в Telegram-разметку).
- **Сохранять поведение обычной вставки**: если специальных MIME-типов Telegram нет, вставлять обычный plaintext как сейчас.

### Шаг 1. Инкапсулировать разбор бинарного формата тегов

- **Создать небольшой вспомогательный модуль/класс**, например `TelegramTagParser` в `[src/processing/telegramformatter.cpp](src/processing/telegramformatter.cpp)` или отдельном файле рядом.
  - Вынести туда логику разбора `application/x-td-field-tags`, в соответствии с уже задокументированным форматом из `[.cursor/telegram_clipboard_tags_format.md](.cursor/telegram_clipboard_tags_format.md)`:
    - `u32 count;` затем `count` записей по 3×`u32` + UTF‑16 BE строка.
  - Вернуть структуру вида `struct TagRecord { qsizetype position; qsizetype length; QStringList tokens; };` где `tokens` — `tagContent.split("\\")` (например, `"**"`, `"custom-emoji://…"`, `"https://…"`).
- **Добавить публичный helper** в `TelegramFormatter`, например:
  - `static QString fromTelegramClipboardToPseudoMarkdown(const QMimeData *mime);`
  - Он будет использовать `mime->data("application/x-td-field-text")` и `mime->data("application/x-td-field-tags")` + `TelegramTagParser`.

### Шаг 2. Алгоритм обратного преобразования в псевдо‑markdown

- **Извлечь исходный текст**:
  - Если есть `application/x-td-field-text` → использовать его как `cleanText` (UTF‑8).
  - Иначе fallback на `text/plain` (в этом случае Telegram-тегов может не быть, и мы просто вернём этот текст как есть).
- **Разбить текст на сегменты по тегам**:
  - На основе `TagRecord` собрать все граничные позиции: `0`, `cleanText.size()`, `position` и `position+length` каждого тега.
  - Отсортировать и удалить дубликаты → получить интервалы `[b[i], b[i+1])`.
  - Для каждого интервала определить **актуальный набор токенов** — объединение `tokens` всех `TagRecord`, которые полностью покрывают этот интервал (`position <= b[i] && position+length >= b[i+1]`).
- **Инвертировать теги в псевдо‑markdown для каждого интервала**:
  - Получить подстроку `segment = cleanText.mid(b[i], b[i+1] - b[i])`.
  - Разделить токены на группы:
    - `styleTokens` = `**`, `__`, `~~`, `||`, `^^`, ```.
    - `quoteTokens` = `>` или `>^` (если присутствуют).
    - `linkToken` = если есть URL типа `https://…`.
    - `emojiToken` = `custom-emoji://<id>?<size>`.
  - **Custom emoji**:
    - Для `emojiToken` разбрать `idWithSize` и, если есть, `?size`.
    - Сформировать базовый маркер: `core = QString("[%1](emoji:%2)").arg(segment, idWithSize);`.
  - **Ссылки**:
    - Если есть `linkToken` и `segment` — сформировать `[segment](<url>)`.
  - **Наложить стили**:
    - Для `styleTokens` обернуть `core` или `segment` в соответствии с текущей соглашением:
      - `**` → `"**" + text + "**"`, `__` → `"__" + text + "__"` и т.д.
      - При комбинировании нескольких стилей использовать фиксированный порядок (например, `code` → `bold` → `underline` → `strike` → `spoiler` → `sup`).
  - **Цитаты**:
    - Если есть `>` → обернуть отформатированный текст в `">" + text + "<"`.
    - Если есть `>^` → `">^" + text + "<^"`.
  - Если токенов нет — вернуть `segment` без изменений.
- **Собрать результат** конкатенацией всех отформатированных сегментов в исходном порядке.

### Шаг 3. Интеграция с редактором шаблонов (`TemplateEditor`)

- В `[src/ui/templateeditor.cpp](src/ui/templateeditor.cpp)` и `[ui/templateeditor.ui](ui/templateeditor.ui)`:
  - **Ничего не ломать в текущем UX**, добавить "умный" путь вставки:
    - Переопределить вставку для полей `postTgMp4Edit` и `postTgMkvEdit` (подкласс `QPlainTextEdit` или обработка `insertFromMimeData` через кастомный виджет/делегат).
    - Внутри `insertFromMimeData(const QMimeData *source)`:
      - Если `source->hasFormat("application/x-td-field-text")` и `source->hasFormat("application/x-td-field-tags")` → вызвать `TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(source)` и вставить полученную строку **вместо** стандартной логики.
      - Иначе вызвать базовую реализацию `QPlainTextEdit::insertFromMimeData(source)` (обычная вставка plaintext/RTF и т.п.).
- Альтернативно, если не хочется лезть в наследование:
  - Добавить рядом с полями кнопки "Вставить из Telegram" и в слотах делать:
    ```cpp
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    const QString md = TelegramFormatter::fromTelegramClipboardToPseudoMarkdown(mime);
    ui->postTgMp4Edit->setPlainText(md);
    ```
  - Но по твоему описанию предпочтительнее именно перехватывать вставку, если в буфере есть Telegram-форматы.

### Шаг 4. Тесты и валидация

- **Собрать минимальные примеры**:
  - Пост с:
    - одним custom emoji,
    - несколькими custom emoji,
    - комбинацией стилизации (жирный + custom emoji + ссылка),
    - цитатами `>` и `>^`.
  - Скопировать из настоящего Telegram-клиента → вставить в редактор шаблонов → проверить, что:
    - Появился псевдо‑markdown (например, `[💙](emoji:documentId?size)`),
    - При последующем использовании текущего `TelegramFormatter::formatAndCopyToClipboard` формат полностью восстанавливается в Telegram (custom emoji, ссылки, стили).
- При необходимости добавить небольшой unit-тест на парсер тегов в отдельном таргете (или хотя бы отладочный helper, который можно вызывать вручную).

### Шаг 5. Расширения (опционально)

- Добавить **отладочное окно**/логгер в dev-сборке, чтобы смотреть распарсенные `TagRecord` и построенный псевдо‑markdown при вставке.
- При необходимости, сохранить ID custom emoji (из `custom-emoji://…`) в дополнительные структуры (например, мапа "позиция → document_id"), если дальше планируется использовать Telegram API `messages.getCustomEmojiDocuments` для анализа/подстановки.

