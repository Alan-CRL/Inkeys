# Configuration, I18n, and Assets

本文使用 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。尤其注意：“新配置 module”是代码年代/结构描述，不表示维护者已宣布它取代传统配置。

## 两套配置系统分别服务哪些代码

| 配置系统 | 文件与磁盘位置 | `【直接确认】`的使用范围 |
| --- | --- | --- |
| 传统 `SetListStruct` | `IdtConfiguration.h`；`IdtConfiguration.cpp::ReadSetting/WriteSetting`；`globalPath + L"opt\\deploy.json"` | `IdtMain`、`IdtDraw`/`IdtDrawpad`、`IdtFloating`、`IdtFreezeFrame`、`IdtHistoricalDrawpad`、`IdtMagnification`、`IdtPlug-in`、`IdtRts`、`IdtWindow`、`Net.Update`、`Bar.Main`、`Setting` 等仍有读写 |
| `Inkeys.Other.Config` | `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`；`Inkeys::Config` class、全局实例 `Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`globalPath + L"Inkeys\\Config\\main.json"` | schema 中可见 `Config.AutoClean`、`Info`、`UI.Bar.Zoom`、`Experimental.Inkeys3.UI3.Animation`、`PlugIn.PPTHelper` 等；消费者包括 `IdtConfiguration.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Net.Update*`、`Bar.Zoom*` 和 `Setting*` |

`【直接确认】` `INKEYS_CONFIG_SCHEMA` 集中声明新 module 已支持的字段；源码中还把启动读取区标注为“新配置 Test”。这说明新系统正在实际使用，但不足以证明迁移已完成或所有新字段必须进入它。

### 容易混淆的真实边界

- `【直接确认】` 是否启用 `Inkeys.UI.Bar` 的开关是传统 `setlist.Experimental.Inkeys3.UI3`，定义于 `IdtConfiguration.h`，由 `IdtMain.cpp` 读入 `useInkeys3UI`。
- `【直接确认】` 新配置中的 `Experimental.Inkeys3.UI3.Animation` 控制的是 UI3 动画相关项，不是选择新旧悬浮栏的总开关。
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

## UI3 Bar 顺序布局配置合同

### 1. Scope / Trigger

修改 `UI.Bar.ButtonLayout`、配置顺序序列 codec、UI3 Bar 按钮注册或有效可见性时适用。该合同不包含设置界面的拖拽排序/显隐编辑。

### 2. Signatures

- `ConfigSequence<T>::Snapshot() -> std::vector<T>`
- `ConfigSequence<T>::Replace(std::vector<T>) -> void`
- `ConfigSequenceAdapter<T>::ElementType / Snapshot(...) / Replace(...)`
- `BarButtomSetClass::RegisterButton(const std::string&, BarButtomClass*, bool allowMultiple) -> bool`

### 3. Contracts

- JSON 路径为 `UI.Bar.ButtonLayout`，值为顺序数组；元素格式固定为 `{ "Id": string, "Visible": bool }`，`Visible` 缺省为 `true`。
- `ConfigSequence<T>` 的快照在共享锁下生成；JSON 先完整解析到临时集合，成功后才在独占锁下整体替换。
- 官方 ID 定义在 `Inkeys::BarButtonId`，当前仅 `Inkeys.Bar.Divider` 允许重复；Redo 没有运行时对象，不进入默认数组。
- Bar 按数组顺序加载。已注册单例只取第一条，后续重复项从内存配置移除；未知 ID（包括重复项）原样保留但不渲染。
- `BarButtomClass::IsVisible()` 是唯一消费入口，结果为配置 `userVisible` 且运行时上下文 `hide` 为 false。

### 4. Validation & Error Matrix

| 输入/状态 | 行为 |
| --- | --- |
| 字段缺失 | 保留 schema 默认布局 |
| 字段不是数组 | 整个字段回退默认布局 |
| 元素不是对象，或 `Id` 缺失/空 | 整个字段回退默认布局 |
| `Visible` 缺失 | 读取为 `true` |
| `Visible` 不是 bool | 整个字段回退默认布局 |
| 已注册单例重复 | 第一条生效，后续项从内存序列移除 |
| 未注册 ID 重复 | 全部保留，不创建 UI |

### 5. Good / Base / Bad Cases

- Good：自定义顺序、隐藏项和多个 Divider 均按数组顺序生效。
- Base：旧配置没有字段时使用 Select、Draw、Eraser、Geometry(hidden)、Recall、Clean、Divider、Pierce、Freeze、Setting。
- Bad：数组任一元素 schema 错误时，不得保留此前已解析的部分结果。

### 6. Tests Required

- 验证缺字段、空数组、调整顺序、隐藏项、多个 Divider。
- 验证单例重复在内存中被清理，下一次 `Config::Write()` 后从 JSON 消失。
- 验证未知及重复未知 ID 均被保留，`configOnce = config` 后序列内容一致。
- 执行 `git diff --check` 和完整 Solution `Debug|ARM64` 构建；仓库没有对应自动化测试时，需明确记录未做 UI 运行验证。

### 7. Wrong vs Correct

- Wrong：逐个解析元素时直接修改运行时容器，或让布局/命中继续直接读取 `hide`。
- Correct：先完整解析并事务替换；所有布局、动画、悬停、命中和隐藏锚点统一调用 `IsVisible()`。

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
