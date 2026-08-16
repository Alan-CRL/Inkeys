# Draw3 着色器规范

- Draw3 shader 源码位于 `Inkeys/Inkeys/Drawing/Draw3/Assets/`，由目标 `Inkeys.vcxproj` 的 `FxCompile` 项增量生成 `.cso`。
- 资源 ID 使用 `301` 起的 Draw3 专用区间，并以 `IDR_DRAW3_*` 命名；不得复用目标 `resource.h` 已占用 ID。
- shader 预处理必须兼容 FXC，公共宏放在 `.hlsli`；产品构建不得引用源仓库输出目录或 x64 预编译库。
- 透明像素使用 premultiplied alpha，背景清除为零 alpha；任何 fallback 都必须保留 dirty-rect 语义。
