# Research: 大缩放下 PPT PageControl 单侧不可见

- Query: 底部右控件可见而底部左控件偶发不可见时，生产布局、Scene、资源、ULW、窗口和页码回执在哪些阶段可能分叉？
- Scope: internal
- Date: 2026-09-25

## Files found

- `Inkeys/Inkeys.vcxproj:856-857`：实际编译 `PageControl.cppm/.cpp`。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cppm:732-1215`：PPT/白板共享分页的缩放、四窗布局、避让、backing 和页码回执规则。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:142-203,785-1061,1398-1706`：逐窗 Scene、动画、资源、ULW、Window Service 提交与回执生产链。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:580-604,1781-1805,2005-2023`：逻辑/呈现包络、有效 scale 与资源入口。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:84-130`：每窗 D2D bitmap 的实际创建和旧资源替换顺序。
- `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp:335-397`：四个 PageControl 客户端固定顺序与 Continue/Retry 调度。
- `Inkeys/Inkeys/Window/Window.cpp:1386-1456,1567-1587`：窗口 owner thread 的 Show/Hide/SetBounds、PPT Z 序提交。
- `Inkeys/IdtMain.cpp:1763-1808`：实际四个 layered HWND 注册与 Bottom 的白板共用容量。
- `InkeysHeadlessTests/page_control_tests.cpp:467-507,940-1002` 和 `PageControl.cpp:2109-2193`：当前纯布局与无 HWND 的真实 Scene 离屏测试。

## Findings: source-confirmed contracts and defects

1. **布局已经具有成对约束，不能把现象简单归于完全没有 clamp。** `ResolveSurfaceLayout` 从同一 `PptLayoutState` 求左右镜像 x/y、成对 scale；白板独立采用底角 5 DIP (`PageControl.cppm:953-1018`)。`ResolveRuntimePageControlLayout` 限制屏幕/资源尺度、clamp pair 位置，底部优先、侧边最多 24 次最近可行位置/缩小，碰撞仅检查底部与侧边四窗 (`PageControl.cppm:1030-1194`)。主栏不参加此求解。底部入场直接在目标位置渐显；只有 Side 首帧使用合法屏外入场起点 (`PageControl.cppm:856-879`, `PageControl.cpp:849-890`)。

   静态推导：同一对两侧即便各自拟合到不同 scale，`FitScale` 给出的横向上限与 `ClampPageControlLayout` 的非负 offset 仍使左控件逻辑主体止于屏幕中线及其左侧，右控件始于中线及其右侧（忽略像素取整的边缘量）。因此“底部右窗完全盖住底部左窗”不是仅靠两窗拟合 scale 不同就可得出的解释；若只启用底部一对，更应先查左侧资源、alpha、HWND 提交及真实可见性。`PresentationBounds` 在主体四周扩展阴影 (`Bar.Scene.cpp:588-604`)，阴影局部越界不等于主体屏外。

2. **同一发布代并无整组布局快照。** 每个 `RenderSurface(index)` 读取相同发布快照后，使用该 `index` 现有 Scene 的 `PresentationOutsetPixels()/appliedSceneScale` 与该 Scene 当前 DeviceContext 的 `GetMaximumBitmapSize`（首次无 context 则用 16384），各自重新调用完整的 `ResolveRuntimePageControlLayout`，再只取本窗目标 (`PageControl.cpp:1444-1477`)。四窗持有独立 Scene、bounds、backing、成功提交状态 (`PageControl.cpp:161-203`)。因此不同旧 Scene/资源状态可能让同一 pair 的最终 scale、offset 和避免侧栏冲突的解不一致；这是已确认的**状态交接风险**，但尚未证明它足以让用户机器的左窗消失。Scene 默认 damage outset 为 10 DIP (`Bar.Scene.cpp:33-36`)，正常 scale 下单纯像素取整通常只产生小差异；不能夸大该因素。实际 GPU bitmap limit 同 epoch 是否不同也尚无证据。

