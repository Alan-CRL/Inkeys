# Research: Settings / config / i18n plumbing for UI3 status indicator

- **Query**: Where/how to add status-indicator settings (master toggle + per-type checkbox group), debug add/remove buttons, migrate the SuperTop indicator toggle, a `ShowSoftwareVersion` direct entry (same entry-animation rule as `ShowSpeedEraser`), config keys, silent-update config, i18n flow.
- **Scope**: internal
- **Date**: 2026-11-02

All paths are relative to repo root `D:\Project\Inkeys\Repo\Inkeys-draw`. Line numbers are from the `feature/state` worktree at the time of writing.

---

## 1. Settings page architecture (where things live)

### Files

| File | Role |
|---|---|
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.h` | Design-layer page API: `PageId`, `NavigationDestination`, `SettingOpenNavigation`, `*Draft`/`*Events` structs, `RenderGeneral/RenderSpeedEraser/...` decls |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp` | Pure page renderers (draw controls, mutate draft, return events; no side effects) |
| `Inkeys/Inkeys/UI/Setting/Setting.Controls.h/.cpp` | Row controls: `ToggleRow`, `ComboRow`, `CheckboxGroup`, `ButtonRow`, `ButtonGroupRow`, `NavigationRow`, `Notice`, `SettingRow` ... |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | The big render coroutine: builds drafts from config each frame, calls renderers, applies events (config write / runtime publish); legacy page `switch (contentPage)` |
| `Inkeys/Inkeys/UI/Setting/Setting.Shell.cpp` | Navigation pane entries (`Setting.Shell.cpp:148-159`) and caption route buttons (`RenderDebugButton` :92) |
| `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h` | `SettingOpenTarget`, `SettingOpenRequest`, `SessionState::RequestShow`, `ShouldPlaySettingEntryAnimation` |
| `Inkeys/Inkeys/UI/Setting/Setting.cppm` | Exported module API (`Show`, `ShowSpeedEraser`, `Hide`, ...) |

### PageId ↔ legacy tab enum

`Setting.Pages.h:13-18`:
```cpp
Home = 0, Language = 1, Build = 2, Configuration = 3, General = 4,
Draw = 5, Plugins = 8, Components = 9,
HotKeys = 10, Version = 11, Support = 12, Debug = 13, Experimental = 14, Feedback = 15, Performance = 16, Linkage = 17,
```
`Setting.cpp:2586-2611` (`settingTabEnum`): `tab1`=Home, `tab2`=General(4), `tab3`=Draw(5), `tab4`=Plugins(8), `tab6`=**Version(11)**, `tab8`=Support(12), `tab9`=**Debug(13)**, `tabExperimental`=14. Plugin sub-tabs `settingPlugInTabEnum { tabPlug1, tabPlug2, tabPlug3, tabSuperTop, tabPlugDDB }` (`Setting.cpp:2603-2610`).

General sub-sections: `enum class GeneralSection : int { Basic, Appearance, Interaction, Storage, Shortcuts };` (`Setting.Design.h:74`). Titles: Basic=`SettingsUI.Design.GeneralBasic` "基本", Appearance=`GeneralAppearance` "外观与动画", Interaction=`GeneralInteraction` "行为与安全" (`Setting.Pages.cpp:974-977`).

### Candidate locations for the two new rows (PRD R8: "same group")

Existing section layout of `RenderGeneral` (`Setting.Pages.cpp:969-1159`):
- **Basic** (980-1031): 操作模式 / 图标排列 / 开机自启和启动行为
- **Appearance** (1032-1096): 主题 / 透明度 / 动画 / 光源 / 缩放
- **Interaction** (1097-1132): 自动收起 / 窗口行为 / 安全与恢复
- **Shortcuts** (1133-1157)

Any of these accepts a new `SectionHeader(...)` + `ToggleRow` + `CheckboxGroup`. Observed facts relevant to choosing:
- Appearance is the section that already owns UI3 Bar visual prefs (theme, opacity, animation, edge lighting), and its i18n description is "动画、光影与主栏缩放" (`zh-CN.jsonc` `SettingsUI.Design.AppearanceDescription`).
- Interaction owns Bar behavior (auto-collapse, top-window, safety).
- The decision is a product/UX choice; no existing "status/notification" section exists anywhere in Settings (searched `SectionHeader` calls in `Setting.Pages.cpp` and `Setting.cpp`).

