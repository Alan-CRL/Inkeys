# Research: Mouse 拖动画布延迟的输入与唤醒链

- Query: Touch 正常、Mouse 拖动画布明显落后而背景较快时，检查 RTS/WM_MOUSE 双路径、控制唤醒、估速重置与无窗口验证接缝；评估只减少白板平移纯位置 cursor wake 的最小修改。
- Scope: internal，代码只读；只写本研究文件；不编译、不启动 GUI、不执行 Git 操作。
- Date: 2026-11-08
- Active task: `.trellis/tasks/11-08-chromium-touch-fling`
- Baseline: 主会话提供当前核心提交 `4b4edb91`；本研究没有通过 Git 再核提交身份。下列行号为研究时工作区内容。

## Findings

### 核心结论

已找到明确的 Mouse 专属高频控制唤醒链：正常 RTS 输入后端仍处理独立 `WM_MOUSE*` cursor 消息，每个成功发布的 Mouse cursor sample 都无条件请求 drawing cursor render，进而发布 urgent control wake。`PublishControlWake` 即使已有控制标记尚未消费，也再次 `SignalWake()`；该调用推进活动帧 interrupt generation。另一方面，白板 Selection 根本不构建应用 cursor，普通接触 Move 已按不打断活动帧的合同处理，tile 恢复却会因任何 pending ingress 或 wake generation 变化提前退出。因此 Mouse 纯位置消息可以持续抢占本来应该用于恢复墨迹的工作，且这条链不由 Touch 普通 Move 产生。

这建立了可确定复现的饥饿来源，和“输入日志延迟低、背景较快、墨迹画布高延迟”相符。用户给的 CanvasPan 日志本身未记录 Mouse cursor 控制标记或恢复中止原因，尚不能单凭该日志断言现场每次可见延迟都只由此产生；应由调用真实生产 publisher 的回归验证该链已切断。

### 文件索引

| 文件 | 作用 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | 两种后端的 WM_MOUSE 处理、cursor mailbox、render/control wake、leave/模式切换 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm` | 无窗口可用的公开测试发布入口及 coordinator 连接 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.MouseInput.h` | Selection/Pan 原子状态与 Win32Mouse 接触发布 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp` | RTS contact callback 真实末包/QPC 发布 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp` | control marker 合并、所有 wake 与活动帧 interrupt generation、普通 Move 唤醒语义 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | Selection cursor early return、恢复预算/中止检查、CanvasPanInput 与无窗口生产探针 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp` | timing reset 只重置估速证据而继续几何更新 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp` | CanvasPan QPC frequency/schema 日志 |
| `InkeysHeadlessTests/draw3_mouse_input_tests.cpp` | 当前仅 MouseInputState/Selection 逻辑测试，未覆盖 WindowController cursor wake |
| `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp` | 时间压缩与 QPC/坐标量化保护测试 |
| `Inkeys/IdtMain.cpp` | 配置/单实例/Drawpad 初始化前直接退出的无窗口 probe 参数 |

### 生产路径与 Touch 差异