3. **自动布局/呈现不是成对提交。** 共享 Scheduler 顺序为左底、右底、左侧、右侧 (`RenderPipeline.cpp:335-394`；`PageControl.cpp:129-140`)。各窗分别 `ConfigureSurface`、`PresentScene`、`Service.SetBounds`、`Service.Show/Hide`，成功后单窗更新 cursor 区域和页码 mask (`PageControl.cpp:1486-1706`)；只有拖动直移走已有 pair 双 `SetWindowPos` 与第二窗失败回滚 (`PageControl.cpp:1112-1184`)。一窗 Retry、另一窗 Idle 是当前可能的中间态，不等于两窗都已成功。

4. **配置/Scene 几何失败可静默流入后续呈现。** `ConfigureSurface` 忽略 `scene.Configure` 和 `scene.TransitionLayout` 的 bool，又忽略末尾 `ApplySceneBounds` 的 bool，却照样推进 `sceneConfigured/configuredMode/observedRevision` 等状态 (`PageControl.cpp:785-890`)。与之相比 `ApplySceneBounds` 自身只在 `SetBounds` 成功后推进缓存 (`PageControl.cpp:682-694`)。后续可能继续使用 Scene 的旧呈现包络；这条状态机缺口已从源码确认，尚无故障现场的失败返回值。

5. **早期失败均会 Retry，但错误阶段/码缺失，确定性失败可能无限循环。** `PresentScene` 的 `EnsureDeviceResources`、缺少 context/GDI、`GetDC`、`ReleaseDC`、`EndDraw`、ULW 失败均返回 Retry 或 DeviceLost (`PageControl.cpp:914-1061`)；`RenderSurface` 在失败时于 Window Service/`FinishSurface` 之前返回 (`PageControl.cpp:1614-1618`)。ULW 的 `GetLastError` 没在调用点捕获，之后 `ReleaseDC/EndDraw` 可覆盖它 (`PageControl.cpp:1020-1055`)。`ResolveStableBackingSize` 只增不减，且未以当代实际 bitmap limit 验证最终 backing (`PageControl.cppm:926-951`; `PageControl.cpp:1540-1551`)；`BarUIRendering::RecreateDeviceResources` 在旧 target 尚未丢弃时创建新 bitmap (`Bar.Rendering.cpp:96-130`)。显示器/设备变化或内存压力下，一个 surface 可重复请求同一不可分配容量，另一窗继续成功；这也是未获现场错误码证实的机制。Scheduler 对 Retry 每个 60 FPS 节拍再次调度 (`RenderPipeline.cpp:335-397`)，不会自动降级或区分永久失败。

6. **窗口提交的 bool 不能证明实际可见。** `Service::Show` 对普通窗调用 `ShowWindow(SW_SHOWNOACTIVATE)` 后主要检查两次 Z 序 `SetWindowPos`，没有读回 `IsWindowVisible` 或实际 RECT；`Hide` 同样直接报成功 (`Window.cpp:1386-1456`)。`RenderSurface` 只在 `SetBounds/Show/Hide` 返回 false 时打印窗口失败/恢复日志 (`PageControl.cpp:1418-1442,1645-1679`)。因此早期 D2D/ULW 失败完全不进入该日志，而 Show 返回 true 也可能让 `PptPageCommitState` 把尚未真实可见的窗当成已提交。PPT 四窗均由主 Drawpad 拥有 (`Window.cpp:835-873`)，`Show` 把目标排在 Bar 下；没有源码证据指向主栏参与位置排斥。

7. **页码回执安全门禁依赖真实完成事实。** `PptPageCommitState` 的 required mask 联合目标可见、上次已呈现、在途 mask；任意一个 required surface 未完成则不 ack (`PageControl.cppm:592-646`)。`RenderSurface` 在 ULW/Window 成功后才 `FinishSurface` 和 `AcknowledgePage` (`PageControl.cpp:1676-1706`)。失败持续时它会阻止 UI-ready，不应为了掩盖单侧消失跳过该门禁；但 `Show` 不读回真实可见性会造成假成功的例外。

8. **现有测试没有复现这个事务。** Headless 的尺度测试每次以同一显式布局结果检查四个 `ResolveSurfaceLayout` (`page_control_tests.cpp:467-507,940-1002`)，没有给左右不同历史 outset/context/backing；`RunOffscreenTests` 逐个新建 Scene 并读回像素/命中，但没有四个真实 HWND、动画、ULW/Show、单窗失败注入或共同 layout revision (`PageControl.cpp:2109-2193`)。故先前通过不能证明用户观察场景。

