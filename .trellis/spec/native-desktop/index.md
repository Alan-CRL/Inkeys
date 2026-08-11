# Native Desktop Guidelines

本层主要覆盖 Inkeys/Inkeys.vcxproj 中的 Windows 桌面程序，并记录仓库内独立 Timeout Solution 的工程边界。证据等级沿用 [../index.md](../index.md)；现状、推断、待确认和历史/兼容路径不能互相替代。

**【直接确认】** 主程序同时编译传统 Idt* 子系统和 Inkeys/Inkeys 下的 C++20 module。**【待确认】** 这是否代表正式迁移方向；不要把文件年代或 module 语法自动当成推荐优先级。

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
- 设置窗口的已编译产品实现是 Dear ImGui Win32 + Direct3D 11；它拥有独立 hardware device/context、discard swap chain、RTV 和图片 SRV，不复用进程级 D2D/WARP device。`Inkeys.vcxproj` 编译带 Inkeys 定制标记的 DX11 backend，仓库不再随附 ImGui DX9 backend。
- RTS 笔/触摸与鼠标回退都构造 TouchMode 记录，并写入 TouchPos、TouchList、TouchTemp 等共享状态。
- 画布合成、撤销历史和按 PPT 页保存的墨迹彼此有关，不能只验证屏幕上的即时笔迹。
- 主 Solution 中 Inkeys 依赖 PptCOM；Timeout 属于另一个 Solution，且本次未发现主产品引用。
- 没有扫描到自动化测试项目。目标架构构建和专题手工检查是当前审计建议，不是已确认的正式发布门禁；正式清单待维护者确认。

## 历史/兼容与待确认

- IdtConfiguration.h 将 Experimental.Inkeys3.UI3 默认初始化为 false；IdtMain.cpp 据此在 floating_main 与 Inkeys::UI::Bar::Initialization 之间二选一。持久化 deploy.json 可改变该值，所以静态默认不等于发布默认。
- IdtFloating 仍是可执行分支，不能仅因文件名/实现方式较旧就标记为废弃；UI3 Bar 也不能仅因名称较新就标记为正式主路径。
- Timeout 的发布、打包和 ARM64 计划均待确认。
