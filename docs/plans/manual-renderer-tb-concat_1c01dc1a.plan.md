---
name: manual-renderer-tb-concat
overview: Добавить в ручной рендер режим умного concat-рендера только блока ТБ, определяя его границы по предоставленным пользователем субтитрам, без использования шаблонов.
todos:
  - id: add-ui-checkbox
    content: Добавить чекбокс useConcatTb в ManualRenderWidget и передавать флаг в ManualRenderer через getParameters().
    status: completed
  - id: create-concat-helper
    content: Вынести логику renderMp4Concat и связанных шагов из WorkflowManager в общий helper ConcatTbRenderer с параметрами входного/выходного файла и сегмента ТБ.
    status: completed
  - id: tb-segment-detection
    content: Реализовать анализ ASS-файла для определения сегмента ТБ по всем Dialogue-строкам (start/end) и хелпер в AssProcessor.
    status: completed
  - id: manualrenderer-integration
    content: Интегрировать режим useConcatTb в ManualRenderer::start, используя ConcatTbRenderer и TbSegment вместо обычного полного рендера, с корректными fallback-ами.
    status: completed
  - id: internal-subs-support
    content: "Добавить поддержку режима concat для внутренних субтитров: временно извлекать выбранную дорожку в .ass и анализировать её для ТБ сегмента."
    status: completed
  - id: logging-and-tests
    content: Обновить сообщения логов для нового режима и прогнать тестовые сценарии с внешними и внутренними субтитрами, в том числе с ошибочными условиями для проверок fallback-а.
    status: completed
isProject: false
---

### Цели

- **Вручную указанный режим concat в ручном рендерере**: добавить чекбокс в `ManualRenderWidget`, который включает умный рендер только сегмента ТБ (как в авто-воркфлоу).
- **Опора на субтитры, а не шаблон**: определять границы ТБ по выбранной пользователем дорожке субтитров (внутренней или внешней), где в этом режиме все реплики считаются ТБ.
- **Повторное использование существующей логики concat**: по возможности переиспользовать части алгоритма `WorkflowManager::renderMp4Concat`, не ломая текущий авто-воркфлоу.

### Ключевые места в коде

- **Ручной рендер**:
  - Виджет: `[src/ui/manualrenderwidget.cpp](src/ui/manualrenderwidget.cpp)` и `[ui/manualrenderwidget.ui](ui/manualrenderwidget.ui)`
  - Логика рендера: `[src/processing/manualrenderer.cpp](src/processing/manualrenderer.cpp)`
- **Текущий concat-рендер ТБ в авто-режиме**:
  - Основной алгоритм: `WorkflowManager::renderMp4Concat` и связанные шаги в `[src/core/workflowmanager.cpp](src/core/workflowmanager.cpp)`
  - Поля для concat: `m_concatTbStartSeconds`, `m_concatTbEndSeconds` и т.п. в `[src/core/workflowmanager.h](src/core/workflowmanager.h)`
- **Работа с ТБ и ASS**:
  - Обработка субтитров и ТБ: `[src/processing/assprocessor.cpp](src/processing/assprocessor.cpp)` (уже есть логика генерации ТБ и подсчёта строк)

### План изменений

#### 1. Расширить UI ручного рендера

- **Добавить чекбокс в `ManualRenderWidget`**:
  - В `ui/manualrenderwidget.ui` под блоком параметров рендера (`groupBox_2`) добавить `QCheckBox` вроде:
    - Текст: «Умный рендер (concat) — перекодировать только ТБ».
  - В `ManualRenderWidget` завести поле `QCheckBox* concatTbCheckBox` через `ui`.
  - В конструкторе `ManualRenderWidget` при инициализации:
    - При необходимости отключать чекбокс, если hardsub выключен (низкий приоритет; можно просто разрешить только вместе с hardsub).
- **Параметры, которые отдаёт виджет**:
  - В `ManualRenderWidget::getParameters()` добавить флаг:
    - `params["useConcatTb"] = ui->concatTbCheckBox->isChecked();`
  - Этот флаг будет передаваться в `ManualRenderer` через `MainWindow::startManualRender()`.

#### 2. Передать новый флаг в ManualRenderer

- В `MainWindow::startManualRender()` (в `[src/ui/mainwindow.cpp](src/ui/mainwindow.cpp)`):
  - Уже создаётся `ManualRenderer` с `m_manualRenderWidget->getParameters()` — флаг `useConcatTb` автоматически попадёт в `ManualRenderer`.
- В `[src/processing/manualrenderer.h](src/processing/manualrenderer.h)` и `.cpp` ничего не нужно менять для конструктора, так как `QVariantMap` уже универсален.

#### 3. Выделить общий helper для concat-логики (по возможности)

- **Создать новый вспомогательный класс**, например `ConcatTbRenderer`, в новом файле `[src/processing/concattbrenderer.{h,cpp}](src/processing/concattbrenderer.cpp)`:
  - Ответственность:
    - по известным значениям `inputMkvPath`, `outputMp4Path`, `tbStartSeconds`, `tbEndSeconds`, `ProcessManager*`, `ffmpegPath`, `ffprobePath`, `resultPath` реализовать тот же ffmpeg/ffprobe-пайплайн, что и в `WorkflowManager::renderMp4Concat`/`concatCutSegment1`/`concatRenderSegment2` и т.п.
  - Публичный интерфейс (через сигналы/слоты или прямые методы):
    - `void start();`
    - сигналы: `logMessage`, `progressUpdated`, `finished` (аналогично другим воркерам).
  - Внутри перенести код из `WorkflowManager::renderMp4Concat()` и связанных методов, сведя зависимость от `WorkflowManager` к минимуму (использовать переданные поля вместо `m_template`, `m_paths` и т.д.).