## Candidate diagnosis and minimal test seam

- **优先按失败阶段定位，而非预设根因。** 对同一 session/publication/layout/display/device 代的四窗，限频记录：目标 monitor/DPI、偏好和运行 scale、layout outset/bitmap limit、logical/presentation/backing、动画起终点和 opacity、资源/`GetDC`/ULW/`ReleaseDC`/`EndDraw`、SetBounds/Show 的返回值及调用点原始 HRESULT/Win32 error、真实 HWND valid/visible/RECT/owner/Z、Retry 次数及 UI required/committed mask。这样可判别屏外、互相覆盖、未 Show、alpha 零、呈现失败和陈旧帧。合法 Side 屏外**起点**不能直接报布局错误，须看动画 deadline 后终点。
- **确定性无 HWND seam：** 扩展 `page_control_tests.cpp`，给同一 `PptState` 注入左右不同的旧 outset/bitmap limit，分别调用生产 `ResolveRuntimePageControlLayout`，确认当前实现可以产生不同 pair 目标；修复后两窗必须消费同一 publication/display/device 代的最终解，并且逻辑主体在屏幕内、pair 镜像、两组无碰撞。还应通过生产 `ResolveStableBackingSize` 构造旧容量大于新 bitmap limit 的资源失败情形；不能只复制布局公式自证。
- **真实生产路径 seam：** 在 `PageControl.cpp` 的四客户端/Window Service 提交边界加可关闭的一次性注入（ULW、bitmap、SetBounds、Show 各一窗），使用隐藏 HWND 与可读回的 D2D/ULW 像素/命中检查两侧最终收敛；释放故障后无需用户再次输入。失败注入不应新增第二条渲染路径。检查 `PptPageCommitState` 在半对期间不 ack，恢复后才 ack。已有 `--bar-eraser-offscreen-test` 只测 Scene，尚不足以覆盖 Window Service。
- **预计最小产品改动文件：** `PageControl.cppm/.cpp`（整组布局输入与失败传播/backing 降级/诊断）、必要时 `Window.cpp`（Show/Hide/SetBounds 真实结果复核），`page_control_tests.cpp` 与已有 hidden/offscreen harness；`Bar.Scene.cpp` 仅当实际 Scene 包络/资源契约被证明错误时修改。无需 PPTCOM、UInk 或 Bar 布局器重写。

## Related specs and external references

