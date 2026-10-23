# Research: native long-scroll staging review

- Query: Review the ignored long-stage proposal before applying it to the current short native ink fixture.
- Scope: internal; static source/staging review only, no build, CLI test, GUI or source mutation.
- Date: 2026-10-22

## Findings

**Disposition: suitable staged direction, but rebase is mandatory and the relay evidence claim needs correction before acceptance. The short ink gate remains required.** The manifest itself says staged-only/not-applied/not-built/not-run; nothing in this review upgrades that status.

### Concrete pre-apply / pre-run actions

1. **Do not copy the proposed inl over the current file.** Manifest baseline is `6C655BF5449AEF8A401DDC0B5A35854069574BD6C99D0A7C280F847B62956D7A`; current file read during review is `FC28A0BE3C5D2F3625BC74890FF81B244A9599B57D10193E13838A45BFF38BDE`. Current `Draw3.ProductLiveTest.inl:135,155,177` contains newly added source/fixture failure diagnostics absent from the staged full file. Rebase the long-specific hunks onto the latest implement-owned version and regenerate the manifest. `IdtMain.cpp` and launcher still matched their manifest baselines at this read. Other agents may continue changes, so recheck all baselines immediately before applying.
2. **`relay=` alone cannot prove feature relay.** Staged inl:457 and the final PASS string explicitly defer relay to ordinary ScrollFollow logs, but Business/ScrollFollow.cpp:220-223 logs `relayGeneration`, which is the accepted-reference generation: VisualMotionTracker.cpp:505 increments it on every accepted frame. The existing statistics already expose `initialSurvivors`, `added`, `retained` and `retired` (VisualMotionTracker.cpp:496-501). For an exact claim that no initialization feature remains, inspect/log those existing result facts with frame identity and require accepted frames with `initialSurvivors==0`, continued correct tracking and replenishment. Otherwise report only that the actual source excursion geometrically removed the birth viewport and returned, and explicitly leave direct feature-retirement observation unverified. Do not add a target-writing or mutable tracker seam to obtain proof.
3. **Use an explicit launcher timeout of at least 250000 ms for the long case.** Proposed launcher:1 retains a default of 100000 ms while long watchdog is 240000 ms (proposed inl:569). The default invocation can stop waiting while its owned test process continues. Invoke the long case with `-TimeoutMs 250000` (or another value strictly above 240000 within the declared limit). Preserve the short case's 100000 ms default. A timeout must retain the existing no-restart rule and must not be called a normal resource-drain PASS.
4. **Per-step ACK is observed, not required.** Proposed inl:410 prints actual ACK, but the settled predicate at :401-406 only requires Tracking, latest/applied, position and accepted frames. Initial and final real ACK assertions remain intact (:738-740 and final handoff). This is adequate for initial/end handoff coverage; do not claim every long step independently required ACK convergence unless that assertion is added intentionally. No manual ACK exists in the proposal.

### Contract review

| Area | Static finding |
|---|---|
| Short case | `RunScrollFollowProductLiveCase(true)` still defaults `longScroll=false`; original vertical +32/-16, margins48, final x0/y16 and A/B Exit remain. Shared InkWheel additionally checks held button, range, paintedX and post-settle source GT; these strengthen checks without changing intended short source motion. Must rerun short case after rebasing/building. |
| Strict CLI | Proposed IdtMain.cpp:1386-1417 adds the eighth exact flag and scans the whole argv; wrong suffix/duplicates/extra args exit2 before normal Main. Harness-off returns2. Header and both harness-on/off definitions exist. Offline runner adds nine malformed-long combinations and explicitly forbids executing the exact live flag. These are static findings, not executed validation. |
| Actual source/input | Proposed inl:384-409 uses real tagged `SendInput` WHEEL/HWHEEL, owned source cursor/foreground checks, rejects held mouse, waits its tagged source paint, then compares real tracker/channel observations. It does not write GT into capture/tracker/channel and does not enable HiddenTestContact. Existing source WndProc handles vertical negative and horizontal positive offsets, with bounds x0..16W/y0..24H (current inl:110-123). |
| Horizontal/reversal | Added horizontal +32/-16 produces source total x16/y16; pixel comparisons use ink shifts x-32 then -16, y-16. Signs match current source camera convention. |
| Ten primary heights | PlanLongVertical(:427-441) rounds 10*physical height upward to a multiple of16, uses steps16..128 and checks source range. LongVerticalRoundtrip(:444-465) sends all outward steps and reverses the exact list, verifies actual peak >=10H and return x16/y16. Peak geometry also verifies A above the screen. It does not pretend to measure initial feature identities. |
| Source GT | ScreenSourceGt's full 99 samples and exact256x192 ROI are unchanged. LongInkPoseSafe(:413-424) plans endpoints avoiding the predicted A corridor, instead of omitting covered samples; endpoint failure aborts. InkWheel checks GT before input and after convergence. The planner can fail safely at unsupported/small geometries; do not weaken GT to make it run. |
| Ink pixels | Shared CompareInk adds dx and checks symmetric core masks with max2px and count90..110%. Long-only margin96 keeps the horizontal shift inside the ROI. A is required before handoff and after short/horizontal moves, on return and after Off; no nonempty A assertion is attempted while A is deliberately offscreen. Returning pixels are checked, not merely the viewport number. |
| End/B/UInk | Final checks use actual source total x16/y16 and base-relative viewport within2px, no clamp, unchanged tool/content and expected tagged wheel count. After actual reverse handoff, B is real OS input; original Exit read checks unique committed private index, page/workspace/file identity, final viewport x/y/scale and B first world point = actual client Down + saved viewport (:468-495). No alternate save path. |
| Timing | Watchdog240sec covers the whole process, not only the roundtrip. At height1920, roughly300 wheel steps plus safe-endpoint adjustments, each requiring >=3 accepted captures, may fit but is not guaranteed. Report measured completion or timeout; do not skip waits or reduce distance after a timeout. |