If placed in General, follow the GeneralDraft pattern (section 2). If instead placed in a non-General legacy page (e.g. Experimental/Plugins), follow the inline pattern used at `Setting.cpp:4466-4507` (Experimental) / `Setting.cpp:3843-3870` (SuperTop): local bool mirror + `ToggleRow` returning changed → write config → `QueueConfigWrite()`/`WriteSetting()`.

---

## 2. GeneralDraft / Events / persistence pattern

### Draft & events structs

`Setting.Pages.h:261-287` `struct GeneralDraft` — note the **first 12 members are positionally aggregate-initialized** in `Setting.cpp:3287-3294`:
```cpp
Inkeys::UI::Setting::Design::GeneralDraft draft{
    StartUp, BarZoom, Experimental.Inkeys3.EdgeLightingEnable,
    TopSleepTime, RightClickClose, RegularSetting.TeachingSafetyMode,
    Experimental.Inkeys3.StartupPreviewPreferenceState.ConfiguredEnabled(),
    Experimental.Inkeys3.AnimationEnable, Experimental.Inkeys3.AnimationSpeedRate,
    Experimental.Inkeys3.DynamicEdgeLighting,
    static_cast<int>(...StartupPlacement...),
    std::clamp(...UseTaskbarHeightAsScaleBase..., 0, 2) };
```
Later members have default member initializers and are assigned after construction (`Setting.cpp:3295-3318`, e.g. `draft.settingEntryAnimation = ...; draft.allowMultipleInstances = ...; draft.shortcuts[item] = ...`). New fields therefore should be appended with defaults (e.g. `bool statusIndicator = true; std::array<bool,3> statusIndicatorTypes{true,true,true};`) and assigned after construction.

`Setting.Pages.h:289-314` `struct GeneralEvents` — one `bool xxxChanged` per control; arrays for grouped controls (`std::array<bool,4> autoCollapseChanged{}`, `std::array<bool,6> shortcutsChanged{}`).

### Render (pure) → apply (side effects)

- Renderer: `GeneralEvents RenderGeneral(GeneralDraft& draft, GeneralSection section)` (`Setting.Pages.cpp:969`). Example toggle: `events.animationChanged = ToggleRow("animation", IA(...).c_str(), IA(...E).c_str(), "\ue768", draft.animation);` (1062-1063).
- Caller: `Setting.cpp:3341` `const auto events = Design::RenderGeneral(draft, destination.section);` then per-section apply blocks. Representative apply for a `main.json` key (`Setting.cpp:3500-3505`):
  ```cpp
  if (events.allowMultipleInstancesChanged)
  {
      Inkeys::config.Config.AllowMultipleInstances = draft.allowMultipleInstances;
      QueueConfigWrite();
  }
  ```
  For arrays (`Setting.cpp:3517-3536`): loop `if (events.autoCollapseChanged[item]) { *collapsePreferences[item] = draft.autoCollapse[item]; QueueConfigWrite(); }`; shortcuts additionally publish runtime (`KeyboardShortcuts::SetEnabled(mask)`) then `QueueConfigWrite()`.
  Bar-affecting changes call a Bar setter and/or `barUISet.UpdateRendering(false)` (e.g. icon order at `Setting.cpp:3509-3515`).
- `QueueConfigWrite()` (`Setting.cpp:1294-1299`) enqueues `SettingBusinessKind::WriteConfig` on the business FIFO under `configWriteQueueMutex` → `Inkeys::config.Write()` (main.json). `WriteSetting()` is the legacy deploy.json write (`QueueBusiness({ SettingBusinessKind::WriteSetting })` around `Setting.cpp:1285`).

### Multi-select checkbox control (笔速橡皮 "enabled devices")

Declaration `Setting.Controls.h:109-116`:
```cpp
struct CheckboxSpec { std::string label; bool* value; bool* changed; };
void CheckboxGroup(const char* id, const char* title, const char* glyph,
    std::initializer_list<CheckboxSpec> items);
```
Implementation `Setting.Controls.cpp:1376-1421`: one card, title + optional glyph header, divider, then 1 or 2 columns of `Checkbox` (2 columns when inner width ≥ max(440 DIP, 2×widest+gap)). Writes `*item.changed` per item; no return value.

