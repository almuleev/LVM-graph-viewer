# Changelog

## v0.9.3

- Исправлено поведение выделенного диапазона при приближении: индикация больше не пропадает, если текущий экран смещён относительно выделенного окна.
- Fixed selected-range behaviour during zooming: the visual indication no longer disappears when the current viewport moves away from the selected window.
- Штриховка и затемнение теперь подчёркивают именно невыделенную область, а внутри выделенного диапазона оставлена только мягкая подсветка без лишнего визуального шума.
- Hatch shading now emphasizes the non-selected area, while the selected range keeps only a soft tint without extra visual noise.

## v0.9.2

- Добавлено ручное задание точных значений для вертикальных и горизонтальных линий.
- Added manual input for exact vertical and horizontal guide line values.
- Выбор диапазона через `Shift` стал заметнее: остальные участки графика приглушены штриховкой, а маркеры диапазона остаются видимыми.
- Shift-based range selection is now easier to read: the rest of the graph is muted with a subtle hatch, and the range handles stay visible.

## v0.9.1

- Added real application screenshots to the GitHub presentation.
- Promoted the dark main workspace and measurement-group workflow as the primary README visuals.
- Cleaned up screenshot assets so the repository keeps only the selected UI images.

## v0.9.0

- Переработано оформление GitHub-страницы: новый баннер, более аккуратная документация и визуальные превью.
- Reworked the GitHub presentation with a new banner, cleaner documentation, and visual previews.
- Добавлены базовые элементы публичного проекта: лицензия MIT, шаблоны для issues и более аккуратная структура репозитория.
- Added public-project essentials: MIT license, issue templates, and cleaner repository hygiene.
- Добавлены независимые группы точек измерения с отдельными цветами и видимостью.
- Introduced independent measurement point groups with separate colours and visibility.
- Обновлено окно настроек: управление цветом активной группы, цветом выбранной группы и видимостью групп.
- Updated the settings window to manage active point colour, selected group colour, and group visibility.
- Улучшены undo/redo для точек и отображение состояния точек.
- Improved point-related undo/redo and point status reporting.

## v0.8.3

- Восстановлено взаимодействие в окне настроек после прошлых UI-рефакторингов.
- Restored settings interactions after earlier UI refactors.
- Доработано поведение меню и настроек.
- Refined menu and settings behaviour.
- Продолжена полировка темы и согласованности интерфейса.
- Continued polishing theme and interface consistency.

## v0.8.1

- Fixed mojibake in Russian interface labels.
- Refined active button visuals and unified `Auto zoom` naming.
- Clamped edge axis labels so they stay inside the chart area.
- Simplified playback speed selection to direct numeric input.

## v0.8.0

- Rebuilt the welcome screen into a cleaner start page.
- Added a unified settings window for language, hotkeys, markers and measurement options.
- Moved channel renaming into the channel list.
- Added signal transform controls with global and per-channel multiplier/offset.

## v0.5.1

- Fixed strict monotonic time rebuild behaviour.
- Corrected DC/Nyquist FFT edge-bin scaling.
- Improved CLI validation for `--fft-samples`.

## v0.5.0

- Added drag and drop support.
- Added channel renaming.
- Added start/end navigation shortcuts.
- Added vertical panning support.
