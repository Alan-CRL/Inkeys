# Research: 剩余动画验收可复用 harness 与证据边界

- Query: 找现有自动驱动真实动画及延迟采样入口，能否绕开旧 More 资格阻塞而不放宽断言；0811 是否可同口径比较。
- Scope: internal
- Date: 2026-10-05

## Findings

### 1. 真 Bar 软件事务 harness：存在，但不等于可见产品验收

- `Inkeys/IdtMain.cpp:1039` 最早调 `TryRunUi3PresentationFixtureEarly`，普通配置/主窗口之前分流。
- `Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.cpp:33,447,314` 定义 CLI、严格参数和私有目录授权。从仓库根执行且 EXE 必须处仓库内：

```powershell
& .\Build\ARM64\Release\Inkeys.exe --ui3-presentation-benchmark --scene main-fold --round 1 --capture on --capacity 32768
& .\Build\ARM64\Release\Inkeys.exe --ui3-presentation-benchmark --scene draw-attribute --round 1 --capture on --capacity 32768
# 仅用有收益/资格变化的新场景证据时跑；不要重复旧失败凑数量。
# 捕获关闭严格为 --capture off --capacity 0，其余参数同样必需。
```

- scene 仅 `main-fold`/`draw-attribute`，round 1..3，capacity <=65536；字段不能重复、不能删目标数，额外参数 exit81；真实 fixture 失败 exit90，成功0。父 child 预算300秒，真实 source 上限260秒，不以命令超时数十秒推定卡死。
- 自动创建 `TestResults/release-hardening/ui3-finite-<GUID>/r<round>/s<scene>/bin/Inkeys.exe`，nonce/原镜像/私有路径/lease 和子进程句柄鉴权；不要手动运行 internal child 模式。自动私有配置，光影 Enable/Dynamic=true、默认动画/zoom（Test.cpp:676–695），不等同之前 product 比较的 zoom1/Dynamic=false。
- 实际 Bar 初始化、业务 Main/Draw callback、共享 Scheduler WARP+D2D、真实 GetDC/ULW/ReleaseDC/EndDraw 完整事务；固定216目标（16warm+200measured），每秒一次、Down/Up+20ms。source 用 own HWND index 消息，真实 Interaction 消费，不依赖慢 Computer Use。
- `Bar.Presentation.Test.cpp:776–784` 窗口名 `Inkeys.Ui3.Fixture.Bar`，初始 visible=false；没有完整 Host/Settings/StartupPreview/业务worker。只查到真实初始化/ULW调用，没有 fixture 的显式 ShowWindow/SetVisible。不能拿此私有窗口路径替代用户要求可见产品动画与完整书写。

### 2. 可用字段与原 More 硬门

- `Bar.Presentation.Test.cpp:1137` 的 `raw-callbacks.csv` 有 client/generation/context_epoch/segment/start/end、bar_stamp_status/真实 bar_commit_ticks/attempt/epoch、present_attempted/ulw_attempted/ulw_succeeded/present_committed、GetDC/ReleaseDC/EndDraw HRESULT、分stage计时和 mask hit/miss/create/create_ms；raw-batches 是共享调度批次，不可混作单 Bar 帧。
- `summary.json`（:1333–1410）有 `owner_up_to_exact_complete`、`owner_down_to_exact_complete` 分布、timed_callback_cost/timed_batch_cost、timed_true_commit_gap、attempts/软件成功commits/failures、完整216未验证分母；GPU/optical_latency 固定null。成功事务不是光学像素可见，CPU父子阶段不重复相加，idle/epoch/generation断链需剔除。
- 当前唯一 ConfigureRawCapture/TakeRawCapture 的产品调用点均在此 fixture（:695/:1199）；普通产品无 raw-capture CLI。不应添加新大框架或向普通全局实例随意注入 capture。
- 旧实证 `.trellis/tasks/09-27-integration-and-release-check/research/ui3-f-first-scene-investigation.md`：2026-10-02第一目标2/2 verified、第二9/10，唯一 More SVG tag0x2000C Overwrite，相交3px；第2目标 Deadline source_failure13，exit90。未开始后215目标。接受/settled 不等于 SVG完整完成，不授予遮挡豁免。
- 失败仍导出 raw 数值prefix；但正式summary只取真WarmEnd后的 MeasuredBegin..MeasuredEnd，第二目标失败使分布不可得，不能把有限prefix当216验收/性能完整样本。无独立CLI减少目标/跳过More/只测本按钮的合法入口。近期改动未触及该原因时，不建议盲跑重复216。

### 3. 可见双方共同自动化：现成 product-compare，仍没有共同真实呈现度量

- `Build/automation-perf/canary-0811-compare-20261003/product-compare.cpp:89–114` 已有独立工具，限定同目录 marked `current`/`canary` 私有根、Inkeys.exe、ownPID/visible HWND/InputDesktop/WindowFromPoint输入归属。输出 CREATE_NEW，使用新前缀避免覆盖旧证据。主会话需先 inspect/snapshot 确认当前点，不沿用过期 HWND：

