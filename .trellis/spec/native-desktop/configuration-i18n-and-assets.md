# Configuration, I18n, and Assets

本文使用 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。尤其注意：“新配置 module”是代码年代/结构描述，不表示维护者已宣布它取代传统配置。

## 两套配置系统分别服务哪些代码

| 配置系统 | 文件与磁盘位置 | `【直接确认】`的使用范围 |
| --- | --- | --- |
| 传统 `SetListStruct` | `IdtConfiguration.h`；`IdtConfiguration.cpp::ReadSetting/WriteSetting`；`globalPath + L"opt\\deploy.json"` | `IdtMain`、`IdtDraw`/`IdtDrawpad`、`IdtFloating`、`IdtFreezeFrame`、`IdtHistoricalDrawpad`、`IdtMagnification`、`IdtPlug-in`、`IdtRts`、`IdtWindow`、`Net.Update`、`Bar.Main`、`Setting` 等仍有读写 |
| `Inkeys.Other.Config` | `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`；`Inkeys::Config` class、全局实例 `Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`globalPath + L"Inkeys\\Config\\main.json"` | schema 中可见 `Config.AutoClean`、`Info`、`UI.Bar.Zoom`、`Experimental.Inkeys3.UI3.Animation/EdgeLighting`、`PlugIn.PPTHelper` 等；消费者包括 `IdtConfiguration.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Net.Update*`、`Bar.Zoom*`、`Bar.Main*` 和 `Setting*` |

`【直接确认】` `INKEYS_CONFIG_SCHEMA` 集中声明新 module 已支持的字段；源码中还把启动读取区标注为“新配置 Test”。这说明新系统正在实际使用，但不足以证明迁移已完成或所有新字段必须进入它。

### 容易混淆的真实边界

- `【直接确认】` 是否启用 `Inkeys.UI.Bar` 的开关是传统 `setlist.Experimental.Inkeys3.UI3`，定义于 `IdtConfiguration.h`，由 `IdtMain.cpp` 读入 `useInkeys3UI`。
- `【直接确认】` 新配置中的 `Experimental.Inkeys3.UI3.Animation` 控制 UI3 动画；`Experimental.Inkeys3.UI3.EdgeLighting` 控制 UI3 边缘点光及第三鼠标光。它们都不是选择新旧悬浮栏的总开关。
- `【直接确认】` Bar 的 zoom 配置走 `Inkeys.Other.Config`；Bar 的其他行为仍可读取传统 `setlist`，因此不能把 Bar 简化成“只用新配置”。
- `【直接确认】` PPT helper 新字段由 `IdtPlug-in.cpp` 等读取，但 `IdtPlug-in.cpp` 同时仍使用传统 PPT/交互设置。
- `【直接确认】` 两套系统写入不同 JSON 文件；全仓未见通用的双向同步或完整迁移器。
- `【直接确认】` `Config.AutoClean` 会影响传统 `WriteSetting`，使其清空旧 `setlistVal`；这是一个明确交点，不代表两个 schema 普遍同步。

`【历史/兼容】` `SetListStruct` 仍是当前大量主路径的真实接口，不能仅因其文件名较旧而标记废弃。

`【待确认】` 维护者需要决定：未来字段按什么标准选择系统、是否计划迁移 `deploy.json`、发布包是否预置这些文件，以及向后兼容周期多长。Codex 在答案明确前不应自行把字段迁到“看起来较新”的 module。

## 配置修改的证据型审查清单

以下是从并存状态推导的 `【合理推断】`，不是声称项目已有统一迁移政策：

1. 搜索字段、JSON key、默认值和 UI 写回的全部读写点，先确定真实所有者。
2. 已在 `INKEYS_CONFIG_SCHEMA` 中的字段继续通过该 module 读写，除非任务明确更改 schema/持久化格式。
3. 仍由 `SetListStruct` 管理的字段保持当前文件兼容；迁移需同时设计旧值读取、默认值、写回和回滚。
4. JSON key 改名属于持久化格式变化；没有迁移代码时不能假设旧 key 会自动升级。
5. 记录设置窗口写的是哪套配置，避免 UI 显示值、运行时缓存与磁盘文件分叉。

## UI3 Bar 分区布局配置合同

### 1. Scope / Trigger

修改 `UI.Bar.FixedButtonsA1` / `ExtensionButtons` / `FixedButtonsA2`、布局序列 codec、UI3 Bar 按钮注册、默认尺寸/显隐或 `Load()` 规范化时适用。该合同不包含设置界面的拖拽排序/显隐/改尺寸编辑。

### 2. Signatures

- `ConfigSequence<T>::Snapshot() -> std::vector<T>`
- `ConfigSequence<T>::Replace(std::vector<T>) -> void`
- `ConfigSequenceAdapter<T>::ElementType / Snapshot(...) / Replace(...)`
- `BarButtomSetClass::RegisterButton(id, button, allowMultiple, zone, defaultUserVisible=true) -> bool`
- `BarButtomSetClass::Load() -> void`

### 3. Contracts

- JSON 路径：
  - `UI.Bar.FixedButtonsA1`：`{ "Id": string, "Size": "twoTwo"|"twoOne"|"oneTwo"|"oneOne" }[]`
  - `UI.Bar.ExtensionButtons`：`{ "Id": string, "Size": ..., "Visible": bool }[]`，`Visible` 缺省 `true`
  - `UI.Bar.FixedButtonsA2`：同 A1 元素形态
- 运行时渲染顺序恒为 `normalize(A1) + normalize(B) + normalize(A2)`。
- **按钮 ID 命名**：
  - 官方按钮必须以 `Inkeys.` 开头，当前形如 `Inkeys.Bar.Select`。
  - 扩展/插件/组件按钮**不得**使用 `Inkeys.` 前缀，且必须为点分 ID：至少两段，形如 `xxx.xxx` 或 `xxx.xxx.xxx`（不允许首尾 `.` 或空段）。
  - `RegisterButton` 按分区强制上述规则；B 区规范化时丢弃官方前缀 ID 与非法点分格式。
- A1 默认 required：Select, Draw, Eraser, Geometry, Recall, Clean（**不含 Divider**）。
- A2 默认 required：Pierce, Freeze, Setting。
- **交界分割线**：运行时注入 `Inkeys.Bar.Divider` 且**不写入**三区配置。B 有可见扩展按钮时在 `A1|B` 与 `B|A2` 各插一条；B 无可上栏扩展项时只在 A1/A2 之间插一条。
- **相邻分割线规则**：配置侧相邻 Divider 只保留一条；运行时通过“先判断 B 是否有可见项再注入”避免相邻交界线。不得对 `only` 单例按钮重复 `buttomlist.Set` 重建列表（会 double-free）。
- A1/A2 **严校验**：配置 Id 多重集合必须恰好等于该区 required 默认集合；缺项、多余/错区 ID、非法重复、字段类型错误 → **仅该区**重置为默认顺序。不做逐项补洞。配置中的 Divider 在 A 区先剥离再校验。
- A 区不持久化用户 Visible；A 元素若误带 `Visible` 则忽略并剥离写回。A 的默认 `userVisible` 仅来自注册写死值（Geometry 默认 false）。
- `Size` 本轮只镜像注册默认；缺省/非法/非默认均纠正为注册默认并写回，**不**因 Size 触发 A 区整区重置。后续设置 UI 可开放用户改 Size。
- B 区：顺序 + Visible；符合扩展 ID 规则的未知/已卸载插件 ID **永久保留**且不渲染；`Inkeys.*` 与非法 ID 格式误入 B 时剔除；已注册扩展单例只取第一条。
- 旧 `UI.Bar.ButtonLayout` 单数组：仅当三个新字段都缺失时拆分迁移；其中 Divider 丢弃不迁入；迁移后 A 仍走严校验，B 保留扩展项 Visible。
- 发版新增 A 区 required 官方按钮：旧配置缺新 ID → 该 A 区整区重置默认；不猜测新按钮插入点。
- `ConfigSequence<T>` 快照在共享锁下生成；JSON 先完整解析到临时集合，成功后才在独占锁下整体替换。
- `BarButtomClass::IsVisible()` 是唯一消费入口：`userVisible && !hide`。`PresetHoming` 等运行时仍可临时改 Freeze 尺寸或上下文 `hide`。

### 4. Validation & Error Matrix

| 输入/状态 | 行为 |
| --- | --- |
| A1/A2 字段缺失 | schema 默认该区 |
| A1/A2 不是数组 / 元素无合法 Id | 配置读失败则保留默认；Load 再严校验 |
| A1/A2 Id 集合不是 required 恰好排列 | 该区重置默认顺序并写回 |
| A 元素带 `Visible` | 忽略剥离，不整区重置 |
| A/B `Size` 缺省/非法/非注册默认 | 纠正为注册默认，不因 Size 重置 A |
| B `Visible` 缺失 | `true` |
| B `Visible` 不是 bool | 整个 ExtensionButtons 字段回退默认（空数组） |
| B 含 `Inkeys.*` 或非点分/空段 Id | 剔除 |
| B 未知插件 Id（合法扩展点分格式） | 保留不渲染 |
| 旧 `ButtonLayout` 且无新字段 | 拆到 A1/B/A2；Divider 不迁入，交界运行时注入 |
| 已有任一新字段 | 不再读旧 `ButtonLayout` |
| B 为空 | 运行时 A1 与 A2 之间仅一条交界分割线 |
| 相邻两条 Divider（运行时/配置） | 只保留一条 |

### 5. Good / Base / Bad Cases

- Good：仅打乱 A1 顺序后启动，顺序保留；B 中插件隐藏后重装仍在原位。
- Base：无新区字段时 A1/A2 默认序，Geometry 注册默认隐藏，B 为空。
- Bad：A1 缺少 Geometry 或混入 Pierce → A1 整区回默认，A2/B 不动。

### 6. Tests Required

- 验证 A 缺项/错区重置、Size 纠正、A 误带 Visible 剥离、B 未知 ID 保留、官方 ID 误入 B 剔除。
- 验证旧 `ButtonLayout` 迁移与 `configOnce = config` 后三区一致。
- 执行 `git diff --check` 和完整 Solution `Debug|ARM64` 构建；无自动化 UI 测试时记录未做运行验证。

### 7. Wrong vs Correct

- Wrong：继续用单数组表达固定区+插件区，或 A 缺项时猜测插入点补洞。
- Correct：三区分存；A 非法整区重置；运行时始终 A1+B+A2；布局/命中统一 `IsVisible()`。

## 国际化源与生成物

| 路径 | `【直接确认】`的角色 |
| --- | --- |
| `Inkeys/src/i18n/zh-CN.jsonc` | 基准语言 |
| `Inkeys/src/i18n/zh-TW.jsonc`、`en-US.jsonc` | 其他语言 |
| `Scripts/i18n.ps1` | `check`/`sync` 工具 |
| `Scripts/i18n.zh-CN.snapshot.jsonc` | 基准语言同步快照 |
| `Inkeys/IdtI18nKeys.g.h` | `sync` 生成的 key header |
| `Inkeys/IdtI18n.cpp/.h` | 运行时加载和查询 |

`【直接确认】` `Scripts/i18n.ps1 sync` 以 `zh-CN.jsonc` 为基准，更新其他语言结构、快照和 `IdtI18nKeys.g.h`；生成 header 带有 “Do not edit manually” 标记。

因此，文案变更应：先改基准 JSONC，再按任务授权运行 `sync`、处理翻译标记、运行 `check`，并审查所有生成差异。只审计时使用 `check`；`sync` 会写文件，不能在未授权的只读/文档任务中执行。不要手工编辑 `IdtI18nKeys.g.h`。

## 产品资源位置与加载路径

`【直接确认】` 可见资源目录/登记点：

- `Inkeys/src/ppt/`：PPT 控件图像；
- `Inkeys/src/quick/`：快捷操作资源；
- `Inkeys/src/setting/`：设置界面资源；
- `Inkeys/src/skin/`：皮肤资源；
- `Inkeys/src/ttf/`：字体；
- `Inkeys/src/UI/`：其他 UI 资源；
- `Inkeys/Inkeys.rc`、`Inkeys/resource.h`：Win32 resource；
- `Inkeys/Inkeys.vcxproj`：字体、图像、manifest、shader 等项目项。

`【直接确认】` 当前加载方式并不统一，包括 Win32 resource、磁盘/解包路径、GDI+/D2D bitmap、ImGui DX11 SRV 和 lunasvg/SVG。设置窗口还把预编译 VS/PS CSO 作为 `SHADERS` 资源嵌入 EXE。`【合理推断】` 新资源应先跟随目标窗口已有加载器、缓存和释放点；建立新生命周期前需说明现有路径为何不适用。

## 二进制与生成资源的范围

- `【直接确认】` `Inkeys/exe/` 当前可见 `DesktopDrawpadBlocker.exe`；其生成/更新来源为 `【待确认】`。
- `【直接确认】` `Inkeys/binarypackage/` 含 EasyX/HiEasyX 相关库/产物；正式生成与更新流程为 `【待确认】`。
- `【直接确认】` `PptCOM.dll`/`.tlb` 由 `PptCOM.csproj` 的构建后步骤生成/复制；仓库内预编译产物还被构建文档用作兼容路径。
- shader、字体和图像是否全部可重建、哪些随发布包解包，需结合 `vcxproj`、`.rc`、打包脚本逐项确认，不能由目录名外推。

涉及这些文件时应核对来源/许可、架构、配置条件、工程/资源登记和可重建性。`Build/`、`Inkeys/Cache/`、`VcpkgInstalled/`、`PptCOM/obj/` 是生成输出，不进入产品文档变更。