Usage in speed eraser (`Setting.Pages.cpp:846-854`):
```cpp
CheckboxGroup("##eraser-devices", IA(speed.EnabledDevices).c_str(), "\ue7f8", {
    { entries[0], &draft.enabled[0], &events.enabledChanged[0] }, ... });
```
Also in palm eraser tools (`Setting.Pages.cpp:723-730`). Draft/events shapes: `SpeedEraserDraft::enabled` `std::array<bool,5>` + `SpeedEraserEvents::enabledChanged` `std::array<bool,5>` (`Setting.Pages.h:221-238`). Apply side: `Setting.cpp:3610-3625` loop over `events.enabledChanged[entry]` then single `QueueConfigWrite()`.

Pattern for conditionally showing a dependent row: speed eraser hides fine-control rows when master is off without overwriting values (`Setting.Pages.cpp:856-857`, comment "显示条件只跟随本帧开关与设备选择，隐藏时保留原有精细偏好"); same contract for Debug FrameRate row (spec `configuration-i18n-and-assets.md` "UI3 脏区调试" §3: hidden child must not overwrite persisted value).

---

## 3. 软件调试 (Debug) page

- Entry: caption bug button, not in nav list. `Setting.cpp:2760-2761`:
  `if (Design::RenderDebugButton(titleBarGeometry.debug, destination.page == Design::PageId::Debug, navigationState, ...)) navigateTo({ Design::PageId::Debug });` (`RenderDebugButton` in `Setting.Shell.cpp:92-95`).
- Page body: `case settingTabEnum::tab9:` `Setting.cpp:4531-4764`. Title `IA(I18nKey.SettingsUI.DebugSoftware.N)` ("调试软件"). Sections in order:
  1. `DebugSoftware.RenderingSection` "UI3 渲染调试": dirty-region `ToggleRow` (4535-4541) + conditional frame-rate row (4542-4549) → write `Inkeys::config...UI3.Debug.*`, `Inkeys::UI::Bar::SetDebugOptions(...)`, `QueueConfigWrite()`.
  2. `DebugSoftware.ConsoleSection` console toggles (4550-4573).
  3. `#if defined(DRAW3_RTS_DIAGNOSTICS)` speed-eraser diagnostics (4574-4686, raw Chinese strings, not i18n).
  4. `DebugSoftware.CanvasInformation` (4687-4693), 5. `DebugSoftware.DisplayInformation` EDID (4695-4762), `break;` at 4763.
- Button controls available: `bool ButtonRow(id, title, description, glyph, label, accent=false, note=nullptr)` returns pressed (`Setting.Controls.h:131-132`); `int ButtonGroupRow(id, title, description, glyph, {ButtonSpec{id,label,accent,disabled}...}, note)` returns pressed index or -1 (`Setting.Controls.h:133-141`). Example two-button row: `Setting.cpp:3026-3031` (`ButtonGroupRow("##update-actions", ..., { { "manual", manual }, { "check", check } })`).
- An "add random / remove one" pair fits `ButtonGroupRow` in a new `SectionHeader` (e.g. after RenderingSection). Side effects should go to the Bar owner via an exported Bar function (as with `Inkeys::UI::Bar::SetDebugOptions`), not touch Bar state from the settings render thread directly.
- i18n node: `SettingsUI.DebugSoftware` (`Inkeys/src/i18n/zh-CN.jsonc:1059-1076`).

---

## 4. 超级置顶 "indicator" toggle on the plugin page

- Plugin overview card: `Setting.cpp:3825-3830` (`renderPluginOverviewCard("super-top", ..., settingPlugInTabEnum::tabSuperTop, ...)`).
- SuperTop sub-page `case settingPlugInTabEnum::tabSuperTop:` `Setting.cpp:3843-3870`:
  - Real toggle `##super-top-enable` → `setlist.plugInSetting.superTop.enable = value; WriteSetting();` (3855-3863).
  - **Indicator row is display-only** (3864-3868):
    ```cpp
    // 指示器继续使用已有配置，只展示说明，不在此改写实际值。
    Design::SettingRow("##super-top-indicator",
        IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.Indicator).c_str(),
        IA(I18nKey.SettingsUI.PlugIn.SuperTop.Capability.IndicatorE).c_str(),
        "\ue841", 0.0F, 0.0F, [](const auto&) {});
    ```
    i18n: `SettingsUI/PlugIn/SuperTop/Capability/Indicator` "超级置顶指示器", `IndicatorE` "软件成功被超级置顶时，在主图标右上角显示一个大头针。" (`zh-CN.jsonc:916-917`).
