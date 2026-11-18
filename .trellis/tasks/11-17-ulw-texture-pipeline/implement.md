# 第2步-B 实施计划

> 分工：Claude 编写全部实现；Codex（gpt-6.1-sol / high，只读）在完成后审阅。收尾顺序：Claude 独立审阅 → Codex 审阅 → 修复 → 复验。不并行写入。
> 改动的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF（Git Bash `sed -i` 会改 CRLF，改完核对）；构建 `InkeysRepo.sln Debug|ARM64`（ARM64 原生 MSBuild，`-t:Inkeys;InkeysHeadlessTests`，超时 ≥ 5 分钟，PowerShell 内 `Remove-Item Env:PATH` + `MSBUILDDISABLENODEREUSE=1`）；增量链接偶发 LNK1104/LNK1000 删 `Inkeys.ilk` 重试。
> 开工命令：`python ./.trellis/scripts/task.py start 11-17-ulw-texture-pipeline`。

## 步骤

0. **开工核对**（有矛盾先停下汇报）：核对 design 引用的行号与调用点；`presentation_.SwapChain()` 全部调用方在 ULW 下能接受空值（C:15537、`Draw3.HiddenWindowTest.cpp:2963`、信息面板）；帧尾 5 个等待分支（C:23680–23770）与 C1 位置（C:21768）；SelectionUlw 只走 `selectionUlwPresenter`；`compositionChangedRequested_` 在主循环中的处理位置；`DWM_TIMING_INFO` 在 Win7 SDK 下可用字段。
1. **B1 Renderer**：`InkRenderer::Resize` 接受 `swapChain == nullptr`（跳过 `GetDesc1/ResizeBuffers`，其余不变）。补无交换链 Resize 的保留 L2/L1 测试（沿用 R:888 附近写法）。
2. **B1 呈现器**：ULW 模式在 `TryInitialize` 中不建交换链，`renderer->Init(nullptr)`；`PublishInformation` 与运行时描述日志改为输出独立纹理描述、读回模式、DWM 状态；DComp 与 DWM 玻璃模式不变。
3. **单遍 SIMD 逐行复制**（design §5.9，独立可测，先做）：`CopyAndInspectUlwDirtyRows` 非诊断实例改为 SSE2 / NEON 单遍实现，保留标量模板；`RunUlwDirtyCopyBenchmark` 增加 SIMD/标量逐字节对照（随机像素、不同宽度与行尾、主画布/辅助）与耗时输出。
4. **纯逻辑组件**（无 D3D 依赖，各自新头文件）：
   - `Draw3.UlwReadbackPacing.h`：即时/流水自适应状态机（design §5.3）与 C3 决策。
   - `Draw3.CompositionClock.h`：合成时钟模型（推算下一次垂直同步、250 ms 重读判定、失效回退、deadline、`commitEstimate`）与提交判定（简单间隔 / 对齐“下一帧是否赶得上”）、相位微调目标（每帧 ≤10%、约数/非约数）。设计为阶段 D/E 可复用。
   - 新增 `InkeysHeadlessTests/draw3_ulw_readback_pacing_tests.cpp`、`draw3_composition_clock_tests.cpp`，在 `animation_tests.cpp` 注册（仿照 `RunDraw3FrameLatencyGateTests`，各加 `--...-only` 参数）。
