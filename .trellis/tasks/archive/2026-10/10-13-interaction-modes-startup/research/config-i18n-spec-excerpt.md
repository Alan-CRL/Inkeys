# Configuration/i18n spec 摘录

来源：.trellis/spec/native-desktop/configuration-i18n-and-assets.md，完整规范仍为权威。避免自动注入截断。

# Configuration, I18n, and Assets

本文使用 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。尤其注意：“新配置 module”是代码年代/结构描述，不表示维护者已宣布它取代传统配置。

## 两套配置系统分别服务哪些代码

| 配置系统 | 文件与磁盘位置 | `【直接确认】`的使用范围 |
| --- | --- | --- |
| 传统 `SetListStruct` | `IdtConfiguration.h`；`IdtConfiguration.cpp::ReadSetting/WriteSetting`；`globalPath + L"opt\\deploy.json"` | `IdtMain`、`IdtDraw`/`IdtDrawpad`、`IdtFloating`、`IdtFreezeFrame`、`IdtHistoricalDrawpad`、`IdtMagnification`、`IdtPlug-in`、`IdtRts`、`IdtWindow`、`Net.Update`、`Bar.Main`、`Setting` 等仍有读写 |
| `Inkeys.Other.Config` | `Inkeys/Inkeys/Other/Other.Config.cppm/.cpp`；`Inkeys::Config` class、全局实例 `Inkeys::config/configOnce`；`Config::ReadAll/ReadMini/Write`；`globalPath + L"Inkeys\\Config\\main.json"` | schema 中可见 `Config.AutoClean`、`Info`、`UI.Bar.Zoom`、`Experimental.Inkeys3.UI3.Animation/EdgeLighting`、`PlugIn.PPTHelper` 等；消费者包括 `IdtConfiguration.cpp`、`IdtMain.cpp`、`IdtPlug-in.cpp`、`Net.Update*`、`Bar.Zoom*`、`Bar.Main*` 和 `Setting*` |

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

实验选项的控制台诊断由旧 `SettingsUI/Experimental/ConsoleOutput/TouchArea` 文案键和 `Experimental.Inkeys3.ConsoleOutput.TouchArea` 保存键继续承载；扩大输出内容时只更新三语言可见名称/说明，不改键以保留已有用户选择。

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

#### 7. Wrong vs Correct

~~~cpp
// Wrong：MessageBox 可被 Windows.h 宏展开。
I18nKey.MessageBox.Common.OK;

// Correct：使用无宏冲突的 schema 节点。
I18nKey.Dialogs.Common.OK;
~~~


## 交互模式设置合同

- 设置标题为「选择交互模式／選擇互動模式／Choose an interaction mode」；用户可见名称为「大屏触控模式／大螢幕觸控模式／Large-Screen Touch Mode」与「桌面交互模式／桌面互動模式／Desktop Mode」。
- 持久化继续使用传统 `opt/deploy.json` 的整数 `PaintDevice`：0 大屏触控，1 桌面交互；不迁移键或数值。内存字段使用已有可复制的 `IdtAtomic<int>`，避免设置线程写入与延后同步worker读取竞争；Sync按单次load快照更新上下文。
- Setting 使用原有 `Widgets::combo` 下拉样式，每帧读取 `setlist.paintDevice`；变化后发起 `SyncDraw3State()`、设置 FIFO `WriteSetting()` 和绘制参数更新，重复选择不保存。绘制期间的 Sync 必须按设置渲染同步合同转交业务 worker，禁止持 ImGui 锁同步等 HWND。
- 两条场景说明位于模式选择块上方的独立信息卡片，使用蓝色信息图标、现有圆角边框/内边距，通过 `Device.TouchE/MousePenE` 国际化，并按实际换行高度布局；不得裁切长英文。
- Surface（含手指触摸）、平板、鼠标操控的大屏及普通投影属于桌面交互的文案场景。2026-10-13 用户明确要求保留现有全部自动启动判定，含“系统有触摸但主屏尺寸未知时默认大屏”；此自动识别与场景定义的差异是后续产品决策，不得借文案修改自动调整启动策略。
- 验证：无 HWND ImGui 回放覆盖下拉双向选择、三语/缩放卡片布局；另以生产调度边界的线程回归覆盖锁内提交与 FIFO 顺序。只把 Sync 替换为计数器的控件回放不能验证卡死修复。

## 启动位置预设配置合同
- UI.Bar.StartupPlacement 属于main.json，IdtAtomic<int>默认0；0居中底栏展开、1左角主按钮、2右角主按钮。NormalizeStartupPlacement统一非法回退0，Mini/正式UI使用同一冻结值。
- 设置恢复Preset导航页，仅启动后位置说明和现有combo。旧deploy.json的Preset与画笔记忆继续退休，不恢复读写或旧条目；启动动画开关仍常规。
- 下拉仅更新偏好并QueueConfigWrite冻结副本，明确下次启动生效，不触发本次Bar移动/折叠或锁内窗口同步。
- 所有新标题/说明/三选项为zh-CN/zh-TW/en-US，通过i18n sync/check生成头与快照。
- 新增导航必须检测整个侧栏与固定页脚/分隔线边界，不以单个新页面回放替代整体布局验证。
