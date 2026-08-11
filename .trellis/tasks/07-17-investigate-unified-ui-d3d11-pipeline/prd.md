# 调查 Bar 与 Setting UI 统一 D3D11 设备和串行渲染管道

## Goal

调查 Bar UI 与 Setting UI 统一使用同一个 D3D11.1 device / immediate context 的可行方案，明确 ImGui DX11 后端从 CSO 加载着色器、同设备串行渲染以及 Setting 普通窗口化所需的边界和渲染时机，为后续单独的实施任务提供证据。当前任务只调查，不修改产品代码。

## Background

- 【直接确认】当前规范记录 Setting 产品实现仍使用 Dear ImGui Win32 + Direct3D 9；UI3 Bar 使用由 D3D11/D2D 初始化链创建的资源。
- 【用户目标】Bar UI 与 Setting UI 最终共享同一个 D3D11.1 device，并以串行方式完成界面渲染。
- 【待调查】历史会话检索尚未找到独立于本次讨论的 CSO 实现证据，必须继续检查当前源码、附带 ImGui backend、分支及 Git 历史。
- 【平台约束】调查需要覆盖现代 Windows ARM64 构建，以及 Windows 7 SP1 + KB2670838 的 x64 兼容边界；不得把 D3D11.1 API 等同于 Feature Level 11_1。

## Requirements

- R1：盘点 Bar 与 Setting 当前使用的 HWND、线程、消息循环、D3D/DXGI/D2D/ImGui 对象、创建与销毁顺序，并以文件和行号作为证据。
- R2：确认共享边界，包括 device、immediate context、每窗口 swap chain、RTV/D2D target、字体和纹理资源分别应共享还是独立。
- R3：检查当前源码、`Inkeys/additional`、相关分支和 Git 历史，确认 ImGui DX11 后端是否已经实现或曾实现 CSO/预编译 Shader 加载；记录可复用文件、提交和缺口。
- R4：研究用构建期 CSO 替代 ImGui DX11 backend 运行时 Shader 编译的方案，覆盖 VS/ARM64 构建、资源交付、错误处理、Shader 版本一致性和 Win7 运行依赖。
- R5：提出串行渲染时间线，明确消息处理、状态更新、Bar 渲染、Setting 渲染、D2D BeginDraw/EndDraw、ImGui NewFrame/Render、RTV 绑定、状态隔离和 Present 顺序。
- R6：调查两个独立 HWND 共享 immediate context 时的线程归属与同步要求，禁止在缺少同步依据时跨线程并发使用 immediate context。
- R7：覆盖 resize、DPI、隐藏/显示、最小化、设备丢失、swap chain 重建、Setting 关闭和应用退出等生命周期分支。
- R8：调查把 Setting UI 改为普通顶层窗口的可行性，比较 owner/parent、激活与焦点、任务栏、标题栏、最小化/最大化、TopMost、NoActivate、DPI 和关闭语义。
- R9：明确普通窗口化是否要求独立 swap chain，以及它与共享 device、串行渲染调度之间的关系。
- R10：形成推荐方案、备选方案、风险清单和可分阶段验证的最小原型建议，但不在本任务内实现原型。

## Constraints

- 不修改 Bar、Setting、ImGui backend、项目文件、Shader 或窗口代码。
- 不引入 ImFluent、RmlUi 或新的第三方依赖；UI 库选型不属于本调查任务。
- 不创建新的程序入口，不改变现有进程模型。
- 不假定两个 HWND 可以共享同一个 swap chain；必须从 DXGI 和现有窗口结构得出结论。
- 不假定 D3D11.1 可选能力或 Feature Level 11_1 在 Win7 上可用；报告必须分别标注接口可用性、运行时探测和回退要求。
- 所有“已有实现”结论必须由当前源码或 Git 历史证明，不能只依据会话记忆。

## Acceptance Criteria

- [ ] 在任务 `research/` 下形成带文件:行号证据的现状架构审计，覆盖 Bar、Setting、设备、Context、窗口和线程所有权。
- [ ] 给出 device/context/swap chain/RTV/D2D target/ImGui resources 的共享与独立矩阵。
- [ ] 给出 CSO 历史检索结论；若存在旧实现，标出提交和复用条件；若不存在，记录检索范围和否定证据。
- [ ] 给出 ImGui DX11 CSO 加载方案，明确编译产物、打包方式、初始化失败行为和 Win7 依赖边界。
- [ ] 给出正常帧、Setting 打开/关闭、resize/device reset 三类串行渲染时序。
- [ ] 给出共享 immediate context 的线程和状态隔离规则，并指出禁止的并发调用路径。
- [ ] 给出 Setting 普通窗口化差异表、推荐窗口所有权模型及独立 swap chain 结论。
- [ ] 给出现代 Windows ARM64 与 Win7 SP1 + KB2670838 x64 的兼容性矩阵和运行时回退点。
- [ ] 给出后续实施任务的建议拆分、验证门和回滚点，但本任务没有产品代码变更。
- [ ] 调查结论由用户评审后，才决定是否创建或激活后续实施任务。

## Out of Scope

- 实际迁移 Setting 从 D3D9 到 D3D11。
- 实际修改或替换 ImGui backend。
- 实际生成、嵌入或加载 CSO。
- 实际合并 Bar/Setting 渲染循环或改变窗口样式。
- Fluent 控件库接入和 Setting 控件重构。
