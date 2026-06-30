# Changelog

## v0.11.0

- Ребрендинг проекта: основное имя приложения изменено на `AM Graph Viewer`, а release-файл теперь называется `AMGraphViewer-v0.11.0-win-x64.exe`.
- Rebranded the project so the main app name is now `AM Graph Viewer`, and the release artifact is now `AMGraphViewer-v0.11.0-win-x64.exe`.
- Обновлены build/release-скрипты, ссылки в документации и пользовательские заголовки.
- Updated the build/release scripts, documentation links, and user-facing window titles.

## v0.10.7

- Исправлены подписи в верхнем меню: пункты общих настроек, горячих клавиш, линий и вертикального панорамирования больше не обрезаются.
- Fixed top-menu labels so general settings, hotkeys, line tools, and vertical panning are shown in full.
- Добавлены глобальные подписи осей X/Y, которые отображаются в углах графика и настраиваются отдельно в окне настроек.
- Added global X/Y axis labels rendered in the graph corners, with dedicated settings fields.
- Настройка отображения измерительных значений теперь хранится отдельно для каждой группы точек.
- Measurement read-outs are now stored separately for each point group.
- Улучшена обработка `.lvm`: имена каналов из чередующихся таблиц сохраняются корректнее, а секции времени восстанавливаются точнее.
- Improved `.lvm` parsing: channel names from interleaved tables are preserved more reliably, and section timing is reconstructed more accurately.
- Окна горячих клавиш и выбора диапазона подстроены под размеры экрана и не блокируют основное приложение.
- The hotkeys dialog and range-selection prompt now fit the screen better and no longer block the main app.

## v0.10.5

- Доработана работа с пропущенными промежутками: подписи внутри графика убраны, сами разрывы по-прежнему выделяются, а подробная информация теперь открывается отдельным окном по клику.
- Окно горячих клавиш больше не блокирует основное приложение: оно открывается в пределах рабочей области экрана, при нехватке высоты использует прокрутку и не мешает дальнейшей работе с главным окном.
- Упрощена сборка проекта: версия приложения теперь передаётся через define, а generated-файл `build_version.hpp` удалён из репозитория.
- Reworked gap handling: inline labels are removed from the plot, the highlighted gaps remain visible, and detailed information is now shown in a separate dialog on click.
- The hotkeys window no longer blocks the main app: it stays within the monitor work area, uses scrolling when vertical space is limited, and keeps the rest of the UI interactive.
- Simplified the build pipeline by passing the app version through a define and removing the generated `build_version.hpp` file from the repository.

## v0.10.4

- Переработан welcome-экран: стартовая страница теперь устойчиво адаптируется к разным размерам окна, получила переключатели темы и более согласованную компоновку.
- Light mode и правая рабочая панель доработаны для больших файлов: выбор временного диапазона, скрытие каналов по умолчанию, более аккуратные отступы, единый стиль переключателей и исправления текстовых артефактов при ресайзе.
- Улучшено редактирование в интерфейсе: ввод имени канала завершается по клику вне поля, текстовые поля больше не перехватывают горячие клавиши, а компактная легенда каналов зачёркивает только название скрытого канала.
- Исправлена обработка много-секционных `.lvm` с повреждёнными временными блоками: время секций теперь восстанавливается по `Date` / `Time` / `X0`, а на больших разрывах времени график больше не рисует ложные прямые линии.
- Reworked the welcome screen so the start page adapts cleanly across window sizes, includes theme switching, and keeps a more consistent layout.
- Refined Light mode and the right-side work panel for large files with explicit time-range selection, channels hidden by default, better spacing, unified toggle styling, and resize artefact fixes.
- Improved in-app editing: channel rename closes on outside click, text inputs no longer trigger global hotkeys, and the compact channel legend now strikes through only the hidden channel name.
- Fixed multi-section `.lvm` handling for corrupted time blocks by rebuilding section timing from `Date` / `Time` / `X0`, while large time gaps now break the plot instead of drawing misleading straight lines.

## v0.10.3

- Исправлено обрезание текста Light mode на приветственном экране и в настройках.
- Подсказка Light mode теперь помещается целиком и не подрезается по нижней границе.
- Обновлена версия релиза для текущего набора UI-исправлений.
- Fixed Light mode text clipping on the welcome screen and in Settings.
- The Light mode hint now fits fully and no longer gets clipped at the bottom.
- Bumped the release version for the current UI polish pass.

## v0.10.2

- Добавлен `undo/redo` для изменений в настройках каналов и точек: видимость, переименование, цвета, коэффициенты и параметры отображения точек теперь откатываются через `Ctrl+Z` / `Ctrl+Shift+Z`.
- Added `undo/redo` for channel and point setting changes: visibility, renaming, colours, coefficients, and point display options can now be reverted with `Ctrl+Z` / `Ctrl+Shift+Z`.
- Исправлено некорректное отображение текста на кнопках правой панели после изменения размера окна: подписи больше не вылезают за границы соседних кнопок.
- Fixed button text clipping in the right-side panel after window resizing so labels no longer spill into neighbouring controls.
- Упрощена панель точек: укорочены подписи переключателей, убрана лишняя подсказка под списком каналов, добавлена явная кнопка смены цвета выбранного канала.
- Streamlined the side panels: shorter point toggle labels, the extra channel hint removed, and an explicit colour picker button for the selected channel.
- Привязка точек и маркеров к графику теперь выбирает ближайшую видимую точку по экранному расстоянию, а не только по оси X.
- Point and marker snapping now chooses the nearest visible point by on-screen distance instead of snapping by X only.

## v0.10.1

- Убраны все добавленные подсказки по формулам из правой рабочей панели.
- Removed the newly added formula hints from the right-side work panel.
- Все пользовательские упоминания формул в панели преобразований переименованы в коэффициенты.
- Reworded the transform panel UI so user-facing formula labels are now presented as coefficients.
- Добавлена отдельная кнопка для сброса всех локальных коэффициентов каналов без затрагивания общего коэффициента.
- Added a separate action to reset all per-channel local coefficients without changing the global coefficient.

## v0.10.0

- Добавлена формульная система преобразования сигналов: общая формула для всех графиков и отдельная формула для выбранного канала.
- Added formula-based signal transforms with a shared formula for all charts and a separate formula for the selected channel.
- В правой рабочей панели появились подсказки по формулам, объяснение переменной `x` и примеры выражений.
- Added inline formula help, an explanation for the `x` variable, and ready-to-use expression examples in the right-side work panel.
- Пункт `About` теперь открывает стартовый welcome-экран, а сам экран показывает версию текущей сборки.
- The About action now opens the start welcome screen, which also shows the current build version.

## v0.10.0-rc1

- Добавлена встроенная правая рабочая панель с вкладками для каналов и точек.
- Added a docked right-side work panel with separate tabs for channels and points.
- В панель каналов вынесены быстрые коэффициенты: общий множитель, общее слагаемое и коэффициенты выбранного канала.
- The channel panel now exposes quick transform coefficients: global multiplier, global offset, and per-channel coefficients for the selected channel.
- В панель точек добавлено переименование групп, а отдельное окно настроек очищено от дублирующих блоков каналов и точек.
- Point groups can now be renamed from the panel, while the separate settings window has been reduced to non-duplicated general settings and hotkeys.

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
