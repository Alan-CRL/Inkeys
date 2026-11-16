# Design — Legacy config migration and project cleanup

## 1. 字段处置清单

依据：只读审计（2026-11-16），以「是否被参与编译的代码读取」为准。未编译文件 = `IdtDrawpad*.cpp`、`IdtHistoricalDrawpad.cpp`、`IdtRts.cpp`、`IdtFloating.cpp`、`IdtWindow.cpp`。`IdtMain.cpp` 中的默认值赋值和 `Bar.Presentation.Test.cpp` 的测试初始化不算消费。

### 1.1 deploy.json → main.json（迁移）

| 旧键 (deploy.json) | 旧字段 | 新键 (main.json) | 消费者 |
|---|---|---|---|
| `UpdateSetting.EnableAutoUpdate` | `setlist.enableAutoUpdate` | `Application.Update.Enable` | Net.Update、Setting |
| `UpdateSetting.UpdateChannel` | `setlist.UpdateChannel` | `Application.Update.Channel` | Net.Update、Setting、`Info.ch` 上报绑定 |
| `UpdateSetting.UpdateArchitecture` | `setlist.updateArchitecture` | `Application.Update.Architecture` | Net.Update、Setting |
| `SelectLanguage` | `selectLanguage` | `Application.Language`（索引值原样保留） | IdtMain、Setting |
| `StartUp` | `startUp` | `Application.StartUp` | IdtMain、Setting |
| `Regular.TeachingSafetyMode` | `regularSetting.teachingSafetyMode` | `Application.TeachingSafetyMode` | IdtMain(CrashHandler)、Setting |
| `SettingDarkMode` | `settingDarkMode` | `UI.Setting.DarkMode` | Setting |
| `SettingEntryAnimation` | `settingEntryAnimation` | `UI.Setting.EntryAnimation` | Setting |
| `TopSleepTime` | `topSleepTime` | `UI.TopSleepTime` | Window.Legacy、Setting |
| `RightClickClose` | `RightClickClose` | `UI.Bar.RightClickClose` | Bar.Interaction、Setting |
| `PaintDevice` | `paintDevice` | `Drawing.PaintDevice` | Draw3、Bar、Setting、PenWidthPolicy |
| `Save.Enable` | `saveSetting.enable` | `Drawing.AutoSave.Enable` | IdtState → Draw3 AutoSave、Setting |
| `PlugIn.SuperTop.Enable` | `plugInSetting.superTop.enable` | `PlugIn.SuperTop.Enable` | IdtMain、Setting |
| `Component.ShortcutButton.*`（16 项） | `component.shortcutButton.*` | `UI.Bar.ShortcutButton.*`（同名子组） | Bar.Button(UI3 B 区投影)、Setting |
| `PlugIn.DesktopDrawpadBlocker.{Enable,RunAsAdmin,SleepTime}` | `ddbInteractionSetList.*` | `PlugIn.DesktopDrawpadBlocker.*` | IdtPlug-in、Setting |
| `PlugIn.DesktopDrawpadBlocker.Intercept.*`（17 项） | `ddbInteractionSetList.intercept.*` | `PlugIn.DesktopDrawpadBlocker.Intercept.*` | 同上 |
| `EraserSetting.EraserMode == 2` | `eraserSetting.savedFixedChoice` | **不迁移**：原实现中 `InitializeEraserInputPreferences()` 早于 `ReadSetting()` 执行，该提示在生产启动中从未生效；保持现状，固定传 -1 | IdtState |

新键路径已按上表落地。`Application.Update.Channel` 为 `ConfigUploadMode::Upload`、上报键 `ch`，取代原 `Info.ch` 只读绑定（上报串顺序不变，写盘时删除旧 `$Info.UpdateChannel`）；字符串字段只经 `Config::GetUpdateTarget/SetUpdateChannel/SetUpdateArchitecture` 加锁访问。`Application.Language=-1`、`Application.Update.Architecture=""` 表示未确定，启动迁移后按系统语言 / 程序架构补齐。各字段的默认值、clamp 和校验逻辑原样搬进 schema 读取钩子（例如 `SettingEntryAnimation` 越界回退 1，`TeachingSafetyMode` 只接受 0~3，语言索引走 `Localization::IsValidIndex`）。

DDB 的 `DdbEdition` / `DdbSHA256` / `mode` / `hostPath` / `restartHost` 原本就不来自 deploy.json，它们留在运行时结构里，供写出 `interaction_configuration.json` 使用。`ddbInteractionSetList` 改为从 `Inkeys::config` 取值后再组装 interaction JSON，输出格式不变。

