# L0 瞬态绘制光标

## Goal

把 Pen、Highlighter 和 Eraser 的自定义系统光标改为应用内瞬态 GPU 光标，绕过目标设备上颜色错误的 Windows 硬件光标平面，同时保持既有工具尺寸、外观和设备接管语义。

## Background

- 微软官方 `CreateAlphaCursor` 示例在目标 Qualcomm ARM64 设备的实体屏幕上同样发暗，因此不再继续修补 `HCURSOR` Alpha 数据。
- 当前墨迹通过 D3D11 和透明窗口呈现链路显示正确；光标应进入相同颜色合成路径。
- 共享 `layerL0` 会在 contact 完成时参与 L2 resolve，光标不得写入该纹理或任何笔迹 CPU 状态。

## Requirements

- 所有自定义工具光标在 L0 帧的最终阶段绘制到 backbuffer，且永不进入 L0/L1/L2 持久状态、contact payload、模型或指标。
- Pen 光标是当前颜色圆形，直径为 `max(当前画笔粗细, 5px * dpiScale)`；Highlighter 是当前颜色 `6.25x50px` 竖直矩形。两者保持浅灰细内描边和 50% 填充 Alpha。
- Eraser 光标直径等于实际 50px 擦除宽度；主体纯白，圆环和两条圆头竖线为 `#CFCFCF`；Hover 整体 Alpha 0.5，Contact 整体 Alpha 1.0。
- Pen/Highlighter 的 Pen Hover 显示应用光标，Pen Contact 隐藏；Mouse 保留系统箭头；Touch 不显示笔尖光标。
- Eraser 的 Pen/Mouse Hover 和 Contact 均隐藏系统光标并显示应用光标；倒转笔尾按 Eraser 处理。
- Eraser 的每个活动 Touch contact 都显示一枚 100% 不透明橡皮圆；Touch 没有 Hover 半透明状态，多指必须同时显示多枚互不替代的光标。
- RTS InAir/Down/Packets/Up 发布最新 Pen 坐标、接触和倒转状态；非 promoted Mouse 消息发布 Mouse 坐标；高频更新必须合并且能唤醒空闲绘制线程。
- 光标移动、隐藏、工具切换、离开窗口、resize、清屏和全量恢复都必须清除旧位置并在当前帧最后重绘新位置。
- 删除自建彩色 `HCURSOR` 路径；只允许窗口线程使用 `SetCursor(nullptr)` 或系统 `IDC_ARROW`，禁止全局光标 API。
- 外观尺寸在绘制时通过常量传递，不因位置或尺寸变化创建 `HCURSOR` 或 GPU 纹理，为后续动态笔速橡皮保留低成本更新能力。

## Acceptance Criteria

- [ ] Pen、Highlighter、Eraser 在实体屏幕上的颜色与同一 D3D 墨迹路径一致，不再经过硬件彩色光标平面。
- [ ] Pen/Highlighter、Eraser、倒转笔尾、Mouse、Touch、Hover/Contact 行为符合设备矩阵，多个 Touch 橡皮 contact 同时显示对应数量的不透明圆。
- [ ] 旧光标 bounds 与新 bounds 的并集被重建；离开窗口、设备失效和 shutdown 后无残影。
- [ ] contact 完成和 L2 resolve 永远不包含光标像素；静止光标不持续 Present。
- [ ] 仓库中不再使用 `CreateIconIndirect`、自建 cursor bitmap 或自建 `HCURSOR` RAII。
- [ ] `Debug|ARM64` 完整解决方案构建通过，HLSL CSO 更新，`inkStrokeModelerTestTests.exe` 通过。
- [ ] 真机验证 SDR/HDR、白/红/黑背景、窗口边界和 Mouse 工具切换。

## Out Of Scope

- 不实现笔速橡皮的宽度算法，只保证光标宽度可低成本动态更新。
- 不改变墨迹模型、contact 数据布局、L0/L1 operator 格式、L2 语义或持久化格式。
- 不为 Pen/Highlighter 的 Touch contact 增加笔尖光标，也不保留彩色系统光标回退。
