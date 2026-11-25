---
name: Debug concat CFR pacing
overview: Подготовить ручной, воспроизводимый протокол отладки ffmpeg-конкатенации, чтобы выявить источник VFR/джиттера на швах и подобрать команду, дающую стабильный CFR 25 fps без полного перекодирования.
todos:
  - id: collect-baseline-metrics
    content: Собрать baseline-метрики исходника и проблемного результата через mediainfo/ffprobe.
    status: completed
  - id: reproduce-manual-concat
    content: Воспроизвести вручную текущую схему seg1+seg2+seg3 и получить контрольный проблемный файл.
    status: completed
  - id: timestamp-root-cause
    content: Локализовать, на каком шаге появляется VFR или аномальные PTS/DTS.
    status: completed
  - id: test-cfr-hypotheses
    content: Прогнать матрицу флагов ffmpeg для стабилизации CFR без полного перекодирования.
    status: completed
  - id: freeze-winning-command
    content: Выбрать финальную рабочую команду и оформить короткий протокол валидации.
    status: completed
isProject: false
---

# План отладки CFR на склейках

## Контекст и точка входа

- Зафиксировать текущую логику пайплайна из правила: [c:\Users\icehole\git\DubbingToolcursor\rules\concat-render-current-state.mdc](c:\Users\icehole\git\DubbingTool.cursor\rules\concat-render-current-state.mdc).
- Работать только ручными командами `ffmpeg`, `ffprobe`, `mediainfo`; код проекта не трогать.

## Шаг 1: Подготовить минимальный воспроизводимый кейс

- Выбрать один проблемный исходник (где видно «пропуски» на стыках) и зафиксировать `TB start/end`.
- Сохранить baseline-метрики исходника (`mediainfo` + `ffprobe`) в отдельные текстовые логи: `avg_frame_rate`, `r_frame_rate`, `time_base`, `start_time`, `duration`, распределение `pkt_duration_time`.
- Снять кадровую «телеметрию» вокруг швов (по 2-3 сек до/после) через `ffprobe -show_frames` с `pkt_pts_time/best_effort_timestamp_time/pkt_duration_time/pict_type`.

## Шаг 2: Повторить текущую схему конката вручную

- Собрать `seg1 copy`, `seg2 encode+hardsub`, `seg3 copy` так же, как в текущем процессе.
- Сделать финальный join в двух вариантах и сравнить:
  - concat demuxer (`-f concat -c copy` где возможно);
  - concat filter (`[0:v][1:v][2:v]concat=...`) с перекодом только видео на финальном шаге (аудио — copy/прямой map).
- Для каждого варианта снять `mediainfo` и `ffprobe` и сравнить min/max frame rate, DTS/PTS монотонность, наличие дублей/дропов.

## Шаг 3: Локализовать источник VFR

- Проверить каждый сегмент отдельно: не появляется ли VFR уже в `seg2` после encode+hardsub.
- Проверить границы сегментов: совпадают ли `start/end` по keyframe и нет ли «дырок»/перекрытий во временной шкале.
- Прогнать join с диагностическими флагами логирования (`-loglevel debug`, `-debug_ts`) и найти предупреждения вида non-monotonous DTS, timestamp discontinuity.

## Шаг 4: Матрица гипотез (по одному фактору за прогон)

- Протестировать влияние timestamp-флагов на финальном шаге:
  - `-fflags +genpts`;
  - `-copyts` / без `-copyts`;
  - `-avoid_negative_ts make_zero`;
  - `-vsync cfr` (или эквивалент `-fps_mode cfr`) + `-r 25` на выходе.
- Протестировать MP4 timescale и muxer-параметры (`-video_track_timescale`, `-movflags +faststart` без влияния на кадропейсинг).
- Протестировать GOP-согласование для `seg2` (фиксированный keyint под 25 fps), чтобы стык не создавал аномальные длительности кадров.

## Шаг 5: Критерии успеха и выбор «рабочей» команды

- Успех: `Frame rate mode: Constant`, `Frame rate: 25.000`, адекватные min/max без экстремумов, отсутствие визуального джиттера на швах.
- Зафиксировать 1-2 команды-победителя:
  - «без полного перекодирования» (допустим лишь минимальный re-encode в узком месте);
  - «fallback с легким финальным перекодом видео» как безопасный вариант, если copy-join физически не удерживает CFR.
- Сформировать короткий протокол «проверка -> команда -> ожидаемый вывод» для быстрого повторения на других файлах.

## Когда переводить в Debug-режим

- Переводи меня в Debug сразу после того, как подготовишь один конкретный проблемный файл и точный диапазон ТБ (start/end).
- В Debug я дам готовый набор команд под твои пути (копипаст-блоками), затем будем итеративно запускать и анализировать логи до стабильного CFR 25.

