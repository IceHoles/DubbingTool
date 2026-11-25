---
name: finalize-manual-tb-concat
overview: "Завершить реализацию режима умного concat-рендера только ТБ в ручном рендерере: анализ ASS, полноценный ConcatTbRenderer и включение чекбокса useConcatTb."
todos:
  - id: tbsegment-type-and-detect
    content: Восстановить/создать TbSegment и реализовать AssProcessor::detectTbSegmentFromFile для анализа диапазона по всем Dialogue строкам .ass файла.
    status: completed
  - id: concat-helper-full
    content: "Доделать ConcatTbRenderer: поиск keyframe-ов, вырезка сегментов 1/2/3 и их склейка в финальный MP4 с учётом только видео-копирования вне ТБ."
    status: completed
  - id: manualrenderer-wiring
    content: Интегрировать ConcatTbRenderer в ManualRenderer::start при useConcatTb && useHardsub, с поддержкой external и internal субтитров и аккуратными fallback-ами.
    status: completed
  - id: ui-flag-enable
    content: Включить чтение useConcatTb из concatTbCheckBox в ManualRenderWidget::getParameters и при необходимости привязать его доступность к состоянию hardsubCheckBox.
    status: completed
  - id: concat-testing
    content: Прогнать ключевые сценарии (внешние/внутренние субтитры, ошибки ffprobe/mkvmerge/ffmpeg, пустые TБ) и убедиться, что concat и fallback работают как ожидается.
    status: completed
isProject: false
---

### Цели

- **Сделать рабочим режим concat-ТБ в ручном рендере**: при включённой галочке рендерить только сегмент с ТБ с перекодированием и склеивать его с копированными кусками до/после.
- **Опора только на субтитры**: границы сегмента брать из выбранной пользователем дорожки субтитров (внешний `.ass` или внутренняя дорожка) без использования шаблонов.
- **Минимизировать дублирование**: по возможности переиспользовать идеи и структуру из `WorkflowManager::renderMp4Concat`, но в изолированном helper-е.

### План работ

#### 1. Восстановить и доопределить тип TbSegment

- Создать (или восстановить) структуру `TbSegment` в новом общем заголовке, например `[src/processing/concattbrenderer.h](src/processing/concattbrenderer.h)`:
  - Поля: `double startSeconds`, `double endSeconds`, метод `bool isValid() const { return endSeconds > startSeconds; }`.
- Убедиться, что она доступна и для `ManualRenderer`, и для кода анализа ASS (можно подключать через `concattbrenderer.h` или вынести в отдельный небольшой заголовок, если потребуется).

#### 2. Реализовать AssProcessor::detectTbSegmentFromFile

- В `[src/processing/assprocessor.h](src/processing/assprocessor.h)` объявить:
  - `static TbSegment detectTbSegmentFromFile(const QString& assPath);`
- В `[src/processing/assprocessor.cpp](src/processing/assprocessor.cpp)` реализовать функцию:
  - Открыть `.ass` с `QTextStream` (UTF‑8), прочитать построчно.
  - Найти секцию `[Events]`, после неё обрабатывать только строки, начинающиеся с `Dialogue:`.
  - Для каждой такой строки:
    - Парсить `Start` и `End` (формат `h:mm:ss.zzz`) из стандартных полей (2‑я и 3‑я ячейка после `Dialogue:`).
    - Конвертировать в секунды (double).
    - Обновлять `segment.startSeconds` как минимум, `segment.endSeconds` как максимум.
  - В этом режиме **не фильтровать по стилю**: все строки `Dialogue` считаются частью блока ТБ.
  - Вернуть `TbSegment` (если ни одной валидной строки не найдено, `isValid()` вернёт false и станет сигналом для fallback-а).

#### 3. Доделать полноценный ConcatTbRenderer

- В уже созданных файлах `[src/processing/concattbrenderer.h](src/processing/concattbrenderer.h)` и `[src/processing/concattbrenderer.cpp](src/processing/concattbrenderer.cpp)`:
  - Убедиться, что конструктор принимает:
    - `inputMkvPath`, `outputMp4Path`, `TbSegment segment`, `qint64 sourceDurationS`, `ProcessManager*`.
  - В `start()`:
    - Проверять `segment.isValid()` и разумность границ (0 ≤ start < end ≤ sourceDurationS, с небольшим допуском);
    - Логировать включение режима concat‑ТБ и запускать `renderMp4Concat()`.
  - В `renderMp4Concat()`:
    - Вычислить `m_concatTbStartSeconds`/`m_concatTbEndSeconds` из `segment`.
    - Определить `m_concatSegmentCount` (2, если ТБ «упирается» в конец видео; иначе 3).
    - Вызвать `concatFindKeyframe()`.
  - В `concatFindKeyframe()` переиспользовать логику из `WorkflowManager::concatFindKeyframe`:
    - ffprobe по интервалу `[tbStart-15; tbStart]` для keyframe до ТБ → `m_concatKfBeforeTbStart`.
    - Если трёхсегментный режим — ffprobe по `[tbEnd; tbEnd+10]` для первого keyframe после ТБ → `m_concatKeyframeTime`.
    - При неудаче логировать и либо переходить в 2‑сегментный режим, либо отменять concat с fallback.
    - В конце вызывать `concatCutSegment1()`.
  - В `concatCutSegment1()`:
    - Собрать команду ffmpeg, вырезающую **видео‑только** сегмент от начала до `m_concatKfBeforeTbStart` в `concat_seg1.ts` c `-c:v copy`, `-an`, `-muxdelay 0`, `-muxpreload 0`.
    - Запустить через `m_processManager->startProcess` и отслеживать завершение (для этого `ConcatTbRenderer` должен быть подключён к сигналу `processFinished` от `ProcessManager` с собственным внутренним состоянием шагов).
  - В `concatRenderSegment2()`:
    - Собрать ffmpeg-команду, которая вырезает и перекодирует **сегмент 2 (ТБ)** в `concat_seg2.ts`:
      - `-ss <tbStart>` (относительно начала входного MKV или от keyframe до ТБ, по аналогии с `WorkflowManager`),
      - `-to <tbEnd>` или `-t <duration>`,
      - нужный видео‑кодек (`concatEncoderForCodec` → hevc_nvenc/libx265/др.), audio/прочие опции.
    - Обязательно включить тот же фильтр субтитров, что и при обычном ручном hardsub‑рендере (аналогично замене `%SIGNS%`/`%INPUT%`), либо взять уже полностью подготовленную команду из пресета и ограничить её `-ss/-to`.
  - В `concatCutSegment3()` (для трёхсегментного случая):
    - Вырезать видео сегмент от `m_concatKeyframeTime` до конца в `concat_seg3.ts` с `-c:v copy`, `-an`.
  - В `concatJoinSegments()`:
    - Собрать временный файл списка (`concat_list.txt`) из `concat_seg1.ts`, `concat_seg2.ts`, `concat_seg3.ts` (или двух сегментов) и вызвать ffmpeg `-f concat -safe 0 -i concat_list.txt -c copy` для получения финального MP4.
    - По завершении почистить временные `.ts`/список, и заэмитить `finished()`.
  - Во всех шагах ретранслировать лог и прогресс через сигналы `logMessage`/`progressUpdated`.

