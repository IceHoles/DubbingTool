---
name: Внедрение фиксов concat CFR и fade
overview: "Внедрить проверенные фиксы concat-рендера в авто и ручной режимы: исправить порядок фильтров seg2 для сохранения fade-in и безопасно нормализовать timestamps на final join через setts (только для CFR, без хардкода 3600), без смены входного файла в ручном режиме."
todos:
  - id: patch-auto-concat
    content: "Обновить workflowmanager: порядок vf для seg2 и setts в final join (только CFR, шаг из fps)."
    status: completed
  - id: patch-manual-concat
    content: "Обновить concattbrenderer: порядок vf для seg2 и setts в final join (только CFR, шаг из fps)."
    status: completed
  - id: verify-runtime
    content: Проверить авто и ручной прогон по ffprobe метрикам и seam-визуалу.
    status: cancelled
  - id: move-report-to-docs
    content: Создать docs/concat-cfr-debug-report.md и перенести проверенные выводы/команды.
    status: completed
isProject: false
---

# Внедрение фиксов concat CFR и fade

## Что меняем

- Внедрить два подтвержденных runtime-фикса в обоих режимах (авто + ручной):
  - порядок фильтров `seg2` (сначала `trim`, потом timeline для `subtitles`), чтобы не срезать начало `\fad`;
  - `-bsf:v setts` на final join при `-c:v copy`, чтобы убрать non-monotonic DTS/VFR-аномалии.
- Ограничение ручного режима:
  - источник для concat в ручном режиме не менять; всегда использовать файл, предоставленный пользователем.
- Цель по артефакту ручного режима:
  - устранить наблюдаемый скачок на втором шве за счет детерминированной логики overlap/timeline при текущем пользовательском входе (в т.ч. MKV).
- Правило безопасности по framerate:
  - применять `setts` только для **CFR**-источников;
  - шаг для `setts` считать из фактического fps (`r_frame_rate`), без хардкода `3600`;
  - для VFR не применять принудительный `N*...` (оставлять passthrough или отдельный fallback-режим).

## Файлы для правок

- Авто-режим: [c:\Users\icehole\git\DubbingTool\src\core\workflowmanager.cpp](c:\Users\icehole\git\DubbingTool\src\core\workflowmanager.cpp)
  - секция `concatRenderSegment2()` (формирование `vfParts`)
  - секция `concatJoinSegments()` (аргументы ffmpeg join)
- Ручной режим: [c:\Users\icehole\git\DubbingTool\src\processing\concattbrenderer.cpp](c:\Users\icehole\git\DubbingTool\src\processing\concattbrenderer.cpp)
  - секция `concatRenderSegment2()` (формирование `vfParts`)
  - секция `concatJoinSegments()` (аргументы ffmpeg join)
- Отчет: создать `docs/concat-cfr-debug-report.md` и перенести туда финальные выводы/команды из текущего отладочного отчета.

## Детали реализации

- В `seg2` собрать цепочку фильтров так, чтобы при наличии `bframeOverlap`:
  - сначала применялся `trim=start=<overlap>` + `setpts=PTS-STARTPTS`;
  - затем (если есть hardsub) выполнялся `setpts=PTS+<tbStart>/TB,subtitles=...,setpts=PTS-STARTPTS`.
- В final join добавить `-bsf:v setts=...` в обоих режимах рядом с `-c:v copy`, но:
  - только если вход распознан как CFR;
  - формулу `pts/dts` строить от `r_frame_rate` (например, через выражение `N*den/(num*TB)`), а не от фиксированного `3600`.
- Для VFR явно оставить текущий путь без `setts` и добавить лог причины.
- Логи/комментарии обновить минимально, чтобы отражали новый порядок и назначение `setts`.
- Для ручного режима:
  - не менять базовый вход (никакого переключения на source MP4);
  - выровнять расчеты overlap и subtitle-anchor в рамках текущего пользовательского входа.

## Проверка после внедрения

- Прогнать релевантный concat-кейс для авто и ручного режимов.
- Проверить результат через `ffprobe`:
  - `avg_frame_rate=25/1`, `r_frame_rate=25/1`;
  - packet durations только `0.040000` без `0.240000/0.000011`.
- Визуально проверить первый стык TB (fade-in больше не обрезан) и второй стык (без регрессии).
- Отдельно проверить ветки:
  - CFR 25 (текущий кейс),
  - CFR 23.976,
  - VFR (убеждаемся, что `setts` не включается).

## Документация в проекте

- Создать `docs/concat-cfr-debug-report.md` с:
  - проблемой и корнем (DTS + порядок фильтров),
  - финальными командами/аргументами,
  - результатами до/после,
  - кратким чеклистом валидации для следующих эпизодов.

