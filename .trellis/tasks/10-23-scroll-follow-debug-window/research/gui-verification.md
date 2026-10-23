# Research: Short real Mag, debugger GUI, and performance verification

- Query: Reuse the smallest existing self-contained fixture for ScrollFollowDebug pixel exclusion before P3, then actual production debugger + Scroll Follow GUI/performance validation.
- Scope: internal source research only; no product/test source edits, app launches, native input, screenshots, builds, remote writes, or Git operations.
- Date: 2026-10-23
- Baseline: task-approved feature/move 980dc39b; line references below are the observed source before P2/P3 changes.

## Findings

### Recommendation

Use two opt-in branches in existing fixtures, preserving every existing default invocation:
1. P2: a short ScrollFollowDebug variant of `RunFreezeCaptureLiveTest` in `Inkeys/IdtMagnification.cpp`. It already exercises the original production MagnifierThread and Capture Start/callback/Stop path. Stop the new short branch after at least three matching static samples and the existing exact crop proof; do not run its 5-second static observation, 20-cycle matrix, cancellation matrix, or Freeze matrix.
2. P3/P4: a debugger-probe option in `RunScrollFollowProductLiveCase(false)` in `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`. Reuse its actual product, Business, StateMonitoring, Mag, private source, output root, and cleanup. Add three bounded 15-second observation phases after actual Tracking ACK: debugger absent, production debugger created/shown, debugger closed. Keep the same actual follow session and source content across phases.

This needs no second Mag instance, external browser, UInk matrix, recording system, arbitrary tracker/viewport setter, or new large fixture. Existing short empty-page mode does not enter the ink/Exit-file branches; the long mode remains uncalled.

### Files found