1. 正常 RTS 模式的 `WM_MOUSE*` 分支只发布 cursor 样本，并不发布绘制/导航 contact。`Draw3.WindowControl.cpp:2049` 起处理消息来源与兼容过滤；通过后 `:2212` 解除 Touch suppression、`:2213` 设 Mouse owner、`:2224` 记 QPC、`:2229` 调用 `PublishMouseCursorSample`。该 Mouse 分支与 RTS contact 回调可以同时到达。
2. RTS `Packets` 取 batch 末包（`Draw3.RealtimeStylus.cpp:1417`），解码时间使用 `QueryQpc()`（`:1433`），然后 `coordinator_.PublishMove`（`:1452`）；非 Pen 的 CanvasPan `rts-batch` 日志在 `:1454`。并没有通过 WM_MOUSE cursor 发布来替代 contact。
3. Win32Mouse 后端独立分支同样调用同一个 cursor publisher。`Draw3.WindowControl.cpp:496` 的 `HandleCompatibilityMouse` 在 Down/Move/Up 操作 `mouseInput_`；`:539` 调用 `PublishMouseCursorSample`。把门控放在该 publisher 可以覆盖两个后端，无需分别修改两处消息路由。
4. `PublishMouseCursorSample`（`Draw3.WindowControl.cpp:1147`）先读旧样本，再发布新 mailbox；只有 Publish 失败才早退。`:1163` 无条件 `RequestDrawingCursorRender()`，`:1164` 才针对 valid/contact 边沿请求 system cursor 刷新。
5. `RequestDrawingCursorRender`（`:1211`）设置 requested 标记并调用 `RequestControlWake`（`:1214`），后者在 `:629` 转到 `coordinator.PublishControlWake`。
6. `ContactInputCoordinator::PublishControlWake`（`Draw3.ContactInput.cpp:380`）已有 pending 时也执行 `SignalWake()`（`:383-386`），成功新 marker 同样 SignalWake（`:397`）。marker 会合并，**wake/generation 不合并**。`SignalWake(bool interruptFrame=true)`（`:467`）默认推进 `frameWakeGeneration`，总是推进 `wakeGeneration` 并 SetEvent。
7. 普通接触 Move 的 idle 订阅唤醒在 `Draw3.ContactInput.cpp:880-881` 使用 `SignalWake(false)`；活动帧因此不会被普通 Move 打断。Touch `SetTouchPanActive`（`Draw3.WindowControl.cpp:926`）也只在状态真正变化时唤醒，非逐 Move。
8. 类似的先例已在 Pen 接触 cursor publisher 使用：`Draw3.WindowControl.cpp:1120-1124` 仅 hover 请求 render；注释指出 contact mailbox 由活动绘制帧消费，避免逐包唤醒。
9. Mouse owner/suppression setter 不会破坏上述最小门控：`SetDrawingCursorOwner`（`:1232-1233`）、`SetTouchCursorSuppressed`（`:1251-1253`）旧值相同即返回。稳定 Mouse 位置流不会另从这两处持续发布控制 wake。

### 为何影响画布恢复而没有需要显示的 cursor

- `buildDrawingCursorVisuals`（`Draw3.DrawingController.cpp:15542`）在 `SelectionMode()`（`:15547`）直接 `return`（`:15557`）；当前模式没有应用绘制 cursor，Mouse 保持系统箭头。`updateCanvasNavigation` 的注释与 `SetTouchPanActive`（`:15424-15425`）明确 Mouse 拖动不进入 Touch Pan 隐箭头态。
- `HasPendingWork`（`Draw3.ContactInput.cpp:1166-1172`）包括队列 control marker。恢复工作取 generation（`Draw3.DrawingController.cpp:18443`）后，在每个 tile 前检查 `input_.HasPendingWork()` 或 generation 变化（`:18448-18449`），任一满足立即 break。高频 cursor 控制流会让该条件反复成立。
- `WaitForFrameDeadline`（`Draw3.ContactInput.cpp:1324-1364`）的 interrupt generation 提前返回机制也会被这些 cursor controls 触发，令原本普通 Move 保持节拍的改进被旁路。
- 背景/Freeze 的具体合成和 budget=0 分支由另一研究主题深入；此处不建议改恢复中止合同，因为真正控制命令、Down、终态仍须及时抢占。

### 用户日志的量纲和 timing reset

