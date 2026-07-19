# Setting 界面 DX9 升级 DX11 并嵌入预编译着色器

## Goal

将设置窗口的 Dear ImGui renderer 从 Direct3D 9 完整迁移到 Direct3D 11，并恢复、修正先前已有的“离线编译 CSO、作为 Win32 资源嵌入可执行文件、运行时直接创建设色器”方案，避免目标机器依赖 `d3dcompiler_47.dll`。

## Background

- 当前产品路径由 `Inkeys/Inkeys/UI/Setting/Setting.cpp` 与 `Setting.Base.cppm` 使用 ImGui Win32 + DX9；`Inkeys.vcxproj` 只编译 DX9 backend。
- 本轮工作区已经包含用户更新后的 ImGui 源码与 DX9/DX11 backend 未提交修改，实施必须保留这些现有改动，只在迁移所需位置追加修改。
- `Inkeys/Inkeys3Temp20260502.7z` 保存了旧 DX11 迁移参考：DX11 图像句柄使用 `ID3D11ShaderResourceView*`，而不是 DX9 的 `LPDIRECT3DTEXTURE9`。
- 仓库已包含 `CustomShaders/*.hlsl`、`*.cso`、`.rc` 的 `SHADERS` 资源项和旧 CSO backend 定制，但当前 ImGui 更新已把 backend 恢复为运行时 `D3DCompile()`。
- 已确认旧实现不能原样复用：`.rc` 中 `IDR_SHADERS1=imgui_ps.cso`、`IDR_SHADERS2=imgui_vs.cso`，旧代码却反向加载；当前 `imgui_vs.cso` 经 `fxc /dumpbin` 验证为 `vs_2_0`，不符合 D3D11 backend 契约。

## Requirements

- R1. 设置窗口的 device、device context、swap chain、render target、resize、present、shutdown 路径全部使用 Direct3D 11，不再调用 `ImGui_ImplDX9_*` 或 D3D9 device API。
- R2. 设置图片资源改为 `ID3D11ShaderResourceView*`；上传函数创建 D3D11 texture + SRV，所有 `ImGui::Image` 继续把 SRV 指针编码为 `ImTextureID`。
- R3. 保持当前 EasyX 图像缓冲的 BGRA 字节语义，选择匹配的 DXGI texture format，不能出现红蓝通道互换或透明度回归。
- R4. `imgui_impl_dx11.cpp` 不得调用或链接 `D3DCompile`；顶点/像素 shader 必须从当前可执行文件内嵌的 `SHADERS` 资源读取，并按 `.rc` 的真实资源映射创建。
- R5. 顶点与像素 CSO 必须由当前 HLSL 离线生成，profile 与当前 ImGui DX11 backend/目标 feature level 兼容；输入布局必须使用顶点 shader 的 bytecode。
- R6. `Inkeys.vcxproj` 与 `.filters` 将 ImGui renderer 编译项从 DX9 切换为 DX11，并修正 HLSL 的 shader type/model 配置；不删除用户已更新但不再参与编译的 DX9 backend 文件。
- R7. 保持目标源码原编码与换行；关键 device/SRV/CSO 资源步骤写简短中文注释；不夹带设置 UI、ImGui 更新或其他渲染路径的重构。
- R8. 构建验证必须使用 ARM64 host `MSBuild.exe` 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，超时不少于 5 分钟。
- R9. 在 `imgui_impl_dx11.cpp` 中用清晰、成对的 Inkeys 注释标记相对 upstream backend 的定制起止范围，并说明该范围仅负责把 shader 创建从运行时编译替换为 EXE 内嵌 CSO，便于后续 ImGui 升级时重新移植。

## Acceptance Criteria

- [x] AC1 (`R1`): 设置代码与工程编译项中不再存在产品路径的 `ImGui_ImplDX9_*`、`LPDIRECT3DDEVICE9`、`PDIRECT3DTEXTURE9` 调用/类型，DX11 backend 已接入。
- [x] AC2 (`R1`, `R2`): 窗口 resize 会释放并重建 render-target view；显示、隐藏、再次显示和线程退出路径按逆序释放 SRV、ImGui backend、swap chain/context/device。
- [x] AC3 (`R2`, `R3`): 全部 10 个现有设置图片加载点成功创建 SRV，`ImGui::Image` 传入的是 `ID3D11ShaderResourceView*` 对应的 `ImTextureID`，上传格式与当前 BGRA 内存一致。
- [x] AC4 (`R4`, `R5`): `imgui_impl_dx11.cpp` 中没有 `D3DCompile` 调用、`d3dcompiler.h` 或 `d3dcompiler` 链接指令；资源加载使用 `IDR_SHADERS2` 创建 vertex shader、`IDR_SHADERS1` 创建 pixel shader。
- [x] AC5 (`R5`): `fxc /dumpbin` 显示新 CSO 分别为预期 vertex/pixel profile，vertex bytecode 包含 POSITION/COLOR/TEXCOORD 输入签名，pixel shader 不对标准 RGBA 结果做错误的 R/B 二次交换。
- [x] AC6 (`R6`): `Inkeys.vcxproj` 与 `.filters` 编译/展示 DX11 backend，HLSL 配置不再生成 `vs_2_0`；`.rc` 继续嵌入与加载映射一致的两个 CSO。
- [x] AC7 (`R7`): 现有未提交 ImGui 更新和其他用户改动均被保留，差异仅覆盖迁移所需文件与 Trellis 任务文档。
- [x] AC8 (`R8`): `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64` 构建成功；若环境原因失败，报告精确失败命令、错误与已完成的静态验证。
- [x] AC9 (`R9`): DX11 backend 的 Inkeys 定制有明确的开始/结束标记、修改目的和 `IDR_SHADERS2=VS`、`IDR_SHADERS1=PS` 映射说明，标记范围不包含未修改的 upstream 逻辑。

## Out of Scope

- 不迁移 Bar、Drawpad、PPT 控件或其他 GDI/D2D/EasyX 渲染路径。
- 不把设置窗口接到进程级共享 D2D/WARP device，不改变窗口线程模型、配置项或 UI 布局。
- 不升级或回退本轮用户已经放入工作区的 ImGui 版本，不删除 DX9 backend 源文件。
