# C++ Conventions

本文把源码观察与未来规则分开：`【直接确认】`描述当前代码，`【合理推断】`给出在现有边界上做最小变更时的审查原则，`【待确认】`表示尚无维护者政策，`【历史/兼容】`表示不得直接当作新代码模板。

## 先识别代码世代

`【直接确认】` 仓库不是单一风格代码库：

- `Inkeys/IdtMain.cpp`、`IdtDrawpad.cpp`、`IdtRts.cpp`、`IdtPlug-in.cpp` 等传统区域广泛使用全局状态、头文件声明、显式锁和 `std::thread`；
- `Inkeys/Inkeys/` 下存在 C++20 module，使用 `Inkeys.*` module 名、`Inkeys` 命名空间、领域子目录以及部分 RAII 类型；
- `Inkeys/Inkeys/UI/Setting/Setting.cpp` 等新区域可见 `std::jthread`，`Inkeys/Helper/Helper.Thread.*` 可见 `StatusGuard`；但 `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` 等 module 区域仍使用 detached thread 和全局状态；
- 一些 module 通过 global module fragment 包含 `IdtMain.h`，继续读取传统共享状态。

因此，不能把“传统 = thread/detach、新 module = jthread/RAII”写成绝对分界。

`【直接确认；AGENTS.md】` 修改应最小化并保持目标文件编码、换行。`【合理推断】` 局部任务应遵循目标子系统已有边界，不把修复扩大成全仓命名、module 或线程模型迁移。

## 当前命名观察

| 对象 | `【直接确认】`的常见形式 | 实例 |
| --- | --- | --- |
| 传统文件/类型/函数 | `Idt` 前缀或 PascalCase | `IdtDrawpad.cpp`、`D2DStarup`、`StartForInkeys` |
| C++ module | `Inkeys.领域.功能` | `Inkeys.Other.Config`、`Inkeys.Net.Update`、`Inkeys.UI.Bar` |
| module 文件 | 领域/功能命名的 `.cppm`，部分配 `.cpp` | `Other.Config.cppm`、`Bar.State.cppm` |
| 命名空间 | `Inkeys` 及子命名空间 | `Inkeys::UI::Bar` |
| 传统类型后缀 | 部分使用 `Class`、`Struct`、`Enum` | `StrokeImageClass`、`SetListStruct` |
| 共享状态 | camelCase、PascalCase 与历史名称并存 | `offSignal`、`TouchList`、`useMouseInput` |

`【历史/兼容】` `Bar.Buttom.cpp`、`ActivateSildeShowWindow`、`D2DStarup` 等拼写已经进入文件名、调用点或 COM 接口。除非任务明确覆盖全部引用及兼容产物，不要顺手改名；它们也不是新名称的推荐拼法。

`【合理推断，不是强制全仓规范】` 在已有 module 目录内新增同领域文件时，优先沿用相邻 module 名和目录结构。是否要求所有未来代码迁入 C++ module，仓库没有给出政策，属于 `【待确认】`。

## module 与传统头文件边界

- `【直接确认】` module 接口单元使用 `.cppm`，需要分离实现时可见同目录 `.cpp`；具体项目项由 `Inkeys/Inkeys.vcxproj` 登记。
- `【直接确认】` `IdtMain.h` 是传统汇聚头，包含多种 Windows/图形依赖并声明大量跨模块状态。
- `【合理推断】` 新独立能力不应无理由新增对 `IdtMain.h` 的依赖；但现有 module 已经依赖它，不能把该建议误写成当前代码全都遵守的规则。
- `【直接确认；AGENTS.md】` 改动 module import/export 或工程项后，应从完整 `InkeysRepo.sln` 验证，而不是单独构建 vcxproj。
- `【历史/兼容】` 传统 `Idt*.h/.cpp` 配对是现状，不代表未来功能必须继续扩大全局头。

## 注释、编码与格式

- `【直接确认；AGENTS.md】` 关键步骤和写法使用简短中文注释；不要求逐行解释。
- `【直接确认；AGENTS.md】` 保留原文件编码和换行。
- `【直接确认】` `Inkeys/Inkeys.vcxproj` 启用 `/utf-8`；根 `.gitattributes` 仅包含 GitHub Linguist 的 C++ 识别设置，没有统一 EOL 规则。
- `【合理推断】` 延续目标文件的缩进、花括号和 include/import 排列，避免功能改动附带整文件格式化。
- `【合理推断】` 非直观线程切换、兼容回退和资源所有权值得在局部写“为什么/谁释放”；这不是要求给历史代码补齐所有注释。

## 状态与并发

`【直接确认】` 当前代码并存：

- `IdtAtomic` 包装的共享状态；
- `std::shared_mutex`、`std::mutex` 与显式 `lock/unlock`；
- `TouchList`、`TouchTemp`、`StrokeImageList` 等跨线程容器；
- `std::thread`、`detach`、全局 `offSignal` 和线程状态；
- 部分代码中的 `std::jthread`、`stop_token`、`StatusGuard`。

修改共享状态时采用以下 `【合理推断】` 审查方法：

1. 先搜索全部读写方，记录实际线程、锁或原子封装；不要仅凭变量名假设同步协议。
2. 新读写路径沿用该状态已经使用的同步入口；若现有访问本身不一致，先记录为风险并单独确认，不能在 Spec 中宣称它已线程安全。
3. 新增长期工作线程时明确退出信号、状态登记、捕获对象生命周期和资源清理，并与所在子系统的协议一致。
4. 改变持锁范围、在锁内新增 COM/I/O/窗口调用或更换线程类型，都属于并发行为变更，需要专门验证，不能从通用建议自动实施。
5. detached thread 是当前实现事实，不等于已确认缺陷，也不等于推荐的新线程模型；快速退出安全性需按具体调用点验证。

## 最小变更边界

- `【直接确认；AGENTS.md】` 只修改完成任务所需的部分，不做未要求的优化。
- `【合理推断】` 修改公开 enum、配置键、i18n key、COM 接口或共享结构前，先搜索全部生产者与消费者。
- `【合理推断】` 不以“现代化”为由顺带替换渲染后端、所有权或异常模型。
- `【历史/兼容】` 发现旧拼写、raw pointer、空 catch 或重复工程项时，先记录证据；除非任务授权并完成行为验证，不把文档观察直接升级成源码清理。