> Примечание: чтобы не усложнять, можно внутри `ConcatTbRenderer` держать собственное enum‑состояние шага и один слот обработки `processFinished`, аналогично паттерну в `WorkflowManager`.

#### 4. Интегрировать ConcatTbRenderer в ManualRenderer::start

- В `[src/processing/manualrenderer.cpp](src/processing/manualrenderer.cpp)` внутри `ManualRenderer::start()` после вычисления `m_sourceDurationS` и перед установкой `m_currentStep = Step::Pass1`:
  - Из параметров взять:
    - `bool useConcatTb = m_params.value("useConcatTb").toBool();`
    - `bool useHardsub = m_params.value("useHardsub").toBool();`
  - Если `useConcatTb && useHardsub`:
    - Определить, какой режим hardsub выбран:
      - `QString hardsubMode = m_params.value("hardsubMode").toString();`
    - Если `hardsubMode == "external"`:
      - взять `subsPath = m_params["externalSubsPath"].toString();`
      - вызвать `TbSegment segment = AssProcessor::detectTbSegmentFromFile(subsPath);`
    - Если `hardsubMode == "internal"`:
      - взять `subtitleTrackIndex` из `m_params["subtitleTrackIndex"]`;
      - временно извлечь дорожку в `.ass` через mkvmerge/ffmpeg:
        - например, `mkvmerge tracks input.mkv s:<index>:temp_internal_subs.ass`;
      - вызвать `detectTbSegmentFromFile` на этом временном `.ass` и сохранить путь для возможной отладки.
    - Если `segment.isValid()` и он находится в пределах длины ролика:
      - залогировать найденные границы ТБ;
      - создать `m_concatRenderer = new ConcatTbRenderer(inputMkv, outputMp4, segment, m_sourceDurationS, m_processManager, this);`
      - соединить сигналы `logMessage`, `progressUpdated`, `finished` `ConcatTbRenderer` с соответствующими сигналами/слотами `ManualRenderer`;
      - вызвать `m_concatRenderer->start();` и **сделать `return;`**, не заходя в стандартный `runPass`.
    - Если сегмент невалиден или что‑то пошло не так при извлечении:
      - логировать предупреждение и продолжить обычный полный рендер через `m_currentStep = Step::Pass1; runPass(m_currentStep);`.

#### 5. Включить передачу useConcatTb из UI

- В `[src/ui/manualrenderwidget.cpp](src/ui/manualrenderwidget.cpp)` в `ManualRenderWidget::getParameters()`:
  - изменить строку с `useConcatTb`, чтобы она реально читала состояние чекбокса:
    - `params["useConcatTb"] = ui->concatTbCheckBox->isChecked();`
- При необходимости в `updateHardsubOptions()` можно добавить UX‑логику:
  - если `hardsubCheckBox` снят — отключать `concatTbCheckBox` (и сбрасывать его), чтобы избежать непонятных комбинаций.

#### 6. Проверить и отладить

- **Внешний `.ass`‑файл**:
  - Заполнить его только строками ТБ, включить `useConcatTb` + hardsub с внешними субтитрами.
  - Убедиться, что:
    - в логах видно обнаруженный интервал ТБ;
    - итоговый ролик получен через concat (два/три сегмента);
    - до/после ТБ видео скопировано, а не перекодировано (по скорости/качеству).
- **Внутренняя дорожка субтитров**:
  - Выбрать internal hardsub, включить `useConcatTb`.
  - Проверить, что внутренняя дорожка извлекается во временный `.ass`, по нему находится сегмент ТБ и concat‑рендер отрабатывает корректно.
- **Fallback-сценарии**:
  - Невалидные или пустые субтитры (нет `Dialogue`): проверить, что concat отключается и выполняется обычный рендер с понятным предупреждением.
  - Ошибки ffprobe/mkvmerge/ffmpeg: убедиться, что приложение не падает и логирует причины, а затем рендерит целиком.

После этого галочка в ручном рендерере действительно будет включать режим: «перекодировать только сегмент с ТБ по субтитрам, остальное копировать», полностью в соответствии с приложенным планом.