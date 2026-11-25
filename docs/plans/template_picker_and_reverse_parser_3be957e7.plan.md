---
name: Template Picker and Reverse Parser
overview: Добавить удобный выбор большого списка шаблонов в редакторе постов и реализовать обратный парсер готового TG/VK-поста в общий набор полей шаблона, сохранив поддержку Telegram rich-text через текущий буферный форматтер.
todos:
  - id: model-meta
    content: Расширить ReleaseTemplate метаданными каталогизации пост-шаблонов и сериализацией JSON
    status: completed
  - id: editor-picker
    content: Добавить в TemplateEditor поиск + группировку + CRUD для пост-шаблонов и базовый список
    status: completed
  - id: reverse-parser
    content: Реализовать обратный парсер post->fields для полного общего набора placeholders
    status: completed
  - id: editor-integration
    content: Интегрировать действие вставки поста с извлечением полей и поддержкой Telegram pseudo-Markdown
    status: completed
  - id: build-verify
    content: Собрать проект по build-instructions и пройти ручные сценарии проверки
    status: completed
isProject: false
---

# План: выбор шаблонов и обратный парсинг постов

## Цели

- В `редакторе постов` добавить понятный выбор шаблонов: поиск + группировка, чтобы при большом количестве вариантов пользователь быстро находил нужный.
- Добавить `post -> fields` парсер для MVP по полному общему набору полей (TG/VK), где оформление отличается, а смысловые данные совпадают.
- Сохранить совместимость с Telegram-пастой (ссылки, custom emoji), используя текущую инфраструктуру pseudo-Markdown и `TelegramFormatter`.

## Что меняем

- UI/данные шаблонов:
  - Расширяем модель `ReleaseTemplate` метаданными для каталогизации пост-шаблонов (группа, подгруппа, тип площадки, метки) в [src/models/releasetemplate.h](src/models/releasetemplate.h) и сериализацию в [src/models/releasetemplate.cpp](src/models/releasetemplate.cpp).
  - В редакторе шаблона добавляем `каталог пост-шаблонов`: список шаблонов с поиском, группами и CRUD элементов (добавить/удалить/переименовать) в [src/ui/templateeditor.h](src/ui/templateeditor.h), [src/ui/templateeditor.cpp](src/ui/templateeditor.cpp), [ui/templateeditor.ui](ui/templateeditor.ui).
  - Дефолтный встроенный набор (базовые TG/VK/комментарии) инициализируем при создании нового шаблона в [src/ui/mainwindow.cpp](src/ui/mainwindow.cpp), по аналогии с текущим заполнением `postTemplates`.
- Обратный парсер:
  - Добавляем модуль обратного извлечения полей из готового поста (с учетом разных оформлений TG/VK) в `processing` рядом с генератором: [src/processing/postgenerator.h](src/processing/postgenerator.h), [src/processing/postgenerator.cpp](src/processing/postgenerator.cpp) или выделенный helper-файл.
  - Вход: текст поста (при необходимости сначала прогон через `fromTelegramClipboardToPseudoMarkdown`), выбранный пост-шаблон. Выход: map извлеченных полей + статус/диагностика пропусков.
  - Алгоритм MVP: компиляция шаблона в регулярное выражение по placeholders + нормализация пробелов/переносов + tolerant matching для блоков списка (`CAST_LIST`) и ссылочных секций.
- Интеграция в редактор:
  - В `TemplateEditor` добавляем действие `Вставить пост и извлечь поля`, которое парсит вставленный текст и заполняет соответствующие поля шаблона.
  - Для TG вставки используем существующий `TelegramPasteEdit`/`TelegramFormatter` для сохранения rich-содержимого в pseudo-Markdown перед извлечением.

## Набор полей для MVP (полный общий)

- `%SERIES_TITLE%`, `%EPISODE_NUMBER%`, `%TOTAL_EPISODES%`
- `%CAST_LIST%`
- `%DIRECTOR%`, `%SOUND_ENGINEER%`, `%SONG_ENGINEER%`, `%EPISODE_ENGINEER%`, `%RECORDING_ENGINEER%`
- `%SUB_AUTHOR%`, `%TIMING_AUTHOR%`, `%SIGNS_AUTHOR%`, `%TRANSLATION_EDITOR%`
- `%RELEASE_BUILDER%`
- `%LINK_ANILIB%`, `%LINK_ANIME365%`
- хештеги/хвостовые секции как необязательные извлекаемые блоки (не фейлят парсер при отсутствии)

## UX решения

- Левая панель списка пост-шаблонов: дерево/группы (`Площадка -> Категория -> Шаблон`) + строка поиска сверху.
- Быстрый фильтр по названию/тегам, сохранение последнего выбранного шаблона в сессии редактора.
- Четкие безопасные действия:
  - `Добавить шаблон` (из встроенной базы или пустой)
  - `Удалить` с подтверждением
  - `Восстановить базовые` (не перезаписывая пользовательские без явного подтверждения)

## Проверка и сборка

- Локально прогоняем сборку строго по инструкции из [build-instructions.mdc](.cursor/rules/build-instructions.mdc) (vcvars + CMake/Ninja).
- Ручные проверки:
  - поиск/группы работают при большом количестве шаблонов;
  - TG/VK посты из ваших примеров корректно вытягивают общий набор полей;
  - TG custom emoji/links не ломаются при вставке и повторном редактировании.

## Риски и оговорки

- Полностью универсальный NLP-парсинг «любого» ручного поста недостижим в MVP; делаем шаблонно-ориентированный tolerant parser с диагностикой нераспознанных блоков.
- Различия в нестрогом формате ручных постов (лишние строки/переименованные заголовки) покрываем таблицей синонимов заголовков и мягкими regex-правилами.