| File | Purpose |
| --- | --- |
| `Inkeys/IdtMagnification.cpp` | Production filter builder plus owner-only PrintWindow and formal capture live fixtures; also includes ProductLiveTest.inl at its end. |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl` | Self-contained dedicated texture source + real Business/Draw3/StateMonitoring/Mag integration. |
| `Inkeys/IdtMain.cpp` | Strict early internal test dispatch, before user config/single instance/normal GUI startup. |
| `Inkeys/IdtMagnification.h` | Existing live fixture declarations. |
| `Inkeys/Inkeys/Business/ScrollFollow.h` | Existing read-only harness LiveTrace observer contract; suitable for tiny numerical aggregation if needed. |
| `Inkeys/Inkeys.vcxproj` | Existing harness macro defaults true for Debug and false otherwise (84–86). |
| Task `prd.md`, `design.md`, `implement.md` and `research/manual-validation.md` | Approved ordinary/non-topmost debugger, single startup opening, GUI authorization, private export and finite testing constraints. |

### P2: actual exclusion with minimum fixture work

`RunFreezeCaptureLiveTest` begins at `IdtMagnification.cpp:1219`. A private case helper with an optional mode and unchanged public no-argument wrapper can avoid changing default tests.

Minimal existing integration points:
- Central overlay role/lookup: `LogLiveWindows` hardcodes Bar at **714**; overlay spec at **1311–1315**; Service Start list **1325–1329**; Show/owner logging **1331–1335**; missing-overlay report **1358–1359**; filter assertion/log **1383–1389**. Parameterize these for the new case, rather than editing the production filter builder to special-case tests.
- P2 skeleton should be the new role with production ordinary non-topmost style and real service owner routing. A dedicated blue client WndProc already exists at **793–805**. For P3 rerun use the real production debugger WndProc/spec and publication path, not the blue stub.
- Source visibility proof: **1349–1382** checks the blue central overlay, all 63 known grid samples, and an exact uncovered dedicated ROI `(64,64,256,192)`. The formal callback at **1394–1443** matches the underlying source at all 63 points and copies an exact center `256x192` crop from actual returned BGRA. This center crop lies underneath the visible overlay, so it gives real exclusion evidence beyond filter membership.
- Capture ready and worker launch **1390–1392**; actual Capture::Start **1445–1447**; phase-wait loop **1448–1452**. In the new short mode, phase 1 is sufficient once at least three samples and its verified covered crop are present. Prefer a second changed pattern phase if budget allows, while retaining all GT requirements.
- Reuse existing Stop + drain and output/cleanup, but skip the long default tail. Default static hold starts **1454**, 20 cycles start **1567**, and full matrix ends **1619**. Do not claim those matrices ran.

**Critical z-order/geometry adjustment:** the source is TOPMOST at **1298**, and the existing overlay is promoted as part of the product overlay group. An ordinary non-topmost ScrollFollowDebug window cannot appear over that source. Only in this new fixture mode, put the fixture-owned source in the ordinary z-band and behind the debugger. Keep the debugger's real non-topmost style/owner null. Verify an actual point inside its client is visible/hits the intended HWND and that uncovered source samples remain correct. Do not add the debugger to the overlay owner/topmost chain to make the test work.

The existing screen checks assume an exactly central-third blue popup (**1368**, **714–758**). A normal overlapped debug window includes borders/titlebar and may use different bounds. In the new case derive covered/client bounds from the actual HWND and map them to screen coordinates; check known blue client points for the skeleton, known debugger-owned client pixels/UI for the full window, and known texture outside its outer bounds. Keep the exact captured comparison inside the covered client ROI. Do not quietly ignore arbitrary mismatches or infer visibility solely from IsWindowVisible.

**Owner-only alternative:** `RunFreezeMagnifierPrintWindowTest` (**1086**) can be parameterized even more narrowly: Bar references **945**, **1154**, **1168**, with the same z-band/visible-client adjustment. Its original timer path directly invokes PrintWindow on the HWND owner (**870–890**, **1021–1025**) and later tries a scaling callback and two Freeze cycles. This is valid additional owner-path evidence but **does not prove the formal producer/cross-thread path**, so the short formal-capture branch above is preferred. Existing default behavior stays unchanged in either approach.

### P3/P4: real debugger, real follow, bounded performance

Use a private fourth optional probe parameter (or a small private enum) on `RunScrollFollowProductLiveCase` (**1097**). Existing wrappers **1554–1560** continue calling their original arguments; a new strict early wrapper selects the debugger case.

Integration sequence:
- Register the **same production debugger spec factory** used by normal startup, adjacent to the existing Setting spec/Service Start at **1186–1191**. Do not recreate its renderer/lifecycle inside the fixture. A spec may be registered while the optional window remains uncreated for the initial absent phase; route actual create/show/close through Service.
- Real product start **1268**; diagnostics **1273**; real StateMonitoring and Mag threads **1274–1285**; initial primary ready **1286–1292**.
- Arm the production debug publisher before **1324** RequestToggle. Actual Tracking/hand-off is checked at **1325–1333**, with the real FollowChannel read. Add the three finite performance phases here, before existing wheel/Off assertions. GUI controls are separate from the timing windows so clicking/export is not attributed to steady preview overhead.
- A short forward/reverse test can reuse **InkWheel (921–948)** even with an empty canvas: it only sends a tagged OS wheel and verifies source/Tracker/channel/GT convergence. +32 then -16 leaves the existing final +16 displacement intact. Update only the new case's expected wheel count from one to two; keep the default final tool/content invariants **1451–1459**.
- Actual Business Off + capture drain + window ACK **1442–1450** remain mandatory. Do not manually set viewport, follow targets, ready/ACK, or Tracker output.

**GT/input constraints to handle explicitly in the probe:**
- Source is TOPMOST **1162** and is placed behind the product overlay chain **1204–1205**. Lower only this dedicated source's z-band while the ordinary debugger must be visible.
- `ScreenSourceGt` (**648–712**) expects the entire 99-point grid plus the exact `(64,64,256,192)` source crop. A visible debugger will intentionally obscure some points. For the new case verify known source outside the current owned debugger rectangle and a separate verified debugger client area; keep the existing all-points path for default/closed phases. The captured input must still match underlying source at the covered ROI. Do not blanket-disable GT.
- Initial source activation requires its own center hit (**1209–1237**); wheel helpers require foreground/source hit (**1402–1420**, **925–927**). Show the debugger after the initial source activation, place it away from the wheel target, and restore foreground to the source through existing own-window activation when needed.
- `SourceProc` accepts only fixture-tagged wheel events; an untagged wheel sets sourceCancelled (**131–148**). Root's native GUI tool should use the debugger's controls, not wheel the source independently while this fixture runs.
- Freeze/marker switches/zoom/pan/export should exercise the production control path. Verify pinned frame identity stays fixed while accepted sequences advance; resume consumes latest; close makes Service.Handle null and following continues. A follow Off/on cycle after close can check no automatic reopen. Do not add production reopen behavior for this probe.

**Performance measurements:**
- Use the same actual follow session and static texture; each phase 15 seconds, with a short separately reported warmup. Sample at bounded intervals using the existing message-pumping `Wait` (**603–631**), never a blocking sleep that starves the source.
- Capture FPS from actual accepted/sequence increments and steady-clock elapsed time; report invalid/rejected/API/skipped counts too. Report UI refresh separately.
- Capture `lastReadDuration` wraps the full backend Read (geometry checks, PrintWindow, GdiFlush, alpha repair), **excluding Analyze**. Pump takes completed before Analyze. Label it **Capture Read**, not isolated PrintWindow latency. (Corrected from the initial research conclusion after P3 source audit.)
- Tracker component times already exist in MotionStatistics (`VisualMotionTracker.h:50`: preprocess/opticalFlow/fit/refill). Aggregate those actual fields consistently across all phases. If true total elapsed is added by the production publisher, use its measured value and label it separately rather than inventing a sum-as-total.
- Do not enable the existing frame-recording `MotionTrace` class: it copies/converts large input arrays and writes a trace; its environment gate is external-only (**1101–1105**) and its own log flags non-normal performance (**1322**). If metrics while debug is absent/closed require a reader, reuse the existing `LiveTrace::Observer::Observe` interface (`ScrollFollow.h:43–49`) with a tiny fixed numerical accumulator installed for all three phases, no image copies/formatting/I/O in Observe. No new mutable test seam is needed.
- Preview copy time/display age are meaningful in the open phase; absent/closed should report disabled/N/A, plus stable counters proving publication/large image copying stopped. Sample process memory and GDI/USER counts at phase boundaries. Label these as finite same-machine observations, not a general benchmark.
- The default short product watchdog is 90 seconds (**1135–1138**). Three 15-second phases + init/GUI/export may exhaust it; give **only the new debugger mode** a modest 120-second watchdog and external finite deadline. Preserve the default 90-second and long 480-second limits. A watchdog process termination is failure, not successful cleanup.

### Strict entry, paths, lifecycle, and gates

- Add any new visible test mode to the existing whole-argv strict early dispatcher `IdtMain.cpp:1381–1418` (prefix malformed check, count==2, DPI awareness, harness guard, else return 2). Do not append the normal debug startup flag to an existing strict fixture flag: the dispatcher rejects more than one argument.
- Place a narrow new entry declaration beside the existing live entries in `IdtMagnification.h:24–38`, provide a harness-disabled return 2, and keep the implementation in the current TU/inl. No extra project source or second fixture executable is required.
- Run from the repository root. Both live fixtures require an existing ordinary, non-reparse `Build/ScrollFollow` directory and `InkeysRepo.sln`: capture **1265–1278**, product `CreateRoot` **1076–1092**. Each creates a fresh PID/tick subdirectory with CREATE_NEW logs; neither should use user's normal configuration or document folders.
- Product root is `Build/ScrollFollow/product-live-PID-tick`; `globalPath` redirects there (**1130**) and autoSaveRoot is explicitly private (**1251**). Capture root is `Build/ScrollFollow/live-PID-tick` (**1276**). Any automated debug export should remain under that fresh root.
- Security gate before any windows/readback: capture **1221–1222**, product **1106–1107**; ongoing desktop/security gate **592–631**. Stop immediately on safety UI; do not alter security windows.
- Capture cleanup **1633–1647**: Stop, stop/join original coordinator worker, then Service.StopAndJoin, destroy own source/class, shut down Display, restore logger/handles.
- Product cleanup **1504–1551**: return only owned mouse-down, normal Off when possible, monotonic shutdown, join state, stop product, stop/join Mag, Service shutdown/join, destroy own source, Display/COM shutdown, restore logger/globalPath, check no live workers/capture/window service, signal watchdog.
- Use the new role's production WM_CLOSE/Service.Destroy path in the open→closed phase; do not set offSignal or post WM_QUIT for that window. Resource counts should be recorded after actual destroy; a merely hidden debugger is not the closed phase.

## Related specs and external references

- `.trellis/spec/native-desktop/index.md`: native ownership/quality boundaries.
- `.trellis/spec/native-desktop/scroll-follow-capture.md`: original producer, borrowed frame lifetime, real pixel proof and bounded sampling evidence.
- `.trellis/spec/native-desktop/scroll-follow-product.md`: real ACK, early isolated fixtures, MTA, tagged OS input and private roots.
- `.trellis/spec/native-desktop/build-and-compatibility.md`: existing harness build gate and full ARM64 solution verification.
- `.trellis/workflow.md`: research persistence and role isolation.
- No new external dependency/documentation lookup was needed; conclusions are source-derived. GUI authorization in the current task supersedes historical no-visible-test permissions recorded in older specs.

## Caveats / Not Found

- No actual verification was executed by this research agent. Native GUI feasibility was reported by root; all pixel/GUI/performance cases remain unverified until root runs them.
- Production debugger UI/publisher APIs did not exist at this inspection stage; use the P2/P3 production factory and read-only publisher once implemented, not an invented parallel test UI.
- Existing Business Snapshot contains only state/session/available; it cannot by itself provide Tracker timing or preview age while the debugger is closed.
- Existing output files/filters, IsWindowVisible, matching phase counters, or a build pass cannot replace actual covered-pixel exclusion proof.
- The local `.codex/skills` directory was empty in this dispatched role; complete native research context was hook-injected. No main-session task creation/activation or skills/context-manifest changes were attempted.



最终专用夹具采用各15秒性能阶段、75秒独立控件时段及仅新模式210秒 watchdog；默认90/480秒不变。ordinary source 与跨owner线程 debug 的相对层级由本进程源线程显式安排，仍保留真实屏幕像素/命中/普通窗口检查。研究建议不是运行证据，实际结果见 validation.md。

最终范围修正：源码确认正式Open早于StateMonitoring与MagnifierThread启动，而最初建议的active Capture中动态Open不代表已承诺的startup-only路径。旧动态尝试的像素FAIL保留。最终产品夹具先完成absent15秒，真实Off/反向ACK/drain后Open，再实际toggle/F0/ACK得到新会话；open/closed仍同一新会话，各15秒与1秒热身不变。跨三段不同会话，明确performance_causal_comparison=0；不能用新流程证明旧225差根因或导出选择器首次可见排除。
