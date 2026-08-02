# 修复 DComp 意外回退 ULW

## Goal

恢复透明图层在支持 DirectComposition 的 Windows 环境中统一优先使用 DComp，避免仅凭 GPU 厂商 ID 把所有 Qualcomm/ARM64 设备强制导向 CPU 读回的 `UlwDirtyRect`，同时保留初始化失败时的既有 DWM/ULW 回退链。

## Background

- 用户当前的 Windows 11 25H2 ARM64 设备过去使用 `DirectCompositionVisualTree`，现在稳定进入 `UlwDirtyRect`；其他 Windows 11 设备仍正常进入 DComp。
- 近一个月提交排查确认，`3f1d6e6` 于 2026-07-19 在 `inkStrokeModelerTest/draw3/transparent_presentation.cpp:42-49,646-658` 新增了 `VendorId == QCOM` 的全厂商 ULW 优先策略，现象与该分支完全一致。
- 当时的历史记录只证明一台 Qualcomm Adreno X1-85 实机曾出现“DComp 初始化成功但透明像素显示为黑色”，没有记录足以稳定限定问题的设备/驱动版本组合；不能把单机视觉故障泛化为所有 QCOM 适配器的永久策略。
- 同类环境判定搜索确认：主业务源码中只有该处 GPU VendorId 会改变运行路径；`runtime_metrics.cpp` 的架构宏只写诊断字段，不改变呈现行为。
- 移除厂商特判后，当前 Adreno X1-85 上 `Debug|ARM64` 连续 6 次都成功进入 DComp，但 `Release|ARM64` 连续 6 次都在补设 `WS_EX_NOREDIRECTIONBITMAP` 时返回 `ERROR_INVALID_PARAMETER (87)`，随后进入 `DwmBlurBehind2 + DXGI_ALPHA_MODE_UNSPECIFIED` 并出现黑屏和高绘制延迟。
- 微软对 `WS_EX_NOREDIRECTIONBITMAP` 的语义是创建顶级窗口时请求不分配 DWM redirection surface。诊断确认 Release 的 `WM_CREATE::CREATESTRUCT::dwExStyle` 已经是 `0`，排除了 USER32/DWM 创建后剥离样式。
- 标准 Release 全量重建仍稳定失败；仅关闭 `WholeProgramOptimization` 后，同一 `/O2` Release 构建恢复创建样式并进入 DComp。根因边界因此锁定为 ARM64 Release LTCG 与 HiEasyX“一次性全局预设 -> 独立窗口线程 -> CreateWindowEx”传递路径的优化敏感交互，而不是系统重装、GPU 驱动或 DComp API 不可用。

## Requirements

- 所有适配器使用同一透明呈现尝试顺序：`DirectCompositionVisualTree -> DwmBlurBehind2 -> UlwDirtyRect`。
- 删除仅凭 Qualcomm VendorId 提前选择 ULW 的函数、常量、模式数组和日志，不新增未经实机矩阵验证的设备/驱动黑名单。
- 保留 DComp API 运行时探测、每次失败后的资源清理、DWM/ULW 初始化回退、Present 失败处理和 Win7 兼容目标。
- 保证首选 DComp 时 `WS_EX_NOREDIRECTIONBITMAP` 由窗口创建参数可靠传入；不得依赖窗口创建后用 `SetWindowLongPtr` 补设该样式来修复缺失。
- 用一次性启动诊断确认 Release 下的 API 预探测结果、`WM_CREATE::CREATESTRUCT` 创建参数和窗口创建后实际扩展样式，再据此做最小修复；最终保留足以定位兼容失败的低频日志，移除无长期价值的临时输出。
- 仅对 `HiEasyX\HiWindow.cpp` 的 Release 编译关闭 `WholeProgramOptimization`，保留 `/O2` 和其余工程的 `/GL/LTCG`。
- 同步修正 `.trellis/spec/` 中“QCOM 优先 ULW”的旧约束；历史研究和已归档基线保留为当时事实，不回写历史记录。
- 修改范围仅限透明 presenter、`HiWindow.cpp` 的文件级工程优化元数据、对应规范和本任务文档；不修改渲染、输入、Shader、HiEasyX 源码或 `Vcpkg/`。

## Acceptance Criteria

- [x] 当前 Qualcomm Windows 11 ARM64 环境启动日志先出现 `Trying transparent present mode: DirectCompositionVisualTree`，初始化成功时 active mode 为 `DirectCompositionVisualTree`。
- [x] 非 Qualcomm 环境的默认顺序不变；DComp 初始化失败时仍依次尝试 `DwmBlurBehind2` 和 `UlwDirtyRect`。
- [x] 主业务源码及现行规范不再包含“QCOM/Qualcomm adapter detected/prefer ULW”运行策略；诊断和历史记录不受影响。
- [x] 完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64` 构建通过，ARM64 Debug/Release 自动测试通过。
- [x] `git diff --check`、改动范围和 CRLF/编码检查通过；未跟踪的 `Vcpkg/` 保持不动。
- [x] `Release|ARM64` 全量重建后连续 6 次启动均激活 DComp，不再因错误 87 回退；编译记录确认 `HiWindow.cpp` 保留 `/O2` 且不含 `/GL`。
- [x] 最终 `Debug|ARM64` 完整构建通过，ARM64 Debug/Release 自动测试通过；此前 Debug 连续 6 次均进入 DComp。
- [x] 用户已确认当前 Adreno X1-85 上 Release 激活 `DirectCompositionVisualTree`，透明桌面背景和基础绘制延迟表现正常，未落入 DWM/ULW 兼容路径。

## Out of Scope

- 自动判断所有设备上“DComp 已成功但桌面最终合成仍为黑底”；本轮只修复已经稳定复现的 Release 创建期样式丢失，并在当前设备人工验证最终视觉。
- 为旧驱动新增命令行开关、环境变量、设备/驱动黑名单或设置界面。
- 实现欢迎页透明度测试；未来欢迎页须在 Windows 7 或 Qualcomm GPU 环境额外提供真实透明度测试页，并用结果选择或建议兼容呈现路径。
- 重写 DComp/DWM/ULW presenter、交换链参数、窗口样式或 dirty rect 实现。

## Technical Notes

- 当前通用回退链与 `.trellis/spec/native/platform-and-resources.md:118-139` 已一致；回归仅来自 2026-07-19 后叠加的厂商特殊排序。
- 若将来再次确认特定驱动存在黑底，必须记录 OS、VendorId、DeviceId、SubSysId、Revision、UMD driver version 和真实背景视觉结果，再单独设计可验证的窄范围兼容策略。
- 欢迎页透明度测试属于后续独立任务；需同时设计可回退选择、结果保存和用户无法判断时的处理，本任务只保留备忘。