- `class=2` 对应 `CanvasPanDeviceClass::Mouse`：`Draw3.DrawingController.cpp:2530` 枚举 `{None, Touch, Mouse}`，`:2595-2599` 将 MouseLeft 归到 Mouse。
- `delay-ms` 是真实消费截面减 **RTS 回调到达** QPC，再除运行期 frequency 乘 1000，见 `Draw3.DrawingController.cpp:2843-2844`、`:2861-2867`。它没有包括 Windows 提供回调以前的设备/消息延迟，也没有包括 GPU、Present 完成或显示屏扫描。
- 例如用户 seq6922 的差值 `1273567272797 - 1273567265492 = 7305`，日志 0.731ms，推得约 10MHz。按此频率，`qpc` 相邻相差 10000 才约 1ms，不能误读为 10ms。确切 frequency 应以启用行确认，生成处是 `Draw3.Diagnostics.cpp:822-825`，用户片段未包含那行。
- “pan-pointer samples40/50ms”反映 tracker 窗口内保存的样本数，并非40条待消费队列；此日志缺乏广泛 callback→consume 长队列迹象，不能据此诊断 contact 消息阻塞。
- 高频 `timing-resets=1` 不是丢掉 Move 几何：`CanvasPanReleaseVelocityTracker::AddSample`（`Draw3.CanvasNavigation.cpp:336`）在 interval<2ms 且距离超过12000DIP/s容许范围时清 history（`:364-367`，阈值 `Draw3.CanvasNavigation.cppm:63-64`）。上层只重置 booster/计数（`:1024-1029`），之后仍更新 pointer/lastQpc 并计入样本（`:1031-1033`）。`ScrollUpdate` 在 `:970-972` 独立累加 `contentDelta`，最终 `:1138` 保留该结果。
- 因此 reset 会造成 `v-content-dip=(0,0)`、释放证据不足/无惯性或预测速度异常，但不要求 `delta-px` 归零。用户在 reset 后非零 delta 与生产代码一致。不能仅为本次 Mouse 持有拖动延迟而放松压缩时间保护或虚构样本时间。
- 当前量化容许已有明确测试：`InkeysHeadlessTests/draw3_canvas_pan_tests.cpp:819-846` 覆盖960Hz、240FPS、10MHz及 float像素→DIP；`:809-812` 和 `:848-850` 保留真实时间压缩拒绝。

### 建议的最小修复

仅在 `WindowController::PublishMouseCursorSample` 的成功 Publish 后，对现有 `RequestDrawingCursorRender()` 加模式/边沿门控：

```cpp
const bool stateChanged = previous.valid != sample.valid ||
    previous.inContact != sample.inContact;
// 白板平移使用系统箭头，纯位置样本无需打断画布恢复；保留接触/有效性边沿。
if (!CanvasPanMode() || stateChanged) RequestDrawingCursorRender();
if (stateChanged) QueueSystemCursorRefresh();
```

作用域必须是 `CanvasPanMode()`，即 Selection 且白板 workspace（`Draw3.MouseInput.h:29`，`Draw3.WindowControl.cpp:686-689`），不要扩大到所有 Selection 或所有 Mouse。mailbox 每次 Publish、坐标/QPC、诊断、消息来源过滤、Mouse contact、Touch contact/history、control coordinator 和恢复中止条件保持原逻辑。

需要保留的边界：

- 首次 valid、Down/Up contact 改变都仍请求 render/control wake 与 system cursor refresh。
- `ClearMouseCursorSample`（`Draw3.WindowControl.cpp:1168-1181`）仍无条件请求 render 与 system refresh（Clear 确实改变时）。`WM_MOUSELEAVE`（`:2239-2259`）清样本及 owner 的链不改。
- `SetSelectionMode`（`:676-683`）、Mouse system/injected 设置（`:858-871`）、owner/suppression 改变（`:1229-1259`）自身 invalidation 不改；因此模式切换和真实 Mouse 接管 Touch 不依赖下一条位置 wake。
- 真正 control commands、Down/Up、cancel 的 urgent wake 保持；不能修改 `PublishControlWake` 全局去重为“pending不Signal”，否则可能破坏当前控制/等待合同。

### 生产路径无 GUI 验证方案

可在 `Draw3.DrawingController.cpp` 既有无窗口生产 probe 内加回归，不需要改 Headless 项目依赖或创建 HWND：