### 1.2 pptcom_configuration.json → main.json（迁移）

| 旧键 | 新键 | 消费者 |
|---|---|---|
| 全部 13 个键（`MemoryWidgetPosition`、`ShowBottomBoth/ShowMiddleBoth/ShowBottomMiddle`、6 个位置、3 个缩放） | `PlugIn.PPTHelper.<原键名>`（扁平、同名；位置/缩放为 double） | Ppt.cpp、Setting |

位置写入日志 `PptSettings::WriteJournal`（版本、保存基线、失败重试）保持不变；`CapturePptComPositionSettingJson` 先把位置写进 `Inkeys::config`，`WritePptComSettingJson` 经 Prepare/Complete 后改为 `Inkeys::config.Write()`。`WriteAtomically` 仍被 memory.json 使用，保留。

### 1.3 删除（不迁移）

- deploy.json 的 `PenWidth` / `Preset` 笔宽退役数据：用户决定丢弃，不迁移；`GetMemory()` 的启动偏好保持默认。
- deploy.json：`SetSkinMode`、`BrushRecover`、`RubberRecover`、`Regular.MoveRecover1`、`Regular.ClickRecover`、`DisableRTS`、`LiftStraighten`、`WaitStraighten`、`EraserSetting.EraserSize`、`Save.SaveDays`、`PlugIn.SuperTop.Indicator`（已迁到 `StatusIndicator.SuperTop`）、`BasicInfo.*`（已由 `Info` 组上报）、早已移除的 `SettingGlobalScale` / `AvoidFullScreen` / `PointAdsorption` / `SmoothWriting` / `HideTouchPointerBeta` / `Performance` / `Preset`。
- 结构体中没有对应 JSON 键、也没有消费者的字段：`SkinMode`、`avoidFullScreen`、`pointAdsorption`、`smoothWriting`、`hideTouchPointer`、`performanceSetting`、`presetSetting`、`updateArchitecture` 以外的冗余副本等。
- pptcom：`AutoKillWpsProcess`、`fixedHandWriting`（只被未编译的 IdtDrawpad 读取）、`showLoadingScreen`（由 `kPptLoadingPageEnabled=false` 固定禁用；`IdtPlug-in.cpp:1202` 改为直接使用该常量）。
- `pptComSetlist.setAdmin` 是运行时探测结果（`IsPowerPointRunAsAdminSet()`），不进配置；改为 PPT 模块里的普通运行时状态。

## 2. 迁移流程

位置：`IdtConfiguration.cpp` 内新增 `MigrateLegacyConfigFiles()`（无参数，操作 `Inkeys::config`），在 `config.ReadAll()` 之后、首次 `config.Write()` 之前调用，替代原 `ReadSetting()` / `PptComReadSetting()` 调用点。主配置加载前的超级置顶判定改用 `ReadStartupSuperTopEnabled()`：旧 deploy.json 尚存时读旧值，否则只读 main.json。

1. 依次检查 `opt\deploy.json`、`opt\pptcom_configuration.json`。文件不存在就跳过。
2. 复用现有的有界读取和 JSON 解析代码（BOM、大小上限、`stackLimit`），这部分保持不变。解析失败按坏文件处理：不迁移，**删除**该文件，并记录日志。用户选择的是「直接删除」，坏文件不再保留。
3. 对 1.1、1.2 表中的每个键：只有当 `config.ReadAllHasValue(newPath)` 为 false（main.json 中没有该键），且旧值类型和范围合法时，才写入 `Inkeys::config` 对应字段。
4. 执行 `config.Write()`（同步写）。只有返回成功，才删除已迁移的旧文件；失败则保留旧文件，下次启动重试。
5. `opt\` 目录变空后，不额外删除该目录（最小修改）。

`ReadSettingMini` 等早期启动路径如果依赖 deploy.json 的字段（例如语言、主题），要改为读 `config.ReadMini` 的对应键，并把这些键加入 Mini 读取集合。实施时需逐一核对 `IdtMain.cpp` 中的调用顺序。

IdtMain 里与 deploy/ppt 读写相关的生产自检（`IdtMain.cpp:690-780` 一带的坏文件、超大、深嵌套用例）要改写为针对迁移函数的自检，不能直接删掉。

## 3. 设置 UI

- 删除无引用变量：`QuestNumbers`、`QueryWaitingTime`、`SetSkinMode`、`SaveSetting.SaveDays`、`PptUiWidgetScale(+Record)`、`BottomBoth/MiddleBoth Width/Height`、`receivedData`，以及 `AutoKillWpsProcess` 的注释残留。
- A 类（配置页 `AutoClean` / `Save.Enable`、组件页 16 个开关）：每帧把 config 读到局部 bool，`if (ToggleRow(...)) { 写 config; 同步; QueueConfigWrite(); }`。`AutoClean` 之所以要同时调用 `WriteSetting()`，是为了让 deploy.json 也按 AutoClean 清理；deploy.json 退役后这次调用一并去掉。
- B 类（插件页 SuperTop / DDB / PPT 显示与记忆、Performance、Experimental、Debug）：参照 Debug 页 `delayWaiting` 的写法，改为现读。
- 通用页 Draft 中来自影子的字段（StartUp、TopSleepTime、RightClickClose、TeachingSafetyMode、ThemeMode）改为现读。以下状态保留，或收敛成显式的 pending 状态，具有跨帧语义：`BarZoom`、`Background/ContentOpacity`、`AnimationSpeedRate` 及对应的 `*SavePending`；PPT 缩放的 `*Record` 和 `*Unifie`；`EnableFixWithChangeArchitecture`（纯会话状态）。
- 保存入口统一为 `QueueConfigWrite()`；`QueueWriteSetting` / `QueuePptComWriteSetting`，以及 `#define WriteSetting` / `#define PptComWriteSetting` 一并移除。
- 迁移后的旧字段一律改为读写 `Inkeys::config.*`。原本靠 `setlistUpdateMutex` 保护的 `UpdateChannel` 字符串，改由 Config 自身的锁 / 快照语义保证安全，并删除 `Info.ch` 的 `BindConfigSource`，直接引用新字段。

