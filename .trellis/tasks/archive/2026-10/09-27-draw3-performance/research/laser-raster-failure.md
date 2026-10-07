# Research: Draw3 激光笔 GPU 提交失败与瞬态图层所有权

- Query: F-025 集成生产 Laser coverage、烘干、完整重绘、Present 失败是否传播；CPU 点何时销毁；最小修复和无 HWND 真 D3D 回归入口。
- Scope: internal（兼容性外部文档只按现有任务矩阵引用，未另行实测）
- Date: 2026-09-28

## Files found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：激光 layer、增量/完整覆盖、烘干、Present 与失败恢复的唯一生产协调者。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererLaser.cpp`、`Draw3.Renderer.cppm`：真实 D3D11 Map/Draw/Resolve，现有返回码边界。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp`：coverage 资源、device/resize 清理。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`：已有 `--draw3-renderer-failure-test` 实际 WARP FL11.0、无 HWND 的失败/重试测试入口；其余 `RunHiddenWindowIntegrationTest` 会建 HWND，不可在当前 GUI 限制下运行。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp`、`Draw3.TransparentPresentation.cpp`：Hardware→WARP FL11.0 和 DComp→ULW、FLIP 选择。
- `.trellis/spec/native-desktop/draw3-integration.md`、`draw3-shaders.md`、`errors-logging-and-resources.md`、`build-and-compatibility.md`：线程/设备、透明/alpha、失败事务与兼容边界。
- `.trellis/tasks/09-27-inkeys3-first-release-hardening/compatibility-matrix.md`：用户 Win7 FLIP 实测与未完成的真机矩阵。

## Findings

### F-025a：完整重绘可在真实 Map 失败后仍成功 Present（当前代码确认，真实 GUI 未复现）

`InkRenderer::DrawLaserCoverage` 在 style/global/InkData `Map` 失败时返回 -1（`Draw3.RendererLaser.cpp:134-207`）。但 `DrawLaserStrokeLayers` 的完整重绘分支丢弃该值（`Draw3.DrawingController.cpp:950-962`），`ResolveLaserStrokeCoverage` 为 void 且丢弃内部 `DrawLaserRectPass` 的 bool（`Draw3.RendererLaser.cpp:279-285`）。`ClearLaserCoverageRect` 也为 void，缺 RTV/Map 时静默返回（同文件 `:304-309`）。结果 `rasterSubmissionFailed` 可保持 false，正常 `PresentFrame` 被调用（`Draw3.DrawingController.cpp:7806-7879`）；`presentSucceeded` 再推进内容就绪/可见观测和 landing（`:7905-7944`）。这证明**失败没有进入错误帧门禁**，不等于已实测每种 GPU/驱动上具体缺像素。

增量路径相对较完整：`UpdateLaserIncrementalCoverage` 检查两个 `DrawLaserCoverage`、live clear，失败不推进 stable cursor/bounds（`Draw3.DrawingController.cpp:784-835`）；调用者切至 FullRedraw 并清空 scratch（`:7194-7218`）。但完整重绘失败未继续传播，所以上述失败仍存在。`DrawLaserStrokeLayers` 的增量 resolve 返回 false 后也转 FullRedraw（`:926-961`），同样落入不检查返回值的路径。

### F-025b：烘干失败会丢失当前批次唯一 CPU 几何（当前代码确认，故障可见程度待注入）

`FinalizeLaserStrokeLayer` 在 Up 时复制 `runtime.stroke.realPoints` 到 `completedPoints`、将 layer.runtime 置空（`Draw3.DrawingController.cpp:837-854`）。随后 `ShouldBakeLaserBatch` 只要求 contact 数为零（`Draw3.InkPrediction.cpp:100-109`），控制器在同帧调用 `BakeLaserStrokeLayers`（`Draw3.DrawingController.cpp:7432-7441`）。完整烘干对每层调用 clear/draw/resolve，却都不查成功；无条件 `layers.clear()`（`:901-919`）。外层也无条件把 `laserCoverageMode` 置 `Inactive`、`laserLiveBounds` 置空（`:7435-7441`）。同帧 Up 后 runtime 又被 Reset/recycle（`:7641-7687`）。因此如果 Map/resolve 失败，当前 batch 的 `completedPoints` 和可重试 layer 被销毁，剩下可能为空或部分写入的 GPU `laserCompositedColor`，下一帧无法从文档重放：Laser 故意不写 L2/Stored（`:7466-7470`）。这属于**瞬态可见激光笔画的不可重试丢失**，不是 UInk、文档或持久数据损坏。

