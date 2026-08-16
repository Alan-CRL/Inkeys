# 迁移 Draw3 并接管 Inkeys Drawpad

## Goal

将 `D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3` 中的 Draw3 完整迁入当前 InkeysRepo，令 Draw3 接管现有 Drawpad 窗口的输入、绘制与透明呈现，同时保留 Inkeys 的窗口链、生命周期和产品兼容边界。

## Requirements

- 完整导入 Draw3 的模块、着色器、资源、第三方头文件、固定版本模型库和可复用测试，保留来源说明；不复制源仓库的 `.git` 目录、其他构建产物和独立示例程序入口。
- 将导入内容重命名并整理到 Inkeys 项目内，更新 `Inkeys.vcxproj` 与 `.filters`，同时同步本次迁移发现的 Trellis 原生桌面规范。
- `Inkeys/IdtDrawpad.cpp` 及 Draw2 相关实现继续保留在仓库，但不得再参与 Inkeys 产品编译或启动；处理其剩余引用，禁止 Draw2 detached 绘制/输入线程运行。
- Draw3 复用 Window Service 创建的主 `WindowRole::Drawpad` HWND，并接管其消息、输入和绘制；允许 Window Service 创建一个不绑定输入/文档/Host 的 `DrawpadPresentation` sibling，禁止第二套 Draw3 Host 或输入运行时。
- 保持 UI/PPT owned-popup 继续 owned 到主 Drawpad；辅助窗与主 Drawpad 同 owned 到 Freeze，只用于选择模式 ULW 穿透呈现。
- Draw3 保留独立的 D3D11.1 device/context、DXGI adapter/factory、交换链以及 DComp、Win7 DWM、ULW 透明呈现实现；不得复用 `Inkeys.UI.RenderPipeline` 的共享设备。
- 同一 Drawpad HWND 只允许一套 RealTimeStylus producer。Draw3 输入接管后，Draw2 RTS 不得同时绑定或重复发布输入。
- 将现有 Bar/产品命令以最小桥接接入 Draw3 已具备的画笔、荧光笔、固定橡皮、激光、图形、撤销/重做及画布命令。
- 保留 Draw3 速度橡皮及其 `SpeedEraserOcController`，删除旧 Draw2 压感橡皮模式和设置入口；普通笔/荧光笔仍保留压力宽度。
- 文件保存、超级恢复、直线拉直、输入测试等尚未准备好的 Draw3 功能不得伪实现，相关产品设置项暂时隐藏或保留为空接口。
- 删除仅供独立实验程序使用的调试代码、窗口结构和启动路径；保留无窗口测试及必要诊断能力。
- 迁移必须保持透明像素、dirty rect、点击/滚轮穿透、窗口层级和 owned-popup 行为正确；主 Drawpad 不动态设置 `WS_EX_TRANSPARENT`，选择态由固定穿透的辅助 ULW 窗口显示同一最终 backbuffer。
- 保持原文件编码和换行；仅修改迁移所需内容，不做无关重构。
- 不创建 commit，不 push，不使用 computer-use，也不启动任何可见窗口。

## Acceptance Criteria

- [x] Draw3 所需模块、着色器、资源、第三方头文件和三架构固定模型库均在当前仓库内，并有可追溯的来源说明。
- [x] `InkeysRepo.sln` 的 `Debug|ARM64` 使用 ARM64 MSBuild 完整构建通过，且 `PptCOM.csproj` 依赖正常参与构建。
- [x] `Inkeys/IdtDrawpad.cpp` 仍存在，但 `Inkeys.vcxproj`/`.filters` 中不再作为 `ClCompile` 编译。
- [x] 产品启动路径只有一套 Draw3 Host/输入运行时；Window Service 创建主 Drawpad 与一个 presentation-only sibling，Draw2 绘制线程和 RTS 不启动。
- [x] Draw3 使用自己的 D3D11.1 设备、交换链和 DComp/DWM/ULW presenter，未接入共享 UI RenderPipeline。
- [x] Drawpad 的 owner 链、PPT/Bar owned-popup、透明 alpha、dirty rect 与 click-through 合同有静态测试或代码级验证。
- [x] 画笔、荧光笔、固定橡皮、激光、支持的图形、清屏及已具备的历史命令能够通过产品状态/命令桥接到 Draw3。
- [x] Draw3 速度橡皮和固定橡皮可用，旧压感橡皮实现及设置入口已删除；未完成的保存、超级恢复、直线拉直、输入测试设置入口已隐藏。
- [x] Draw3 输入队列、文档/历史、命令桥接、透明呈现模式选择和退出顺序具有无窗口测试覆盖。
- [x] 所有静态检查与无窗口动态测试通过；验证过程未创建可见窗口，也未使用 computer-use。