- `.trellis/spec/native-desktop/rendering-and-ui.md:118-180,346-369,1569-1644`：唯一共享 Scheduler、四窗独立资源、PageControl/Bar 共用行为、白板共享窗、失败 Retry、页码门禁和主栏不参与避让。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md:176-186`：`BeginDraw`→ULW→`EndDraw` 完整失败事务。
- `.trellis/spec/native-desktop/index.md`：当前编译入口与 native 修改前检查。
- 本次未查外部文献，未做 Office/Windows GUI 现场验证。

## Caveats / Not Found

- 未获得用户故障时四窗的 HRESULT、ULW `GetLastError`、实际 HWND RECT/alpha/Z、DPI/分辨率和配置；**不能断言**单侧消失已由某一个候选机制导致。
- 未运行构建、headless、离屏或隐藏窗口测试；上述均为本轮源码审查，不把旧任务记录的通过结果算作本轮验证。
- 主栏展开与本次现象可同时存在，但代码中的位置避让只用四个 PageControl；没有发现 Bar 是布局障碍的证据。各窗都由 Drawpad 拥有，若 owner 可见性影响子窗，需要实测其全组效果，不能凭用户“只剩右边”归因于 owner。

## 修复前确定性布局回归（本轮实际执行）

- B 实施代理先只加测试，`InkeysHeadlessTests.vcxproj Debug|ARM64` **exit 0**；`InkeysHeadlessTests.exe --no-window` **exit 1（预期失败）**，新增断言 `one publication cannot give each peer a different fitted scale` 失败。
- 注入生产 `ResolveRuntimePageControlLayout` 的同一发布态：monitor `7680×4320`、DPI `2.5`、用户倍率 `3`；模拟左右旧 Scene 的 `(outsetDip, bitmapLimit)` 分别为 `(10,16384)` 与 `(35,512)`。两个独立求解得到同一底部 pair 的不同有效倍率。这证明当前**按 surface 局部旧资源输入重求整组布局**可不一致；它仍不是用户设备“左窗消失”的现场错误码或实际 HWND 复现。后续隐藏四窗测试须证明生产消费、ULW/窗口阶段与恢复。

## 修复后 B 验证阶段记录

- 当前产品改动令同一 publication/direct-move/display/device 代的四窗消费同一保守 outset/bitmap budget；Scene configure/bounds 失败不提交该代；backing 受当前 bitmap 上限约束；ULW 调用点立即保存 Win32 error。PPT Window Service Show/Hide/SetBounds 在 owner thread 读回真实结果。相同确定性失败的昂贵事务有界退避，页面 UI required mask 在失败侧恢复前不提前 ack。
- 新的 `--page-control-hidden-test` 在 `(-30000,-30000)` 屏外以生产 RenderPipeline、Window Service、四个真实 PageControl 客户端和 ULW 运行；一次性保持左底 ULW 失败（**注入** `ERROR_GEN_FAILURE=31`）时右侧先可见、左侧未提交且无页码 ack，解除后无额外 pointer/publication 即恢复左侧并 ack。DPI `96/144/192/240`、bottom-only/side-only/both、倍率大→小→大和白板覆盖返回的终态 HWND bounds、Scene 实际像素读回与命中检查 **exit 0**、`[PageControlHidden] failures=0`。[stderr](../../../../Build/Validation/pptui-regression-20260925/pagecontrol-hidden-third.stderr.log)。
- 首次隐藏测试的 12 个“final body”失败来自测试把尚未完成入场的 Scene 动画中间帧当终态；第二轮改为等待发布代、bounds/Scene 动画及 deadline。第二轮 DPI 变化停在旧帧，是测试直接改内部 DPI 原子却未调用产品显示变更唤醒；第三轮通过 `NotifyLayoutChanged()` 模拟真实显示通知后通过。这两次测试驱动修正不能被报告成用户现场根因。
- 一次 `/m` 全 Solution 尝试在托管 TLB 输出后无编译诊断退出，并留下 50 个由同一已结束 PID 创建的 `/nodemode:1 /nodeReuse:false` 孤儿节点；核对父 PID/命令行/启动时刻后仅清理该批节点，随后 ARM64 原生 MSBuild `/m:1` 完整 Solution **exit 0**。Headless 起初仍运行旧预修复断言的二进制，单独重建 `InkeysHeadlessTests.vcxproj Debug|ARM64` 后 `--no-window` **exit 0**；Bar offscreen **exit 0**、`[PageControlScene] failures=0`。
- 最终 `InkeysRepo.sln Debug|ARM64 /m:1` **exit 0**；`--page-control-hidden-test` **exit 0**、`[PageControlHidden] failures=0`，在 DPI 96/144/192/240 的四 HWND 最终几何、ULW、像素、命中和白板返回均通过。`INKEYS_PAGECONTROL_PRESENT_TRACE=1` 报告左底注入 ULW `error=31` → recovered；另注入资源 `hr=0x8007000E (E_OUTOFMEMORY)` 后把该窗旧 backing high-water 降到 `1×1`，重试用当前目标的 `555×188` 成功，半对期间 UI ack 未推进。[最终 stderr](../../../../Build/Validation/pptui-regression-20260925/pagecontrol-hidden-final.stderr.log) / [stdout](../../../../Build/Validation/pptui-regression-20260925/pagecontrol-hidden-final.stdout.log)。最终 Headless `--no-window`、Bar offscreen、i18n check 均 **exit 0**，详见本轮 verification 记录。
- 以上证明了可控的一侧 ULW 故障与四窗布局/呈现恢复路径；用户机器上“底右有、底左无”究竟是此类 ULW、资源、Show、alpha、动画还是其他原因，仍缺真实故障时的四窗日志，必须标 **NOT VERIFIED**。