5. **双槽 Presenter**：`UlwDirtyRectPresenter` 异步模式（`stagingSlots[2]`、`pending`、`accumulatedDirty`、`Flush`）；拆出 `CompletePending(wait)`（Map → SIMD 复制 → ULW）与 `DiscardPending()`；`Present` 异步时变为“拷出判定 → 拷贝入队 → C2 背压”；同步模式保持现有路径。Resize/Reset/`ReleaseReadbackResources` 时丢弃并清空累计脏区。
6. **控制器接口**：`TransparentPresentationOptions::{enableUlwAsyncReadback, enableUlwCompositionAlign}`；`TransparentPresentationController` 新增 C1 `ProbeDeferredPresent()`、C3 `CompleteDeferredPresent(bool idleNext, …)`（返回成功与阻塞耗时）、对齐查询（下一帧开始的建议等待时长）；`SetOutputTarget` 切到 SelectionUlw 前完成主画布待完成帧并拷出累计脏区；熔断转同步。
7. **DWM 状态与刷新间隔**（design §5.10）：启动与 `compositionChangedRequested_` 处理时检测 `DwmIsCompositionEnabled`，刷新间隔来源 DWM → `EnumDisplaySettings` → 60 Hz，记录日志并刷新合成时钟。
8. **帧循环接入**（`Draw3.DrawingController.cpp`）：C1 放在 `PrepareFrameLatency` 旁；C3 用一个 lambda 统一放在各帧尾等待之前（空闲分支 `idleNext=true`）；固定帧截止等待分支按对齐建议调整等待时长，打断式唤醒不受影响；`Map` 阻塞与对齐等待计入 `frameLatencyWaitMs_` 与 `metricsState_->frame.latencyWaitMs`；完成失败 → `graphicsRecoveryPending_ = presentation_.RecoveryPending()` + `window_.RequestFullPresent()`；帧外 Present（C:15487/15521）之后立即完成。
9. **诊断**：`[Draw3Diag][ulw-pipeline]`、`[Draw3Diag][ulw-align]` 每秒汇总，DWM 状态、模式切换、熔断、对齐启停日志；`ulw-pixels` 增加 `map_wait_ms` 与完成点。
10. **配置与设置页**：`Other.Config.cppm` 新增 `Experimental.Inkeys3.Draw3.EnableUlwAsyncReadback`（NoUpload，`IdtAtomic<bool>`，默认 true）与 `Experimental.Inkeys3.Draw3.UlwCompositionAlign`（同上，无界面）；`IdtMain.cpp` → `Draw3.Host.h/.cpp` → `TransparentPresentationOptions` 透传（参照 `EnableWaitableWait`，IdtMain.cpp:3918、Host.cpp:2221）；`Setting.cpp` 在 waitable 开关（约 4664 行）之后新增 ToggleRow：“ULW 模式下异步读回”，说明“在 Windows 7 等使用 ULW 呈现的系统上，让 CPU 计算与 GPU 绘制同时进行，并按显示器刷新时机提交画面，减少卡顿与延迟”，`RestartRequired`；20 语言 i18n 与 `IdtI18nKeys.g.h` 同步。
11. **构建与自测**：Debug|ARM64 构建；`InkeysHeadlessTests.exe --no-window`（新测试 + Renderer 相关）；`Inkeys.exe --draw3-ulw-copy-benchmark`；双槽顺序/节流合并/Resize 丢弃自测；i18n sync/check；编码、BOM、EOL 与 `git diff --check`。HiddenWindowTest 强制 ULW 场景需用户授权。
12. **审阅**：Claude 独立审阅 → Codex 只读审阅 → 修复 → 复验。
13. **交用户实机**（清单见 design §9）。通过后更新规范（`draw3-integration.md` 新 Scenario；`settings-performance-and-drawing.md` 两个新配置键与“渲染与兼容”节顺序），经授权提交；之后用实机日志评估 ULW 提交线程（D4）。

## 风险与回退

- 某驱动上 C2 的阻塞 `Map` 实际等到本帧：流水模式退化为同步，不比现状差；`map_wait_ms` 可见，必要时固定即时模式。
- 自适应阈值或对齐余量不合适（来回切换 / `lateCount` 偏高）：常量可调；对齐可用配置键单独关闭，异步整体可用设置页开关关闭。
- 相位调整影响帧节奏或 A2 光标节拍：只调固定帧截止等待，打断式唤醒不变；出现问题先关对齐对照。
- Win7 去掉 HWND 交换链后出现意外：只影响 ULW 初始化，开工后优先请用户在 Win7 冒烟测试 B1。