- `WindowController::PublishHiddenTestMouseCursor`（`Draw3.WindowControl.cpp:735-739`，公开声明在 `.cppm:240`）解除 suppression、设 Mouse owner后直接调用**同一个生产私有 publisher**，无 HWND 条件。默认构造 `WindowController` 就可用。
- 连接真实 `ContactInputCoordinator`：`window.SetInputCoordinator(&input)`（`:623-626`）会发布一次初始 control wake；测试必须先 drain/ack，并消费 drawingCursorRequested，再建立基线 generation。否则会把初始化 wake 错算成纯位置 wake。
- 既有 `CanvasPanProbeRig`（`Draw3.DrawingController.cpp:6428-6431`）包含真实 coordinator、默认无 HWND WindowController 与 CanvasPanInput；构造（`:6444-6448`）尚未连接 coordinator。可在测试局部显式连接，避免无关修改全 Rig。`Frame`（`:6484-6486`）已通过真实 `DrainIngressBatch` ack null control marker；`:6494` 设置白板 workspace；`:6501` 调真实 pan.Step。
- `--draw3-control-fence-test` 在 `Inkeys/IdtMain.cpp:2037-2039` 提前路由到 probe；上方`:2009` 明确在配置、单实例、Drawpad HWND 初始化前退出。`RunDraw3ControlFenceProductionProbe` 调 `RunCanvasPanProductionProbe` 在 `Draw3.DrawingController.cpp:8063`。`--draw3-canvas-pan-feel-test`（`IdtMain.cpp:2034-2036`）可作为同次动力学回归。

最小而有意义的断言矩阵：

| 场景 | 应验证 |
| --- | --- |
| 白板平移稳定 valid+inContact，连续大量位置样本 | mailbox最后坐标/QPC持续更新；requested不置位；controlWakes和wake generation不增加；HasPendingWork不因cursor出现 |
| 平移稳定 valid+hover | 同样不因纯位置新增cursor wake；系统箭头位置由系统更新 |
| 首次valid / contact Down / Up / invalid-Clear | 每个边沿仍产生cursor invalidation与真实control wake；drain/ack后可回空闲 |
| 白板平移以外的绘制 / 桌面Selection | 位置样本仍产生现有cursor render request；模式范围无回归 |
| 真实 Mouse coordinator Down→Move→Up 加并行cursor位置流 | 真实导航history仍消费；拖动delta正确；末Up闭合；不能仅测试门控辅助函数 |
| 真控制命令与Touch普通Move | 真控制仍推进interrupt；Touch history/节拍不变 |

旧行为下“平移稳定位置流不增加generation”的测试必须失败。无窗口probe能证明调用链与输入几何合同，不能证明光学延迟数值或系统箭头现场可见性。

## Related Specs

- `.trellis/workflow.md`：角色隔离、研究落盘与不修改他人任务产物。
- `.trellis/spec/native-desktop/input-and-ink.md:19`：RTS模式WM_MOUSE只控制cursor，RTS contact单独准入；`:25` 要求共同入口或说明另一后端；`:38-44` 主cursor/suppression/来源过滤边界。
- `.trellis/spec/native-desktop/canvas-pan.md:22-28`：真实callback历史、不改变绘制latest、普通Move维持节拍、拓扑/控制urgent；`:33-40` 估速可靠性与持有几何；`:71-73` ARM64 solution与生产无窗口probe。

## External References

未进行外部检索。本结论限仓库内已实现的输入/唤醒/恢复合同，不依赖 Windows RTS 未经验证的采样时间性质。API外部文档若后续用于改输入时间来源，应再查官方资料；本次最小修复不需要改时间来源。

## Caveats / Not Found

- 没有执行构建、测试、GUI或Git；所有测试均为建议，最终结果由主会话实施/验证。
- 当前用户日志未含启用行frequency，也未含系统鼠标cursor wake/每帧tile中断计数或GPU/Present阶段耗时，无法量化可见延迟或唯一归因。10MHz来自多条差值与delay-ms的反推。
- RTS callback到达早于/晚于OS WM_MOUSE消息的真实现场关系尚无日志记录；本研究只证明两条生产路径并存及cursor唤醒可以抢占恢复。
- 本次不解决屏幕物理尺度/大屏profile，也不放松时间压缩保护、改RTS包历史或重做恢复策略。
- `.trellis/spec/native-desktop/quality-guidelines.md` 不存在；没有假设其内容。研究时已有其它 `scale-stage*` 产物不改。
