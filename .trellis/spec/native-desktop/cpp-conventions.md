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

## 模块导出与独立纯接口

`【直接确认】` 当前 ARM64 MSVC 下，**同一模块**的 interface partition 之间前置声明一个导出类时，前置声明必须保持同一导出身份，例如 `UI3.Renderer.cppm` 里的 `export class PanelPoseScope;`（完整定义在 `:Panel`）、`Bar.Main.cppm` 里的 `export class BarUISetClass;`。漏掉 `export` 时，外部消费者经根模块拿到的是另一个未完成类型，报 `C2027`。这只适用于同一模块的分区之间；**跨模块不得前置声明**对方拥有的类型，需要就 `import` 对方（见下文「module 与传统头文件边界」）。修改这类边界时，用同工具链的最小模块探针确认，不能为了验证把纯测试接口改成完整主栏依赖。

## 当前命名观察

| 对象 | `【直接确认】`的常见形式 | 实例 |
| --- | --- | --- |
| 传统文件/类型/函数 | `Idt` 前缀或 PascalCase | `IdtDrawpad.cpp`、`D2DStarup`、`StartForInkeys` |
| C++ module | `Inkeys.领域.功能` | `Inkeys.Other.Config`、`Inkeys.Net.Update`、`Inkeys.UI.Bar` |
| module 文件 | 领域/功能命名的 `.cppm`，部分配 `.cpp` | `Other.Config.cppm`、`Bar.State.cppm` |
| 命名空间 | `Inkeys` 及子命名空间 | `Inkeys::UI::Bar` |
| 传统类型后缀 | 部分使用 `Class`、`Struct`、`Enum` | `StrokeImageClass`、`SetListStruct` |
| 共享状态 | camelCase、PascalCase 与历史名称并存 | `offSignal`、`TouchList`、`useMouseInput` |

`【历史/兼容】` `ActivateSildeShowWindow`、`D2DStarup` 等拼写已经进入调用点或 COM 接口。除非任务明确覆盖全部引用及兼容产物，不要顺手改名；它们也不是新名称的推荐拼法。标准按钮的通用部分在 `UI3.Button.*`（`ButtonClass`、`Button*` 常量与入口，不带 `Bar` 前缀），产品按钮元数据与主栏缺省值包装在 `Bar.Button.*`（`BarButtonClass`、`BarButtonListClass`）；新增引用不得恢复迁移前的旧名称。

`【合理推断，不是强制全仓规范】` 在已有 module 目录内新增同领域文件时，优先沿用相邻 module 名和目录结构。`【直接确认】` `Inkeys/Inkeys/` 下的新代码按下一节的头文件判定标准写成 module；传统 `Idt*` 文件不为此迁移。

## module 与传统头文件边界

