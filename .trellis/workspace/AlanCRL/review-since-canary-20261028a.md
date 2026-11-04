# Lifecycle / edge-case review since canary 20261028a

- Range: `718b0ebd` (Restore publish 20261028a) .. `c91b60d8` (83 commits).
- Trigger: the single-instance protection (`0ebc4a44`) held its mutex until process exit, so a slow shutdown made relaunch report "already running". Fixed in `c91b60d8`.
- Method: four read-only code reviews (startup/shutdown, Draw3 ink, PPT linkage, settings/i18n). Everything below comes from reading code; nothing was built or run.
- Decision (2026-11-03): findings are **recorded only, not fixed** — judged not necessary for now. Re-evaluate before a stable release.

Severity is the reviewer's guess. "Unverified" means the reviewer could not confirm it from code alone.

## Startup / shutdown / update

1. **Medium — staged update can be silently dropped after the `c91b60d8` fix.**
   `IdtMain.cpp` ~2192 skips the old-process wait when the instance was handed over (`adopted`); the replace at ~2265 and the `fail:` block at ~2296-2298 follow. With the mutex now released at Close, the user can reopen while the old process is still cleaning up with `Inkeys.exe` mapped. The new instance reads `installer\update.json`, hands off to the installer, `MoveFileExW(... REPLACE_EXISTING)` fails on the running image, and `fail:` deletes `update.json` and relaunches the old exe.
   Fix idea: also wait for the root exe process to exit in the adopted path, or retry the replace for a few seconds.
2. **Low — Restart with no helper never releases the mutex.** `ReleaseForClose` runs only for `signal == 1`. If `ArmShutdownSupervisor` returns `FallbackArmed`/`Failed`, nothing relays the lease, so a manual relaunch within the 15 s still says "already running".
3. **Low — final config/PPT-settings flush runs after the release.** `Setting.cpp` ~975-1010 (`MustDrain` writes drained in `Setting::Shutdown`) and the `PersistSettings` drain in `PPTLinkageMain`. A very fast relaunch can read stale `main.json`/`setting.json` or have its startup write overwritten.
4. **Low — `-WarnTry` retry races the release.** `Window.Legacy.cpp` ~143-144 launches the child before `SetOffSignal(1)`. Reviewer says the path is effectively unreachable (needs no startup tracker).
5. **Unverified — handoff Ready wait is a hard 10 s.** `IdtSingleInstance.h` `Handoff::Wait`. A slow disk/AV scan could time out: updates roll back, restarts do not relaunch.

## PPT linkage

6. **Medium-low — slow touch/pen tap on a heavy slide dims controls right after the tap.** The 3 s external-turn grace (`PptControlNavigationGrace`, `PageControl.cppm`; stamped in `InvokeDirection` ~915-918, compared in `PublishPptState` ~6062-6072) counts from the click, but the new page number is published only after Draw3 is ready. Past 3 s it is treated as an external turn and all four controls drop to 40 % opacity until the next interaction. Mouse users are unaffected (hover blocks dimming).
   Fix idea: start the grace at navigation completion / page publication.
7. **Low-medium — page clicks while the end-show confirm dialog is open spin the loading ring.** The `ConfirmEndShow` modal runs on the single consumer thread in `IdtPlug-in.cpp` (~1612-1660, `QueuePptUiBusinessCommand` ~148-225); `pptUiPageCommandOutstanding` does not guard it. The queued page turn calls `PublishNavigationWaiting`, the badge shows a ring that cannot finish until the dialog closes, and Cancel still lets the queued turn run (the late turn pre-dates this range). Dialog running synchronously on the consumer thread was inferred, not traced.
8. Unverified: badge stuck in `Pending` with the ring spinning if the trusted target is rejected without a recovery state; silent `continue` when `I18n::capture` fails in the confirm-end-show handler (needs a missing base key).

## Draw3 / ink

9. **Low — palm promoted to eraser after Down keeps the "Drawing" accepted-stroke notice.** `Draw3.DrawingController.cpp` 12361-12366 vs 11527-11551; the Bar picks `AutoCollapse.Drawing` (`Bar.Interaction.cpp:956`) while completion reports Eraser (`:17519`). Visible with Drawing=collapse, Erasing=stay open and a palm whose area ramps up within 100-500 ms.
10. **Low (design note) — cursor-light suspend fix no longer checks the stroke start is still current.** `Bar.Interaction.cpp` 8740-8742, 10159-10163 (commit `e622c5d6` replaced the `ActivityCount` check with a serial comparison). A queued `BarCanvasDrawingActivityMessage` for an already-ended stroke still collapses / suspends cursor light / clears recall. No concrete bad outcome traced.
11. Plausible, not proven: `BarCanvasDrawingStartSerial` is bumped even when no message is posted (`offSignal` set or no `floating_window`). Harmless today.

## Settings / i18n

12. **Medium — Brand desktop-shortcut name is permanently reset by a language change.** `Setting.cpp` 2691-2697 and `IdtPlug-in.cpp` 1908-1915 rewrite the Brand preset ("智绘教Inkeys", valid only in zh-CN) to Default and save it whenever the UI language is not zh-CN. Switching back does not restore it; with `CorrectLnk=true` the next launch deletes "智绘教Inkeys.lnk" and creates "Inkeys.lnk".
13. **Medium (deliberate per PRD, behaviour change) — upgrade drops old `CorrectLnk`/`CreateLnk` and deletes desktop shortcuts.** `IdtConfiguration.cpp` 617-618, `IdtPlug-in.cpp` 1921-1950, `Other.Config.cppm`. Old `PlugIn.ShortcutAssistant.*` is no longer read and is erased on save (task design: `.trellis/tasks/archive/2026-11/11-03-shortcut-settings-integration/design.md` line 68, "no migration"). Users with `CorrectLnk=false` lose custom-named shortcuts on first launch; default users see the name change from "智绘教(屏幕批注工具)" to "Inkeys".
14. **Low-medium, unverified — Bar theme change does not wake PPT page / whiteboard surfaces.** `Bar.Main.cpp` 346-353: `SetThemeMode` requests only `Client::Bar`, while `SetResidentOpacity`/`SetAnimationOptions` also request `PptPageMask()`. Page surfaces compare `appliedSurfaceColor` only when they render (`Bar.Scene.cpp:658`), so page controls may keep the old theme until hover/page change.
15. **Low — first-install language detection matches the exact LANGID only.** `IdtI18nLocales.h` 94-102 (called from `IdtMain.cpp:3098`); no primary-language fallback, so pt-PT, fr-CA, de-AT, es-AR, en-AU etc. start in en-US. A test asserts fr-CA → English, so likely intentional.
16. **Low — failed language switch is silent.** `Setting.cpp` 5311-5333 clears `settingPendingLanguage` before `prepare`/`ReplaceSettingFonts`; on failure the combo snaps back with no message.

## Checked, nothing found

Single-instance admission (`Acquire`/`Adopt`/`IsPresence`), restart and crash-restart relay, SuperTop lease chain, startup-failure paths; palm-eraser preference publication, config validation, lifecycle, reconnect identity, canvas diagnostics, performance controls; PPT navigation-waiting tracker, idle fade, loading ring, page-number cache, PptCOM descriptor fast path, whiteboard navigation; all 20 locale files (702 keys each, placeholders, font coverage), config fallback for language/theme/opacity, live language switching, Setting window DPI/backdrop handling.