`BakeLaserStrokeLayers` 的单层增量分支检查 coverage/resolve 并可降级完整重绘（`:861-900`），但降级分支同样无条件清层；失败后的 partial GPU compositor 若直接在下一帧重播会二次 source-over，不能仅把 `layers.clear()` 改成“失败时保留”就称修复。`Down` 在 Hold/Fade 还会先烘旧 batch，调用点同样忽略结果并马上接收新 layer（`:3103-3146`）。

### F-025c：成功烘干后没有 CPU 复原源，device lost 会提前抹去未完成的 Laser Hold/Fade（静态确认）

成功烘干也清除 CPU layers（`:893-897`, `:917-919`）；之后 Hold/Fade 仅从 GPU `laserCompositedColor` resolve，且其 void 返回同样吞掉失败（`Draw3.DrawingController.cpp:7859-7866`；`Draw3.RendererLaser.cpp:296-302`）。graphics recovery 清 GPU coverage 后只根据**剩余** `laserStrokeLayers` 重算 layer bounds（`Draw3.DrawingController.cpp:6395-6418`）；此时已烘干批次没有 CPU 源。Resize 正常路径尝试复制旧 compositor texture（`Draw3.Renderer.cpp:377-411`），但 device lost/重建不适用。这里可使设计中的短暂 Laser 轨迹提前消失；因 Laser 非持久工具，不应标为文档损坏。实际设备丢失触发及持续时间未测。

### F-025d：Present 失败与覆盖失败必须分层记账

`DrawingController::PresentFrame` 对 presenter 失败设 recovery/full present（`Draw3.DrawingController.cpp:1518-1535`）；主循环的 `rasterSubmissionFailed` 处理可停止 Present、要求 active L0/L1 重建、计数限频并检查 device removed（`:7881-7904`）。Laser 调用未向这个布尔值传递失败，使现有保护不可达。即使完成 bool 传播，D3D `Draw` 命令异步失败仍可能只在 Present/设备状态暴露；不能把 bool true 当作光学成功，继续以实际 `Present` 回执为门禁。`ResolveLaserCompositedColor`、`DrawLaserDots`/粒子也目前是 void/忽略 Map；后两者是相邻的瞬态边界，应另记，不在此把所有绘图接口无差别改型。

## Minimal fix / contract proposal

1. `Draw3.Renderer.cppm`/`RendererLaser.cpp`：让 `ClearLaserCoverageRect`、`ResolveLaserStrokeCoverage`、`ResolveLaserCompositedColor` 返回 `bool` 并原样转发 `DrawLaserRectPass` 的结果；非空目标缺资源为 false，裁剪后确实无像素为合法 true。`DrawLaserCoverage` 现有 `int` 保留。预热调用可以有意识忽略返回，生产调用必须检查。
2. `DrawLaserStrokeLayers` 返回 bool。完整重绘逐层检查 clear/draw/resolve；失败立即停止当前 frame，设置 `rasterSubmissionFailed`，不调用 Present。增量 resolve 失败可转完整重绘，但完整重绘失败也必须上报。下一帧强制全 canvas 重建 backbuffer，以清掉失败前可能已有的部分像素。
3. `BakeLaserStrokeLayers` 返回成功/失败并只在成功后释放当前 CPU layers、提交 `laserStableBounds`/mode。失败保留 CPU 点和 dirty 以重试，绝不把“已提交”状态推给 Present。**部分 source-over 的重试必须具备事务边界**：优先在独立 scratch compositor 上从稳定颜色复制后烘当前批，所有 pass 成功才交换/发布；或在 Laser 生命周期保留全部 CPU batch，从透明覆盖重新构建。仅保留当前 `layers` 而直接重试现有 compositor 会叠色；即便使用 scratch，device lost 后已烘旧批次仍需 CPU 源或明确作为独立未闭环风险。
4. 主循环两个 bake 调用点（同帧旧 batch→新 Down、末次 Up）均检查结果。旧 batch 失败时不清旧 layer 或启动新 batch 的成功状态；若新 Down 必须继续接收输入，保留旧+新 CPU layer 并锁定 FullRedraw，不能在 GPU 故障时丢 Down/Up 样本。失败计入统一有界错误处理，但不要让错误时的 `activeLayerRebuildPending` 误称 Laser 已重建。
5. 性能/资源权衡：Laser CPU 层若为了 device-lost 重建延长到整段 Hold/Fade，必须有已有输入/几何容量边界和 fade 结束清理；新增全尺寸 scratch 时测显存峰值。不能以关粒子、减画质、降低帧率规避失败。

