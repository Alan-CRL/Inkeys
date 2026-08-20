# Draw3 迁移实施计划

## 阶段 1：导入与工程登记

- [x] 固化源仓库 commit、文件清单、许可证和编译配置；排除 `.git`、`.vs`、输出目录及独立 EXE 入口。
- [x] 复制并重命名 Draw3 模块、一方依赖、shader/资源到 Inkeys 目录，保持源编码与换行。
- [x] 更新 `Inkeys.vcxproj`、`.filters`、模块依赖与链接库，使导入代码在产品工程中可编译。
- [x] 核对映射：`draw3/*.cpp|*.cppm -> Inkeys/Inkeys/Drawing/Draw3/Draw3.*`，HLSL/CSO -> `Drawing/Draw3/Assets`，第三方头文件 -> `Inkeys/additional/`，固定库 -> `inkStrokeModelerTest/lib/`；shader 资源 ID 301-304 只能在 `Inkeys/resource.h`/`Inkeys/Inkeys.rc` 使用。
- [x] 明确排除源 `main.cpp`、demo/性能 HUD、独立测试窗口、`.git/.vs`、ARM64/Win32/x64/Release/TestResults 输出和第三方 `.cc`；保留三架构 `ink_stroke_modeler_merge.lib`。
- [x] 将可复用纯逻辑测试登记到 `InkeysHeadlessTests`。
- [x] 回滚点：仅撤销新增文件和工程登记，不触碰现有 Draw2 启动路径。

## 阶段 2：外部 HWND Host 与透明呈现

- [x] 将 Draw3 `WindowController` 拆为外部 HWND 适配器/mailbox，移除产品路径的窗口创建、topmost 和销毁职责。
- [x] 在 Window Service 创建主 Drawpad 与 presentation-only sibling；主窗按能力探测预置 DComp 样式，辅助窗固定 layered/transparent，三态可见性由 owner thread 批量切换。
- [x] 让 Draw3 presenter 接受双 HWND，共用独立 D3D11.1 device、单一 swap chain、renderer/final backbuffer，并保留主 DComp/DWM/ULW 与辅助 ULW target。
- [x] 修正 ULW alpha/dirty-rect 行为，确保透明背景和窗口链不被整窗覆盖。
- [x] 添加 presenter 模式选择、样式合同和 shutdown 顺序的无窗口测试。
- [x] 添加输出 generation、内容 revision、全帧 clean 握手，选择态先预热隐藏目标；Laser/粒子淡出后才隐藏空辅助窗。
- [x] 回滚点：Draw3 host 可编译但不接管 `drawpad_main`。

## 阶段 3：输入、绘制与产品命令接线

- [x] 以 Draw3 RTS 替换 Draw2 RTS 初始化，确保同一 HWND 只有一套 producer。
- [x] 将现有 `DrawpadMsgCallback` 变为消息适配器，并由 Draw3 runtime 执行绘制循环。
- [x] 新增最小 Bar/产品状态桥接，映射工具、颜色、宽度、清屏、undo/redo 和已实现页面命令。
- [x] 保留 Draw3 速度橡皮、固定橡皮和 `SpeedEraserOcController`；删除旧 Draw2 压感橡皮实现及设置入口。
- [x] 隐藏保存、超级恢复、直线拉直、输入测试等未实现设置入口。
- [x] 添加输入队列、工具映射、历史命令与停止唤醒测试。
- [x] 回滚点：恢复 `drawpad_main` 调用即可重新启用旧实现。

## 阶段 4：Draw2 隔离与清理

- [x] 将 `IdtDrawpad.cpp` 从编译项移到 `None`，保留文件内容；按依赖审计处理 Draw2 专属 RTS/辅助文件。
- [x] 删除 Draw3 demo `wmain`、独立窗口线程、性能 HUD 和无产品用途的调试结构之编译登记。
- [x] 搜索确认无 Draw2 detached drawing/input worker、双 RTS、独立 Draw3 `CreateWindowEx` 产品路径。
- [x] 更新 Trellis 原生桌面规范，记录 Draw3 的 HWND/设备/透明呈现/输入所有权。

## 阶段 5：验证

- [x] 静态检查：工程项、模块引用、窗口 role/owner、样式、输入唯一性、Draw2 保留但不编译、未实现设置隐藏。
- [x] `git diff --check` 和工程文件结构校验。
- [x] 使用 ARM64 `MSBuild.exe InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64`，超时不少于 5 分钟。
- [x] 运行 `InkeysHeadlessTests` 的无窗口模式和迁入的 Draw3 纯逻辑测试。
- [x] 不启动 Inkeys GUI、draw3 demo 或任何可见测试窗口，不使用 computer-use。

## 当前验证状态（2026-08-16）

- 来源快照 `8d045298eaaac76f752b4f8b5f3303b3520e50b7`、文件映射、资源 ID 和排除清单已完成静态审计。
- 源任务历史已统一移动到 `.trellis/tasks/archive/2026-08/draw3-source/`；5 个源 active task 保留原状态但不污染目标 active task 列表。
- `InkeysRepo.sln Debug|ARM64` 与 `Release|ARM64` 已用 ARM64 MSBuild `/m:1` 构建通过，`PptCOM`、`Inkeys` 和 `InkeysHeadlessTests` 均成功。
- `Build\\ARM64\\Debug\\InkeysHeadlessTests.exe --no-window` 通过：`PASS animation correctness`。
- `Build\\ARM64\\Debug\\Inkeys.exe --draw3-hidden-test` 通过（退出码 0）：真实 D3D11.1 硬件、主/辅助隐藏 HWND、DComp/DWM2/DWM/ULW、双目标 generation/clean、命令/resize/停止流程均执行；旧 DComp 主 HWND 上的 ULW 启动按真实不可变样式失败并完整清理 Host，随后重建 legacy 主/辅助窗口链并验证所有 fallback。
- 最终已删除激光增量 benchmark/诊断结构和热路径诊断计时；Ink Stroke Modeler/Abseil `.cc` 全部删除，`type_matchers.h` 等头文件保留，不引入 gtest/gmock 产品依赖。
- 最终 `git diff --check`、迁入文件 CRLF/BOM 与静态合同检查通过；实现修改保持未提交且未 push。

## Review Gates

- 导入后先确保模块/依赖独立编译问题已收敛，再切换产品入口。
- Draw3 接管前确认 Window Service 仍唯一创建/销毁 Drawpad HWND。
- Draw2 停止编译前确认所有 Bar/PPT 命令已有 Draw3 桥接或明确留空。
- 最终检查覆盖所有改动包，并核对 PRD 每项验收标准。
