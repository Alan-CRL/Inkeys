# Design: Setting DX11 renderer 与嵌入式 CSO

## Scope and ownership

设置窗口继续拥有独立的 D3D device 生命周期。`Setting.Base.cppm` 管理 D3D11 device/context、DXGI swap chain、render-target view 和设置图片 SRV；`Setting.cpp` 管理 ImGui context/backend 的初始化、每帧渲染、resize、present 和关闭时序。其他共享 D2D/D3D11 对象不参与本次迁移。

## Device and frame flow

1. `CreateDeviceD3D(HWND)` 创建 hardware D3D11 device/context 与 windowed swap chain，并创建主 render-target view。
2. `WM_SIZE` 只写入待处理宽高；渲染线程在下一帧释放 RTV、`ResizeBuffers`、重建 RTV。
3. 每帧调用 `ImGui_ImplDX11_NewFrame`，完成 UI 后 `ImGui::Render`，绑定/清空 RTV，调用 `ImGui_ImplDX11_RenderDrawData`，最后通过 swap chain `Present`。
4. occluded 状态不忙等；退出或隐藏时先结束仍使用资源的 ImGui backend，再释放用户 SRV、RTV、swap chain、context 和 device。

保留 `DXGI_SWAP_EFFECT_DISCARD` 等兼容 Windows 7 的传统 swap-chain 语义，不引入 flip-model 或共享 D2D device。

## Texture contract

ImGui DX9 backend 把 `LPDIRECT3DTEXTURE9` 作为 `ImTextureID`；DX11 backend 把 `ID3D11ShaderResourceView*` 作为 `ImTextureID`。因此 `TextureSettingSign` 改为 SRV 数组，`LoadTextureFromMemory` 创建：

- `ID3D11Texture2D`
- 与输入 BGRA 字节匹配的 `DXGI_FORMAT_B8G8R8A8_UNORM`
- `ID3D11ShaderResourceView`

临时 texture 在 SRV 创建后释放，SRV 保持 texture 引用并由设置窗口关闭路径释放。shader 使用标准 `sample * vertex color` 输出，不额外交换 R/B。

## Precompiled shader and resource flow

`imgui_vs.hlsl` 与 `imgui_ps.hlsl` 离线编译成兼容 ImGui backend 的 CSO。`.rc` 的真实映射是：

- `IDR_SHADERS1` → `imgui_ps.cso`
- `IDR_SHADERS2` → `imgui_vs.cso`

DX11 backend 使用 Win32 resource API 和 `resource.h` 宏从当前 EXE 读取 bytecode，分别调用 `CreateVertexShader`、`CreateInputLayout`、`CreatePixelShader`。这样 backend 不需要 `d3dcompiler.h`、`d3dcompiler.lib` 或目标机器的 `d3dcompiler_47.dll`。

采用直接 Win32 资源读取，而不是旧版 `Inkeys::Load::ExtractResourcePtr(std::wstring)`：后者不能安全接收 `MAKEINTRESOURCEW` 数值资源名，原旧代码会构造无效的 `std::wstring`。

相对 upstream `imgui_impl_dx11.cpp` 的定制必须用成对的 `[Inkeys]` 注释精确圈定。标记说明只覆盖“从 EXE 资源读取 VS/PS CSO 并创建设色器/输入布局/常量缓冲”的替换段，不把后续 blend/rasterizer/depth/sampler 等未修改 upstream 逻辑纳入定制范围。

## Project integration

- `Setting.Wrap.h` 从 DX9 backend/header/lib 切换到 DX11。
- `Inkeys.vcxproj` 和 `.filters` 将 renderer 源/头登记从 DX9 切到 DX11。
- HLSL 的 ShaderType/ShaderModel 对所有 Win32/x64/ARM64 Debug/Release 配置显式一致，避免 vertex shader 落到默认 `vs_2_0`。
- 生成的 `.cso` 继续作为受版本控制的预编译输入，由 `.rc` 嵌入。

## Compatibility and rollback

- device 使用 D3D11 API，shader profile 与声明的 feature levels 匹配；创建行为保持 hardware 路线，与原 DX9 HAL 意图一致。
- 不依赖当前进程的共享 WARP/D2D device，避免改变 Bar/PPT 生命周期。
- 回滚可按文件恢复 `Setting.Wrap.h`、`Setting.Base.cppm`、`Setting.cpp`、vcxproj/filter 和 DX11 backend 定制，同时还原两个 CSO；不需要迁移配置或用户数据。

## Existing dirty worktree

ImGui core、DX9/DX11 backend 等已存在用户未提交更新。实现必须基于工作区版本做局部补丁，不得用 HEAD 或旧归档整文件覆盖。
