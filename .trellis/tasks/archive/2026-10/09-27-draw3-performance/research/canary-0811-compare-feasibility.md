# Research: 0811 Canary comparison feasibility

- Query: Existing automation and common metrics for current 0a19182c vs packaged 20260811a Canary, before code-normalization/security closeout.
- Scope: internal; read-only source/records/binary observation; no product launch, build, checkout, git command, or production/test edit by researcher.
- Date: 2026-10-03 (Asia/Shanghai)

## Findings

### Baseline and files found

- Parent performance.md:23 fixes user-confirmed Canary archive SHA256 28F4E0B32B5C56EA6375A776A539C77FC013A72A6F6B4F7A5A901BFB6BCB004E and EXE 81A3DBB26A845308EA2AAFE7869844B389968E31F3798AEE2E0987F947A6D07E. Exact source is 82f7b7c02080c253661514d31b55f3a827382e1d, not inferred from edition string. Existing D:/Project/Inkeys/InkeysRelease/Inkeys/Canary-Inkeys20260811a-arm64.zip is present.
- Old IdtD2DPreparation.cpp and Bar.FramePacing.cppm additionally confirm default WARP and common counter formula.
- Main prepared isolated roots Build/automation-perf/canary-0811-compare-20261003/{canary,current}, no user ink copied; old EXE 81a3dbb2 and current final Release 293eb4e8. Main exported exact old source (no checkout) under canary-source-observation; researcher read those files, did not run git.
- Canary IdtMain.cpp, IdtConfiguration.cpp/.h: old routing and actual startup path. Canary Bar.RenderLoop.cpp: old successful UI3 commit/FPS behavior. Canary IdtDrawpad.cpp/IdtDraw.cpp: legacy drawing/input/pacing.
- Current Inkeys/IdtMain.cpp, UI/Bar/Bar.RenderLoop.cpp, Bar.FramePacing.cppm: normal product diagnostics and success counters. Draw3 Host/RuntimeMetrics: current-only metrics, not present in packaged Canary.
- inkStrokeModelerTestTests/runtime_benchmark.cpp: existing automatic demo SendInput benchmark. It is not a main-product runner and must not be reused unchanged.

### Critical routing and backend difference

- Exact Canary IdtConfiguration.h:153 defaults setlist.Experimental.Inkeys3.UI3=false. IdtConfiguration.cpp:347-352 reads BOOL Experimental.Inkeys3.UI3 from opt/deploy.json. IdtMain.cpp:1089 chooses useInkeys3UI from it. A copied current deploy.json lacks this obsolete flag and would run old Floating UI instead of UI3. Set ONLY Canary private deploy Experimental.Inkeys3.UI3=true, then verify visible UI3. Animation/light/debug settings remain separate GROUP objects in Inkeys/Config/main.json.
- Current UI3 is unconditional; current IdtConfiguration.cpp:594-603 removes old routing on write. No need to change current production routing.
- Canary IdtMain.cpp:33-45 imports legacy IdtDraw/Drawpad/Rts, :1276 creates hiex Drawpad HWND, :1301-1304 starts InitRTSLogic/RTSSpeed, :1314 starts drawpad_main. There is NO Draw3 Host/import/runtime metrics CLI in this source. This is product drawing comparison (legacy Draw2 vs integrated Draw3), not Draw3-before vs Draw3-after.
- Canary IdtDrawpad.cpp:1446-1483 and :2143-2151 shows old Drawpad GDI image DC to UpdateLayeredWindowIndirect and drawpadFps pacing; IdtMain.cpp:911 defaults it to72. Current Draw3 normal device is hardware-first, DComp/ULW capability/config choice, active pacing120. Preserve each product normal backend; do not force all devices ULW/WARP or label callback rate optical FPS.
- Existing actual current evidence: UI3 WARP/ULW, independent Draw3 Adreno Hardware/DComp. Verify old and current actual display/DPI and startup selection separately in this comparison. Exact old IdtD2DPreparation.cpp:190-198 initializes WARP by default; :27-29 maps explicit backend to WARP/Hardware. Old UI3 is WARP/ULW by source, with optional explicit prepared-backend switching; confirm no switch during the run.

### Common counters and controls

