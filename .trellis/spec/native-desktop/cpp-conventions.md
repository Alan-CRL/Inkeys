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

`【历史/兼容】` `ActivateSildeShowWindow`、`D2DStarup` 等拼写已经进入调用点或 COM 接口。除非任务明确覆盖全部引用及兼容产物，不要顺手改名；它们也不是新名称的推荐拼法。UI3 按钮文件与类型现已统一为 `Bar.Button.cpp` / `BarButton*`，新增引用不得恢复旧名称。

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

## 显示快照与订阅合同

`【直接确认】` `Inkeys.Display` 是主程序显示器枚举、主屏/虚拟桌面、工作区、有效 DPI、方向与 EDID 物理尺寸的统一来源。消费者不得恢复 `MainMonitor`、`DisplaysInfo` 等拆分全局状态。

- 每次业务操作或渲染帧只保留一个 `SnapshotPtr`，从同一快照读取 bounds、workArea、DPI、方向和 EDID；不得分别调用或缓存字段后拼出跨 generation 状态。
- 刷新先在局部完整构造候选快照；只有语义变化才递增 generation。新快照原子发布后，首次订阅通知和后续刷新通知都进入同一串行 publication 队列；单个订阅者不得重复或倒序收到 generation。
- publication 回调在 Display 内部锁外执行。回调只发布目标或请求 UI 客户端，不直接操作 HWND/D2D 资源，也不能要求调用方持有 Display 内部锁。
- `Subscription::Reset()` 与 `Shutdown()` 返回前必须等待该订阅者正在执行的回调退出；回调自行注销时不得等待自身。Shutdown 先禁止新刷新并清空发布状态，再在刷新锁外 drain 回调，避免回调重入 `Refresh()` 时死锁。
- 后续枚举失败保留最后一个快照的几何、DPI和原始诊断数据，但必须发布 `TopologyUnknown` 物理失效状态；首次失败发布显式 `fallback`，其 EDID 仍为 unknown。不得用系统指标伪造物理尺寸。

## 场景：显示物理尺寸业务标尺

### 1. Scope / Trigger

当业务需要把屏幕像素距离换算为厘米，或读取 `MonitorInfo::edid`、`MonitorInfo::physicalSize`、`Snapshot::topology` 时，必须应用本合同。目标是避免复制屏、错配 EDID 或伪造尺寸进入笔速橡皮等输入热路径。

### 2. Signatures

- 原始设备信息：`EdidInfo { valid, status, devicePath, deviceId, rawBytes, rawPhysicalWidthCm, rawPhysicalHeightCm }`。
- 业务信息：`PhysicalSizeInfo { available, widthCm, heightCm, unavailableReason }`。
- 快照拓扑：`DisplayTopology::{Unknown, Single, Extended, CloneOrMixed}`。
- 纯策略：`ClassifyTopology(targets)` 与 `ResolvePhysicalSize(edid, orientation, topology, uniqueTarget, fallback)`。

### 3. Contracts

- `EdidInfo::valid` 仅表示原始 EDID 解析成功；业务不得用它代替 `physicalSize.available`。
- 原始尺寸永远保持 EDID 原始方向；业务宽高按当前 90/270 度方向交换，不可用时均为零。
- 单屏和纯扩展允许物理标尺；纯扩展逐屏判定。任何 source 对应多个 target 时均为复制或混合，所有屏禁用。
- DisplayConfig、SetupAPI 和注册表只允许在 `Refresh` 枚举阶段调用；逐点输入和渲染热路径只读取同一 `SnapshotPtr` 或由其发布的低频配置。

### 4. Validation & Error Matrix

| 条件 | `PhysicalSizeUnavailableReason` |
| --- | --- |
| 首次 fallback | `SnapshotFallback` |
| 活动拓扑查询不可靠 | `TopologyUnknown` |
| 复制或部分复制 | `CloneOrMixed` |
| source/target 与逻辑屏不是一一对应 | `DisplayTargetAmbiguous` |
| EDID 未找到、读取失败、解析失败 | 对应 `Edid*` 原因 |
| 任一原始边为 0 | `MissingDimensions` |
| 任一原始边小于 5 cm | `DimensionsBelowMinimum` |
| 其余单屏或纯扩展屏 | `None` 且 `available=true` |

### 5. Good/Base/Bad Cases

- Good：两台同分辨率显示器拥有不同 source，分别匹配各自 target 和 EDID，按屏提供物理尺寸。
- Base：纯扩展中一台 EDID 读取失败，只禁用该屏，另一台可靠屏继续可用。
- Bad：主屏 source 同时驱动两个 target，即使两个 EDID 都正常也必须全局禁用物理标尺。

### 6. Tests Required

- 纯策略测试断言单屏、等分辨率扩展、复制和部分复制分类只取决于活动 source/target 关系。
- EDID 测试断言 128 字节基础块头、校验和、零尺寸、4 cm、5 cm 和旋转行为。
- 快照测试断言拓扑、target、EDID 或业务失效原因变化属于语义变化，等价刷新不增加 generation。
- 现场枚举测试不得假设测试机拓扑，但必须断言 DPI/像素始终保留，复制拓扑没有可用物理标尺。

### 7. Wrong vs Correct

~~~cpp
// Wrong：原始 EDID 已解析不代表当前拓扑允许业务换算。
if (monitor.edid.valid)
    UsePhysicalScale(monitor.edid.rawPhysicalWidthCm);

// Correct：只消费快照已经判定并按方向处理的业务尺寸。
if (monitor.physicalSize.available)
    UsePhysicalScale(monitor.physicalSize.widthCm);
~~~

## 最小变更边界

- `【直接确认；AGENTS.md】` 只修改完成任务所需的部分，不做未要求的优化。
- `【合理推断】` 修改公开 enum、配置键、i18n key、COM 接口或共享结构前，先搜索全部生产者与消费者。
- `【合理推断】` 不以“现代化”为由顺带替换渲染后端、所有权或异常模型。
- `【历史/兼容】` 发现旧拼写、raw pointer、空 catch 或重复工程项时，先记录证据；除非任务授权并完成行为验证，不把文档观察直接升级成源码清理。
