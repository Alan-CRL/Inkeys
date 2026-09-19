# Draw3 低速停笔预测收敛修复实施计划

## Implementation

1. 在测试宿主与产品各自的 `InkPrediction` / `StrokeGeometry` 增加同构的 modeled-tip 收敛判定：finite 检查、raw endpoint 误差、`Result::velocity × frame interval` 门槛。
2. 扩展 `UpdateIdleFreezeState`，把 `modelSettled` 作为必要条件；保留 live-tip 停笔时长和三帧 L0 position/radius 稳定门槛。
3. 在两侧 `DrawingController` 的帧首 raw snapshot 消费后，为未结束、非 reconnect 的普通笔 runtime（产品 Pen/HardPen，测试宿主 Pen）增加 stationary model advance：
   - 使用最近接受进入模型路径的 snapshot position/stylus state 与单调 frame logical time；
   - `inputSpeed=-1`，不写真实速度相关字段；
   - 仅在 model 未 settled 且 stroke 未 frozen 时执行；
   - stationary `Update` 明确失败后按 contact 锁存，避免逐帧重试和日志；下一份成功真实输入或 reconnect 解除锁存；
   - 添加简短中文注释说明不能用重复 prediction 代替模型追赶。
4. 在渲染更新后重新计算 model convergence，并传入 freeze 判定；确认 resize/reconnect/Up 分支不执行 synthetic advance。
5. 若模型级测试表明 stationary 输出近重复点仍明显超预算，只在 idle-only append 路径增加 position/radius 容差合并；不要修改真实 snapshot 的 append 行为。

## Tests

1. 在 `inkStrokeModelerTestTests/contact_input_tests.cpp` 用真实 `StrokeModeler` 构造低速轨迹：证明只重复 `Predict` 时内部 modeled endpoint 不前进，而 stationary Update 后会追到 raw endpoint。
2. 断言收敛前 endpoint error/velocity gate 阻止冻结，收敛后三帧才冻结。
3. 断言 frozen 后继续模拟长按不会增加 modeled/real/L0 点数。
4. stationary settle 后送入下一份真实 Move，断言时间单调、Update 成功，预测/可见 endpoint 不出现与实际位移不成比例的甩出。
5. 同坐标 Up 后最终 endpoint/radius 相对停稳 L0 不超过现有视觉容差。
6. 覆盖 prediction disabled/空 prediction、非 finite result 以及两个独立 ActiveStroke 的收敛隔离。
7. 静态/现有测试确认 Eraser、Highlighter、Laser、Shape 未进入新增路径。

## Validation

1. `git diff --check`，检查仅任务文件和预期源码/测试变更，确认 BOM/CRLF 无大面积 diff。
2. 运行 `inkStrokeModelerTestTests` 的无窗口测试，确认新增及现有断言通过。
3. 按仓库规则定位 Visual Studio ARM64 原生 MSBuild，在同一 PowerShell invocation 中移除重复 `PATH` 并设置 `MSBUILDDISABLENODEREUSE=1`。
4. 构建完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64`，至少允许 5 分钟；核对退出码和 shader/resource 链。
5. 按根 `AGENTS.md` 用 ARM64 原生 MSBuild 构建完整 `InkeysRepo.sln Debug|ARM64`，再运行 `InkeysHeadlessTests.exe --no-window`。
6. GUI/Computer Use 未获当前授权，不自动启动主窗口。交付时给出人工复现清单：低速停笔、停稳后原地 Up、停稳后再移动、多 contact 一动一停、prediction disabled 对照。

## Review Gates

- 收敛条件不能退化为固定延迟或只比较 prediction 数组。
- synthetic idle sample 不得写入真实速度/压力基准。
- frozen 后点数必须恒定，不能靠长期重复点维持稳定。
- 不修改第三方 modeler、HLSL 或其他工具语义。

## Rollback Point

源码改动可按三处独立回滚：controller stationary advance、收敛 helper、freeze predicate；没有持久格式或资源迁移。

## Validation Results (2026-09-19)

- `inkStrokeModelerTest.sln /t:Build /m:1 Debug|ARM64`：通过。
- `ARM64/Debug/inkStrokeModelerTestTests.exe`：通过，包含低速停笔、prediction 开/关、finite gate、长停点数恒定、恢复 Move 与 Up 收敛回归。
- `InkeysRepo.sln /t:Build /m:1 Debug|ARM64`：通过；仅有既有第三方数值转换 warning。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：通过。
- `Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test`：通过，覆盖产品 Host、RTS、绘制线程和 presenter 隐藏集成。
- `git diff --check`、Trellis context validate、源码 UTF-8 BOM + CRLF 检查：通过。
- 可见 GUI 与真实设备人工复现未执行（仓库规则禁止默认启动交互式窗口）；保留为维护者验收项。