- Backing config (legacy deploy.json): `SetListStruct::plugInSetting.superTop.indicator` `IdtAtomic<bool>` (`Inkeys/IdtConfiguration.h:144-151`); read `IdtConfiguration.cpp:401-402` (`PlugIn.SuperTop.Indicator`, bool-checked), written `IdtConfiguration.cpp:631`; default `true` at `IdtMain.cpp:2989` (enable default false at 2988).
- Only consumer is `IdtFloating.cpp:5577-5582` (draws pin `floating_icon[30]` when `hasSuperTop`), but `IdtFloating.cpp` is `<None Include>` in `Inkeys/Inkeys.vcxproj:1192` → **not compiled; no live consumer in UI3 Bar** (grep for `SuperTop|superTop` under `Inkeys/Inkeys/UI/Bar` only hits `Bar.Presentation.Test.cpp:639`).
- Runtime "is super-topped" signal: `IdtAtomic<bool> hasSuperTop` (`Inkeys/SuperTop/IdtSuperTop.cpp:340`, extern `IdtSuperTop.h:9`), set once at `IdtMain.cpp:2562` (`hasSuperTop = tokenReady && hasUiAccess(tok_self);`).
- Moving it: remove the `SettingRow` at `Setting.cpp:3864-3868` and render the SuperTop type as one checkbox in the new status-indicator group. Whether the SuperTop checkbox keeps persisting to `deploy.json PlugIn.SuperTop.Indicator` (preserves existing users' value) or moves to new main.json keys is a persistence decision — see §6 caveat from spec.

---

## 5. 软件版本 page and a `ShowSoftwareVersion` direct entry

### Version page
`case settingTabEnum::tab6:` `Setting.cpp:2976-3259`. Header `SettingsUI.Version.N`; first a status `Notice("##update-status", ...)` driven by `AutomaticUpdateState` (2980-3021) plus action rows; then version info notice (3049-3053), user info, repair, update (auto-update toggle 3156-3180), channel and architecture combos. Nav entry `Entry{ PageId::Version, I18nKey.SettingsUI.Version.N, "\ue946" }` (`Setting.Shell.cpp:159`); Build/CICD page counts as Version selection (`Setting.Shell.cpp:196`).

### How the eraser gear opens the speed-eraser page

1. Bar: `Bar.EraserAttribute.cpp:501` `else if(item==9)Inkeys::UI::Setting::ShowSpeedEraser();` (module import at line 16 `import Inkeys.UI.Setting;`).
2. Export: `Setting.cppm:24-25` `void ShowSpeedEraser();` → `Setting.cpp:5333` `void ShowSpeedEraser() { ShowSetting(SettingOpenTarget::SpeedEraser); }`.
3. `ShowSetting(target)` `Setting.cpp:5214-5224`: under `settingStateMutex` call `settingSessionState.RequestShow(target)`, then `settingBusinessQueue.Enqueue({ SettingBusinessKind::ShowWindow })` (HWND owner thread restores/activates) and `RenderPipeline::Request(Client::Settings)`.
4. `SessionState::RequestShow` `Setting.SessionState.h:272-278`: `opening = !visible_`, `SetVisible(true)` (bumps `showSerial_` only on real hidden→shown), publishes `openRequest_ = { serial+1, showSerial_, hideSerial_, target, opening }` atomically with Show/Hide identity. Target enum: `enum class SettingOpenTarget : std::uint8_t { Current, SpeedEraser };` (`Setting.SessionState.h:254`).
5. Render thread each frame (`Setting.cpp:2543-2575`): snapshot `hideSerial/showSerial/visible/openRequest` under lock; consume Hide reset first (`ConsumeSettingHomeReset`, 2558-2561); then
   ```cpp
   const auto openAction = openNavigation.Consume(openRequest, settingVisible, showSerial, hideSerial,
       Experimental.Inkeys3.AnimationEnable
       && Inkeys::UI::Setting::ShouldPlaySettingEntryAnimation(setlist.settingEntryAnimation, showSerial));
   ...
   if (openAction == Design::SettingOpenNavigation::Action::SpeedEraser)
   {
       // 在首帧正文排布前选中详情；开屏缓存从一开始就是目标页。
       navigateTo({ Design::PageId::Draw });
       Design::NavigateDrawingSubpage(navigationState, drawingSubpage, Design::DrawingSubpage::SpeedEraser);
       settingFrameSubmission.Reset();
   }
   ```
6. `SettingOpenNavigation::Consume` `Setting.Pages.h:187-206`: ignores already-consumed serial; rejects stale requests whose show/hide serial ≠ current or target ≠ SpeedEraser; sets `fadeOnly = request.opening && entryAllowed` (only a real hidden→shown open, and only if the entry policy allows). `Action` enum `{ None, Home, SpeedEraser }` (Home is declared but `Consume` never returns it).
7. Entry policy: `ShouldPlaySettingEntryAnimation(mode, showSerial)` `Setting.SessionState.h:99-105` (mode 0 off, 1 first real Show of process, 2 always; invalid → 1). Config `setlist.settingEntryAnimation` (deploy.json `SettingEntryAnimation`, `IdtConfiguration.cpp:546-548`).
8. Fade-only overlay: `Setting.cpp:2786-2791`:
   ```cpp
   Design::ConfigureHomeMotion(homeMotion, destination.page==Design::PageId::Home, showSerial,
       navigationDelta, ...AnimationEnable, ...AnimationSpeedRate,
       !blockContentInput && settingWindowActive..., ShouldPlaySettingEntryAnimation(...),
       openNavigation.fadeOnly && destination.page==Design::PageId::Draw
       && drawingSubpage==Design::DrawingSubpage::SpeedEraser);
   ```
   `ConfigureHomeMotion` (`Setting.Pages.h:137-160`) does `home |= fadeOnly`, so the overlay machinery runs on the target page; `HomeMotionState::fadeOnly` makes `EntryTotal() = WritingEnd + Dock` (`Setting.Pages.h:110-111`); `ResolveHomeEntryPose` fades the wordmark in place after writing (`Setting.Pages.cpp:236-241`), `ResolveHomeEntryLayout` skips the hero-fit check when `fadeOnly` (`Setting.Pages.cpp:519`), `RenderHomeEntryOverlay` multiplies wordmark alpha (`Setting.Pages.cpp:472-478`). If the user leaves the target page, the last arg becomes false → entry cancelled (headless test "leaving the target cancels the overlay").
9. Spec contract: `.trellis/spec/native-desktop/eraser-attributes.md:46` (same Show FIFO, target published with Show/Hide identity in the lock, consumed only by render thread; already-visible → navigate directly without Toggle/replay; hidden open → first frame already on target page, warm-up/capture from that page; when policy allows, writing overlay fades in place, no Home banner dock; animation off or first-open chance consumed → direct; Hide / newer ordinary Show / leaving the detail ends it). Full entry-animation contract: `.trellis/spec/native-desktop/rendering-and-ui.md` ~2300-2345 (HomeEntryTiming, preparation/capture rules).
10. Tests: `InkeysHeadlessTests/setting_design_tests.cpp:3055-3100` `CheckSettingOpenNavigation` (mode 0/1/2 fadeOnly, visible activation, newer Show cancels, Hide cancels, stale route, animation disabled, `ConfigureHomeMotion(... fadeOnly)` Pending, leaving target cancels); registered at `:6111`.

### Touch points for an analogous `ShowSoftwareVersion`
(description of what currently hard-codes SpeedEraser)
- `Setting.SessionState.h:254` enum `SettingOpenTarget` — only `Current, SpeedEraser`.
- `Setting.Pages.h:189` `Action` enum and `:199-204` `Consume` hard-codes `request.target != SettingOpenTarget::SpeedEraser` → `Action::SpeedEraser`.
- `Setting.cpp:2569-2575` action dispatch (would need a Version branch: `navigateTo({ Design::PageId::Version })` + `settingFrameSubmission.Reset()`).
- `Setting.cpp:2790-2791` fadeOnly predicate is tied to `PageId::Draw && DrawingSubpage::SpeedEraser`; a Version entry needs its own "still on target" predicate (e.g. `destination.page == PageId::Version`), and must remember which target was opened so leaving cancels.
- `Setting.cppm:22-25` export list + `Setting.cpp:5331-5333` wrapper (`void ShowSoftwareVersion() { ShowSetting(SettingOpenTarget::SoftwareVersion); }` style).
- Caller from Bar must go through the exported function (Bar input thread may not touch ImGui/window; comment at `Setting.cpp:5220`).
- Headless test `CheckSettingOpenNavigation` to extend.

---

## 6. Config persistence

### Two systems (spec `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:5-34`)
| System | File on disk | Declared | Read/Write |
|---|---|---|---|
| Legacy `SetListStruct setlist` | `globalPath + opt\deploy.json` | `Inkeys/IdtConfiguration.h:10-155` | `ReadSetting()` / `CaptureSettingJson()` + `WriteSetting()` in `Inkeys/IdtConfiguration.cpp` (manual JSON per key; defaults assigned in `IdtMain.cpp` ~2906-2990 before `ReadSetting`) |
| New `Inkeys::Config` (`Inkeys::config`) | `globalPath + Inkeys\Config\main.json` | `INKEYS_CONFIG_SCHEMA` macro `Inkeys/Inkeys/Other/Other.Config.cppm:370-518` | `config.ReadAll()` (`IdtMain.cpp:3041`), `config.Write()`; from Settings via `QueueConfigWrite()` |

Spec guidance (same file :28-34): fields already in `INKEYS_CONFIG_SCHEMA` keep using it; `SetListStruct` fields keep file compatibility; key rename/migration is a persistence-format change that needs explicit read/default/write/rollback design; "Codex 在答案明确前不应自行把字段迁到'看起来较新'的 module" (maintainer decision 【待确认】).

### Adding a new main.json key (one line per key)
Schema line form (`Other.Config.cppm:466-505`):
```cpp
GROUP(UI3, \
    ...
    GROUP(Debug, \
        X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, Enable, false) \
        X(ConfigUploadMode::NoUpload, "NaN", IdtAtomic<bool>, ShowFrameRate, true) \
    ) \
```
- JSON path = group nesting + member name, e.g. `Experimental.Inkeys3.UI3.Debug.ShowFrameRate`; C++ access `Inkeys::config.Experimental.Inkeys3.UI3.Debug.ShowFrameRate` (`IdtAtomic<bool>`, `.load()` / assignment).
- Default in the 5th X arg; missing key in old file → schema default automatically (spec "旧配置缺少 ShowFrameRate → 使用 schema 默认 true；不需要手写迁移").
- Comment in `Other.Config.cppm:510-519`: "新增项时，只需要在下面的 schema 里继续写这一处".
- Note `Other.Config.cppm:474` currently has three `X(...)` on one physical line (ThemeMode/BackgroundOpacity/ContentOpacity) separated by tabs — still valid macro text; be careful when editing nearby.
- Startup sync to Bar after `ReadAll()`: `IdtMain.cpp:3105-3135` (normalize → write back → `Inkeys::UI::Bar::SetThemeMode/SetResidentOpacity/SetAnimationOptions/SetEdgeLightingOptions/SetDebugOptions(...)`). Settings writes follow "update `Inkeys::config` → call Bar setter → `QueueConfigWrite()`" (Debug example `Setting.cpp:4535-4541`; spec §"UI3 脏区调试" 3).
- Settings-local mirrors of UI3 config are initialized once in the coroutine struct at `Setting.cpp:2440-2460` (e.g. `bool DebugMode = Inkeys::config.Experimental.Inkeys3.UI3.Debug.Enable;`) — pages that read `Inkeys::config` directly each frame (General draft, `Setting.cpp:3295-3318`) don't need a mirror.

### Adding a legacy deploy.json key
Three edits: member in `SetListStruct` (`IdtConfiguration.h`), default in `IdtMain.cpp` init block (~2906-2990), read with `isMember`+type check in `ReadSetting` (pattern `IdtConfiguration.cpp:398-403`), write in capture (`IdtConfiguration.cpp:629-632`).

### Silent update (静默更新)
- UI label `SettingsUI/Version/Update/AutoUpate` = "自动更新(静默)" (`zh-CN.jsonc:416`) — the "silent update" switch **is** the auto-update toggle.
- Storage: legacy `setlist.enableAutoUpdate` (`IdtConfiguration.h:15`), JSON `UpdateSetting.EnableAutoUpdate` in deploy.json; read `IdtConfiguration.cpp:323-328`, written `IdtConfiguration.cpp:536-541,588-589`; default `true` at `IdtMain.cpp:2913`. Guarded by `setlistUpdateMutex` (shared_mutex).
- Accessors: settings lambdas `GetEnableAutoUpdate/SetEnableAutoUpdate` (`Setting.cpp:1983-1992`); update module snapshot `GetUpdateTargetSnapshot().enableAutoUpdate` (`Inkeys/Inkeys/Net/Net.Update.cpp:41-47`), used at `Net.Update.cpp:521,645,687`.
- UI toggle: `Setting.cpp:3156-3180` (`##version-auto-update`).
- Update state & progress (for the R2 progress ring): `AutomaticUpdateStateEnum` (`Net.Update.cppm:53`, global `AutomaticUpdateState` `Net.Update.cpp:27`, states incl. `UpdateDownloading`, `UpdateRestart`, `UpdateNew`), `downloadNewProgramState.downloadedSize/fileSize` atomics (`Net.Update.cppm:96`); the Version page computes percent at `Setting.cpp:3006-3012`.

---

## 7. i18n

- Files: base `Inkeys/src/i18n/zh-CN.jsonc`; 10 others auto-enumerated: `de-DE en-US es-ES fr-FR ja-JP ko-KR pt-BR ru-RU zh-HK zh-TW` (11 total). Snapshot `Scripts/i18n.zh-CN.snapshot.jsonc`; generated header `Inkeys/IdtI18nKeys.g.h` (do not hand-edit).
- Workflow (header comment `Inkeys/IdtI18n.h:7-18`, spec `configuration-i18n-and-assets.md:303-318, 384-425`):
  1. Add keys to `zh-CN.jsonc`.
  2. `pwsh ./Scripts/i18n.ps1 sync` → syncs structure to all locales, inserts `[[Need translation: <zh text>]]` markers (`Scripts/i18n.ps1:17-18, 224-247, 266-290`), regenerates snapshot and `IdtI18nKeys.g.h`.
  3. Replace every marker with a real translation in all 10 non-base files (PRD requires all 11 languages; check rejects markers/empty except where the base itself is "").
  4. `pwsh ./Scripts/i18n.ps1 check` (key set, order, placeholder tokens, newline counts).
- Key → C++: each path segment becomes an identifier (`Convert-SegmentToIdentifier`, `i18n.ps1:401-415`; leading digit → `K_` prefix). Leaf: `const char* X = "A/B/X";`. Use `IA(I18nKey.SettingsUI.X.Y)` (std::string UTF-8) / `IW(...)` (wstring); macros `IdtI18n.h:20-21`. Don't name a segment after a Win32 macro (e.g. `MessageBox`) — spec :384-425.
- Naming conventions in existing nodes: `N` = title, `E` = description, `Note` = row note (e.g. `SettingsUI.Draw.Eraser.TouchArea.{N,E,Note}`); section headers as flat strings (e.g. `SettingsUI.Design.AnimationSection`, `SettingsUI.DebugSoftware.RenderingSection`).
- Relevant existing nodes: `SettingsUI.Design` (`zh-CN.jsonc:214-287`), `SettingsUI.Regular.Appearance` (510+), `SettingsUI.Version` (369+), `SettingsUI.PlugIn.SuperTop` (905-920), `SettingsUI.DebugSoftware` (1059-1076). Bar-side strings live under top-level `UI.Bar` (`zh-CN.jsonc:42+`, e.g. `UI.Bar.EraserAttributes`, `UI.Bar.MainButtons`).
- Formatted strings use `std::vformat(IA(key), std::make_format_args(...))` with `{}` tokens (e.g. `Setting.Pages.cpp:639-641`); spec warns check only validates token text/order, not format type.

## Caveats / Not Found

- No existing "status indicator" config, section, or i18n key exists; no UI3 Bar code reads `superTop.indicator`.
- Placement (which General section vs another page) and persistence home (main.json vs deploy.json for the migrated SuperTop indicator flag) are not dictated by code; the config spec explicitly marks system choice as maintainer-pending.
- `SettingOpenNavigation::Action::Home` exists but is unreachable from `Consume`; not relevant unless the enum is extended.
- Line numbers in `Setting.cpp` are large-file positions and will drift after edits.