## No-HWND regression plan

现有 `Draw3.HiddenWindowTest.cpp::RunRendererFailureCommitTest`（`:2187-2349`）已经直接创建真实 WARP FL11.0 device、把合法 `D3D11_USAGE_DEFAULT` 非 CPU 可写 buffer 注入 `InkRenderer::inkDataBuffer`，触发真实 `Map` HRESULT，恢复 writable buffer 后复验 Pen/Highlighter/Eraser/Shape。可以沿用**同一无 HWND 测试入口与真实生产 renderer**，为 Laser 添加有/无 CPU 可写 buffer 的 `DrawLaserCoverage` 失败→恢复断言，并在可读回 RTV 上验证像素或至少真实资源状态；不要在产品里留测试开关或复制一套烘干算法。为验证 controller 的 `layers`/bounds 提交，可在生产 helper 所在翻译单元增加无 HWND 调用入口，直接调用生产 `BakeLaserStrokeLayers` / `DrawLaserStrokeLayers`；失败用真实 Map 错误产生，断言失败时仍保留同一 CPU 点、相同 layer ID 与 dirty，成功后仅提交一次，重复 Present 不叠色。多 layer 第一层成功第二层失败必须测试事务/颜色顺序；单纯第一层 Map 失败不能证明部分烘干安全。测试用离屏 RTV/readback 和 WARP，不创建隐藏 HWND，不使用外部真实用户文档。

回归还要覆盖：单点、快速 Move、Up 后新 Down、Cancel、多 contact、Fade 中 resize/graphics recovery、WARP/HW，比较 failure 后恢复的 BGRA 与无故障同序列。`RunHiddenWindowIntegrationTest` 会调用 Window Service 创建 HWND（`:2352+`），当前授权下只能列 MANUAL；无 HWND 测试也不能证明 ULW/DComp Present、光学延迟或 Win7 真机。

## Win7 / FL11 / transparency constraints

生产 `InitializeGraphicsDevice` 先 Hardware `[11.1,11.0]`，`E_INVALIDARG` 用 `[11.0]` 重试，Hardware 仍失败再用同策略 WARP（`Draw3.GraphicsInitialization.cpp:49-99`）。Win7 SP1+仅 KB2670838 的 FL11 硬件和无 FL11 硬件/WARP 格子目前只有静态路径与 Win11 ARM64 无 HWND WARP 探针，**没有 Win7 成功 Present 证据**。生产交换链仍固定 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`（`Draw3.TransparentPresentation.cpp:635-652`），DComp API 缺失后仅尝试 ULW（`:689-700`）；两种 DWM 模式禁用。必须遵守用户在 Win7 实测 FLIP 可用的约束，不引入 discard/bitblt swap-effect 回退。项目矩阵记录的 Microsoft 通用 swap-effect 文档与该实测相冲突；此调查没有新的真机原始日志可消除差异。

## Caveats / Not Found

- 未构建、运行、GUI、性能或 Win7 真机；本文的“确认”是针对当前源码控制流与可构造的真实 D3D Map 失败，不是已执行的 Laser 故障注入结果。
- 尚未看到 production `BakeLaserStrokeLayers` 被单独暴露给无 HWND 测试；现有 `--draw3-renderer-failure-test` 只覆盖 Pen/Highlighter/Eraser/Shape，Laser 扩展需要在同翻译单元调用生产 helper 或重构最小可复用边界。
- `DrawLaserRectPass` bool 只能报告同步 Map/资源失败。GPU 命令异步执行、驱动/device removed、ULW/DComp 错误需 Presenter 回执及设备重建测试独立验证。
- 当前 specs 的 `draw3-integration.md` 已记录用户 FLIP 与 DComp→ULW 合同；`build-and-compatibility.md` 较早的 README 最低 Windows 7 RTM 声明是泛项目描述，不能覆盖本任务 Draw3 的 Win7 SP1+KB2670838 发布基线。