```powershell
& .\Build\automation-perf\canary-0811-compare-20261003\product-compare.exe --pid <本测试PID> --inspect
& .\Build\automation-perf\canary-0811-compare-20261003\product-compare.exe --pid <本测试PID> --hwnd <本测试BarHWND> --seconds 22 --roi <ROI左上宽高> --out .\Build\automation-perf\canary-0811-compare-20261003\<新前缀> --click-at 1000 <x> <y> --click-at 3000 <x> <y> --click-at 5000 <x> <y> --click-at 5150 <x> <y> --click-at 7000 <x> <y> --click-at 9000 <x> <y> --click-at 20000 <x> <y>
```

- 输出 WGC 回调/ROI像素变更、进程累计 CPU/内存/GDI与USER对象、实际输入发送结果。WGC到达/回调数/nextchanged 延迟是观察链指标，不是产品Present或可见FPS，也不是严格输入到光学延迟。可以自动复现同场景功能及累计CPU比较，不能据其 capture p95 作产品慢帧通过判定。
- 已存在原包 `D:/Project/Inkeys/InkeysRelease/Inkeys/Canary-Inkeys20260811a-arm64.zip` 与私有 Canary EXE；只读二进制确认后者不包含 UTF16 `--ui3-presentation-benchmark` 或ASCII `UI3Diag` 字段。**不要给旧EXE该未知参数**，旧普通入口可能照常启动产品而非拒绝。
- 保存的 Canary源码 `Build/.../canary-source-observation/Bar.RenderLoop.cpp:9533,9608–9640` 确有GetDC/ULW/ReleaseDC/EndDraw成功决策和 debug FPS，但没有本轮 raw 事务输出。debug FPS还含原节拍等待，开启它会改变产品路径/布局，不能用于严谨双方同口径性能通过。

### 4. 普通当前产品 UI3Diag 可复用诊断，但限制更严

- `IdtMain.cpp:2180` 默认注册 async discard_new sink；`UI/RenderPipeline/RenderPipeline.Diagnostics.h:12,109,157–274` 的 HealthySummariesEnabled=false，健康窗口定时清空且不输出；仅>=50ms活动间隔/回调/批次、资源失败、异常/恢复触发，至少1秒限频。故日志只覆盖触发汇总，不是全部动画分布；无日志不等于全部帧通过。
- 尤其 `commitGapMs(raw/active)` 是 AddCallback 的 **callback-end end** 时间差（:219–232），并非 raw 的 barCommitTicks；原commit计数是真软件事务成功数量，时间终点是回调代理。不能称其Present准确gap/光学间隔。
- 现 `Build/.../release-continued-20261003/summarize-ui3.py` 汇总该日志并把每window callback/max/GDCshare取均值；仅其样本集合内成立，不推广为全场景P95。旧 probe曾临时开启健康日志，`Diagnostics-original.bin` 是备份不是当前产品承诺；不能恢复临时常量污染最终普通产品。
- `Build/.../ui3-product-20261003/targeted-check.cpp` 仅运行 WindowGeometry/DirtyRegion/PresentDecision 三组数值检查，不是动画延迟程序。
- `InkeysHeadlessTests --no-window --benchmark` 会运行 curve/Value/Pct/Color/keyframe 与 FramePacing wait-policy 微基准（animation_tests.cpp:1625–1694 / frame_pacing_tests.cpp:537），无真实窗口ULW，可验证算法/等待开销但不完成可见动画验收。

## Related Specs / External References

- `.trellis/spec/native-desktop/index.md`：UI3唯一悬浮栏、共享WARP/串行render，与Draw3独立设备区分。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：raw/seal/真实软件提交/idle/无窗口边界；日志健康静默与50ms阈值。
- `.trellis/tasks/09-27-integration-and-release-check/{prd,design,implement}.md`：HF指纹/真实GUI/适用构建与不推断Win7。
- External: 未使用外部资料；全部为当前代码与现存产物只读研究。

## Caveats / Not Found

- 无 research.jsonl；role规定不读取implement.jsonl/check.jsonl。只读活动任务PRD/design/implement、workflow和相关spec，不遍历历史提交。
- 未运行GUI/CLI/build、未修改源码/普通配置/旧产物，未使用Git。仅本research文件为新写入。
- 未找到不依赖finite SVG资格且向双方原EXE导出成功ULW/DComp/光学延迟的现成公共harness；没有本仓库PresentMon/ETL现成产物。若本轮不添加测量框架，严格可见“性能不倒退”应仍标未达到可信证明，只能报告功能/累计CPU和观察边界。不能靠放宽More断言、回调/WGC代Present、恢复failed容量/A8/sincos方案制造PASS。
