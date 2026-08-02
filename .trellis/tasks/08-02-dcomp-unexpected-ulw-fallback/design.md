# DComp 意外回退 ULW：技术设计

## 边界

本修复只调整 `TransparentPresentationController::Initialize` 的候选模式排序来源，不改变任何 presenter 的创建、窗口样式、交换链、清理、resize 或 present 逻辑。

## 根因

`3f1d6e6` 增加的 `PreferUlwForAdapter` 仅读取 `DXGI_ADAPTER_DESC::VendorId`。Qualcomm 适配器无论设备型号、系统版本、驱动版本及当前 DComp 行为如何，都会使用 `qualcommModes`，其首项固定为 `UlwDirtyRect`。因此当前 Qualcomm ARM64 设备进入 ULW 是确定性策略命中，不是 DComp 初始化失败或 Win11 版本差异。

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