- `【直接确认】` module 接口单元使用 `.cppm`，需要分离实现时可见同目录 `.cpp`；具体项目项由 `Inkeys/Inkeys.vcxproj` 登记。
- `【直接确认】` **头文件判定标准**（2026-11-17 起，`Inkeys/Inkeys/` 下适用）：含类或函数定义的写成 `.cppm`（需要实现体时加 `.cpp`）；只起引用作用的（纯宏、纯前置包含）可以保留 `.h`。`.cppm` 的全局模块片段（`module;` 与 `export module` 之间）内可以 `#include` 传统头与第三方头。
- `【直接确认】` **独立模块与分区的选用**：不依赖 D2D 设备运行时状态、不依赖 `IdtMain.h`、可能被测试工程或其他领域单独导入的叶子能力写成独立模块（`Inkeys.UI.UI3.<Name>`、`Inkeys.UI.Bar.<Name>`）；被 `InkeysHeadlessTests` 消费的东西必须是独立模块。彼此需要对方完整类型、只作为整体使用的重型运行时写成分区（`Inkeys.UI.UI3:<Name>`）：接口侧单向依赖，反向依赖放进实现单元；根模块 `export import` 全部分区。
- `【直接确认】` 不得在一个模块里前置声明另一个模块拥有的类型；需要对方类型就 `import` 对方。同一模块的分区之间需要前置声明时，声明必须带与定义相同的导出身份（见上文「模块导出与独立纯接口」）。
- `【直接确认】` 预处理效果不能导出：`NOMINMAX`、`min`/`max` 的保存恢复只在写它的翻译单元内有效，各消费者自行处理；模块内部统一写 `(std::min)` / `(std::max)`。
- `【直接确认】` 「依赖模块符号的普通头，在 import 之后再 include」不可靠（同一头在接口与实现单元各包含一次会重复定义），这类头一律转成模块。导出分区的全局片段若带进含默认 `operator<=>` 的头，所有导入方都需要 `<compare>`：不需要对外的内容放进实现单元或内部分区。
- `【直接确认】` 新符号避开 Windows SDK 已有名字（实例：`GestureRecognizer` 与 SDK 类型撞名，改为 `PointerGestureRecognizer`）。
- `【直接确认】` `IdtMain.h` 是传统汇聚头，包含多种 Windows/图形依赖并声明大量跨模块状态。
- `【合理推断】` 新独立能力不应无理由新增对 `IdtMain.h` 的依赖；但现有 module 已经依赖它，不能把该建议误写成当前代码全都遵守的规则。
- `【直接确认；AGENTS.md】` 改动 module import/export 或工程项后，应从完整 `InkeysRepo.sln` 验证，而不是单独构建 vcxproj。
- `【历史/兼容】` 传统 `Idt*.h/.cpp` 配对是现状，不代表未来功能必须继续扩大全局头。

## 注释、编码与格式

- `【直接确认；AGENTS.md】` 关键步骤和写法使用简短中文注释；不要求逐行解释。
- `【直接确认；AGENTS.md】` 保留原文件编码和换行。
- `【直接确认】` 新建或被改动的 `.cpp` / `.cppm` / `.h` 一律 UTF-8 **带 BOM** + CRLF；原本无 BOM 的源文件在被修改时补上 BOM。未被修改的文件不为统一格式而改动；`.vcxproj`、`.filters`、`.sln` 与 Markdown 保持各自现有格式。
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
- Shell/Win32 采样可能同步派发窗口消息。`Refresh` 在加锁前检测同线程重入，只合并请求并返回 `false`；完整拓扑刷新必须覆盖任务栏结构/可见性刷新。普通调用仍同步采样，最多两轮后一次性发布，离开重入作用域和内部锁后才 drain 订阅回调。
- 末轮采样后到达的请求必须保留，由带 observer epoch 的合并消息处理；延后处理自身不无限自投递，持续溢出的请求由下一次外部刷新或现有 5 秒轮询消费。Shutdown 清空旧生命周期请求，旧 epoch 投递不得占住新 observer 的唤醒名额。
- `INKEYS_DISPLAY_TESTING` 仅在 headless 目标开放采样替身；回归必须经过生产 `Refresh` 的加锁、请求合并和发布路径，不用复制合并算法代替崩溃路径验证。
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

## Scenario: StateMode 跨线程值快照与长手势版本

### 1. Scope / Trigger

Bar 渲染/交互、MouseHook、PPT 接管或配置保存访问全局 StateModeClass 时适用。StateModeSelect、Target 和 Echo 含义不同，不能因当前常一起写就合并。

### 2. Signatures

`GetStateModeSnapshot()` 在已有模式锁内复制完整值；`GetStateModeVersionedSnapshot()` 在同一锁内返回 `{ state, revision }`。`GetPenWidth/Color/EffectivePenOpacity(snapshot)` 和 Bar.Layout 带快照重载只读传入值。长手势最终提交用 `SetPenWidthIfRevision(value, expectedRevision)` 或 `SetShapeModeSelectIfRevision(shape, expectedRevision)`。

### 3. Contracts

