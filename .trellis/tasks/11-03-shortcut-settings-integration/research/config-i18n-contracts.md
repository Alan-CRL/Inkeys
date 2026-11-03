# 配置与 i18n 规范摘录

为避免完整规范超过上下文注入大小，本任务只收录直接相关片段。来源：`.trellis/spec/native-desktop/configuration-i18n-and-assets.md`；实现时若需扩大范围，回读原规范。根 AGENTS 与用户本次约束优先于旧规范中的历史说明。

## 原规范 L1–L40

# Configuration, I18n, and Assets

本文使用 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。尤其注意：“新配置 module”是代码年代/结构描述，不表示维护者已宣布它取代传统配置。

## 两套配置系统分别服务哪些代码

| 配置系统 | 文件与磁盘位置 | `【直接确认】`的使用范围 |
| --- | --- | --- |
| 传统 `SetListStruct` | `IdtConfiguration.h`；`IdtConfiguration.cpp::ReadSetting/WriteSetting`；`globalPath + L"opt\\deploy.json"` | `IdtMain`、`IdtDraw`/`IdtDrawpad`、`IdtFloating`、`IdtFreezeFrame`、`IdtHistoricalDrawpad`、`IdtMagnification`、`IdtPlug-in`、`IdtRts`、`IdtWindow`、`Net.Update`、`Bar.Main`、`Setting` 等仍有读写 |
| `Inkeys.Other.Config` | `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`；`Inkeys::Config` class、全局实例 `Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`globalPath + L"Inkeys\\Config\\main.json"` | schema 中可见 `Config.AutoClean`、`Info`、`UI.Bar.Zoom2`、`UI.Bar.UseTaskbarHeightAsScaleBase`、`Experimental.Inkeys3.UI3.Animation/EdgeLighting`、`PlugIn.PPTHelper` 等；消费者包括 `IdtConfiguration.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Net.Update*`、`Bar.Zoom*`、`Bar.Main*` 和 `Setting*` |

`【直接确认】` `INKEYS_CONFIG_SCHEMA` 集中声明新 module 已支持的字段；源码中还把启动读取区标注为“新配置 Test”。这说明新系统正在实际使用，但不足以证明迁移已完成或所有新字段必须进入它。

### 容易混淆的真实边界

- `【直接确认】` `Inkeys.UI.Bar` 是唯一产品悬浮栏入口；传统 `setlist.Experimental.Inkeys3.UI3` 路由字段和 `useInkeys3UI` 已删除，写旧配置时清理遗留 key。
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

## UI3 Bar 深浅主题配置合同

持久化路径为 `Experimental.Inkeys3.UI3.ThemeMode`，类型为整数，默认值 `1`。`ThemeModeDark=1`、`ThemeModeLight=2`；`NormalizeThemeMode()` 仅保留 `2`，其余值一律归一到 `1`，`ThemeModeUsesDarkStyle()` 是配置值到 renderer `darkStyle` 的唯一映射。


## 原规范 L76–L103

- Setting UI 只使用当前窗口 `DpiScale(dpi)`，不叠加用户倍率；主栏 `UI.Bar.Zoom2`、任务栏基础量及 PPT 缩放不随此项改变。
- `SettingEntryAnimation` 沿用传统 `deploy.json`，整数 0=关闭、1=首次启动时、2=始终，缺项/类型错误/越界回退1；只控制设置开屏，与应用启动预览开关独立，也不关闭主页悬停/视差。
- 首开按全进程 `SessionState.ShowSerial()==1` 判定，在首次真实隐藏→显示即消耗；不依赖实际播放成功，尺寸不足、总动画关闭、取消和 resident 重建不能让后续 Show 补播。激活已显示窗口不递增序号；系统 DPI 改变仍走既有字体/纹理重建。
- 删除 UI 不等于删除当前结构体中的所有历史成员；只要成员仍被未编译兼容源码引用，可以保留为非持久化占位，但活动代码不得再读取或写入对应配置。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 旧 `deploy.json` 含任一废弃键且 `AutoClean=false` | 读取忽略；下一次保存从输出中删除该键，其他未知键仍按原策略保留 |
| 旧 PPT 配置含 `FixedHandWriting` | 读取忽略；下一次 PPT 配置写回不再输出 |
| 配置缺少 `Save.Enable` | 使用现有默认值；不得因清理旧键关闭 Draw3 自动保存 |
| 未编译 Draw2 文件仍引用兼容成员 | 不为清理活动配置而重写或重新编译该历史路径 |

### 5. Good / Base / Bad Cases

- Good：旧配置关闭 `AutoClean`，保存后只移除明确废弃键，`Save.Enable` 与其他设置保持原值。
- Base：新安装没有废弃键，捕获结果稳定且不会创建空的 `Performance` / `Preset` 对象。
- Bad：只删除 Setting 卡片但继续读取/写回旧键，或把 `Save.Enable` 与 Draw2 遗留项一起删除。

### 6. Tests Required

- 静态搜索确认活动 Setting、配置读取和 i18n 不再引用删除项，且没有用 `#if 0` 代替删除。
- 以包含全部旧键、`AutoClean=false` 的配置检查捕获结果：废弃键缺失、未知键保留、`Save.Enable` 保留。
- 运行 i18n `sync/check`、完整 `InkeysRepo.sln` `Debug|ARM64` 构建和 `InkeysHeadlessTests.exe --no-window`。

