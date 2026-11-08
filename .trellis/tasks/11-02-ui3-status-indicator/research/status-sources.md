# Research: UI3 status indicator — status sources (PPT slideshow / SuperTop / update) and icon glyphs

- **Query**: For each of the three status providers, find the authoritative state, the thread that changes it, and the cleanest hook for publishing "appeared / removed / progress". Also cover old Inkeys badges/icons, PowerPoint brand color, and the icon font/glyphs.
- **Scope**: internal (code + git history), plus a local cmap probe of the bundled font
- **Date**: 2026-11-02

## Specs read

- `.trellis/spec/ppt-interop/index.md`
- `.trellis/spec/ppt-interop/native-session-ui3.md`: each slideshow has its own session revision. Bar/PPT stable projection reads the coordinator **Committed** workspace. `PublishSession(session, authoritativeShowActive, hwnd)` is the lifecycle edge, and hiding the page controls is not the same as ending the show (lines 101-107, 170-173).
- `.trellis/spec/native-desktop/single-instance-and-startup-handoff.md`: SuperTop three-stage launch and update/restart handoff.
- `.trellis/spec/native-desktop/bar-theme-material.md`: the main button uses a single `logo1` SVG and resident surface opacity.
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:307-316`: edit `zh-CN.jsonc`, then run `Scripts/i18n.ps1 sync`. Never hand-edit `IdtI18nKeys.g.h`.

---

## 1. PPT slideshow (放映模式)

### Authoritative state and edges

| Layer | Location | Meaning |
|---|---|---|
| Native session token | `Inkeys/IdtPlug-in.cpp:108-120` (`pptSessionMutex`, `currentPptSession`, `CapturePptSession()`); `PptSessionActive()` at `:936-939` | `PptSessionToken.active` plus `localSession` id. This is the business truth. |
| **Start edge** | `Inkeys/IdtPlug-in.cpp:1173-1199` inside `PptInfo()` | New `localSession` is assigned (`++localSessionSequence`). Then `Inkeys::UI::Ppt::PublishSession(session.localSession, true, ppt_show)` (`:1195`), `Freeze::SetPresentationActive(true)` and `barUISet.UpdateRendering()`. `ppt_software` is set to `L"WPS"` or `L"PowerPoint"` at `:1189-1191`. |
| Same-session window upgrade (not a new start) | `:1200-1218`, `:1219-1225` | `PublishSession(session, true, hwnd/nullptr)` again with the same session id. This only re-anchors the window and must not count as a new appearance. |
| **End edge** | `EndSession` lambda `Inkeys/IdtPlug-in.cpp:985-1008` | `currentPptSession.active=false`, then `PublishSession(localSession, false, nullptr)` (`:998`), `Freeze::SetPresentationActive(false)`, `UpdateRendering()`. It is also called at the end of `PptInfo()` (`:1490`, on shutdown) and before every new session (`:1176`). |
| UI fan-out | `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:416-462` `Ppt::PublishSession` | Drops stale sessions (`session < positions.session`, `:425`). Only real begin/end edges set `changed`, and it then calls `Inkeys::UI::Bar::PublishPptSession(session, active, showWindow)` (`:461`). |
| Bar background fact | `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp:424-442` `PublishPptSession` | Under `pptSceneMutex` it updates `pptPresentationActive` (atomic), `pptPresentationWindow` and `pptSceneState.Publish(session, active)`, then calls `barUISet.UpdateRendering(...)` when something changed. Getter: `PptPresentationActive()` at `:413-416`. The edge is consumed by `ConsumePptSceneTransition()` (`:444-450`), which the render loop calls at `Bar.RenderLoop.cpp:13417`. |
| Stable workspace projection | `Inkeys/IdtState.h:140-158` `WorkspaceScene{Desktop,Presentation,Whiteboard}`, `GetWorkspaceSceneSnapshot()` (`IdtState.cpp:1337`) | `committed == Presentation` is what stable chrome uses (`Bar.Main.cpp:470-475`, `Bar.RenderLoop.cpp:13420-13422`). While the whiteboard is active, committed is Whiteboard even if a show is still running. The background fact keeps updating (spec `native-session-ui3.md:173`). |
| Existing Bar consumer | `Bar.Button.cpp:965-978` → `ResolveBarA2Projection(presentation, ...)` (`Bar.A2.h:46-60`) | The EndShow button is shown when `PptPresentationActive()`. This is an existing example of a Bar element driven by the slideshow fact. |

### Threads

- `PPTLinkageMain` runs on a `jthread` started at `Inkeys/IdtMain.cpp:3900`. It spawns `thread infoThread(PptInfo)` at `IdtPlug-in.cpp:1529`.
- All start/end edges run on the **`PptInfo` thread**. It polls every 16 ms while a session is active and every 100 ms when idle (`:1481-1487`).
- `Bar::PublishPptSession` is thread-safe because it uses atomics plus `pptSceneMutex`. Rendering is woken through `UpdateRendering`.

### Hook candidates (existing call sites only)

- `Inkeys::UI::Bar::PublishPptSession(session, active, hwnd)` (`Bar.Main.cpp:424`) is the one Bar entry point that already sees the deduplicated begin/end edges and the session id. Same-session re-anchors reach it as `changed=false` with `windowChanged=true`.
- Alternative: read `PptPresentationActive()` or the committed `WorkspaceScene` at render time (polling style, like A2/EndShow does).
- For a session-scoped status: the `localSession` id is available on both edges.

### Old Inkeys "放映标志" (slideshow badge)

- The legacy floating bar is in `Inkeys/IdtFloating.cpp`. It is **not compiled** (vcxproj `None` item, see native-desktop index). Its main-button badge is at `IdtFloating.cpp:5315-5321` (SkinMode 1), `:5332-5338` (SkinMode 2) and `:5564-5570`:
  ```cpp
  if (PptInfoState.TotalPage != -1) {
      if (ppt_software == L"PowerPoint") hiex::TransparentImage(&background, x + 38, y + 67, &floating_icon[16], 255);
      else if (ppt_software == L"WPS")   hiex::TransparentImage(&background, x + 38, y + 67, &floating_icon[20], 255);
  }
  ```
  The badge is 20×20 at the main-button ellipse offset (+38,+67), which is the lower area of the main button.
- The resources were removed in commit `66595757` ("chore(ui): retire legacy resources…"). Before that, `Inkeys.rc` had `ICON16 PNG "src\\ms_ppt.png"`, `ICON20 PNG "src\\wps_wpp.png"` and `ICON21 PNG "src\\pin.png"`. Loading was at `git show 66595757^:Inkeys/IdtFloating.cpp` lines 869-874.
- Colors sampled from the old PNGs (via `git show 66595757^:Inkeys/src/...`):
  - `ms_ppt.png` (40×40): a white disc (#F8F8F8) with a **Microsoft 365-style purple/blue gradient hexagon** (#7050A0, #4078D0, #48B0E8, #7878D8). It is not the orange PowerPoint "P".
  - `wps_wpp.png` (40×40): white disc with WPS red-orange (#F82018 / #F83808 / #F84008).
- **PowerPoint brand color: not found.** None of the usual PowerPoint oranges (#C43E1C, #D24726, #B7472A, #ED6C47, #D35230) appear in `Inkeys/`, `PptCOM/` or the specs. The only old "PowerPoint" visual is the M365-gradient PNG above.

---

## 2. Super top-most (超级置顶) plugin

### State

| Item | Location | Notes |
|---|---|---|
| Config "enable" | `setlist.plugInSetting.superTop.enable` (`IdtAtomic<bool>`, `Inkeys/IdtConfiguration.h:144-151`). JSON `PlugIn.SuperTop.Enable` in `opt/deploy.json` (`IdtConfiguration.cpp:400`, write `:630`) | Default `false` (`IdtMain.cpp:2988`). Forced to `false` and written on `SuperTopFailSignal` (`IdtMain.cpp:3188-3192`). |
| Config "indicator" (提示 toggle) | `setlist.plugInSetting.superTop.indicator` (`IdtConfiguration.h:149`). JSON `PlugIn.SuperTop.Indicator` (`IdtConfiguration.cpp:402`, write `:631`) | Default `true` (`IdtMain.cpp:2989`). **No consumer in compiled code.** The only reader was the legacy `IdtFloating` (see below). |
| **Is it currently active** | `IdtAtomic<bool> hasSuperTop` (`Inkeys/SuperTop/IdtSuperTop.cpp:340`, extern in `IdtSuperTop.h:9`) | Set **once** in `wWinMain`: `hasSuperTop = tokenReady && hasUiAccess(tok_self);` (`IdtMain.cpp:2562`). This happens after the SuperTop relaunch branch (`:2410-2560`) and before product startup. It never changes at runtime. |
| Enable toggle effect | `Setting.cpp:3859-3863` | Writes `setlist...enable` and `WriteSetting()`. It takes effect only after a restart (i18n `SuperTopNote` says "重启软件后生效"). |

- Thread: the main thread during startup. After that the value is a constant fact, so the indicator is effectively "present from startup" or never present. There is no runtime enable/disable edge.

### Settings UI (current)

- SuperTop plugin page: `Inkeys/Inkeys/UI/Setting/Setting.cpp:3843-3869`.
  - The Enable toggle uses glyph `"\ue718"` (Pin) (`:3856-3863`). Its description switches on `hasSuperTop` between `SuperTopE1` ("当前已经超级置顶") and `SuperTopE2` ("当前未超级置顶").
  - The Indicator row (`:3864-3868`) is a static `Design::SettingRow` with glyph `"\ue841"` (PinnedFill). It has **no toggle**. A comment says "指示器继续使用已有配置，只展示说明，不在此改写实际值".
- i18n (`Inkeys/src/i18n/zh-CN.jsonc:905-921`): `Indicator`: "超级置顶指示器", `IndicatorE`: "软件成功被超级置顶时，在主图标右上角显示一个大头针。"

### Inkeys2-era pin (大头针)

- Legacy drawing, from `git show 66595757^:Inkeys/IdtFloating.cpp` around line 5602-5607:
  ```cpp
  // SuperTop 指示器
  if (setlist.plugInSetting.superTop.indicator)
      if (hasSuperTop)
          hiex::TransparentImage(&background, x + 70, y, &floating_icon[30], Sign1.transparency);
  ```
  The image was loaded via `idtLoadImage(&floating_icon[30], L"PNG", L"icon21"); // SuperTop` and `icon21` was `src\pin.png`.
