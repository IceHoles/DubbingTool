# Архив планов архитектуры и фичей (Cursor Plan Archive)

В этом каталоге собраны все планы и проектные решения, выработанные в процессе проектирования и разработки DubbingTool:

## Автоматизация и пакетный режим
- `batch_episodes_rollout_8888b13e.plan.md` — проектирование пакетной обработки серий.

## Интерфейс и миграция на QML Fluent UI
- `qml-fluent-ui-auto-mode-first_693ba781.plan.md` — первичная миграция вкладки автоматического режима на QML.
- `qml-manual-extraction-tab_a468a2d2.plan.md` — план портирования ручной разборки на QML.
- `extractionpage_rewrite_plan_2ecacc82.plan.md` / `extractionpage_rewrite_plan_c1ff3604.plan.md` — переработка страницы извлечения.
- `intro_outro_timing_ui_acbc996c.plan.md` — интерфейс разметки таймингов интро/аутро.
- `mpv_viewfinder_integration_598e9766.plan.md` — интеграция просмотрщика на базе mpv.

## Склейка, CFR и рендеринг видео
- `concat-cfr-debug-report.md` (в `docs/`) — подробный отчёт по швам и `setts`.
- `debug_concat_cfr_pacing_1020a1fa.plan.md` — исследование заиканий и пэйсинга кадров CFR.
- `внедрение_фиксов_concat_cfr_и_fade_f535d5a6.plan.md` — сохранение эффекта `\fad` при склейке.
- `finalize-manual-tb-concat_42ec7016.plan.md` — финализация ручной склейки ТБ.
- `manual-renderer-tb-concat_1c01dc1a.plan.md` — интеграция `ConcatTbRenderer` и `ManualRenderer`.
- `fix_concat_splice_artifacts_...` / `fix_concat_splice_glitches_...` — серия итераций устранения артефактов на стыках.

## Форматирование Telegram и шаблоны
- `telegram-clipboard-to-template-markdown_2d4288bd.plan.md` — обратный парсер из буфера обмена Telegram в псевдо-markdown.
- `template_picker_and_reverse_parser_3be957e7.plan.md` — селектор шаблонов и парсинг постов.

## Тестирование, CI и инфраструктура
- `testing_strategy_rollout_c72db364.plan.md` — стратегия внедрения тестов и покрытия кода.
- `ci_workflow_review_359e8bd9.plan.md` — ревизия GitHub Actions workflow.
- `custom_project_directory_1d1ef803.plan.md` — поддержка произвольного каталога проекта.
- `chapters_integration_clarifications_a2ddddaf.plan.md` — интеграция глав MP4/MKV.