## Out Of Scope

- 实现 Draw3 尚不具备的文件保存、超级恢复、直线拉直和交互式输入测试。
- 将 Draw3 图形设备或交换链合并进 Inkeys UI 的共享渲染管线。
- 删除 Draw2 源文件历史，或迁移无关 UI/PPT/配置模块。

## Migration Record

- 目标仓库分支：`draw`；源仓库：`https://github.com/Alan-CRL/Inkeys3-Draw3`；源 commit：`8d04529`。
- 历史合并提交：`1bc302d1 merge: import Draw3 source history`；首轮实现提交：`fc9a8b8 feat(draw3): integrate Draw3 drawpad runtime`。后续固定库调整保持未提交且不 push。
- 文件映射：Draw3 模块、Host、Bridge、Product、透明 presenter 和 shader 迁入 `Inkeys/Inkeys/Drawing/Draw3/`；Ink Stroke Modeler/Abseil 头文件迁入 `Inkeys/additional/`；三架构固定库保留在 `inkStrokeModelerTest/lib/`。
- shader 资源 ID 由源 101--104 映射到目标 301--304，名称统一为 `IDR_DRAW3_*`；不覆盖目标 `resource.h`。
- 排除项：源 `main.cpp`、独立 demo/测试窗口、可见 performance HUD、`.git/.vs`、ARM64/Win32/x64/Release/TestResults 构建输出和第三方 `.cc`；仅保留固定版本 `ink_stroke_modeler_merge.lib`。
- 所有权记录：Window Service 唯一创建/显示/隐藏/销毁主 Drawpad 与辅助呈现 HWND；Draw3 独立拥有 D3D11.1 device/context、单一 DXGI swap chain、双 target presenter 和绘制线程；Draw3 是唯一 RTS producer；owner 层级为 `MagnifierHost -> Freeze -> {DrawpadPresentation, Drawpad -> PPT/Bar}`。
- 源任务历史：统一移动到 `.trellis/tasks/archive/2026-08/draw3-source/`；5 个源 `in_progress` 任务位于其 `active/` 子目录，原已完成记录按 2026-07/08 月份保留，不覆盖目标任务。

## Verification Log

- 已完成：历史合并、工程登记、模块/命名空间重命名、Draw2 编译隔离、Window Service 样式接口、Draw3 外部 HWND Host 和产品桥接。
- 已完成：来源/资源映射、资源 ID 301-304、HWND/owner/Z 序、独立设备/交换链/透明 presenter 和唯一 RTS 的静态审计。
- 已完成：ARM64 `InkeysRepo.sln` `/m:1` Debug 与 Release 构建；输出确认 `PptCOM.csproj`、`Inkeys.exe`、`InkeysHeadlessTests.exe` 成功。
- 已完成：`Build\\ARM64\\Debug\\InkeysHeadlessTests.exe --no-window`，输出 `PASS animation correctness`。
- 已完成：`Build\\ARM64\\Debug\\Inkeys.exe --draw3-hidden-test`，退出码 0；测试覆盖主/辅助固定样式、Freeze sibling owner、互斥可见、同步 bounds、输出 generation/clean 握手，以及 DComp/DWM2/DWM/ULW、命令、Clear、resize 和停止流程，未显示产品窗口。
- 已完成：移除 `LaserIncrementalDiagnostics`、`[LaserPerf]`、诊断开关和激光热路径诊断计时；保留激光增量覆盖与 fallback 算法。
- 已完成：`git diff --check`、迁入文件 CRLF/BOM、Draw2 `ClCompile` 排除、资源 ID 冲突、完整工程登记、独立设备和窗口创建路径静态审计；测试专用 `type_matchers.cc` 以 `None` 保留，不进入产品编译。
- 说明：实现改动仍在工作树中未提交，未执行 push；`trellis-finish-work` 的自动归档/日志提交因用户明确要求“不提交”而未执行。