## Files Found / Related Specs

- `Build/ScrollFollow/product-live-stage/long-stage/manifest.json`: five proposed file hashes and explicit unbuilt status.
- `Build/ScrollFollow/product-live-stage/long-stage/long-stage.patch`: proposed isolated long case, dispatcher and launcher/offline runner updates.
- `Build/ScrollFollow/product-live-stage/long-stage/proposed/Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`: line citations above refer to this staged snapshot where stated.
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl`: live short contract and newer failure diagnostics; read-only review.
- `Inkeys/Inkeys/CV/VisualMotionTracker.cpp:496`: initial feature survival and replenishment facts.
- `Inkeys/Inkeys/Business/ScrollFollow.cpp:220`: current ordinary per-frame diagnostic fields.
- `.trellis/spec/native-desktop/scroll-follow-product.md`: actual ACK, pixel/GT separation, A/B world coordinates and original Exit persistence.
- `.trellis/spec/native-desktop/scroll-follow-capture.md`: shared original Mag, no callback dependency, security/GT and real evidence boundaries.
- `.trellis/spec/native-desktop/scroll-follow-tracking.md`: immutable content anchors, accepted reference updates, ten-screen and initial-feature retirement acceptance.
- Task `prd.md` / `design.md`: actual authorization addendum and separate native real-screen gate.

## External References

No external API behavior needed a new claim; this is an internal review of existing Win32 wheel routing and repository contracts. No web search or external dependency/version changes were performed.

## Caveats / Not Found

- No staged code was applied, built or run. Short real ink had not passed at dispatch; its gate cannot be replaced by this review or skipped.
- This case is native dedicated content, not Edge/Word/PDF, hardware pen, DPI/device recovery, history/eraser or cold-load coverage.
- The actual Exit checks A's first point and B's first world point plus two Pen strokes; it does not compare A's full point array/style history. Earlier software regressions remain separate evidence for those claims.
- No requirement to force tile cache budgets, no manual targets and no second capture backend were introduced. On-return pixels demonstrate visible recovery under the actual run's cache conditions; they do not independently prove forced eviction/recovery occurred.

## 2026-10-23 当前 D83E 基线上的实际 long 增量复审

旧 proposal 的未应用/未构建状态仅属上文历史。本节审查新 `long-stage-1023`，在 PID22200 完整 short 独立通过后由主会话授权应用。checker 没有构建、产品 CLI、GUI、新截图或产品源码修改；允许先重跑旧 short，再单独运行 long，不提前记作 long PASS。

### 冻结文件与精确差异

独立逐文件校验 manifest 的六份 before/current/proposed SHA256，当前文件与 proposed 完全相同，before 与各自 baseSha256 一致；六文件均保留 UTF-8、原 BOM 状态、纯 CRLF 和原 EOF（Main 仍无末尾换行）。scoped/全量 `git diff --check --ignore-submodules=all` exit0。

- inl `18F4311C00267FB75ED4D2031BE11783B50084DAFDAC7E0A5E642408547143DC`，before 为已验 short 的 D83E9F65。
- Business `C50A18FA56892319D82B92AAF76B7DA886F86207B6076F74BF7DE03B6DF9B0F7`，before FFE81249。
- Main `8964C94FD50DD51CEDABC759C98C6EEFD5A02E6E99FC883EDB52AFB2BA0FF7E4`；header `C951ACA3A27798731488A41F346E5F156F1EE57FF89D7B046366323BE0FA20F6`。
- ignored launcher `3D3C18F5059CBBFE5213433D9C624823C44C826CE05F7394AE8EDA3FEB75B36F`；ignored offline runner `16AAE56BBAA2F8C3035F84E58D54D86C4828D2110A08E18B0EFA29DCA3E69DB1`。

原 `KnownInkEdge/ReadInkScreen` 与 `NativeMouseDownBound/ReadInkExit` 两块以字符串区间独立比对完全相同。原 failure/security/fixture 诊断与 GT 原函数没有差异；没有用旧整文件覆盖丢掉 D83E 修订。CompareInk 只加入二维 dx 与反向符号，原 ±2 像素/count90–110%、计时及失败保持；short 默认 dx0，原两滚轮及48边距保持。InkWheel 新增范围/held/paintedX及收敛后 SourceGT，属于更严格条件。

### 真实源、长程与 GT

- 只有 real tagged SendInput WHEEL/HWHEEL；发送前检查自身 Source、cursor/foreground、安全状态、无自己的或物理 held。没有 TestPorts、HiddenTestContact、手工 capture/tracker/viewport目标或 ACK。
- HWHEEL正值使 Source x增加，对应墨迹左移；+32再-16后实际 Source x16/y16，CompareInk dx分别-32/-16，最终 viewport=base+(16,16)。符号一致。
- 十个物理主屏高度按16像素向上取整；每步16–128，真实 Source paint、Tracking、两个不同 latest/applied 正确位置及至少3个新 accepted capture 后检查同样99点+精确ROI。GT只读，不能推动 owner。不能把每步日志 actual_ack 称作每步强制ACK门槛；真正初始/最终交接ACK仍为断言。
- LongInkPoseSafe 改选不让已知 A 走廊覆盖 GT 的端点，绝不删任何 GT 点；无安全端点直接失败。独立复算当前2880×1920规划：每程151步、19200像素，所有往返端点安全，回到 Source(16,16)。在 A 有意离屏时不伪造非空 A 像素要求；返回及 Off 都强制真实 A 核心屏幕对应。
- 峰值只直接证明 Source 几何离开初始视口及A离屏。初始特征全部退休必须另核同一 session/capture_session 的 `accepted=1 reason=None initial_survivors=0` 帧，且后续仍可信、位置/返回图正确并有补点事实。启动前 rejected/NotInitialized 的零统计不能充当该证据。运行输出 PASS 字符串明确保留 initial-survivor-log-check-required。

### 诊断与超时

Business 唯一改动为原 verboseFrames 门限下的既有 DiagnosticOutput::Printf 行，追加已有 session/capture_session/initialSurvivors/added/retained/retired；整数字段类型/格式匹配，仍使用原有界异步队列，没有新公共 API、可变Tracker接缝或新的逐帧默认输出。

long 内部 watchdog480000ms、short90000ms；launcher short 默认100000ms不变。精确 long flag 在 StartProcess 前必须显式 TimeoutMs≥500000，否则立即拒绝，避免标准默认调用只等100秒留下未等待进程。实际 long 使用 `-TimeoutMs 500000`；监督强制结束只属于异常保护，不能称为正常 resource drain 或强杀线程保证。若超时，不重启，不降低距离/门槛以迁就预算。

### 构建与最小 CLI 实际证据

- 当前 ARM64 EXE SHA256 `24D8FAE8308B605CE88ECF40D6DBC46BC20DBBA107906A3EC99BB11323A397E4`。
- 原完整日志 `motion-tracker-build-product-live-long-1023.log` 确认 InkeysRepo.sln Debug|ARM64、Build succeeded、0 Error；内部18.60秒，wrapper18.904秒。
- 本次共65 Warning（含重编 Main/Setting 的既有 C4244、hashlib 等），不能沿用上一小改的“仅3条”描述；未见归于本次新增行的警告。没有为清零既有警告改无关代码。
- `offline-5882187/result.json` 原27行：25坏argv全部exit2、stdout/stderr均0B；protocol exit0/229ms、state exit0/81ms，各stderr原PASS。包括新long suffix/错位/额外/重复/与其它test混用，精确单一long没有在offline runner执行。
- 没有重复157秒全无窗口回归，也没有 checker 执行新产品CLI。

**判定：静态、格式、完整构建与最小 CLI 预审通过；无阻断一次旧 short 及后续单独 long 原生运行的问题。当前不是 long 实测通过。** 硬件笔、外部应用、编辑矩阵、强制cache eviction、DPI/设备失效等边界保持上文限制。
