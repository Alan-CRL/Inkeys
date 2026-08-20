# 实施计划

- [x] 在 `Bar.Animation.cppm` 增加 Laser 阶段枚举、纯状态决策和笔型扩展槽位 helper。
- [x] 在 `Bar.RenderLoop.cpp` 将扩展入口改为按笔型独立进度、独立锚点绘制，并同步更新动画推进与活跃需求判断。
- [x] 在 `Bar.Initialization.cpp` 与 `Bar.RenderLoop.cpp` 修正一级菜单及按锚点锁存的二级浮窗文案和测量。
- [x] 在 `Bar.RenderLoop.cpp` 接入阶段化 Laser 芯层/红壳目标，按红壳在下、语义芯在上的顺序绘制，并隔离逻辑粗细与预览视觉粗细。
- [x] 扩展 `InkeysHeadlessTests/animation_tests.cpp`，覆盖完整阶段序列、反向、扩展交叉淡化和文案解析。
- [x] 静态复核 Slider/FineDial/快捷预设/Draw3 数据流未改变，检查新增动画值进入续帧和脏区条件。
- [x] 使用 ARM64 Host MSBuild 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`。
- [x] 运行 `Build\\ARM64\\Debug\\InkeysHeadlessTests.exe --no-window` 与 `git diff --check`；按项目约束未启动可见窗口，人工视觉矩阵留待独立验证。
- [x] 审查纠正 `LeavingShell` 提前读取新笔宽的问题：增加 `Hold/Laser/NonLaser` 纯目标策略，锁存 Laser 芯/壳 target，并用 `max(core, current shell)` 计算预览包络。
- [x] 最终 Trellis 全量审查修复初始化文件 BOM 与旧测试语义，重新通过完整 ARM64 Debug Solution、HeadlessTests 和差异检查。
- [x] 同步 `.trellis/spec/native-desktop/rendering-and-ui.md` 的六阶段时序、Hold 策略、包络与回归测试合同。
- [x] 修复帧内复用 solid brush 被红壳改色后继续用于 semantic core，导致 Laser 白芯变红；保持状态机与动画 target 不变。
