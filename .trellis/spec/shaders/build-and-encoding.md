# Shader Build and Encoding

## Source Encoding

仓库中的 `ink.hlsli`、`laserParticleCommon.hlsli`、`inkVertexShader.hlsl`、`inkPixelShader.hlsl`、`laserParticleUpdateCS.hlsl` 和 `laserParticleEmitCS.hlsl` 是 UTF-8 BOM + CRLF，与当前 C++ 源文件约定一致。

Windows SDK FXC 10.1 不接受文件开头的 UTF-8 BOM，因此不要直接移除仓库源文件 BOM。工程中的 `PrepareFxcCompatibleHlsl` target 会：

1. 创建 `$(IntDir)fxc_utf8`。
2. 读取上述六份仓库 HLSL/HLSLI 源。
3. 以 ASCII 写入无 BOM 临时副本。
4. 让实际 `FxCompile` items 从临时目录编译 Shader Model 5.0。

只有 HLSL 当前使用的字符可由 ASCII 临时副本表示。新增非 ASCII shader 源文本前，必须确认 MSBuild 转换不会破坏内容。

## Build Items

仓库中的原始 `inkPixelShader.hlsl` 与 `inkVertexShader.hlsl` FxCompile items 标记为 `ExcludedFromBuild=true`。真正参与构建的是：

- `$(IntDir)fxc_utf8\inkPixelShader.hlsl` -> `inkPixelShader.cso`
- `$(IntDir)fxc_utf8\inkVertexShader.hlsl` -> `inkVertexShader.cso`
- `$(IntDir)fxc_utf8\laserParticleUpdateCS.hlsl` (`Compute`, SM5.0) -> `laserParticleUpdateCS.cso`
- `$(IntDir)fxc_utf8\laserParticleEmitCS.hlsl` (`Compute`, SM5.0) -> `laserParticleEmitCS.cso`

不要再添加一套平行的手工 FXC 命令或直接编译原始 BOM 文件。

## Resource Embedding

`inkStrokeModelerTest.rc` 将输出嵌入为 `SHADER` 资源：

- `IDR_PS1` -> `inkPixelShader.cso`
- `IDR_VS1` -> `inkVertexShader.cso`

`laserParticleShaders.rc` 使用独立 ASCII 资源头嵌入：

- `IDR_LASER_PARTICLE_UPDATE_CS` -> `laserParticleUpdateCS.cso`
- `IDR_LASER_PARTICLE_EMIT_CS` -> `laserParticleEmitCS.cso`

`InkRenderer::LoadShaders` 通过 `FindResource/LoadResource/LockResource` 获取字节码。VS/PS 缺失仍令 renderer 初始化失败；两个 CS 或其固定资源失败只关闭粒子并记录一次诊断。

修改文件名、资源 ID 或输出位置时必须同步：

- `.vcxproj`
- `.vcxproj.filters`
- `.rc`
- 对应资源头（`resource.h` 或 `laserParticleResource.h`）
- `renderer.cpp`

## Generated Artifacts

- `.cso` 是构建生成并被资源脚本消费的字节码，不手工编辑或作为 shader 逻辑评审依据。
- `$(IntDir)fxc_utf8` 是中间目录，不提交、不直接修改。
- 评审 shader 行为以 `.hlsl/.hlsli` 源和 CPU 绑定代码为准。

## Validation And Cases

- Good：完整 `inkStrokeModelerTest.sln` `Debug|ARM64` 构建日志同时出现四份 “compilation object save succeeded”，随后两个 `.rc` 编译并成功链接 exe。
- Base：只修改共享 HLSLI 也会因 target Inputs 变化而重建 VS/PS/两个 CS。
- Bad：直接让 FXC 读取 BOM 源、只新增 `.hlsl` 而不扩展 ASCII target Inputs/Outputs，或生成 `.cso` 却漏掉资源脚本。

| Failure | Required result |
|---|---|
| ASCII 临时副本缺少共享 HLSLI | FXC 构建失败，不得使用陈旧 `.cso` 冒充成功 |
| VS/PS 资源缺失 | renderer Init 返回 false |
| UpdateCS/EmitCS 资源缺失 | 应用继续，粒子 `IsAvailable == false` |
| 只构建单个 `.vcxproj` 导致依赖缺失 | 改用 ARM64 MSBuild 构建完整解决方案 |

完整验证必须构建 `inkStrokeModelerTest.sln` 并运行 ARM64 测试；仅做 HLSL 文本检查不能证明资源嵌入链有效。

Wrong：手工运行另一套 `fxc.exe` 生成未被 `.rc` 消费的输出。

Correct：只维护仓库源、`PrepareFxcCompatibleHlsl`、临时 `FxCompile` item 与资源 ID 的单一链路。
