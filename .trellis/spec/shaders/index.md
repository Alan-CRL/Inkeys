# Shader Layer Guidelines

本层覆盖：

- `inkStrokeModelerTest/ink.hlsli`
- `inkStrokeModelerTest/laserParticleCommon.hlsli`
- `inkStrokeModelerTest/inkVertexShader.hlsl`
- `inkStrokeModelerTest/inkPixelShader.hlsl`
- `inkStrokeModelerTest/laserParticleUpdateCS.hlsl`
- `inkStrokeModelerTest/laserParticleEmitCS.hlsl`
- `draw3/renderer.cppm` 与 `draw3/renderer.cpp` 中的 CPU/GPU 镜像
- `.vcxproj`、`.rc` 与 `resource.h` 中的 shader 构建和资源嵌入

## Guides

| Guide | Content |
|---|---|
| [CPU/GPU Contracts](./cpu-gpu-contracts.md) | 结构布局、寄存器、shape/operator 和混合语义 |
| [Build and Encoding](./build-and-encoding.md) | BOM/CRLF、FXC 临时副本、`.cso` 与资源脚本 |

## Pre-Development Checklist

- [ ] 同时读取 C++ 结构、buffer 创建、HLSL include 和使用该结构的 shader。
- [ ] 搜索寄存器槽、shape type、operator kind 和 resource ID 的全部引用。
- [ ] 明确改动是否影响 premultiplied alpha、Add/Retain 或 dirty bounds。
- [ ] 保持 HLSL 源 UTF-8 BOM + CRLF。

## Quality Check

- [ ] C++/HLSL 字段顺序、大小、stride 和枚举值一致。
- [ ] 常量缓冲区仍是 16 字节倍数，并同时绑定到需要的 shader stage。
- [ ] SRV/RTV 绑定解除完整，没有资源冲突。
- [ ] CPU bounds 覆盖 shader 实际绘制区域和 AA padding。
- [ ] `.vcxproj` 的临时副本、FXC 输出和 `.rc` 嵌入链仍一致。
- [ ] 若任务修改 HLSL、CPU/GPU 契约或工程资源链：完整解决方案构建成功，vertex/pixel/update compute/emit compute 四个 shader 编译和 `.cso` 资源嵌入链均有成功证据。
- [ ] 若任务修改视觉数学：执行相应人工场景；无法运行时明确标记“未验证”。
- [ ] 纯文档任务可跳过构建和视觉验证，但必须检查索引、链接、格式与规则一致性。