- `pin.png` is 24×30, a light-blue pushpin (#C0D0E8 / #C8D8F0 / #D0D8F0) drawn at the **top-right** of the main button. It was removed in `66595757`.

---

## 3. Update / new version

### Files

| File | Role |
|---|---|
| `Inkeys/Inkeys/Net/Net.Update.cppm` | `AutomaticUpdateStateEnum` (`:55-69`), `extern AutomaticUpdateState` (`:68`), `DownloadNewProgramStateClass{atomic downloadedSize, fileSize}` + `downloadNewProgramState` (`:71-96`), `mandatoryUpdate`, `downloadLine`, `AutomaticUpdate()` |
| `Inkeys/Inkeys/Net/Net.Update.cpp` | State definition `AutomaticUpdateStateEnum AutomaticUpdateState;` (`:27`, a **plain non-atomic global**). Loop in `AutomaticUpdate()` (`:600-791`). `DownloadNewProgram()` (`:346-595`) |
| `Inkeys/Inkeys/Net/Net.Update.Download.cpp` | `DownloadEdition(...)` (`:174-230`) writes progress |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp` | Version page status and percentage (`:2976-3060`), auto-update toggle (`:3195-3220`) |
| `Inkeys/IdtMain.cpp:2263+` | Applies `installer\update.json` at **next startup** |

### State machine (`AutomaticUpdateStateEnum`)

`0 NotStarted, 1 ObtainInformation, 2 InformationFail, 3 InformationDamage, 4 InformationUnStandardized, 5 Downloading, 6 DownloadFail, 7 DownloadDamage, 8 UpdateRestart, 9 Latest, 10 Newer, 11 UpdateNew`

- Detection: `GetEditionInfo(channel, arch)`, then compare `editionInfo.editionDate > editionDate` (`Net.Update.cpp:627-645`, `:755-760`).
- **Silent update (静默更新)** is `setlist.enableAutoUpdate`, JSON `UpdateSetting.EnableAutoUpdate`, default `true` (`IdtMain.cpp:2913`). It is read under `setlistUpdateMutex` via `GetUpdateTargetSnapshot()` (`Net.Update.cpp:44-48`). Setting label: `SettingsUI.Version.Update.AutoUpate` = "自动更新(静默)" (`zh-CN.jsonc:416`). Toggle at `Setting.cpp:3195-3220`.
- Branches:
  - Auto update **off** and newer version found: `AutomaticUpdateState = UpdateNew` (`:757`). Nothing is downloaded. i18n reads "发现软件新版本".
  - Auto update **on** (or `mandatoryUpdate`):
    - If `installer\update.json` is already staged and verified, it goes straight to `UpdateRestart` (`:691-694`).
    - Otherwise `UpdateDownloading` (`:704`, `:712`) loops over the download lines (`downloadLine = i+1`). It ends with `DownloadNewProgram(...)`, which returns `UpdateRestart` on success (`:595`), or `UpdateDownloadFail`/`UpdateDownloadDamage`. It returns `UpdateNew` if auto update was turned off mid-download (`:521-525`).
  - `UpdateRestart` means **downloaded, verified (md5+sha256), staged as `installer\new_procedure_*.exe` plus `installer\update.json`, and pending install on next launch**. i18n: "重启软件更新到最新版本".
  - `mandatoryUpdate` (set by "更新到最新版本" / repair) calls `RestartProgram()` right after staging (`:729-733`).
- Re-check: when the cycle finishes it sleeps 1800 s on success, or 10 s on failure, before the next round (`:762-782`). The Settings UI forces a re-check by writing `AutomaticUpdateState = UpdateObtainInformation` (e.g. `Setting.cpp:3040, 3049, 3053, 3081`). The loop polls that each second.

### Progress

- Bytes: `DownloadEdition` → `writeChunk` → `downloadedSize.fetch_add(written)` (`Net.Update.Download.cpp:208`). It resets to 0 at `:195` and again on the HTTPS→HTTP fallback (`:219`), so progress can go **backwards** within one line. `DownloadNewProgram` resets `downloadedSize=0` and sets `fileSize = editionInfo.fileSize` (package bytes) before each line (`Net.Update.cpp:412-413`).
- Percentage formula already in use (`Setting.cpp:3003-3010`):
  ```cpp
  if (size > 0.0 && downloaded <= size) status += format(" {:.1f}%", downloaded / size * 100.0);
  ```
  The status string `"新版本正极速下载中（线路{}）"` is formatted with `downloadLine`.
- After the bytes finish there is a non-byte phase (unzip, hashing, file moves) while the state is still `UpdateDownloading`, until `UpdateRestart`.

### Threads

- `AutomaticUpdate()` runs on a **detached `std::thread`**. It is started at `IdtMain.cpp:3337` (`#ifdef IDT_RELEASE` only, so Debug builds stay at `UpdateNotStarted`), or from the Settings business worker on `SettingBusinessKind::StartAutomaticUpdate` (`Setting.cpp:1203-1205`, queued by `QueueAutomaticUpdate()` at `:1356`).
- Both progress and state are written on that update thread. The httplib `client.Get` content receiver is synchronous in the caller (`Net.Update.Download.cpp:59-77`).
- The Settings render thread also **writes** `AutomaticUpdateState` (see above). The enum itself is not atomic. Only `downloadedSize`/`fileSize` are `atomic_ullong`.
- There is no event/callback hook today. Every consumer polls the global (Settings reads it each frame).

### Legacy indicator

- The old floating bar drew a red dot `RGBA(228,55,66,255)`, 10×10, at the "选项/Options" icon when `AutomaticUpdateState == UpdateRestart` (`Inkeys/IdtFloating.cpp:5200-5201`, not compiled).

---

## 4. Icon fonts and glyphs

### What renders where

| Surface | Icon mechanism | Evidence |
|---|---|---|
| UI3 Bar / main button | **SVG resources** of type `UI` (e.g. `LOGO1`, `BARENDSHOW`, `BARSETTING`, ...) loaded into `BarUiSVGClass` (`Bar.UI.cppm:309-326`, `InitializationFromResource`/`TransitionToResource`). Enum `BarUISetSvgEnum` at `Bar.Main.cppm:416+` | `Inkeys/Inkeys.rc:268-339`. Files are in `Inkeys/src/UI/*.svg`. There is no pin, download or slideshow SVG today, only `barEndShow.svg`. |
| Bar/PageControl DWrite text | Private font collection built in `IdtMain.cpp:3365-3382`: HarmonyOS SC/TC (+Bold), Noto JP/KR | **Segoe Fluent Icons (`IDR_TTF2`) is NOT in the DWrite collection.** A glyph-based Bar icon would need the font added there, or an SVG. |
| Settings (ImGui) | `IDR_TTF2 TTF "src\\ttf\\Segoe Fluent Icons.ttf"` (`Inkeys.rc:151`, id 257) merged into text faces (`Setting.cpp:1913-1923`) and the standalone `ImFontDesignIcons` (`:1929-1932`, `ImFluent::SetIconFont`). `IDR_TTF4` = `icomoon.ttf` is also merged. | Glyphs are passed as UTF-8 literals like `"\ue718"` |

### Codepoints already used in Settings (relevant)

- `"\ue718"` Pin: SuperTop enable (`Setting.cpp:3858`)
- `"\ue841"` PinnedFill: SuperTop indicator row (`:3867`)
- `"\ue895"` Sync: all update rows/buttons (`:3027, 3036, 3045, 3055, 3078, 3198`)
- `"\ue768"` Play: used 4 times

### Candidate glyphs (present in the bundled font's cmap, checked locally)

Presence was checked by parsing the cmap of `Inkeys/src/ttf/Segoe Fluent Icons.ttf`. The font's `post` table is format 3 and has no glyph names. The names below come from Microsoft's Segoe Fluent Icons reference (https://learn.microsoft.com/windows/apps/design/style/segoe-fluent-icons-font) and should be checked visually before use.

| Purpose | Codepoint | Name (MS docs) |
|---|---|---|
| Slideshow | U+E786 | SlideShow |
| Slideshow (alt) | U+E768 / U+F5B0 | Play / PlaySolid |
| Pin | U+E718 | Pin (already SuperTop) |
| Pin (alt) | U+E840 / U+E841 | Pinned / PinnedFill |
| Download / new version | U+E896 | Download |
| Download (alt) | U+EBD3 | CloudDownload |
| Update (existing convention) | U+E895 | Sync |

All of E718, E840, E841, E77A, E896, E118, EBD3, E895, E786, E173, E768, F5B0 returned **present**.

## Caveats / Not Found

- The PowerPoint brand color is not defined anywhere in the repo. The old "PowerPoint" badge PNG is an M365-style purple/blue gradient, not PowerPoint orange.
- There is no runtime SuperTop state change. `hasSuperTop` is decided once at startup, and the enable toggle needs a restart.
- `superTop.indicator` is persisted (default true) but has no compiled consumer and no UI toggle today.
- The update state has no change notification. It is a non-atomic global written by both the update thread and the Settings render thread. Release-only start means Debug builds never leave `UpdateNotStarted` unless Settings queues it.
- Download progress can reset to 0 (per line, and on the HTTPS→HTTP fallback). There is also a post-download verify/unzip phase with no byte progress.
- Glyph names are taken from the external Microsoft reference, not from the font file.
