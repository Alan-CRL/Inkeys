# DComp 意外回退 ULW：技术设计

## 边界

第一轮修复只调整 `TransparentPresentationController::Initialize` 的候选模式排序来源。第二轮处理同一设备恢复 DComp-first 后暴露的 Release 创建期窗口样式丢失，允许最小修改窗口创建参数传递与相应低频诊断；不改变绘制、交换链格式、resize 或 present 数据路径。

## 根因

`3f1d6e6` 增加的 `PreferUlwForAdapter` 仅读取 `DXGI_ADAPTER_DESC::VendorId`。Qualcomm 适配器无论设备型号、系统版本、驱动版本及当前 DComp 行为如何，都会使用 `qualcommModes`，其首项固定为 `UlwDirtyRect`。因此当前 Qualcomm ARM64 设备进入 ULW 是确定性策略命中，不是 DComp 初始化失败或 Win11 版本差异。

第一轮移除特判后又发现第二个独立问题：Debug 创建的窗口带 `WS_EX_NOREDIRECTIONBITMAP`，Release 创建的窗口扩展样式为 `0`，后续 `SetWindowLongPtr(GWL_EXSTYLE)` 返回错误 87。

临时诊断确认 DComp API 预探测成功，主线程预设值为 `0x200000`，但 Release 的 `WM_CREATE::CREATESTRUCT::dwExStyle` 已为 `0`。标准 Release 全量重建仍失败；保持 `/O2`、只关闭全程序优化后则创建样式和 DComp 同时恢复。由此排除系统重装、驱动更新、DComp API 缺失、USER32 创建后剥离样式和增量缓存，根因边界锁定为 ARM64 Release LTCG 与 HiEasyX 的全局一次性预设/独立窗口线程路径之间的优化敏感交互。

本轮没有继续修改第三方源码来区分编译器优化缺陷和 HiEasyX 内部未定义同步行为；该内部机制判断仍属推断。对产品修复而言，故障边界和文件级规避已经由 A/B 构建稳定验证。

## 方案

1. 删除 `kQualcommVendorId` 与 `PreferUlwForAdapter`。
2. 删除 `qualcommModes` 和条件分支，只保留一个固定候选数组：

```text
DirectCompositionVisualTree
  -> DwmBlurBehind2
  -> UlwDirtyRect
```

3. 循环继续调用现有 `TryInitialize`；失败时继续使用 `ReleaseAttempt` 后尝试下一模式。
4. 更新现行 native/cross-layer 规范，明确厂商、架构或 OS 标签本身不能作为透明模式降级依据。

第二轮：

1. 临时记录预探测结果、预设值、`WM_CREATE::CREATESTRUCT::dwExStyle`、创建后样式和线程 ID；完成定位后删除这些高噪声输出。
2. 仅在 `.vcxproj` 对 `HiEasyX\HiWindow.cpp` 的 Release 编译设置 `WholeProgramOptimization=false`，保留 `/O2` 和其余工程的 LTCG，不改第三方源码。
3. DComp 配置阶段若发现创建期样式缺失，输出实际 `GWL_EXSTYLE` 并直接回退，不再尝试用 `SetWindowLongPtr` 补设 `WS_EX_NOREDIRECTIONBITMAP`。
4. Debug/Release 都执行多轮启动并核对 DComp active mode，再由用户完成当前设备真实桌面透明与基础绘制验证。

## 防回归分析

- 根因类别：跨层契约、隐式假设与 Release 测试缺口。调用层假设 HiEasyX 的预设能在所有优化配置下可靠进入窗口线程，但原验证只覆盖了 Debug/API 初始化结果。
- 先前修复为何不足：Qualcomm 全厂商 ULW 策略遮蔽了 DComp 创建链；移除该策略后才暴露独立的 Release 问题。错误 87 只说明后设失败，不能直接指向 GPU 驱动。
- 已完成预防：单文件关闭全程序优化；缺少创建期样式时提供精确诊断；Debug/Release 对照、多轮启动和编译命令检查写入质量门槛。
- 相邻风险：HiEasyX 的 `PreSetWindowStyle`、`PreSetWindowPos`、`PreSetWindowShowState` 使用同一传递机制，未来改动这些预设或升级工具链时都要做 Release 对照。
- 后续改进：若未来维护第三方补丁，应把窗口预设按值传入窗口线程，并用明确同步原语处理完成标志；这超出本次最小修复范围。

## 兼容性与取舍

- Windows 7 目标不受影响：DComp API 仍按运行时可用性和初始化结果回退。
- 当前 Qualcomm 设备恢复 GPU 合成，消除 ULW CPU readback 的非预期性能路径。
- 曾出现黑底的旧环境可能再次暴露该驱动视觉问题。由于现有记录没有精确驱动边界，本次不保留误伤所有 QCOM 的黑名单；后续若能复现，应基于完整设备/驱动元组或显式用户选择另立任务。
- 未来欢迎页在 Windows 7 或 Qualcomm GPU 环境增加真实透明度测试页，并用用户确认结果选择或建议兼容路径；本修复只记录该后续需求，不实现 UI、结果持久化或模式切换设置。

## 数据流与不变量

```text
固定候选顺序
  -> TryInitialize(mode)
      -> ConfigureWindow
      -> CreateSwapChain
      -> InitializeRenderer
      -> InitializePresenter
  -> 成功：记录 active mode
  -> 失败：ReleaseAttempt 后尝试下一项
```

- 任何尝试失败都必须清理部分资源。
- GPU 路径继续使用 premultiplied alpha；ULW 只在 CPU 输出副本加入命中底层。
- 不修改 `kPreferredTransparentPresentMode`、`ShouldPreconfigureNoRedirectionBitmap` 或窗口初始隐藏/首帧显示协议。

## 回滚

源码改动集中于模式选择函数和数组；若验证失败，可单独还原这些删除及两处规范描述，不涉及资源格式或持久数据迁移。
