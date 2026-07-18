# Shader Build and Encoding

## Source Encoding

仓库中的 `ink.hlsli`、`inkVertexShader.hlsl` 和 `inkPixelShader.hlsl` 是 UTF-8 BOM + CRLF，与当前 C++ 源文件约定一致。

Windows SDK FXC 10.1 不接受文件开头的 UTF-8 BOM，因此不要直接移除仓库源文件 BOM。工程中的 `PrepareFxcUtf8Sources` target 会：

1. 创建 `$(IntDir)fxc_utf8`。
2. 读取三份仓库 HLSL 源。
3. 以 ASCII 写入无 BOM 临时副本。
4. 让实际 `FxCompile` items 从临时目录编译 Shader Model 5.0。

只有 HLSL 当前使用的字符可由 ASCII 临时副本表示。新增非 ASCII shader 源文本前，必须确认 MSBuild 转换不会破坏内容。

## Build Items

仓库中的原始 `inkPixelShader.hlsl` 与 `inkVertexShader.hlsl` FxCompile items 标记为 `ExcludedFromBuild=true`。真正参与构建的是：

- `$(IntDir)fxc_utf8\inkPixelShader.hlsl` -> `inkPixelShader.cso`
- `$(IntDir)fxc_utf8\inkVertexShader.hlsl` -> `inkVertexShader.cso`

不要再添加一套平行的手工 FXC 命令或直接编译原始 BOM 文件。

## Resource Embedding

`inkStrokeModelerTest.rc` 将输出嵌入为 `SHADER` 资源：

- `IDR_PS1` -> `inkPixelShader.cso`
- `IDR_VS1` -> `inkVertexShader.cso`

`InkRenderer::LoadShaders` 通过 `FindResource/LoadResource/LockResource` 获取字节码，并创建 D3D11 vertex/pixel shader。

修改文件名、资源 ID 或输出位置时必须同步：

- `.vcxproj`
- `.rc`
- `resource.h`
- `renderer.cpp`

## Generated Artifacts

- `.cso` 是构建生成并被资源脚本消费的字节码，不手工编辑或作为 shader 逻辑评审依据。
- `$(IntDir)fxc_utf8` 是中间目录，不提交、不直接修改。
- 评审 shader 行为以 `.hlsl/.hlsli` 源和 CPU 绑定代码为准。

完整验证必须构建 `inkStrokeModelerTest.sln`，仅做 HLSL 文本检查不能证明资源嵌入链有效。
