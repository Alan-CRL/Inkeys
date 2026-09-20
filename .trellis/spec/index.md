# Inkeys Trellis Spec

本目录记录从当前仓库实际代码中提炼出的工程事实和保守开发指引。首次 Bootstrap 的依据是 2026-07-14 扫描到的 Solution、项目文件、源码、资源和构建配置；本轮未运行程序或实际编译。

## 证据等级

- **【直接确认】**：可由列出的源码、Solution、项目或配置文件直接复核。
- **【合理推断】**：根据重复实现或调用关系得到的保守建议，不代表团队已确认的长期路线。
- **【待确认】**：需要维护者意图、实际构建或运行矩阵才能确定；未来 Codex 不得自行选择答案。
- **【历史/兼容】**：代码仍存在或仍可被选择，但仅凭存在不能判断它是推荐路线、弃用路线还是发布默认。

“代码目前这样写”只证明现状。只有 AGENTS.md、明确的项目文档、重复稳定模式或维护者确认，才能升级为未来任务的强制规范。

## 有效 Code-Spec

| 改动范围 | 必读入口 | 专题文档 |
| --- | --- | --- |
| Inkeys 原生桌面程序 | [native-desktop/index.md](native-desktop/index.md) | [目录](native-desktop/directory-structure.md)、[构建/兼容](native-desktop/build-and-compatibility.md)、[C++](native-desktop/cpp-conventions.md)、[渲染/UI](native-desktop/rendering-and-ui.md)、[输入/墨迹](native-desktop/input-and-ink.md)、[错误/资源](native-desktop/errors-logging-and-resources.md)、[配置/i18n/资源](native-desktop/configuration-i18n-and-assets.md) |
| PowerPoint/WPS COM 桥接 | [ppt-interop/index.md](ppt-interop/index.md) | [COM 契约与数据流](ppt-interop/com-contract.md) |
| 思考清单 | [guides/index.md](guides/index.md) | guides 是 Trellis 通用提示，不是 Inkeys 架构事实；其中 Web/API 示例需映射到本项目实际层 |
| Draw3 迁移兼容 | [native-desktop/index.md](native-desktop/index.md) | [Draw3 集成契约](native-desktop/draw3-integration.md)、[Draw3 shader 迁移](native-desktop/draw3-shaders.md) |

**【直接确认】** Timeout/InkeysTimeout 是独立 Solution，未出现在 InkeysRepo.sln、主项目、当前 Windows CI 构建或 Inkeys/exe 中。**【待确认】** 它是否属于当前产品发布范围；native-desktop 只记录其仓库和工程边界。

## 实施前决策门（强制）

以下规则由开发者在 Bootstrap 最终收尾中明确确认。命中条件且任务上下文没有给出答案时，必须在实施前询问开发者，不能由 Codex 自行选择：

| 未决边界 | 强制动作 | 详细依据 |
| --- | --- | --- |
| UI 工作无法判断应修改 `IdtFloating`、`Inkeys.UI.Bar` 还是两者 | 先询问当前发布路线和要求覆盖的 UI；不得因文件新旧自行选边 | [native-desktop/index.md](native-desktop/index.md)、[rendering-and-ui.md](native-desktop/rendering-and-ui.md) |
| 新配置字段无法判断属于 `SetListStruct` 还是 `Inkeys.Other.Config` | 先询问字段归属、持久化兼容和迁移要求；不得按“新/旧”名称自行决定 | [configuration-i18n-and-assets.md](native-desktop/configuration-i18n-and-assets.md) |
| 任务依赖 Windows/Win32/x64/ARM64 支持结论，但只有工程配置或 README 声明 | 不得写成“已经验证支持”；先取得开发者确认或任务明确授权的运行证据 | [build-and-compatibility.md](native-desktop/build-and-compatibility.md) |
| PPT/WPS 支持版本、位数、安装方式或兼容分支范围不明确 | 先询问正式支持范围；不得自行扩大或缩小，也不得据此删除兼容分支 | [ppt-interop/index.md](ppt-interop/index.md) |

## 使用原则

1. 先读对应层的 index，再读与改动类型直接相关的专题文档。
2. **【合理推断】** 小范围修改优先保持目标子系统的既有技术栈和所有权模型；这来自 AGENTS.md 的最小修改要求，不代表传统 Idt* 或 C++20 module 中任一体系已被确认是全仓未来路线。
3. 规范中的代码路径是可核对的依据。若代码与文档不一致，应先重新调查，再更新规范。
4. “待确认”不是默认决策；命中上方实施前决策门时必须先询问开发者。其他事实性问题只有在任务明确授权时，才能用构建或运行证据补全。
5. Vcpkg 是第三方依赖子模块，Inkeys/additional、Inkeys/HiEasyX、Timeout/InkeysTimeout/json 也包含外部代码。除任务明确要求外，不把第三方实现当作本项目风格样本。

6. Draw3 迁移代码必须遵守 native-desktop 的外部 Drawpad HWND、独立 D3D 设备、透明呈现回退和单一 RTS producer 契约；源仓库的 standalone demo 不属于主产品入口。
