# Implementation plan

## 1. Renderer and texture migration

- [x] 更新 `Setting.Wrap.h`：引入 DX11 backend 与 D3D11/DXGI 依赖，移除设置模块的 DX9/D3DX9 依赖。
- [x] 更新 `Setting.Base.cppm`：实现 device/context/swap-chain/RTV 创建、resize、清理；把图片纹理改为 SRV，并按 BGRA 输入创建 texture/SRV。
- [x] 更新 `Setting.cpp`：替换 backend 初始化、新帧、render、present、resize、occlusion 和 shutdown；保证 hide/show/stop 均释放资源。
- [x] 搜索所有 `TextureSettingSign` 与 `ImGui::Image` 调用，确认 DX11 的 `ImTextureID` 实际指向 SRV。

## 2. Embedded precompiled shader

- [x] 基于当前工作区的 `imgui_impl_dx11.cpp` 恢复局部 Inkeys 定制，保留用户的 ImGui backend 更新。
- [x] 用成对 `[Inkeys]` 注释精确标记 shader/CSO 定制的开始、结束、目的和 VS/PS 资源映射，不圈入未修改 upstream 逻辑。
- [x] 用 Win32 resource API 按 `IDR_SHADERS2`/`IDR_SHADERS1` 读取 vertex/pixel CSO；删除 runtime `D3DCompile` include、link 与代码。
- [x] 修正 HLSL 为标准 ImGui 颜色输出，并为所有工程配置显式指定正确 vertex/pixel shader profile。
- [x] 使用 ARM64 Windows SDK `fxc.exe` 离线生成两个跟踪的 `.cso`，再用 `/dumpbin` 核验 profile、签名和 pixel 输出。

## 3. Project registration

- [x] 在 `Inkeys.vcxproj` 和 `.filters` 中把 ImGui renderer 编译项从 DX9 切到 DX11。
- [x] 核对 `.rc`、`resource.h`、CSO 文件名与 backend 加载 ID 四者一致。

## 4. Validation

- [x] 运行定向 `rg`：产品设置路径无 DX9 API，DX11 backend 无 `D3DCompile`，SRV/资源映射完整。
- [x] 检查目标文本文件编码、BOM 与 CRLF/LF 保持原状；运行 `git diff --check`。
- [x] 使用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln`：
  `MSBuild.exe InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64`
- [x] 构建失败时只修复本任务引入的问题，不处理无关警告/历史问题。
- [x] 交给 `trellis-check` 做 spec、数据流、资源生命周期与差异范围复核。

## Risk and rollback points

- `imgui_impl_dx11.cpp` 有用户未提交更新：禁止整文件还原或从旧归档复制。
- CSO 为二进制生成物：修改 HLSL/profile 后必须重新生成并反汇编检查，避免 `.rc` 嵌入旧字节码。
- resize/关闭顺序错误会导致 `ResizeBuffers` 失败或资源泄漏：RTV/SRV 与 backend/device 必须按依赖逆序释放。
