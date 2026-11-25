---
name: ExtractionPage rewrite plan
overview: "Переписать вкладку «Ручная разборка» с нуля: упростить модель и делегаты, исправить ресайз столбцов (формальные параметры в Qt 6), гарантировать отображение названий дорожек и разнести заголовок секции и кнопки."
todos: []
isProject: false
---

# План: переработка вкладки «Ручная разборка» (ExtractionPage)

## Диагностика текущих проблем

**1. Названия в первом столбце не отображаются**  
Делегат берёт данные по цепочке: `parent.parent.t` (Label → внутренний Row → внешний Row). Внешний Row имеет `property var t: parent.trackData`, а `trackData` привязан к `ExtractionController.tracks[trackIndex]`. При пересборке `displayModel` или при обновлении списка из C++ привязка может не пересчитываться, либо контекст `parent` в делегате даёт не тот объект. Плюс обращение по индексу к `ExtractionController.tracks` из делегата — хрупкая схема.

**2. «Выбрать все» и стрелка раскрытия сжаты**  
Один RowLayout: стрелка (28px) + название группы + spacer + две кнопки. При недостаточной ширине или приоритетах layout кнопки и стрелка сжимаются в одну зону. Высота строки 44px при плотном контенте тоже даёт эффект «всё в кучу».

**3. Ресайз столбцов не работает + предупреждение в консоли**  
В Qt 6 не рекомендуется неявная подстановка параметра `mouse` в обработчики сигналов. Нужно явно объявлять параметр, иначе возможны сбои и предупреждение:
`Parameter "mouse" is not declared. Injection of parameters into signal handlers is deprecated.`

В [ExtractionPage.qml](ui/qml/pages/ExtractionPage.qml) в 4 MouseArea для ресайза используются:

- `onPressed: { headerRow.dragStartX = mouse.x; ... }`
- `onPositionChanged: if (pressed) root.colExtract = ... mouse.x ...`

Нужно заменить на обработчики с формальным параметром, например:
`onPressed: (mouse) => { ... }`, `onPositionChanged: (mouse) => { ... }`.

---

## Предлагаемая архитектура (переписать с нуля)

### Идея

- **Одна плоская модель для списка** — каждый элемент либо «секция», либо «дорожка» с полным набором полей. Никакого доступа по индексу к `ExtractionController.tracks` внутри делегата.
- **Два отдельных делегата** — отдельно для строки-секции и для строки-дорожки, без вложенного `ItemDelegate` и сложной цепочки `parent`.
- **Ресайз столбцов** — один переиспользуемый компонент с корректными обработчиками `(mouse) => ...`.
- **Заголовок секции** — два ряда: первый — стрелка + название, второй — «Выбрать все» / «Снять все», чтобы ничего не сжималось.

### Модель данных (остаётся в QML)

- Источник данных по-прежнему [ExtractionController](src/core/extractioncontroller.h): `tracks` (QVariantList) и методы `setTrackChecked`, `setGroupChecked` и т.д. C++ не трогаем.
- При формировании `displayModel` для каждой **дорожки** класть в массив не только `isSection: false` и `trackIndex`, а **полную копию объекта дорожки** (все поля из `ExtractionController.tracks[i]`): `displayName`, `id`, `ext`, `lang`, `info`, `checked`, `isAttachment`, `attachmentName` и т.д. Так делегат строки дорожки будет использовать только `modelData` и не обращаться к синглтону по индексу.
- Структура элемента:
  - Секция: `{ isSection: true, group: "Видео" }`.
  - Дорожка: `{ isSection: false, trackIndex: i, displayName, id, ext, lang, info, checked, isAttachment, attachmentName }`

