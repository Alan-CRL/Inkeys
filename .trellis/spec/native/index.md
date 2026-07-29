# Native Layer Guidelines

本层覆盖 `inkStrokeModelerTest/main.cpp` 与 `inkStrokeModelerTest/draw3/` 的 C++20/Win32/D3D11 实现。

## Guides

| Guide | Content |
|---|---|
| [Modules and Code Style](./modules-and-code-style.md) | `.cppm/.cpp` 分工、命名、格式、注释和编码 |
| [Runtime and Rendering](./runtime-and-rendering.md) | 输入、建模、工具、L0/L1/L2、脏矩形和帧循环 |
| [Platform and Resources](./platform-and-resources.md) | COM/D3D 生命周期、resize、透明 presenter 与错误回退 |
| [Quality and Validation](./quality-and-validation.md) | 修改边界、构建命令、人工验证和审查清单 |

## Pre-Development Checklist

- [ ] 阅读任务文档，并确认修改的是当前 `draw3` 路径而非历史/参考代码。
- [ ] 阅读将修改模块的 `.cppm` 接口和 `.cpp` 实现。
- [ ] 用 `rg` 搜索相关常量、枚举、结构、资源和文档镜像。
- [ ] 涉及 `additional/`、`HiEasyX/`、`lib/` 时先确认独立第三方补丁范围和上游来源。
- [ ] 涉及 CPU/GPU 契约时同时阅读 [Shaders](../shaders/index.md)。
- [ ] 涉及三层画布、线程回调或 presenter 时阅读 [Cross-Layer Guide](../guides/cross-layer-thinking-guide.md)。
- [ ] 保持原文件 UTF-8 BOM + CRLF，不格式化无关代码。

## Quality Check

- [ ] 只修改任务需要的文件，没有顺手重构第三方或历史代码。
- [ ] `.cppm` 导出契约和 `.cpp` 实现同步。
- [ ] D3D 操作仍在绘制线程执行，窗口回调只发布请求。
- [ ] L0/L1/L2、dirty rect、resize 和 presenter fallback 不变量保持一致。
- [ ] 失败路径记录足够上下文，并释放/复位部分创建的资源。
- [ ] 若任务修改业务源码、HLSL 或工程配置：使用 ARM64 MSBuild 构建完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64` 配置，并确认 VS/PS/UpdateCS/EmitCS 四个 Shader 编译及资源链成功。
- [ ] 若任务修改运行行为：启动后检查 D3D Debug Layer 输出；当前启用方式尚待验证时必须如实标记。
- [ ] 若任务修改运行行为：至少完成人工基础绘制、prediction、抬笔烘干和窗口 resize；没有执行的验证必须明确说明。
- [ ] 纯文档任务可跳过上述构建和运行检查，但必须完成文档链接、格式、规则一致性及 Git 范围检查，并在交付中说明。
