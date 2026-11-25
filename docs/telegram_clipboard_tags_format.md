## Формат буфера обмена Telegram: `application/x-td-field-tags`

Этот бинарный блок лежит в буфере обмена рядом с `application/x-td-field-text` и описывает разметку текста (жирный, ссылки, цитаты, custom emoji и т.п.).

Ниже — формат, который использует текущий проект.

---

### 1. Общий вид структуры

Все целые числа — **big-endian** (`>I`, беззнаковые `uint32`).

```
u32 tag_count;

repeat tag_count times:
    u32 position;        // Позиция в "чистом" тексте
    u32 length;          // Длина участка в "чистом" тексте
    u32 utf16_byte_len;  // Размер строки-тега в байтах
    u8  utf16_data[utf16_byte_len]; // UTF‑16 BE строка с метаданными
```

После `tag_count` сразу идут записи тегов, без выравнивания и паддинга.

---

### 2. Семантика полей

- **position**  
  Индекс начала размеченного диапазона в строке `cleanText` (той, что кладётся в `application/x-td-field-text`).  
  Это индекс `QString` (по сути — позиция в последовательности UTF‑16 код-юнитов).

- **length**  
  Длина размеченного диапазона, в тех же единицах, что и `position`.

- **utf16_byte_len**  
  Размер поля `utf16_data` в байтах. Должен быть кратен 2, так как строка в UTF‑16 BE.

- **utf16_data**  
  Строка в кодировке **UTF‑16 BE**. В коде она строится как:

  ```cpp
  QByteArray tagData;
  for (const QChar &ch : tagContent) {
      ushort u = ch.unicode();
      tagData.append(static_cast<char>(u >> 8));
      tagData.append(static_cast<char>(u & 0xFF));
  }
  ```

  Где `tagContent` — строка с «типами» разметки, объединившимися по бэкслешу.

---

### 3. Формат строки тега (`tagContent`)

`tagContent` — это один или несколько тегов, объединённых **символом `\`**:

Примеры:

- Просто жирный текст:

  ```text
  "**"
  ```

- Жирный + custom emoji:

  ```text
  "**\custom-emoji://5278229754099540071?10"
  ```

- Жирный + ссылка:

  ```text
  "**\https://example.com"
  ```

Типичные значения отдельных «подтегов»:

- `**`, `__`, `~~`, `||`, `^^`, `` ` `` — обычные markdown‑подобные стили.
- `>`, `>^` — цитаты разных видов.
- `link` представляется как URL в самом тексте; в теге остаётся сам URL.
- **Custom emoji**:

  ```text
  custom-emoji://<document_id>?<size>
  ```

  где:

  - `<document_id>` — `int64` из Telegram (тот самый `document_id` для `messageEntityCustomEmoji`, см. [документацию Telegram](https://core.telegram.org/api/custom-emoji)).
  - `<size>` — число (например, `10`, `11`, `30`) — используемый размер/масштаб emoji.

При наличии нескольких эффектов над одним участком строки (например, жирный + custom emoji) они объединяются:

```text
"**\custom-emoji://5278229754099540071?10"
```

---

### 4. Как распарсить файл (псевдокод)

#### Общее чтение структуры

```cpp
struct TagRecord {
    quint32 position;
    quint32 length;
    QString tagContent; // уже декодированный UTF‑16 BE
};

QVector<TagRecord> parseTags(const QByteArray &bytes) {
    QVector<TagRecord> result;
    if (bytes.size() < 4) return result;

    const uchar *p = reinterpret_cast<const uchar*>(bytes.constData());
    int offset = 0;

    auto readU32 = [&](quint32 &out) {
        if (offset + 4 > bytes.size()) return false;
        out = (quint32(p[offset])   << 24) |
              (quint32(p[offset+1]) << 16) |
              (quint32(p[offset+2]) << 8)  |
              (quint32(p[offset+3]));
        offset += 4;
        return true;
    };

    quint32 count = 0;
    if (!readU32(count)) return result;

    for (quint32 i = 0; i < count; ++i) {
        quint32 pos, len, sz;
        if (!readU32(pos) || !readU32(len) || !readU32(sz)) break;
        if (offset + int(sz) > bytes.size()) break;

        QByteArray utf16 = bytes.mid(offset, sz);
        offset += sz;

        QString tagContent = QString::fromUtf16(
            reinterpret_cast<const char16_t*>(utf16.constData()),
            utf16.size() / 2
        );

        result.append({pos, len, tagContent});
    }
    return result;
}
```

#### Достаём `document_id` custom emoji

```cpp
QStringList extractCustomEmojiIds(const QVector<TagRecord> &tags) {
    QStringList ids;
    QSet<QString> seen;

    for (const auto &tag : tags) {
        const QStringList parts = tag.tagContent.split('\\');
        for (const QString &part : parts) {
            static const QString prefix = QStringLiteral("custom-emoji://");
            if (!part.startsWith(prefix))
                continue;

            QString core = part.mid(prefix.size());
            int q = core.indexOf('?');
            if (q >= 0)
                core.truncate(q);

            if (!core.isEmpty() && !seen.contains(core)) {
                seen.insert(core);
                ids.append(core); // это и есть document_id
            }
        }
    }
    return ids;
}
```

---

### 5. Связь с Telegram API

Полученные таким образом `document_id` можно использовать в Telegram API (см. [Custom emojis](https://core.telegram.org/api/custom-emoji)):

- метод `messages.getCustomEmojiDocuments` принимает список `document_id: Vector<long>` и возвращает документы с `documentAttributeCustomEmoji`, где лежит:
  - текстовый `alt` (обычный emoji),
  - флаг `free`,
  - связанный `stickerset`,
  - флаг `text_color` и т.д.

Таким образом:

1. Из буфера обмена читаем `application/x-td-field-text` и `application/x-td-field-tags`.
2. По формату выше восстанавливаем список диапазонов и строк `tagContent`.
3. По `custom-emoji://...` вытаскиваем `document_id`.
4. По `document_id` через `messages.getCustomEmojiDocuments` достаём полную информацию о кастомных эмодзи.

---

### 6. Вставка в DubbingTool (Qt)

Стандартный `QPlainTextEdit` при вставке из Telegram обычно берёт только `text/plain` (или HTML) и **не** читает `application/x-td-field-text` / `application/x-td-field-tags`, поэтому моноширинный фрагмент теряет обёртку `` `...` `` — в «чистом» тексте нет обратных кавычек, а стиль был только в бинарных тегах.

В проекте для полей, куда имеет смысл вставлять текст, скопированный из Telegram Desktop, используется `TelegramPasteEdit` (`insertFromMimeData` → `TelegramFormatter::fromTelegramClipboardToPseudoMarkdown`). Для участка с тегом `` ` `` в `tagContent` форматтер восстанавливает псевдо-markdown `` `текст` ``.

Если новое поле должно принимать такие вставки, в `.ui` у виджета должен быть класс `TelegramPasteEdit` (как у постов Telegram и VK в редакторе шаблона), а не обычный `QPlainTextEdit`.

