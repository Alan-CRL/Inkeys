# Native Desktop Guidelines

本层主要覆盖 Inkeys/Inkeys.vcxproj 中的 Windows 桌面程序，并记录仓库内独立 Timeout Solution 的工程边界。证据等级沿用 [../index.md](../index.md)；现状、推断、待确认和历史/兼容路径不能互相替代。

**【直接确认】** 主程序同时编译传统 Draw2/PPT 等 Idt* 业务子系统和 Inkeys/Inkeys 下的 C++20 module。UI3 已是唯一悬浮栏入口；`IdtFloating` 与旧 `IdtWindow` 源码仅以工程 `None` 项暂存，不参与产品编译。

## 文档索引

- [directory-structure.md](directory-structure.md)：Solution、目录、文件归属与第三方边界。
- [build-and-compatibility.md](build-and-compatibility.md)：构建入口、配置、依赖和 Windows 兼容约束。
- [cpp-conventions.md](cpp-conventions.md)：命名、模块、注释、共享状态和并发习惯。
- [rendering-and-ui.md](rendering-and-ui.md)：Win32、D3D11、D2D、GDI/EasyX、ImGui 的实际分工。
- [input-and-ink.md](input-and-ink.md)：RTS、鼠标归一化、多点触控、墨迹合成和历史记录。
- [errors-logging-and-resources.md](errors-logging-and-resources.md)：错误传播、日志、COM/DirectX/Win32 资源生命周期。
- [configuration-i18n-and-assets.md](configuration-i18n-and-assets.md)：配置模式、国际化生成链和资源归属。

PPT/WPS 的托管 COM 服务和原生边界另见 [../ppt-interop/index.md](../ppt-interop/index.md)。

## 开始修改前

1. 确认目标属于传统 Idt*、Inkeys.* module、设置窗口、UI3 Bar、IdtFloating、PPT 联动还是独立 Timeout。
2. 确认项目文件是否需要登记新源码、module、资源、shader 或 manifest。
3. 对渲染和输入修改，先画清窗口线程、渲染线程与共享状态的所有权。
4. 对配置修改，先确认字段实际位于 `opt/deploy.json` 的 `SetListStruct` 路径，还是 `Inkeys/Config/main.json` 的 `Inkeys::Config` class / `Inkeys::config` instance；二者当前并存且用途不同。
5. 构建主程序时遵守仓库根 AGENTS.md 的完整 Solution 与 ARM64 MSBuild 要求。

## 实施前决策门（阻塞）

- **UI 路线**：若任务没有明确目标，且从代码/发布上下文无法判断应修改 `IdtFloating`、`Inkeys.UI.Bar` 还是两者，必须在写代码前询问开发者；不得以“旧文件/新 module”为由自行选择。
- **配置归属**：新增字段无法从现有消费者或任务要求确定应进入 `SetListStruct` 还是 `Inkeys.Other.Config` 时，必须先询问开发者；不得先实现后再决定迁移或持久化兼容。
- **平台支持**：Solution、vcxproj、CI matrix 或 README 中存在 Win32/x64/ARM64/Windows 7 条目，只能证明声明或配置存在。若任务需要承诺、扩大或缩小平台支持，必须先取得开发者确认或任务明确要求的运行验证。

## 已直接确认的边界

- wWinMain 位于 Inkeys/IdtMain.cpp。新增其他入口会改变现有进程模型，属于架构变更，而不是本 Spec 已批准的路线。
- D2DStarup 在 UI 分支选择前无条件创建 D3D11 WARP、D2D factory/device 和 DWrite 对象；d2dDevice_WARP 被 UI3 Bar 使用，d2dFactory1/dWriteFactory1 也被 PPT 控件使用。
- 设置窗口的已编译产品实现是普通 Win32 顶层窗口上的 Dear ImGui + Direct3D 11；它拥有独立 hardware device/context、discard swap chain、RTV 和图片 SRV，不复用进程级 D2D/WARP device。
- RTS 笔/触摸与鼠标回退都构造 TouchMode 记录，并写入 TouchPos、TouchList、TouchTemp 等共享状态。
- 画布合成、撤销历史和按 PPT 页保存的墨迹彼此有关，不能只验证屏幕上的即时笔迹。
- 主 Solution 中 Inkeys 依赖 PptCOM；Timeout 属于另一个 Solution，且本次未发现主产品引用。
- `InkeysHeadlessTests` 覆盖 Surface、HiMsg、窗口合同和 UI3 算法；受限环境必须用 `--no-window` 跳过会创建 HWND 的窗口测试。

## 历史/兼容与待确认

- `Experimental.Inkeys3.UI3` 容器仅保留 Animation、EdgeLighting 和 Debug 配置，不再包含路由开关；旧 JSON `Experimental.Inkeys3.UI3` 路由字段在写配置时清理。
- `IdtFloating` 保留一段迁移期供阅读，但不得重新加入编译或被生产代码 include；复用业务必须迁入 `Inkeys.Business` / `Inkeys.Input`。
- Timeout 的发布、打包和 ARM64 计划均待确认。
