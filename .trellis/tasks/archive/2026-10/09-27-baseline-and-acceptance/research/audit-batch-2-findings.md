# Audit batch 2 — interim handoff

- 范围：父任务 `audit-coverage.tsv` 数据行 201–400，终点 H0 `8b156fca59f0337a6afc6d722941666fcf143080`。
- 已对 200 个 SHA 运行逐父 `git diff --no-ext-diff --no-textconv --no-renames --submodule=short --unified=3`，在 `audit-batch-2.tsv` 保存各父差异的文件/增删行/摘要。含两个 merge（228、383）；逐父差异已经读取，冲突解决与当前代码审查仍待继续。
- 200 行均已按实际差异内容标注语义类别、H0 产品路径映射、结论与验证边界；其中 44 行是纯 workflow/docs，10 行 Canvas Navigation 代码仍受 H0 的 `kCanvasNavigationProductIntegrationEnabled=false` 产品门禁约束，2 个 merge 已读双父及 combined diff。早期独立 demo `main.cpp` 不直接参与产品编译；它的设备/呈现合同按 H0 模块单列复核。
- 已检查 206–228、229–280、281–340、341–400 的代码变更摘要、hunk 与关键条件，并针对高风险路径复核当前 `Draw3.GraphicsInitialization.cpp` 的 11_1→11_0/HARDWARE→WARP、`Draw3.TransparentPresentation.cpp` 的 FLIP/候选/强制/恢复、`Draw3.WindowControl.cppm` 的 gate、`Draw3.RealtimeStylus.cpp`/`Draw3.ContactInput.cpp` 的容量/发布状态、`Draw3.HapticFeedback.cpp` 的 WinRT 动态装载、`IdtState.cpp` 的业务同步入口、`Draw3.InkHistory.cpp` 的撤销/重做入口。所有结论为静态审查；没有运行本批构建、GUI 或性能采样。
- `fc9a8b865cd8` 一次导入 953 文件，含约 70 个直接相关的第一方产品/Shader/Host 路径，以及固定 Modeler/Abseil。TSV 明确保留“深度当前代码审查待专项”；此项需与 Work 3 和最终独立 review 合并，不能因逐 SHA 差异索引已完成就宣称导入风险已清零。`13e81fa0cee2` 将 Modeler 从源码编译改为固定静态库并调整 Debug CRT/vcpkg，三架构 ABI 与产物核验也仍属发布验证项。

## B2-001 — H0 正式透明路径仍包含被禁用的 Win7 DWM 模式

- 状态：已确认失败，中等严重性（相对于本任务明确的正式模式约束；未运行真机）。
- 关联历史：`719ef85ea58a`、`0e1d1d5d48b6`、`fe5af1ad14fe` 引入 DWM 方案；`fc9a8b865cd8` 将其导入产品；后续 H0 仍保留。
- 当前证据：`Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:89–95` 的候选顺序包括 `DwmBlurBehind2` 和 `DwmBlurBehind`；同文件 `:657–660` 仍按两个模式初始化；`Draw3.Host.cpp:190–191`、`:219–220` 仍可在 Host 模式与 presenter 模式之间映射。两种模式也保留在 `Draw3.Host.h:149–150`。
- 触发与影响：DComp 不可用/初始化失败时自动候选可进入 DWM 模式，且强制选择通路可映射到它们，与用户要求“正式仅 DComp 和 ULW”不符；实际运行行为尚待无窗口/Win7 真机验证。
- 最小修复建议：在正式候选与强制模式解析层只允许 DComp/ULW，拒绝或归一化两个 DWM 选择；保持 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`，不引入 bitblt 回退。保留历史实现与诊断代码是否删除由 owner 以最小 diff 决定。
- 回归建议：候选顺序、DComp 不可用→ULW、两个旧强制值不能选中 DWM、HARDWARE→WARP 与 FL11.0 有/无分别覆盖；Win7 SP1+KB2670838 真机仍为人工门禁。

## 恢复入口

优先接续 TSV 的 227 行 `fc9a8b865cd8`：将导入的第一方 Draw3/Host/Shader/工程合同同 Work 3 的生产链路、三架构 ABI 构建和最终 reviewer 证据合并；226 行固定 Modeler 库需发布矩阵复验。228 行的双父和 combined diff 已检查，未见独有产品代码冲突；383 行 combined diff 为空。Win7 SP1+KB2670838 的 FL11.0 有/无、HARDWARE/WARP、DComp 不可用→ULW、保持 FLIP 仍需可比真机运行证据；不能以静态路径存在代替通过。