### 7. Wrong vs Correct


## 原规范 L303–L316

## 国际化源与生成物

| 路径 | `【直接确认】`的角色 |
| --- | --- |
| `Inkeys/src/i18n/zh-CN.jsonc` | 基准语言 |
| `Inkeys/src/i18n/*.jsonc`（除zh-CN） | 当前其他十九种语言，脚本自动枚举 |
| `Scripts/i18n.ps1` | `check`/`sync` 工具 |
| `Scripts/i18n.zh-CN.snapshot.jsonc` | 基准语言同步快照 |
| `Inkeys/IdtI18nKeys.g.h` | `sync` 生成的 key header |
| `Inkeys/IdtI18n.cpp/.h` | 运行时加载和查询 |

`【直接确认】` `Scripts/i18n.ps1 sync` 以 `zh-CN.jsonc` 为基准，更新其他语言结构、快照和 `IdtI18nKeys.g.h`；生成 header 带有 “Do not edit manually” 标记。

因此，文案变更应：先改基准 JSONC，再按任务授权运行 `sync`、处理翻译标记、运行 `check`，并审查所有生成差异。只审计时使用 `check`；`sync` 会写文件，不能在未授权的只读/文档任务中执行。不要手工编辑 `IdtI18nKeys.g.h`。

## 原规范 L397–L402

- 发布者不在i18n锁内执行字体生成、D3D或跨窗口调用。生产切换由共享渲染线程安全帧边界完成；不在绘制Combo期间修改锁定atlas。成功后主动唤醒Bar/PageControl，不能仅更新revision而等待下一次用户输入。语言页不显示即时生效说明，简中说明使用“界面使用的显示语言。”；其余注意提示默认保持图标。
- Bar固定/状态型/组件标签原位更新，保留对象与工具状态；每个独立renderer同时更新字体族、locale、测量缓存。PageControl在业务revision不变时也消费语言revision，隐藏表面下次呈现使用新版本。记录所消费的版本，不能用“旧语言数据+随后读取的新revision”跳过刷新。
- capture在一次shared_lock内获取整组UTF-16文案和LANGID；缺少任一键/异常时保持output不变。nonblocking模式仅try_lock，崩溃路径不得改为阻塞读取；失败整组使用一致fallback。
- 排队提示在生产者侧冻结正文、标题、按钮与语言，worker只消费owned数据；正文若由同一渲染帧生成，要维持该帧内不能插入语言commit的合同。其他线程用同一capture取得所有字段。MessageBox保存request.language，DPI/主题恢复仍从该快照选字体。
- 系统已托管的历史通知、外部插件内容和用户数据不改写；不得为了语言刷新重启进程或重建整个业务UI。


## 原规范 L438–L474

### 生成键标识符与 Win32 宏合同

#### 1. Scope / Trigger

新增或重命名 i18n JSONC 节点时适用。`i18n.ps1 sync` 会把每个路径段直接生成为 C++ 成员标识符，因此这不只是 JSON schema 变更，也是生成 C++ API 变更。

#### 2. Signatures

~~~text
"Dialogs/Common/OK" -> I18nKey.Dialogs.Common.OK
~~~

#### 3. Contracts

- 每个路径段必须能生成合法 C++ 标识符，并且不得与目标翻译单元可见的 Windows/项目宏同名。
- 特别禁止使用被 `<Windows.h>` 定义的 `MessageBox` 作为节点名；对话框文案使用 `Dialogs`。
- 不通过调整 `#undef` 顺序掩盖 schema 命名冲突，因为生成头可被多个全局模块片段以不同顺序包含。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 节点名与 Win32/项目宏冲突 | 在基准 JSONC 重命名节点，重新 `sync`，再更新调用点 |
| 非默认语言出现新 key | 填完翻译标记后 `check` 必须 100% 通过 |
| 生成头与 JSONC 不一致 | 重跑 `sync`，不手改生成头 |

#### 5. Good / Base / Bad Cases

- Good：`Dialogs/Common/OK` 生成 `I18nKey.Dialogs.Common.OK`，在包含 `<Windows.h>` 的单元中可直接编译。
- Base：普通非冲突节点按现有生成规则使用。
- Bad：`MessageBox/Common/OK` 会让成员 token 在预处理期被改写为 `MessageBoxW`。

#### 6. Tests Required

- 运行 `pwsh ./Scripts/i18n.ps1 sync` 和 `check`。
- 搜索生成头中的新成员名，并完整构建至少一个包含 `<Windows.h>` 且消费该 key 的翻译单元。

