# Draw3 着色器规范

- Draw3 shader 源码位于 `Inkeys/Inkeys/Drawing/Draw3/Assets/`，由目标 `Inkeys.vcxproj` 的 `FxCompile` 项增量生成 `.cso`。
- 资源 ID 使用 `301` 起的 Draw3 专用区间，并以 `IDR_DRAW3_*` 命名；不得复用目标 `resource.h` 已占用 ID。
- shader 预处理必须兼容 FXC，公共宏放在 `.hlsli`；产品构建不得引用源仓库输出目录或 x64 预编译库。
- 透明像素使用 premultiplied alpha，背景清除为零 alpha；任何 fallback 都必须保留 dirty-rect 语义。
- Laser 的 `ClearLaserCoverageRect`、`ResolveLaserStrokeCoverage`、`ResolveLaserCompositedColor` 返回同步资源/绘制提交结果；`DrawingController` 必须将失败传给当帧的 raster/Present 门禁。多笔烘干先从已提交预乘颜色层复制到独立 scratch，逐层提交成功后才交换并释放权威 CPU 点列；失败批次保留几何供重试，不能对已污染的旧目标重复 source-over。`CopyResource` 和 draw 的异步 GPU 错误仍由设备/Present 失败链处理，返回 `true` 不等于像素已可见。scratch 随 resize、Laser 生命周期结束和设备 epoch 释放；成功烘干后 Hold/Fade 的 device-lost 视觉恢复仍需单独验证。
