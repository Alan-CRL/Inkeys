# Draw3 着色器规范

- Draw3 shader 源码位于 `Inkeys/Inkeys/Drawing/Draw3/Assets/`，由目标 `Inkeys.vcxproj` 的 `FxCompile` 项增量生成 `.cso`。
- 资源 ID 使用 `301` 起的 Draw3 专用区间，并以 `IDR_DRAW3_*` 命名；不得复用目标 `resource.h` 已占用 ID。
- shader 预处理必须兼容 FXC，公共宏放在 `.hlsli`；产品构建不得引用源仓库输出目录或 x64 预编译库。
- 透明像素使用 premultiplied alpha，背景清除为零 alpha；任何 fallback 都必须保留 dirty-rect 语义。
- Laser 的 `ClearLaserCoverageRect`、`ResolveLaserStrokeCoverage`、`ResolveLaserCompositedColor` 返回同步资源/绘制提交结果；`DrawingController` 必须将失败传给当帧的 raster/Present 门禁。多笔烘干先从已提交预乘颜色层复制到独立 scratch，逐层提交成功后才交换并释放权威 CPU 点列；失败批次保留几何供重试，不能对已污染的旧目标重复 source-over。`CopyResource` 和 draw 的异步 GPU 错误仍由设备/Present 失败链处理，返回 `true` 不等于像素已可见。scratch 随 resize、Laser 生命周期结束和设备 epoch 释放；成功烘干后 Hold/Fade 的 device-lost 视觉恢复仍需单独验证。

- Win7 WARP 默认完整兼容 PS 的实际 driver/OS 选择、精确诊断 override、原版 probe 隔离及源数学更新同步合同见 [input-and-ink.md](input-and-ink.md#scenario-win7-warp-默认完整-shader-修复2026-10-09)。成功预编译数组保持完整公式；未来源码数学变化不能只更新正式资源而留旧兼容字节。

- 2026-11-22（阶段 B）：`inkPixelShader.hlsl` 新增类型 25（单层合成，只读 `LiveOperatorAdd/Retain`，即 t4/t5），顶点着色器把 25 与 1、2 一同按全屏矩形展开。新增图元类型必须写成独立的提前返回分支：Win7 受保护着色器的尾部条件是“类型 0、3、小于 0 或大于 22”，落进尾部的类型会被当成普通墨迹。改 `inkPixelShader.hlsl` 后必须用 `.trellis/tasks/archive/2026-10/10-18-draw3-cursor-policy/research/Generate-Win7-GuardedShader.ps1` 重新生成 `Draw3.Win7GuardedShader.h`；当前为 41856 字节，SHA256 `2DEC1FF46DF01019B11950982568D273C5C59F6A34299DB7546644FB1C656234`。六个着色器产物仍全部是 SM5.0。