- 模式、Pen/Shape 子型、宽色的生产写入在 stateModeTransitionMutex 下进行；Bar 一帧只取一次值快照，交互每消息更新。长手势按下时同锁取状态和 revision，抬手或惯性提交仍须在锁内核版本。
- 条件 PPT 入口原有 expected revision 不得替换成无条件 setter。重复选中同笔型可更新意图版本，但无视觉变化时不重复发布 Draw3 命令。
- 模式锁内只复制/修改小型状态或提交无 HWND 等待的 Draw3 bridge；工作区门控、工具恢复和 workspace/Presentation target（含清除）发布共用此边界，锁顺序为白板意图锁 → 模式锁 → product 提交锁；PPT 必须带采样时的 scene epoch，Exiting 可恢复场景，但退出选择仍校验退出边沿的严格 input epoch。恢复 Presentation 工具不单独发布无 target 的工作区。不得持锁调用会再次取锁及等待窗口的 SyncDraw3State，也不做 SetMemory 磁盘 I/O、HWND/GPU/COM 调用；这些副作用在解锁后由原 owner 执行。无参 getter 各自安全，但连续调用不能代替同一业务/帧快照。

### 4. Validation & Error Matrix

| 场景 | 预期 |
| --- | --- |
| PPT 旧回调在用户选笔之后到达 | revision 不匹配，旧结果被拒绝 |
| FineDial/预设/形状按下后模式切换 | 旧候选取消，最终条件 setter 不写新工具 |
| Bar 帧中途收到模式切换 | 本帧继续用已取得的同代值，下帧看到新状态 |
| 模式锁争用或保存文件慢 | 不在锁内等待磁盘/窗口/渲染线程；需另量尾延迟 |

### 5. Good / Base / Bad Cases

Good：同锁获取 mode、Laser、penMode、宽色及 revision，帧/手势只消费该份值；Base：单一低频判断只调用一次 GetStateModeSnapshot；Bad：逐字段 IdtAtomic 后以为跨字段天然一致，或裸写子型再调用模式 setter。

### 6. Tests Required

完整 InkeysRepo.sln Debug|ARM64 与适用 Release 构建，InkeysHeadlessTests --no-window；Headless 不直接编译执行 IdtState 入口。

显式 `Inkeys.exe --state-mode-production-test` 在配置、单实例和 HWND 初始化前执行真实 IdtState/bridge 与 BarInteractionSession，成功退出 0，额外参数退出 2。覆盖工具/PPT 等价投影、重复意图 revision+1 但 bridge 不重发、快速切换、旧 revision 的宽度/形状/Selection/PPT 请求拒绝，以及 FineDial 失效 tick、跨代惯性和取消提交。常规宽色 setter 使用 `setMemory=false`；工作区保存隔离回归只在独立临时目录调用真实 `SetMemory()`，退出恢复路径并清理本次创建的目录，不读写用户配置。Cancel 后先断言候选清理才调用空 Commit，失效 Commit 必须被真实版本门拒绝。禁止在未初始化路径的早期检查中执行会保存配置的成功 FineDial Commit。

该 CLI 不启动 Host/Window Service、设备、RTS 或保存 worker；不能证明实际 HWND owner、光标、真实 Office 回调、FineDial 成功保存或呈现尾延迟。获 GUI 授权后以私有配置自动操作分别验证这些边界，不把观察器帧交付次数称为产品可见帧率。

### 7. Wrong vs Correct

Wrong：Bar 先裸写 Pen.ModeSelect，再无条件切 Pen；FineDial 用旧候选调用普通 SetPenWidth。Correct：用锁内 Pen 工具事务；长手势从同锁版本快照取工具与 revision，最终用 IfRevision 入口提交。

## 最小变更边界

- `【直接确认；AGENTS.md】` 只修改完成任务所需的部分，不做未要求的优化。
- `【合理推断】` 修改公开 enum、配置键、i18n key、COM 接口或共享结构前，先搜索全部生产者与消费者。
- `【合理推断】` 不以“现代化”为由顺带替换渲染后端、所有权或异常模型。
- `【历史/兼容】` 发现旧拼写、raw pointer、空 catch 或重复工程项时，先记录证据；除非任务授权并完成行为验证，不把文档观察直接升级成源码清理。
