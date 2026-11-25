---
name: project-context-bootstrap
overview: Собрать контекст по прошлым чатам и состоянию репозитория DubbingTool, проверить актуальность билд-инструкций и зафиксировать стартовую точку для дальнейшей работы.
todos:
  - id: confirm-build-instructions
    content: Подтвердить, что build-instructions.mdc совпадает с актуальным CMake/Qt окружением и CI
    status: completed
  - id: summarize-ci-context
    content: Свести в голове (и объяснить пользователю) текущее состояние CI, clang-tidy и format-check по транскрипту f5d22e...
    status: completed
  - id: sync-with-main-head
    content: Убедиться, что текущая ветка совпадает по коммиту с origin/main и различия только в локальных изменениях
    status: completed
  - id: align-next-steps
    content: Согласовать с пользователем, с какой задачи начинать после онбординга (QML фронтенд, багфиксы, CI и т.п.)
    status: completed
isProject: false
---

## Цели

- **Поднять контекст прошлых сессий** (CI, clang-tidy, ветки, сборка) из agent-transcripts, чтобы не повторять работу.
- **Проверить актуальность билд-инструкций** в `.cursor/rules/build-instructions.mdc` относительно текущего состояния репозитория и CI.
- **Синхронизироваться с `main`**: понять, где находится текущая ветка и какие есть локальные незакоммиченные изменения.
- **Осмыслить последние изменения** по `CHANGELOG.md`, чтобы от них дальше отталкиваться.

## Используемый контекст

- **Транскрипт про CI/clang-tidy и форматирование** — `[CI and clang-tidy cleanup](f5d22efd-fb20-4b8c-891b-d7d18fadc8ed)`
  - Настроен CMake-билд, clang-tidy и format-check.
  - В CI:
    - `format-check` использует `pip install clang-format` (v21.1.8).
    - `clang-tidy` использует LLVM 21 через `KyleMayes/install-llvm-action@v2`.
    - В `CMakeLists.txt` закручены `--warnings-as-errors` для `bugprone-*` и `clang-analyzer-*` за вычетом известных фолс-позитивов.
  - В коде уже исправлена пачка `bugprone-*` и narrowing-конверсий в `assprocessor.cpp`, `manualassembler.cpp`, `manualrenderer.cpp`, `workflowmanager.cpp`, `workflowmanager.h`, `manualassemblywidget.cpp`, `setupwizarddialog.cpp`.
- **Краткий транскрипт про ветку `telegram-formatter` и CMake** — `[Telegram formatter CMake work](8b1346b8-b041-448a-8c63-44d4473e85b9)`
  - Ветка старого форматора была адаптирована/собрана через CMake по образцу `main`.
- **Текущая ветка и состояние git**
  - Активная ветка: `qt_qml_rework`.
  - `git diff --stat origin/main...HEAD` пустой → `qt_qml_rework` по коммиту совпадает с `origin/main`; отличия только в незакоммиченных локальных изменениях.
- **Билд-инструкции** — `[c:\Users\icehole\git\DubbingTool\.cursor\rules\build-instructions.mdc](c:\Users\icehole\git\DubbingTool\.cursor\rules\build-instructions.mdc)`
  - Описывают CMake+Ninja билд под Qt 6.10.2 и VS 2026 Build Tools, плюс `windeployqt`.
  - Совпадают с тем, что реально использует CI (CMake-билд, без qmake, Qt6).
- **CHANGELOG** — `[c:\Users\icehole\git\DubbingTool\CHANGELOG.md](c:\Users\icehole\git\DubbingTool\CHANGELOG.md)`
  - Последний релиз `1.1.0` от 2026-02-05:
    - Добавлен CMake-билд, clang-tidy, clangd, VSCode-настройки, TODO/CHANGELOG.
    - Перестроена структура проекта (`src/core`, `src/processing`, `src/ui`, `src/models`, `ui/`, `resources/`).
    - Убран `DubbingTool.pro` (qmake).

## План дальнейшей работы (когда выйдем из Plan Mode)

- **Не создавать пока новые файлы в `.cursor`**
  - По твоему ответу, достаточно устного/одиночного плана без дополнительных RULE/AGENTS файлов.
  - При желании позже можно будет добавить краткий `RULE.md` с выжимкой по сборке и CI.
- **Опора на уже настроенный CI и билд**
  - При любых правках (QML фронтенд, багфиксы, новые фичи) держаться текущего пайплайна: CMake + clang-tidy + format-check.
  - Не менять версии clang/LLVM без особой необходимости, т.к. сейчас локальное окружение и CI синхронизированы на LLVM 21.1.8.
- **Перед началом конкретных задач**
  - Уточнить у тебя, с чего именно стартуем дальше: продолжение QML-переписывания, новые фичи, дальнейший CI-тюнинг или что-то ещё.
  - При старте новой задачи — при необходимости точечно перечитать нужные части транскрипта `[CI and clang-tidy cleanup](f5d22efd-fb20-4b8c-891b-d7d18fadc8ed)` (например, если будем трогать workflows или CMake).