- Packaged Canary has no --metrics-output, --draw3-host-metrics-smoke, --draw3-history-benchmark, UI3 raw/diagnostic CLI in exact Main. ASCII/UTF16 binary string observation only found ShowFrameRate among searched modern metric flags; absence is corroboration, not sole proof. Current normal Main also does not opt in Draw3 runtime metrics (HostStartOptions default false).
- COMMON UI3 supplemental debug overlay: Experimental.Inkeys3.UI3.Debug.Enable=true and ShowFrameRate=true in BOTH private main.json. Canary Bar.RenderLoop.cpp:9540-9564 only sets frameRateSamplePending after successful GetDC, ULW, ReleaseDC, EndDraw; :9623 ticks OneSecondFrameRate(activeFrameTime,frameEnd). Current :13203/:13304 equivalent success gate. Old Bar.FramePacing.cppm:142-175 and current :163-199 use the same formula for successful software commits/wall seconds and commits/active-work seconds. These are not optical refresh/FPS, GPU duration, per-frame P95 or input latency.
- Debug overlay draws red dirty border/text and changes work. Use only as a separately labeled, same-overlay auxiliary run; ordinary visual experience runs retain Debug.Enable=false. Do not compare normal run to overlay run.
- Common external observations can use process CPU-time deltas over fixed identical action/wait schedules, private bytes/working set/handle snapshots and idle CPU. No source modification is needed. Working set is noisy; CPU totals include product worker/background time and cannot identify GPU or individual frame tails.
- Main session Computer Use may execute visible controls and identical pointer trajectories; verify actual HWND/visible canvas and results. Screenshot/click tool round-trip duration is not product frame duration or accurate input-to-visible latency.
- Old common tools: brush/highlighter/eraser; Ctrl+Z undo in Canary IdtDrawpad.cpp:422-438. Legacy recall truncates to10 snapshots (:1547 etc). Use <=3 undo operations; do not replay prior 30-undo cold-cache test against old product. No Laser references or common Redo implementation found in these old drawing sources; current-only Laser/Redo checks must be reported separately.
- Safe common exit: observed Settings ExitSoftware control; current Setting.cpp:1583-1591 hides Settings then CloseProgram, Helper.CrashHandler.cppm:61-63 routes SetOffSignal(1); Canary Main:1358 waits offSignal and joins Setting/UI3. Do not use arbitrary WM_CLOSE on Drawpad: current Window.cpp:173 destroys a single HWND, not full product exit. Main-button right-click CloseProgram is gated by deploy RightClickClose (false in copied roots); not automatically available. Do not force terminate to obtain metrics.
- Existing demo runtime_benchmark.cpp:123-125 injects demo-only --metrics-output/--strict-metrics, numeric hotkeys1..4, page0 and exit9; :142/:219 TerminateProcess timeouts. Thus unsuitable for either main-product binary. Current hidden fixtures and removed temporary visible Host injector cannot produce a common old-binary workload without rebuilding/modifying old product.

### Bounded viable protocol

1. Freeze EXE hashes and private config before/after; disable auto-update in BOTH private roots only to prevent identity replacement. Preserve production configuration and user data.
2. Same monitor/resolution/refresh/DPI, power, animation Enable/SpeedRate1, EdgeLighting Enable/Dynamic setting, UI.Bar.Zoom1 with measured effective zoom, same tool width/color and blank private content. No build/GPU check alongside timed actions.
3. Three paired rounds with alternating version order, one product at a time: idle10s; main expand/collapse, middle reversal and pulse; fixed pen attribute/hover/width operation; same small brush strokes, some additional content, limited undo, eraser of private test content; idle10s and first expansion/stroke. Use exactly the same Computer Use steps/timing and check endpoints.
4. Record external CPU/wall, memory/handles and idle deltas per version per round; normal screenshots for visible completion/artifacts. If available, separately perform same debug-overlay UI3 commit/work-rate run and record counter values over sustained identical activity.
5. Compare repeated paired distributions and variation. Declare only the measured observations non-regressing if supported; no default overall latency/FPS parity claim without common frame/latency evidence. A reproducible regression is a finding, not a trigger to resume broad optimization under this user request.
6. Exit through product UI, retain isolated outputs/config hashes, and continue independently to code/security closeout even if some perf evidence is concretely blocked. Win7 ULW remains deferred.

### Related specs and external references

- .trellis/workflow.md; native-desktop/index.md; rendering-and-ui.md; ui3-render-diagnostics.md; input-and-ink.md; draw3-integration.md; native/quality-and-validation.md.
- Existing baseline-and-acceptance and parent performance records define same effects, at least3 runs and software/optical distinction. No external API performance claim; no external reference needed.

## Caveats / Not Found

- Researcher did not launch/build/control GUI or obtain performance results. Main owns actual config/GUI/counter capture and evidence.
- No packaged common per-frame latency/long-frame JSON, Draw3 raw metrics, GPU timing or optical capture capability was found. WPR policy denial from prior run remains a concrete limitation if unchanged.
- Old feature/render backend differences cannot be hidden by same filenames/config; product UX comparison and common UI3 software commit rate have a narrower claim than complete Draw3 latency parity.
- Process resource observations alone cannot prove all animation/drawing tails non-regressing. Incomplete observations must remain unverified, not all-green closeout.