- **Подключить helper в WorkflowManager**:
  - В `WorkflowManager::renderMp4Concat()` вместо прямой реализации алгоритма и последовательности шагов, по возможности перевести на использование `ConcatTbRenderer` (создать объект, подписаться на его сигналы, передать параметры).
  - Это позволит избежать дублирования логики и упростит добавление того же поведения в `ManualRenderer`.

#### 4. Получать границы ТБ из субтитров для ручного рендера

- **Новый helper для анализа ASS-файла** (предпочтительно в `AssProcessor`):
  - Добавить метод, например `TbSegment AssProcessor::detectTbSegmentFromFile(const QString& subsPath);`, который:
    - открывает указанный `.ass` файл;
    - парсит секцию `Dialogue:`;
    - собирает:
      - `tbStartSeconds` = минимальное время начала среди всех реплик;
      - `tbEndSeconds` = максимальное время конца среди всех реплик;
    - возвращает структуру `TbSegment { double start; double end; bool valid; }`.
  - В этом режиме (по ответу пользователя) **все строки файла считаются ТБ**, так что не нужно фильтровать по стилю: анализировать все `Dialogue`.
- **Источники субтитров для ручного рендера**:
  - Если пользователь выбрал режим `hardsub` + `external`:
    - взять путь из `m_params["externalSubsPath"]` и передать в `detectTbSegmentFromFile`.
  - Если выбран режим `hardsub` + `internal`:
    - уже сейчас для internal hardsub `ManualRenderer` временно переименовывает входной MKV и использует `subtitles=%SIGNS%` с `si=<index>`;
    - для анализа ТБ нужно сначала извлечь внутреннюю дорожку субтитров во временный `.ass` (через `m_processManager` + `mkvmerge` или `ffmpeg`) и затем вызвать `detectTbSegmentFromFile` на этом временном файле.
  - Если hardsub выключен или подходящих субтитров нет:
    - `useConcatTb` игнорировать и выполнять обычный полный рендер (с логом-предупреждением).

#### 5. Расширить ManualRenderer для concat-режима

- В `[src/processing/manualrenderer.cpp](src/processing/manualrenderer.cpp)`:
  - В `ManualRenderer::start()` после загрузки пресета и определения длительности файла:
    - если `m_params["useConcatTb"].toBool()`:
      - попытаться выяснить сегмент ТБ (как описано выше);
      - если сегмент валиден (start < end и в пределах длины файла):
        - **передать управление в новый helper `ConcatTbRenderer`** (используя `ProcessManager` текущего `ManualRenderer` или отдельный) и подписаться на его сигналы, чтобы ретранслировать `logMessage`/`progressUpdated`/`finished` наружу;
        - завершить `start()` (основная логика ручного рендера будет в helper-е).
      - если сегмент не удаётся определить, логировать предупреждение и выполнить стандартный полный рендер через существующую логику (`runPass`), чтобы не ломать сценарий пользователя.
  - При использовании concat-режима **игнорировать стандартные поля пресета, связанные с двухпроходным рендером**, и вместо этого использовать кодек/параметры по аналогии с `concatEncoderForCodec` (либо вынести и переиспользовать этот helper из `WorkflowManager`).

#### 6. UX и сообщения пользователю

- В логах `ManualRenderer` при включённом режиме `useConcatTb` писать понятные сообщения:
  - «Включён режим умного рендера ТБ (concat).»
  - «Анализ субтитров для определения границ ТБ...»
  - «Concat рендер: ТБ начало=... конец=..., используем N сегмента(ов).»
  - «Не удалось определить границы ТБ, выполняется полный рендер.»
- При невозможности извлечь или проанализировать внутреннюю дорожку субтитров показывать явное предупреждение, но не останавливать рендер целиком (fallback).

#### 7. Тестирование

- **Сценарии для проверки**:
  - Внешний `.ass` с одним непрерывным блоком ТБ: проверить, что отрезок совпадает с ожиданиями и concat работает.
  - Внешний `.ass` с несколькими блоками (если возможно): убедиться, что берётся общий min start / max end.
  - Внутренние субтитры (internal hardsub): проверить, что дорожка корректно извлекается во временный `.ass`, а затем используется для расчёта.
  - Падение ffprobe/ffmpeg/mkvmerge: убедиться, что приложение логирует проблему и переходит на полный рендер, не падая.

### Итог

После реализации пользователь в ручном рендерере сможет:

- выбрать входной MKV и субтитры (внутренние или внешний `.ass`);
- включить галочку «умный concat только ТБ»;
- получить рендер, где **только сегмент с ТБ перекодируется с хардсабом, а остальное видео копируется без потерь**, без участия шаблонов и ручного указания времени начала ТБ — всё берётся из самих субтитров.