## 4. 工程文件

- 删除文件及其 vcxproj / filters 项，见 PRD §3。删除 `IdtDrawpadFacade.cpp` 前，要移除 `IdtDrawpad.h` 等头文件中对应的声明。如果某个头文件只剩这些声明，就整个删除，同时删掉 `IdtMain.h` 等处的 `#include`。所有删除都以 grep 无引用、并且构建通过为准。
- `IdtDraw.h` 登记成 `ClCompile` 属于写法错误，改为 `ClInclude`。
- Filters：
  - `头文件\Additional\<库>`：子 filter 名与磁盘目录一致（大小写也一致），例如 `hashlib++`、`imgui`、`imgui\misc\freetype`、`ink_stroke_modeler\internal\prediction`、`absl\...`。HiMsg（独立 sln）与 DirectX SDK 头文件目前不在项目中，**不新增**。
  - `Modules\Inkeys\<Category>`：补齐缺失的 `UI\SelectionOverlay`，以及 Bar 中未分类的文件。未登记的模块私有头文件（如 `Window.*.h`、`Bar.*.h`、`Draw3.*.h`）作为 `ClInclude` 登记到对应目录。
  - `..\inkStrokeModelerTest\draw3\*` 仍放在 `Modules\Inkeys\Drawing\Draw3`（现有规范）。
  - ink_stroke_modeler 的「过期条目」可能来自 `ink_stroke_modeler.filters.props`，要先展开 props 确认后再处理。
  - 删除空 filter（`资源文件\Draw3`）。
- `InkeysHeadlessTests.vcxproj(.filters)`：同步删除或改写对已删除文件的引用；如果新增了被直接编译的模块单元，也要同步登记。
- 不处理（超出范围，仅记录）：`shape_core_tests.cpp` 被编进主程序。

## 5. 兼容与回滚

- 迁移只向 main.json 写入「缺失的键」，不覆盖已有值；旧文件删除前必须确认 main.json 写盘成功。
- 回滚：旧版本程序找不到 deploy.json 时会按默认值重建，因此迁移后回退版本会丢失这些设置。该风险已由用户接受（选择了「直接删除」）。
- 整个任务分阶段完成（见 implement.md），每阶段都能独立编译。

## 后续决策变更（2026-11-16，PR #274 审阅后）

- 用户决定 **完全不兼容 Inkeys2 旧配置**：删除 `MigrateLegacyConfigFiles()` / `ReadStartupSuperTopEnabled()`，不读取、不迁移、不清理 `opt\deploy.json` 与 `opt\pptcom_configuration.json`，也不创建 `opt` 目录；Inkeys2 升级用户所有设置为缺省值。本文中关于“一次性迁移 / 迁移后删除旧文件”的描述因此作废，以 spec“不兼容旧配置合同”为准。
- 保存入口统一为 `Inkeys::config.Write()`（写出最新完整配置），设置页 FIFO 不再复制 Config 快照；PPT 位置保存与设置页共用该入口。
