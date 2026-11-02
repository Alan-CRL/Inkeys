# Additional component and DDB UI translations

## Delivered scope

Added the agreed **76** active-interface keys to each of:

- `Inkeys/src/i18n/zh-HK.jsonc`
- `Inkeys/src/i18n/ja-JP.jsonc`
- `Inkeys/src/i18n/ko-KR.jsonc`
- `Inkeys/src/i18n/pt-BR.jsonc`
- `Inkeys/src/i18n/ru-RU.jsonc`

Each file now contains **679** leaf strings. The key inventory consists of 37 component/settings/toolbar keys, 38 DesktopDrawpadBlocker controls and license keys, and DebugSoftware.DisplayInformation. Source inventories are `component-keys.json`, `ddb-keys.json` and `misc-ui-keys.json` in this research directory.

This substage changed only these five language files and task-local translation/validation notes and drafts. No C++, generated header, snapshot, other locale, sync, build or GUI operation was performed.

## Translation decisions

- Components use complete names in settings and compact toolbar labels. Examples: HK `檔案 / 工作管理 / 控制台`, JP `ファイル / タスク / 制御パネル`, KR `파일 / 작업 관리 / 제어판`, PT `Arquivos / Tarefas / Controle`, RU `Файлы / Задачи / Панель`.
- Native Chinese product identifiers are preserved, including 希沃白板3/5, 希沃轻白板, 希沃品课教师端, 希沃PPT小工具, 希沃课堂助手, 欧帝白板, 天喻教育云互动课堂, 畅言智慧课堂, C30智能教学 and 希沃桌面. Only descriptive UI/window wording is localized; these display strings are not matching-engine identifiers.
- ClassIsland, IslandCaller, SecRandom, NamePicker, AiClass, ClassIn X, DesktopDrawpadBlocker, DDB, URL and IPC retain their names. Requirements explicitly distinguish URL registration from IPC registration and keep the loaded-timetable prerequisite.
- The DDB license text retains all five lines: learning/exchange/research-only restriction, GPLv3/open-source statement, original GitHub repository URL, no quality warranty, and acceptance/responsibility for consequences. No source condition was removed or legally reinterpreted.
- Existing 603 values were preserved while merging the new source schema. In particular, Japanese PalmEraser.Introduction still begins `手のひらで画面に触れると` and was not reverted to the earlier wrong prefix.
- Japanese ClassIslandProfileLabel uses the compact `プロフ編集`, with the full settings title `ClassIsland プロファイル編集`, so it retains the profile-editor meaning without expanding to an overlong toolbar label.

## Verification

- Standard `Scripts/i18n.ps1 check`: all ten non-base locales **679/679 PASS** after the five additions.
- Merge validation checked unique inventory count, complete 679-key set/order against the final zh-CN schema, unchanged pre-existing values, exact format-token order, newline contract, no unfinished/invalid empty translations, correct root Language, UTF-8 without BOM and CRLF.
- The DDB license key has exactly four newline characters in all five locales, matching its five-line source.
- Actual glyph/width/consumer replay remains part of the main session's integrated C++ validation. Script completeness is not a claim that the new strings have already passed compiled rendering measurements.
