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

- `config.ReadAll()` 后先归一并写回内存，再调用 `Inkeys::UI::Bar::SetThemeMode()`；旧配置缺字段、类型不符或非法整数均得到深色。
- “常规 > 外观 > 主题”分区依次为“主栏主题”与“设置主题”两个下拉框，均只显示深色与浅色（同一选项顺序）。主栏主题选择后依次更新 `Inkeys::config`、发布 Bar 主题目标、调用 `QueueConfigWrite()` 异步保存；设置主题写 `setlist.settingDarkMode`，与标题栏切换按钮同一路径（`WriteSetting()` + `settingThemeSerial` 递增 + 请求 Settings 帧），两处始终显示同一值。
- `SetThemeMode()` 只交换 `requestedDarkStyle` 并唤醒 Bar；D2D 资源和连续材质动画仍由串行渲染线程处理。
- Windows `WM_THEMECHANGED` 不读取注册表或改写配置；`WM_SETTINGCHANGE` 继续承担显示器/系统设置刷新。
- 该字段不复用传统 `SetSkinMode`，也不改变设置窗口自身主题。

无窗口测试必须覆盖默认 `1`、有效 `1/2`、非法值回退、`darkStyle` 映射及 Config 快照编解码；完整验证仍使用 `Debug|ARM64` Solution 构建和 `InkeysHeadlessTests.exe --no-window`。

## UI3 常驻透明度配置合同

持久化路径为 `Experimental.Inkeys3.UI3.BackgroundOpacity` 与 `ContentOpacity`，均为整数百分比。背景默认 `0`（`ResidentBackgroundOpacityThemeDefault`，跟随主题：Dark 80%、Light 92%），其余值经 `NormalizeResidentBackgroundOpacity()` 收敛到 30~100；内容默认 `100`，经 `NormalizeResidentContentOpacity()` 收敛到 30~100。

- 背景为绝对值：用户值即展开主栏的填充透明度；主按钮（收起 0.6 基准）、底部吸附指示与分页按与主栏 0.80 基准的比例同步。内容透明度是图标/文字的倍率。两者只作用于常驻表面（主栏、主按钮、吸附指示、PPT/白板分页），弹出面板、属性栏、菜单保持原透明度。
- 启动在 `ReadAll()` 后归一并写回内存，再调用 `Inkeys::UI::Bar::SetResidentOpacity()`；该函数只交换 `BarStyleClass::requested*Opacity` 并唤醒 Bar 与 `PptPageMask()`，渲染线程在帧首应用并整幅重画，分页在 Advance 中比较已应用值。
- “常规 > 外观 > 透明度”分区两条滑块（30%~100%）带“恢复默认”按钮。拖动实时发布，松手、重置或离页时再 `QueueConfigWrite()`；背景重置写回 `0`，滑块显示当前主栏主题的默认值。动画速率（默认 1.0x）与主栏缩放（默认 100%）滑块同样带“恢复默认”，重置后沿原有释放保存路径。
- Startup Preview 在 `ReadMini()` 阶段冻结同一主题及两项透明度，背景/反光/柔光流彩/像素速度线作为背景组，进度轨道/填充单独消费内容透明度；角落保持深色并使用主按钮比例。最终整窗淡入淡出不替代这两项材质透明度。

无窗口测试必须覆盖两个字段的默认值与归一、主题默认 80%/92%、绝对值在任意材质权重下对展开主栏成立、收起主按钮比例与非法覆盖值回退。
PPT 闲置淡化不改写这两个正常偏好；三范围配置与 30%/60% 独立上限见 [PPT 闲置淡化合同](ppt-idle-fade.md)。

## Draw3 光标透明度配置合同（2026-11-05）

`Drawing.Cursor.PenOpacity`、`HighlightPenFillOpacity`、`DotOpacity`、`HighlightDotFillOpacity` 是 `main.json` 中的 NoUpload 整数百分比，合法范围均为 25..100，默认依次为 50/50/25/50。缺项、非整数、超宽和越界值恢复对应字段默认，不影响设备样式或旧 `MouseStyle/PenStyle` 回落。

四项经 Config→State→Bridge→Controller 的完整值快照发布；普通 Pen/Dot 控制整体 alpha，高亮变体只控制填充。设置页复用 `SliderResetAction` 和 FIFO 冻结保存，拖动即时同步，松手、重置或离页写盘。视觉与打包细节见 [Draw3 产品光标合同](draw3-cursors.md)。

## 启动加载效果选择合同（2026-11-04）

### 1. Scope / Trigger

启动动画效果的持久化、Mini 快照、设置选择与对应渲染交接；视觉/生命周期细节见 [startup-preview.md](startup-preview.md)。

### 2. Signatures

- `Experimental.Inkeys3.UI3.StartupPreview.Effect : IdtAtomic<int> = 0`。
- `StartupPreview.Effect.h`：`StartupPreviewEffect { Shimmer=0, Aurora=1, PixelAssembly=2 }`、`NormalizeStartupPreviewEffect(int)`。
- `StartOptions.effect/themeMode/backgroundOpacity/contentOpacity` 是本次预览的冻结输入。
- `GeneralDraft.startupEffect` / `GeneralEvents.startupEffectChanged` 贯通设置选择与保存。

### 3. Contracts

- `ReadMini`/`ReadAll` 仅接受真实 JSON 整数 0/1/2；未知值、非整数类型、超宽整数均回退 0，不允许先窄化再命中有效值。旧配置缺失效果仍为“掠光”。
- `IdtMain` 的 Mini 路径同时选择 Effect、ThemeMode、BackgroundOpacity、ContentOpacity；归一后传给 StartOptions，不等待完整配置加载才选择外观。
- “常规 > 外观 > 启动加载动画”卡片的下半区是内联“启动效果”下拉（`ToggleRow` + `InlineCombo`，visibilityId `##startup-effect-visibility`），选项为“掠光 / 柔光流彩 / 像素拼装”（柔光流彩原名彩虹晕染，key 仍为 `Aurora`）；开关关闭时内联区随条件动画收起、父行保持常驻并保留效果值，开启时展开。收起过程中沿共用输入禁用规则，不产生隐藏控件事件。2026-11-04 起不再是独立卡片，原说明键 `StartupEffect.E` 已删除。
- 效果选择通过 `QueueConfigWrite()` 保存且下次启动生效；不重新启动当前预览，不清除宽度缓存。新文案遵循现有二十语源/键生成合同。

### 4. Validation & Error Matrix

| 条件 | 行为 |
| --- | --- |
| 缺失/非法/超宽 Effect | 回到 Shimmer，保留无关配置 |
| Preview Enable=false | 保留选择；内联区收起且不启动预览 |
| 关闭后再开启 | 选择值不丢失，内联区随既有条件动画展开 |
| 当前会话改效果 | 保存下次偏好，本次冻结外观不变 |

### 5. Good / Base / Bad Cases

Good：Mini 已取得 PixelAssembly + Light + 两项透明度后启动正确预览。Base：旧 main.json 沿原掠光启动。Bad：效果与主题直到 ReadAll 后才切换；或关闭开关时把用户选择重置为默认。

### 6. Tests Required

真实 Config Mini/All、缺失/错类型/超宽整数、写回/重读及冻结快照；真实 ImGui 选择、开关折叠/恢复、两主题/缩放、禁动画输入与事件隔离。完整 ARM64 Solution 与 `--no-window` 验证按主项目规则执行。

### 7. Wrong vs Correct

```cpp
// Wrong：宽整数可被窄化为有效选项。
const int candidate = static_cast<int>(jsonValue.asInt64());
// Correct：先验证原始整数类型与可表示范围，再归一。
const int candidate = integerType && jsonValue.isInt() ? jsonValue.asInt() : -1;
value = static_cast<int>(NormalizeStartupPreviewEffect(candidate));
```

## 设置页废弃配置清理合同

### 1. Scope / Trigger

删除传统 Setting 条目或停止兼容旧 `deploy.json` / PPT 配置字段时适用；被工程标记为 `None` 的 Draw2 源文件不因此重新进入活动实现范围。

### 2. Signatures

- `ReadSetting()`：不再读取已废弃键。
- `CaptureSettingJson()`：不再输出已废弃键，并在返回 JSON 前显式 `removeMember`。
- `PptComReadSetting()` / `CapturePptComSettingJson()`：PPT 配置只读写仍有活动消费者的字段。

### 3. Contracts

- `SettingGlobalScale`、`Regular.AvoidFullScreen`、`PointAdsorption`、`SmoothWriting`、`HideTouchPointerBeta`、`Performance`、`Preset` 已退出活动配置合同；保存时必须清除，即使 `Config.AutoClean=false`。退休 `Preset` 的宽度/记忆字段可按 [pen-width.md](pen-width.md) 仅为保存兼容数据解码、保留迁移证据；当前发布启动读取/恢复已注释禁用，残留旧default/Startup/memory不能覆盖Medium，不得恢复旧设置页或重复按px解释新DIP。
- PPT `FixedHandWriting` 已退出活动配置合同；PPT 捕获函数构造新对象，省略该字段即完成下一次写回清理。
- `Save.Enable` 不是废弃键：它继续控制 Draw3 桌面画布在清空和退出时保存非空画布，设置文案必须说明该真实语义。
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

```cpp
// Wrong：AutoClean 关闭时旧键被原样带回磁盘。
if (configAutoClean)
	setlistVal.clear();

// Correct：全量清理由用户开关控制，明确退役的键始终清除。
if (configAutoClean)
	setlistVal.clear();
setlistVal.removeMember("Performance");
setlistVal.removeMember("Preset");
```

## UI2/旧设置资源退役边界

- 按活动工程、RC 和真实加载/绘制消费者确认归属，不能只因资源名含 UI2 或位于旧目录就删除。保留 UI3 共用的四个 PluginFlag、头像、反馈码、赞助图、SIGN4 定格标识及当前 SVG/字体/演示 atlas。
- 旧首页 Home1/Home2 整图不再由 Setting 初始化或列入 requiredTextureIndexes；保留 1/2 槽位编号，避免改动其他共享纹理索引和设备重建合同。旧 UI2 专用资源从 RC、项目项和历史加载调用同步退役；已不存在的资源项目项也一并清除。
- 用户要求移入回收站时使用原生 recycle-on-delete，不永久删除；操作前核对仓库内绝对路径/清单/哈希，操作后核对实际回收站副本。验证保留 RC 文件、项目筛选器及生成 EXE 的嵌入内容，不以编译通过代替资源完整性检查。

## UI3 脏区调试与帧率显示配置合同

### 1. Scope / Trigger

修改 UI3 脏区红框、下方帧率文字、调试软件页卡片或其启动同步时适用；传统 `IdtFloating` 不在此合同内。

### 2. Signatures

~~~cpp
namespace Inkeys::UI::Bar
{
	export void SetDebugOptions(bool enable, bool showFrameRate);
}
~~~

持久化路径为：

~~~text
Experimental.Inkeys3.UI3.Debug.Enable        : bool = false
Experimental.Inkeys3.UI3.Debug.ShowFrameRate : bool = true
~~~

### 3. Contracts

- `Enable` 只控制实际业务脏区红框；设置卡片名称为“脏区调试”，与附属帧率行一起位于调试软件页。配置路径仍保留在 Experimental.Inkeys3.UI3.Debug，不因 UI 移页而迁移。
- `ShowFrameRate` 只控制下方帧率文字和为采样维持的连续帧；其设置卡片只在 `Enable=true` 时显示，隐藏不得覆盖持久化值。
- 新字段默认 `true`，使旧配置开启调试后继续显示帧率。设置修改先更新 `Inkeys::config`，再调用 `SetDebugOptions` 即时同步运行时，最后 `Config::Write()`。
- 启动 `ReadAll()` 后必须把两个字段一起传给 Bar；运行时负责用旧/新覆盖层快照清除被关闭的文字或红框。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 旧配置缺少 `ShowFrameRate` | 使用 schema 默认 `true`；不需要手写迁移 |
| `Enable=false` | 隐藏帧率子卡片；不绘制红框或文字 |
| `Enable=true, ShowFrameRate=false` | 仅在真实业务 damage 时绘制红框；不得持续空帧 |
| 配置写入失败 | 本次运行时开关仍即时生效；下次启动按磁盘值恢复 |
| 关闭任一项 | 请求清理其上次成功呈现边界；失败时保留到重试成功 |

### 5. Good / Base / Bad Cases

- Good：开启脏区调试后出现帧率子项；关闭帧率文字后红框仍工作，静止 Bar 回到 idle。
- Base：两个字段保持默认值时，调试默认关闭；用户开启后同时看到红框和帧率。
- Bad：用 `Enable` 同时永久覆盖 `ShowFrameRate`，或隐藏子卡片时写回 `false`。

### 6. Tests Required

- Headless 验证一秒帧率锁存、无限制帧率排除 pacing 等待，以及覆盖层关闭清理。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`；手工验证四种切换组合、设置容器高度、重启持久化和静止 CPU。

### 7. Wrong vs Correct

~~~cpp
// Wrong：脏区调试关闭时顺带覆盖用户的帧率偏好。
config.Debug.Enable = false;
config.Debug.ShowFrameRate = false;

// Correct：隐藏只影响可见性，两个持久化字段独立同步。
config.Debug.Enable = dirtyDebugEnabled;
SetDebugOptions(config.Debug.Enable, config.Debug.ShowFrameRate);
~~~

## UI3 Bar 分区布局配置合同

### 1. Scope / Trigger

修改 `UI.Bar.FixedButtonsA1` / `ExtensionButtons` / `FixedButtonsA2`、布局序列 codec、UI3 Bar 按钮注册、默认尺寸/显隐或 `Load()` 规范化时适用。该合同不包含设置界面的拖拽排序/显隐/改尺寸编辑。

### 2. Signatures

- `ConfigSequence<T>::Snapshot() -> std::vector<T>`
- `ConfigSequence<T>::Replace(std::vector<T>) -> void`
- `ConfigSequenceAdapter<T>::ElementType / Snapshot(...) / Replace(...)`
- `BarButtonSetClass::RegisterButton(id, button, allowMultiple, zone, defaultUserVisible=true, legacyField={}, legacyEnabled={}, categoryName={}, settingsName={}, closeMoreAfterAction=true) -> bool`
- `BarButtonSetClass::RegisterLayoutMarker(id) -> bool`（只接受无实体的官方布局标识）
- `BarButtonSetClass::TryGetRegistration(id, outRegistration) -> bool`
- `BarButtonSetClass::GetExtensionRegistrations() -> std::vector<BarButtonRegistrationClass>`
- `BarButtonSetClass::GetMoreButtonSnapshot() -> BarMoreButtonSnapshotClass`
- `BarButtonSetClass::GetMoreButton() -> BarButtonClass*`
- `BarButtonSetClass::RegisterBuiltInComponents() -> void`
- `BarButtonSetClass::SyncLegacyExtensionButtons() -> void`
- `BarButtonSetClass::Load() -> void`

### 3. Contracts

- JSON 路径：
  - `UI.Bar.FixedButtonsA1`：`{ "Id": string, "Size": "twoTwo"|"twoOne"|"oneTwo"|"oneOne" }[]`
  - `UI.Bar.ExtensionButtons`：`{ "Id": string, "Size": ..., "Visible": bool }[]`，`Visible` 缺省 `true`
  - `UI.Bar.FixedButtonsA2`：同 A1 元素形态
- 运行时渲染顺序恒为 `normalize(A1) + normalize(B) + normalize(A2)`。
- **UI2/UI3 并行期覆盖规则**：启用 UI3 时，`Load()` 的运行时 B 不读取 `UI.Bar.ExtensionButtons`，而是按设置页固定顺序读取 16 个 `setlist.component.shortcutButton.*` 开关。该投影只存在于当前进程，不调用 `ExtensionButtons.Replace()`、新版 `Config::Write()` 或其他 B 区持久化入口；持久化 B 的排序与编辑能力留到移除 UI2 后恢复。
- 内置组件注册 ID 使用非 `Inkeys.` 的稳定点分形式，例如 `Component.ShortcutButton.Appliance.Explorer`。注册项同时保存旧字段字符串、无参数开关读取器、设置页分类/名称、`UI` 类型 SVG 资源名（`comp*`，见 [bar-icons.md](bar-icons.md)）、栏内短文字和点击动作；注册顺序单独保存，不依赖 `unordered_map` 遍历顺序。
- 设置页修改旧组件开关时，先更新 `setlist` 并执行 `WriteSetting()`，再在 UI3 下调用 `SyncLegacyExtensionButtons()` 和 Bar 渲染唤醒入口。打开开关按固定列表位置加入 B，关闭开关立即移除，其余组件相对顺序不变；UI2 的首个有效组件规则保持不变。
- 运行时按钮列表整体替换为注册表持有的 `shared_ptr` 序列；两条交界 Divider 使用长期持有的独立对象。组件按钮各自使用本地状态，避免多个扩展按钮共享按压/选中状态。
- 按钮图标载荷可为 SVG 或 PNG；内置组件自 2026-11 起全部为主题占位色 SVG，PNG 路径当前无内置使用者但保留。SVG 继续走主题着色和内容切换；PNG 复用 SVG 的布局/透明度动画状态并由 `BarUiPNGClass` 绘制。两者的 D2D 位图都属于当前 device generation；设备 epoch 切换或 `D2DERR_RECREATE_TARGET` 时，必须通过 `BarUIRendering::DiscardDeviceResources()` 重置 `svgMap`、`pngMap` 及注册按钮的 SVG/PNG 缓存，再由下一帧按原始 SVG 文本或 PNG 解码像素重新上传。
- **按钮 ID 命名**：
  - 官方按钮必须以 `Inkeys.` 开头，当前形如 `Inkeys.Bar.Select`。
  - 扩展/插件/组件按钮**不得**使用 `Inkeys.` 前缀，且必须为点分 ID：至少两段，形如 `xxx.xxx` 或 `xxx.xxx.xxx`（不允许首尾 `.` 或空段）。
  - `RegisterButton` 按分区强制上述规则；Extension 仅额外允许已注册官方实体 `Inkeys.Bar.Setting`，布局标识必须走 `RegisterLayoutMarker`；B 区规范化时丢弃未注册官方前缀 ID 与非法点分格式。
- A1 默认 required 与顺序：Select, Draw, Geometry, Eraser, Recall, Clean（**不含 Divider**）。
- A2 默认 required 与顺序：`Whiteboard/twoOne`、`Freeze/twoOne`、`EndShow/twoTwo`；Setting 属于 Extension 的显式 More 项。旧 A2 若恰好是 Whiteboard/Freeze 的任一相对顺序，则保留该顺序并在末尾追加 EndShow；其他缺项、多余项或错区 ID 仍按严校验整区回默认。旧持久化 `Pierce` ID 仍退休，新 A2 配置不接受该 ID；分离式的独立穿透只作为运行时伴随按钮。
- A2 的 Freeze/Pierce 按“选择与穿透”合同运行时投影：合并式只显示可用 Freeze `twoTwo`；分离式在同一列显示上 Pierce `twoOne`、下 Freeze `twoOne`。空页 Selection 隐藏 Pierce 并放大可用 Freeze；Freeze 不可用时隐藏并把可用 Pierce 放大。Whiteboard 不提供 Pierce；Whiteboard/EndShow 沿现有工作区投影，隐藏的白板功能入口不因此开放。以上 `hide/size` 不改写 A2 持久化順序。
- **交界分割线**：运行时注入 `Inkeys.Bar.Divider` 且**不写入**三区配置。当前虚拟投影始终包含 More 与 Setting，因此主栏恒按 `A1 | Divider | 最多两个 B 实体 | More | Divider | A2` 构建；旧组件全关时仍保留两条 Divider。Divider 保留 `oneTwo` 的两行布局占用，但只绘制 `1x50` DIP、圆角 `0.5` 的 Shape 细线并垂直居中；SurfaceFrame 填充透明度为 `0.30`，不得加载或绘制 SVG；PointLight 关闭主光并复用几何分隔线 `0.30` 的第三鼠标光强度。它不增加主栏横向宽度，而是居中复用上一组尾端已有的 `5` DIP 间隙；前组小按钮留下未填满列时必须先封列，再从新列排下一组，统一横坐标镜像继续保证左右布局对称。
- **交界分割线交互**：Divider 必须从主栏悬停动画推进、指针扫描及点击/按压命中入口显式排除。遗留 hover、pressed、pressScale 状态应恢复为 `None/None/1.0`；不得通过禁用 Shape 或把可见态 `frameLightPct` 清零来实现不可交互，否则会错误关闭第三鼠标光。
- **固定 More 入口**：运行时在 B 末尾、`B|A2` Divider 前注入一个硬编码 More 按钮。它不登记到 `ExtensionButtons`，不进入配置；主栏折叠时隐藏，浮层打开时使用普通按钮的 `Selected` 视觉状态。
- **MoreBoundary 标识**：`Inkeys.Bar.MoreBoundary` 是 Extension 中最多一个的无实体布局标识。它只定义旧组件前/后的分组边界：边界前最多两个旧组件进入主栏，其余进入 `forcedOverflow`；边界后进入 `explicitMore`。标识缺失时不自动补齐，规范化时固定输出注册默认尺寸与 `Visible=true`，不产生按钮实体。
- **运行时投影顺序**：UI2/UI3 并行期间，旧组件开关按首次注册顺序建立活动栈；关闭项移除，重新启用或新启用项追加到栈尾。主栏仅保留前两个，More 浮层显示顺序为 `explicitMore` 后接 `forcedOverflow`，Setting 默认在显式组远端。运行时继续忽略并且不规范化/写回持久化 `ExtensionButtons`。
- **More 浮层**：每个标准单元为 70 DIP，按 `twoTwo`/`twoOne`/`oneTwo`/`oneOne` 子网格近方形打包，最多五列；强制组靠近主栏，显式组在远端。仅两组均非空时绘制整行横向分割线，分割线跨过 X 侧栏并保持面板左右内边距一致。根面板从 More 按钮中心的 60×30 紧凑态展开，时长使用 `BarUiDefaultOperationDur`，几何使用 Back、透明度使用 Sine；子内容围绕完整面板中心等比缩放，按钮持续保存为主栏局部坐标并在隐藏时缩在 More 入口下方，补位到主栏时不得从远端飘入。面板先于主栏绘制，使收拢部分从主栏下层出现。关闭按钮位于按钮网格右侧窄栏的右上角，不额外增加顶部高度。主栏左右换边不改变逻辑顺序，上下展开仅翻转物理行方向。
- **More 交互**：点击外部先关闭并继续处理同一鼠标消息；面板正文消费点击；X 复用独立按钮悬停填充、按下缩小、拖出取消与抬起关闭；浮层完全隐藏时直接同步内部按钮的填充、边框、图标和文字颜色，Selected 青色必须在下次展开前落稳。浮层按钮复用普通 `clickFunc`，默认按 `closeMoreAfterAction=true` 在回调前关闭，设为 false 时保持打开。打开绘制属性、几何、颜色/粗细子面板、主栏折叠或互斥面板时关闭 More。
- **More 入口视觉**：三角图标比原尺寸略小并保持固定朝向，不随开关或上下换边旋转；浮层打开时 More 入口使用普通按钮的 `Selected` 状态，使背景、图标和文字切换为青色 Accent 高亮。浮层几何继续使用不截断的 Back 进度形成弹性展开，并与主栏锚点保留独立间隙。SVG 设备缓存与注册按钮一起在 device epoch 重建时清理。
- **相邻分割线规则**：配置侧相邻 Divider 只保留一条；运行时通过“先判断 B 是否有可见项再注入”避免相邻交界线。不得对 `only` 单例按钮重复 `buttonList.Set` 重建列表（会 double-free）。
- A1/A2 **严校验**：配置 Id 多重集合必须恰好等于该区 required 默认集合；缺项、多余/错区 ID、非法重复、字段类型错误 → **仅该区**重置为默认顺序。不做逐项补洞。配置中的 Divider 在 A 区先剥离再校验。
- A 区不持久化用户 Visible；A 元素若误带 `Visible` 则忽略并剥离写回。A 的默认 `userVisible` 仅来自注册写死值；Geometry 注册默认可见，但选择模式通过运行时 `hide` 隐藏。
- `Size` 本轮只镜像注册默认；缺省/非法/非默认均纠正为注册默认并写回，**不**因 Size 触发 A 区整区重置。后续设置 UI 可开放用户改 Size。
- B 区：顺序 + Visible；符合扩展 ID 规则的未知/已卸载插件 ID **永久保留**且不渲染；未注册的 `Inkeys.*` 与非法 ID 格式误入 B 时剔除；已注册官方 Setting、MoreBoundary 和扩展单例只取第一条。
- 旧 `UI.Bar.ButtonLayout` 单数组：仅当三个新字段都缺失时拆分迁移；其中 Divider 丢弃不迁入；迁移后 A 仍走严校验，B 保留扩展项 Visible。
- A1 仅将 `Select, Draw, Eraser, Geometry, Recall, Clean` 这一精确旧默认顺序迁移为当前默认；其他 required 集合的合法自定义排列保持原顺序。
- 发版新增 A 区 required 官方按钮：旧配置缺新 ID → 该 A 区整区重置默认；不猜测新按钮插入点。
- `ConfigSequence<T>` 快照在共享锁下生成；JSON 先完整解析到临时集合，成功后才在独占锁下整体替换。
- `BarButtonClass::IsVisible()` 是唯一可见性消费入口。主栏注册/配置的基础尺寸与用户可见性独立于运行时有效 `size/hide`；`Load()` 只更新基准和成员，不能把配置默认写回已显示的动态尺寸。所有主栏有效尺寸、显隐及业务高亮/禁用统一由 `StateUpdate` 的最终计算/应用入口派生，A2几何不受文字/内容styleKey短路。组件、历史、计时器与外部工具刷新先更改数据状态并走统一刷新，不直接覆盖有效属性。构造默认、独立分页/属性按钮、指针反馈和渲染SetTar不属于该主栏业务写入口。

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
| B 含未注册的 `Inkeys.*` 或非点分/空段 Id | 剔除；已注册 Setting/MoreBoundary 保留 |
| B 未知插件 Id（合法扩展点分格式） | 保留不渲染 |
| 旧 `ButtonLayout` 且无新字段 | 拆到 A1/B/A2；Divider 不迁入，交界运行时注入 |
| 旧 A2 恰好为 Whiteboard/Freeze（任一顺序） | 保留相对顺序，在末尾追加 EndShow 并规范化默认尺寸 |
| 旧 `ButtonLayout` 或 A2 含 `Inkeys.Bar.Pierce` | 迁移时丢弃；A2 最终按 Whiteboard/Freeze/EndShow required 集合严校验，不将 Pierce 注册为持久化布局实体 |
| 已有任一新字段 | 不再读旧 `ButtonLayout` |
| 旧组件全关 | 运行时仍显示 More；浮层仅含远端 Setting，无横向分割线 |
| 相邻两条 Divider（运行时/配置） | 只保留一条 |
| UI3 并行期且 `ExtensionButtons` 含已有数据 | 运行时忽略该数据且不改写；B 只反映旧组件开关 |
| 任一旧组件开关 `false -> true` | 完成旧配置写入后，按注册顺序立即加入当前 UI3 B |
| 任一旧组件开关 `true -> false` | 完成旧配置写入后，立即从当前 UI3 B 移除 |
| 多个旧组件开关同时为 true | 全部显示，不受 UI2 单组件容量限制，顺序与设置页一致 |
| D2D device epoch 切换或 `D2DERR_RECREATE_TARGET` | 释放所有面板和注册按钮 SVG/PNG 位图缓存；保留原始载荷供下一帧重建 |
| `ExtensionButtons` 含 `MoreBoundary` 多次/带实体 | 只保留首个规范标识；不产生可点击按钮 |
| 旧组件开关 `true -> false -> true` | 关闭时从主栏/More 移除，重新启用后追加到活动栈尾 |
| More 面板打开时点击 X/外部/普通 More 按钮 | 分别关闭面板、关闭并继续原点击、切换独立展开目标 |
| 主栏 Divider 前存在未填满的小按钮列 | 先结束该列，Divider 居中放入既有 5 DIP 间隙，下一组从新列开始且总宽度不因 Divider 增加 |
| 指针经过或按下主栏 Divider | 不进入 hover/pressed/click 状态；纯 Shape 与独立第三光保持可见 |

### 5. Good / Base / Bad Cases

- Good：仅打乱 A1 顺序后启动，顺序保留；B 中插件隐藏后重装仍在原位。
- Good（并行期）：`ExtensionButtons` 保留未来排序数据，同时 UI3 按 16 个旧开关的固定顺序即时投影多个组件。
- Base：无新区字段时 A1/A2 使用当前默认序；Geometry 注册默认可见并在选择模式运行时隐藏；旧组件开关全关时主栏无普通 B 实体，但 More 仍提供 Setting。
- Bad：A1 缺少 Geometry 或混入 Pierce → A1 整区回默认，A2/B 不动。
- Bad（并行期）：设置页只写旧开关却继续从持久化 `ExtensionButtons` 构建 B，导致 UI 与开关状态分叉。

### 6. Tests Required

- 验证 A 缺项/错区重置、Size 纠正、A 误带 Visible 剥离、B 未知 ID 保留、官方 ID 误入 B 剔除。
- 验证旧 `ButtonLayout` 迁移与 `configOnce = config` 后三区一致。
- 静态确认 16 个内置组件均已注册且顺序与设置页一致，16 个 toggle 写入后均触发 UI3 同步。
- 验证 UI3 启动和 toggle 同步均不读取、替换或写回 `UI.Bar.ExtensionButtons`。
- 手工验证 SVG/PNG 图标在 device epoch 重建后重新显示、组件 SVG 深浅主题着色、全部组件同时布局、toggle 即时增减，以及 UI2 首个有效组件行为。
- 手工验证 0/1/2/3+ 个旧组件的主栏容量、MoreBoundary 两组顺序、分割线条件、上下展开物理行方向、与绘制属性一致的时长及 Back/Sine 动画、隐藏态 Selected 青色同步、More 固定小三角、右上角 X 悬停/按压/拖出，以及 `closeMoreAfterAction=false` 保持打开。
- 手工验证主栏两条 Divider 保持 `oneTwo` 两行布局占用但只绘制垂直居中的 `1x50` DIP 纯 Shape，以及 `0.30` 填充/第三光强度、5 DIP 间隙居中、半列封列、左右镜像；指针经过/按下不产生背景、缩放或点击。
- 执行 `git diff --check` 和完整 Solution `Debug|ARM64` 构建；Headless/静态断言确认旧 Whiteboard/Freeze 顺序迁移、A2 三态可见性、EndShow 单次业务投递、旧 Pierce 被迁移且新配置不接受；无自动化 UI 测试时记录未做运行验证。

### 7. Wrong vs Correct

- Wrong：继续用单数组表达固定区+插件区，或 A 缺项时猜测插入点补洞。
- Correct：三区分存；A 非法整区重置；运行时始终 A1+B+A2；布局/命中统一 `IsVisible()`。
- Wrong（UI2/UI3 并行期）：组件 toggle 改变时写入或重排持久化 `ExtensionButtons`。
- Correct（UI2/UI3 并行期）：旧开关仍是唯一持久化来源，UI3 只重建当前进程的 B 序列；移除 UI2 后再恢复持久化 B 排序。

## Setting 主题偏好

`【直接确认】` `SetListStruct::settingDarkMode` 与 SettingGlobalScale 位于同一旧配置路径，映射 `opt/deploy.json` 的 `SettingDarkMode`。默认 false；ReadSetting 对缺项和非 bool 回退 false，CaptureSettingJson 写 bool。设置标题栏切换先更新运行时，再按原 FIFO 冻结快照保存，不改变 Windows/Bar 主题；配置失败沿用原错误处理。完整主题/窗口线程与测试合同见 [rendering-and-ui.md](rendering-and-ui.md) 的“Setting 实时浅深主题合同”。

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

`【直接确认】` 本地化连接符允许有意空串：仅当基准节点本身为 `""` 时，目标语言显式 `""` 通过 `sync/check`（例如中文 HeroJoin，英文对应空格）。缺失或非字符串节点、非空基准的空翻译、明确待翻译标记及基准变更仍沿用原校验；不得把所有空翻译一律视为完成。

实验选项的控制台诊断由旧 `SettingsUI/Experimental/ConsoleOutput/TouchArea` 文案键和 `Experimental.Inkeys3.ConsoleOutput.TouchArea` 保存键继续承载；扩大输出内容时只更新全部支持语言的可见名称/说明，不改键以保留已有用户选择。

### 二十语言身份、字体与验证合同（2026-11-02）

#### 1. Scope / Trigger

新增地区语言、改变语言选择/资源/字体、修改带格式参数的译文或处理固定文字槽时适用；当前活动UI包括主栏/更多/组件、设置及DDB详情、白板/分页、MessageBox。原始诊断数据、初始化前故障兜底和已证实不可达旧UI不因资源扩展而恢复或改动。

#### 2. Signatures

- `Inkeys/IdtI18nLocales.h`：`Locales`、`IsValidIndex(int)`、`LocaleForIndex(int)`、`FindLocale(std::wstring_view)`、`IndexForSystemLanguage(LANGID)`、`NativeLabel(const Locale&)`；`DisplayOrder`、`DisplayIndexForLocale(int)`、`LocaleIndexForDisplay(int)`。
- `I18n::load(1, L"JSON", code)`、`I18n::languageId()`、`I18n::locale()`；元数据包含实际JSON resource、LANGID、Regular/Bold资源及字体族。
- `BarUIRendering::FitButtonLabelFontSize(content, fontSize, width, height, weight)`；`Word(..., bool fitButtonLabel = false)`默认不变，仅主标签开启；拟合比例同时作用于当前字面与fontCorrection的名义目标。
- `Setting::Design::TextFontConfig(rasterizerDensity, notoSource, TextFontRole role, notoPrimary)`：共享设置字体profile，区分源字族尺度、主字族基线和Legacy/Design角色。
- Debug harness入口 `Inkeys.exe --i18n-locales-test`，在配置/单实例/产品UI初始化前返回；另复用 `--bar-eraser-offscreen-test`、`InkeysHeadlessTests.exe --no-window`。

#### 3. Contracts

- 传统 `deploy.json/SelectLanguage` 保持 `0=en-US,1=zh-CN,2=zh-TW`；追加 `3=zh-HK,4=ja-JP,5=ko-KR,6=fr-FR,7=de-DE,8=es-ES,9=pt-BR,10=ru-RU`。后续追加 `11=id-ID,12=vi-VN,13=tr-TR,14=it-IT,15=pl-PL,16=uk-UA,17=es-MX,18=ro-RO,19=en-GB`。不能为下拉排序重排保存索引或迁移到main.json。
- 全部二十项菜单按语言码ordinal字典序显示，`DisplayOrder`由同一Locales表生成保存索引视图；Combo选中显示行须经`LocaleIndexForDisplay`再请求/保存，恢复时经`DisplayIndexForLocale`反查。非法保存值仍选英语，不能误用排序第一项de-DE；准备失败恢复原选中语言。
- 未保存选择时精确匹配新增地区，已有ReadSetting偏好优先；新加坡/澳门中文及未覆盖地区保持旧回退。非法值仅本次回退英语；未知嵌入代码加载失败，不能拿中文资源却记录为另一个语言。
- 列表显示母语名称、语言码及Beta，只有zh-CN不含Beta；通用品牌/新快捷方式zh-CN使用智绘教Inkeys，其余Inkeys，主页既定地区化标题/标语/版权例外保留。社区沿zh-CN→QQ、其余→GitHub分流。
- 新语言须同时进入JSONC、RC/resource、Locales及必要工程/Solution展示项；消息框默认按钮另按PRIMARYLANGID分派，须同步补齐且与JSONC术语一致，地区英/西复用既有按钮规则；脚本自动枚举其他JSONC，无需另加语言白名单。所有叶子完整翻译，不能把品牌/母语菜单/原生软件专名等有意同值误判为漏译。
- Noto Sans JP/KR采用固定官方来源的静态TrueType Regular/Bold，保留OFL与来源哈希；DWrite资源集合、ImGui普通/粗体/Design脸及MessageBox独立GDI+路径分别验证。主语言字形优先，SC/TC/JP/KR同字重合并仅补缺，保证任何当前语言下的母语列表可读；Noto不套Harmony光学校正。
- 语言选择改为安全帧边界热切换，具体发布/刷新/消息框快照见下节；组件注册保存的字符串副本必须随语言revision原位刷新。DDB等跨帧表存稳定key，每帧取文案，不能保存临时`IA(...).c_str()`。
- 脚本token正则只验证字面/顺序，不能证明std::format语法和参数类型。真实模板测试须使用生产参数类型（含IdtAtomic<int>下载线路、字符串笔宽与浮点指标），绕开生产异常兜底；兼容swprintf消费者另查百分号和固定缓冲边界。
- 设置Noto Design以1.448×TextOpticalScale（当前约1.40456）、Legacy以1.448/1.213（约1.193735）校准stb的hhea归一化；Harmony Design和Legacy原配置保持。merged字形共用primary的baked.Ascent，因此ExtraSizeScale按source字族，GlyphOffset按primary字族及role，不可分别给fallback套自身基线；图标、名义字号、行框和FontScaleDpi不随此补偿改变。
- 固定主栏/组件网格不因翻译扩张；短标签可在不误导操作的前提下精炼，必要局部适配必须让生产绘制与测量共用实际字体/几何。SelectClear/ClickThrough仅各自允许0或1个换行，其他资源继续按基准严格检查。

#### 4. Validation & Error Matrix

| 输入/条件 | 必须结果 |
| --- | --- |
| 旧0..10保存值，新11..19或未保存精确系统地区 | 保持旧值含义，新值/首次匹配选择正确，保存偏好不被系统覆盖 |
| 排序菜单选择、取消或失败 | 显示行和保存索引双向一致；导航未提交不写配置，默认英语不能变成de-DE |
| 非法索引/未知嵌入代码 | 前者内存回退英语；后者load失败且不伪造语言身份 |
| 资源ID错配 | 根Language、实际查询Language与请求code不符时测试失败 |
| 内嵌/合并字体缺字或真实槽溢出 | DWrite glyph-run/ImGui glyph及严格边界测试报具体locale/key/尺寸，不以RC条目存在或全局缩字冒作通过 |
| std::format说明符/类型不合法、兼容printf意外转换 | 真实模板专项失败，不允许靠fallback字符串隐藏异常 |
| 目标语言处于空串/待译标记 | 除基准自身为空的连接符外，check失败；发布资源不能靠中文fallback代替完整翻译 |

#### 5. Good / Base / Bad Cases

- Good：新增语言先登记共享元数据、内嵌资源及实际字体，补齐全部当前key后运行三类无窗口验证。
- Base：旧配置仍0/1/2，改变显示名称或新增语言不会改写原选择。
- Bad：只增加下拉或JSONC，让启动上限仍为2；只跑token比较；在测量里使用生产绘制没有的字体/字号；把Pierce伪造成Select标签转换验证不存在的生产路径。

#### 6. Tests Required

二十语JSONC完整性/同序/token/换行；排序后真实Combo按键选择与保存身份（含首行de-DE、en-GB/en-US及末行）；真实EXE资源Language与LANGID/索引/首次地区及Beta；所有活动格式模板的实际类型；DWrite实际glyph-run与37/70DIP槽、菜单/组件/提示；ImGui常规/粗体混合母语列表及窄/宽窗/不同倍率的真实组件、DDB、赞助长文；MessageBox默认按钮及私有字体。按用户要求完整Debug|ARM64 Solution构建后执行，无GUI/Computer Use授权时不声称真实桌面/触控/其他系统已验收。改已有文件时比较检出字节格式（本仓库core.autocrlf=true），不能把Git内部LF误当原工作区换行。

设置字体校准必须以真实字体共同汉字/H/数字的字面上下界和基线验证，并独立保留descender完整性、Noto主→Harmony缺字和Harmony主→Hangul混排基线；旧Noto profile用跨字号聚合负对照，防止仅“不溢出”放过整体过小。位图quad的小字号量化允许相对±10%或绝对≤1设备像素，不把该像素容差随DPI放大；同页真实ImGui离屏对照覆盖100/150/200%，不等同于GUI实机验收。

#### 7. Wrong vs Correct

```cpp
// Wrong：列表已有新语种，实际加载仍退回英语。
if (selection == 1) I18n::load(1, L"JSON", L"zh-CN");
else I18n::load(1, L"JSON", L"en-US");
// Correct：启动、选择、资源和字体共用稳定语言身份。
I18n::load(1, L"JSON", Inkeys::Localization::LocaleForIndex(selection).code);
```
### 全界面语言热切换与消息框快照（2026-11-02）

#### 1. Scope / Trigger

更改运行时语言、字体优先级、已缓存本地化文字、延后消费的提示请求时适用。全部程序自有表面更新；已打开独立消息框保持原语言，新请求使用新语言。原生字体渲染各自保留：设置FreeType、Bar/PageControl DirectWrite、MessageBox GDI+。

#### 2. Signatures

- `I18n::PreparedLanguage { language, values, baseRevision }`；`prepare(type,path,lang,output)`、`commit(PreparedLanguage&&) noexcept`、`revision() noexcept`。
- `I18n::TextSnapshot { LANGID language; vector<wstring> text; }`；`capture(initializer_list<const char*> keys, TextSnapshot&, bool nonblocking=false) noexcept`。
- `BarButtonSetClass::RefreshLocalizedText()`、`BarUIRendering::RefreshLocalizedTypography()`；Setting帧边界语言请求与字体替换。

#### 3. Contracts

- prepare构造候选map但不改变当前语言；commit在独占锁内检查baseRevision，再交换map/语言并递增revision。未知语言、缺失资源或过期候选不发布；保存SelectLanguage仍沿旧配置路径/索引，只有成功应用才写回。
- 发布者不在i18n锁内执行字体生成、D3D或跨窗口调用。生产切换由共享渲染线程安全帧边界完成；不在绘制Combo期间修改锁定atlas。成功后主动唤醒Bar/PageControl，不能仅更新revision而等待下一次用户输入。语言页不显示即时生效说明，简中说明使用“界面使用的显示语言。”；其余注意提示默认保持图标。
- Bar固定/状态型/组件标签原位更新，保留对象与工具状态；每个独立renderer同时更新字体族、locale、测量缓存。PageControl在业务revision不变时也消费语言revision，隐藏表面下次呈现使用新版本。记录所消费的版本，不能用“旧语言数据+随后读取的新revision”跳过刷新。
- capture在一次shared_lock内获取整组UTF-16文案和LANGID；缺少任一键/异常时保持output不变。nonblocking模式仅try_lock，崩溃路径不得改为阻塞读取；失败整组使用一致fallback。
- 排队提示在生产者侧冻结正文、标题、按钮与语言，worker只消费owned数据；正文若由同一渲染帧生成，要维持该帧内不能插入语言commit的合同。其他线程用同一capture取得所有字段。MessageBox保存request.language，DPI/主题恢复仍从该快照选字体。
- 系统已托管的历史通知、外部插件内容和用户数据不改写；不得为了语言刷新重启进程或重建整个业务UI。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| prepare失败/候选revision过期 | 不发布，不持久化新选择，旧语言可继续使用 |
| 字体候选初始创建/上传失败 | 恢复旧atlas及字体绑定，不提交语言 |
| 发布后动态新字形上传失败 | 不Present失败帧，沿既有Retry/DeviceLost恢复；不冒称全部未来字形均已预热或已回滚语言 |
| 静止可见Bar/分页窗口 | 提交后主动重绘，无需额外输入 |
| 旧提示入队后切换语言 | 旧请求全字段一致，新请求全字段采用新快照 |
| capture try_lock失败 | 不修改已有output，崩溃调用者使用整组英文fallback |

#### 5. Good / Base / Bad Cases

- Good：zh-CN→ja-JP时文字和字体优先级同步，Bar工具/白板内容不变；旧消息框仍为中文。
- Base：同字族语言复用字体，刷新文本和缓存即可。
- Bad：逐个IW再单独languageId拼消息框；只替换JSON却保留初始化标签或静止表面；先写配置再尝试加载。

#### 6. Tests Required

`--i18n-locales-test`包含候选/过期提交/并发成组快照；`--bar-eraser-offscreen-test`保留工具和按钮identity，并经PageControl::RunOffscreenTests验证四个表面的真实ConfigureSurface语言变化；`--ui3-multitarget-benchmark`验证真实Bar RenderFrame的语言刷新；`--setting-font-lifecycle-test`在无HWND WARP中经过生产字体替换和失败路径。Debug harness须在GUI初始化前返回；实际执行结果记录于任务验证文档，不以源码存在推断通过。

#### 7. Wrong vs Correct

```cpp
// Wrong：三次读取可能跨越语言发布。
auto title = IW(titleKey);
auto body = IW(bodyKey);
auto language = I18n::languageId();
// Correct：一次快照为整个请求选文案与字体身份。
I18n::TextSnapshot snapshot;
if (I18n::capture({titleKey, bodyKey}, snapshot)) {
    // 构造拥有snapshot.text和snapshot.language的消息框请求。
}
```

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
- `【直接确认】` 原分架构 EasyX 静态库已从 `Inkeys/binarypackage/` 删除；不得由打包流程重新复制。
- `【直接确认】` `PptCOM.dll`/`.tlb` 由 `PptCOM.csproj` 的构建后步骤生成/复制；仓库内预编译产物还被构建文档用作兼容路径。
- shader、字体和图像是否全部可重建、哪些随发布包解包，需结合 `vcxproj`、`.rc`、打包脚本逐项确认，不能由目录名外推。

涉及这些文件时应核对来源/许可、架构、配置条件、工程/资源登记和可重建性。`Build/`、`Inkeys/Cache/`、`VcpkgInstalled/`、`PptCOM/obj/` 是生成输出，不进入产品文档变更。

## H 实时信息来源与更新 Referer 合同

### 1. Scope / Trigger

修改 H 来源、输出或 Config 复制时适用。

### 2. Signatures

```cpp
H(writeFile, writeReferer, refererName, valueType, valueName, source)
ConfigDetail::ConfigReadOnlyValue<T>::Snapshot() const -> T
ConfigDetail::BindConfigSource(const T*, std::shared_mutex*)
```

### 3. Contracts

- H 接受固定值、const T* 或带锁地址。构造不解引用；地址/锁须覆盖使用期，空地址抛 invalid_argument。构造 explicit，普通 T 赋值不能替换绑定。
- ReadAll/ReadMini/Reset/ApplyDocument 不覆盖 H。Config 复制 X 状态和 H 描述符/固定值，不读写外部来源；configOnce 的 H 仍实时取值。
- 两开关独立。关闭文件输出须移除已知键，未知键策略不变。Snapshot 仅持共享锁复制；锁序 Config rwMutex -> 来源锁，调用 Config 前须释放来源锁，编码/I/O 不持来源锁。
- `$Info` 保存原六项及 UpdateChannel。Referer 为 id/ed/pa/we/ch，不含 ev；仅发送副本去 UserId 花括号，再 URL 编码，文件保留原值。
- ch 绑定 setlist.UpdateChannel/setlistUpdateMutex，main.json 保存副本，deploy.json 沿用原入口；两个更新 GET UA 均为 Inkeys-Updater/3.0。

### 4. Validation & Error Matrix

| 来源/输出 | 行为 |
| --- | --- |
| 固定/地址 | 固定值/输出时最新值 |
| 空地址 | 拒绝，不解引用 |
| file/referer | 四组合独立，禁用输出不取快照 |
| 读/重置/复制 | 不写穿来源 |

### 5. Good / Base / Bad Cases

Good：改通道后下次输出即更新。Base：固定 H、普通 X 各自状态。Bad：初始化缓存或回写来源。

### 6. Tests Required

真实 Config headless 覆盖来源、四组合、GUID/ev/ch、读/重置/复制与 X 回归；合成 ID、私有临时目录、并发同锁。完整 solution Debug|ARM64 与 --no-window，不请求真实服务器。

### 7. Wrong vs Correct

```cpp
// Wrong：启动缓存
H(true, true, "ch", std::string, UpdateChannel, ::setlist.UpdateChannel)
// Correct：输出时加锁复制
H(true, true, "ch", std::string, UpdateChannel,
    ConfigDetail::BindConfigSource(&::setlist.UpdateChannel, &::setlistUpdateMutex))
```

## 交互模式设置合同

- 设置标题为「选择交互模式／選擇互動模式／Choose an interaction mode」；用户可见名称为「大屏触控模式／大螢幕觸控模式／Large-Screen Touch Mode」与「桌面交互模式／桌面互動模式／Desktop Mode」。
- 持久化继续使用传统 `opt/deploy.json` 的整数 `PaintDevice`：0 大屏触控，1 桌面交互；不迁移键或数值。内存字段使用已有可复制的 `IdtAtomic<int>`，避免设置线程写入与延后同步worker读取竞争；Sync按单次load快照更新上下文。
- Setting 使用原有 `Widgets::combo` 下拉样式，每帧读取 `setlist.paintDevice`；变化后发起 `SyncDraw3State()`、设置 FIFO `WriteSetting()` 和绘制参数更新，重复选择不保存。绘制期间的 Sync 必须按设置渲染同步合同转交业务 worker，禁止持 ImGui 锁同步等 HWND。
- 两条场景说明位于模式选择块上方的独立信息卡片，使用蓝色信息图标、现有圆角边框/内边距，通过 `Device.TouchE/MousePenE` 国际化，并按实际换行高度布局；不得裁切长英文。
- Surface（含手指触摸）、平板、鼠标操控的大屏及普通投影属于桌面交互的文案场景。2026-10-13 用户明确要求保留现有全部自动启动判定，含“系统有触摸但主屏尺寸未知时默认大屏”；此自动识别与场景定义的差异是后续产品决策，不得借文案修改自动调整启动策略。
- 验证：无 HWND ImGui 回放覆盖下拉双向选择、三语/缩放卡片布局；另以生产调度边界的线程回归覆盖锁内提交与 FIFO 顺序。只把 Sync 替换为计数器的控件回放不能验证卡死修复。

## 启动位置预设配置合同
- UI.Bar.StartupPlacement 属于main.json，IdtAtomic<int>默认0；0居中底栏展开、1左角主按钮、2右角主按钮。读盘仅接受JSON int/uint且isInt()可表示的0..2；浮点、超宽整数及其他类型直接回退0，不能先窄化后Normalize。NormalizeStartupPlacement统一非法回退0，Mini/正式UI使用同一冻结值。
- 启动后位置说明和现有combo位于通用→基本的启动行为分类，不提供Preset导航页。旧deploy.json的Preset与画笔记忆继续退休，不恢复读写或旧条目；启动动画开关仍常规。
- 下拉仅更新偏好并QueueConfigWrite冻结副本，明确下次启动生效，不触发本次Bar移动/折叠或锁内窗口同步。
- 所有新标题/说明/三选项为zh-CN/zh-TW/en-US，通过i18n sync/check生成头与快照。
- 新增导航必须检测整个侧栏与固定页脚/分隔线边界，不以单个新页面回放替代整体布局验证。

## 选择与穿透操作模式合同

### 1. Scope / Trigger
修改合并式/分离式配置、Select/Pierce工具身份、Freeze互斥、主栏Clear禁用、说明卡片及三语短标签时适用。仅UI3产品；不恢复旧Draw2穿透共存模型。

### 2. Signatures
- `UI.Bar.SelectAndClickThroughMode: IdtAtomic<int> = 0`，main.json中仅0（Combined）/1（Separate），不上传。
- `GetSelectAndClickThroughMode() noexcept -> int`；`SetSelectAndClickThroughMode(int) -> bool`，返回配置是否变化，不直接写盘。
- `StateModeSelectEnum::IdtClickThrough`末尾追加，既有枚举值不变。
- `IsComputerInteractionMode(StateModeSelectEnum) noexcept -> bool`判定Selection或ClickThrough。
- `ChangeStateModeToClickThrough() -> bool`显式幂等进入；`ToggleClickThroughFromButton() -> bool`用户按钮入/出；`ChangeStateModeToSelectionFromButton() -> bool`；`ToggleFreezeFromButton() -> void`。

### 3. Contracts
- schema缺失或非法值回Combined；读取严格验证JSON整数0/1，不允许大整数窄化误变Separate。
- 设置每帧取实时模式；先改变配置/工具，沿既有SyncDraw3State的render defer guard同步，再通过Setting FIFO冻结配置payload。重复选择不写盘；不得持ImGui锁同步等待HWND。
- 设置切Combined→Separate时，当前有墨迹Selection映射ClickThrough；反向ClickThrough映射Selection，始终保留内容；其他工具不变。
- 决定Selection→ClickThrough映射所需的页面内容在取得stateModeTransitionMutex后按需读取，不能在等锁前预读并传入布尔值。模式锁不阻止Host独立发布新内容；采用提交决策时的最新快照，锁内不发布Draw3命令或调用窗口。
- Separate Select按钮先通过现有Draw3 Clear事务清当前页，Accepted后进入Selection；拒绝保持工具。空页直接选择。普通系统/可信PPT退出Selection入口保持原语义，不附加清空。
- ClickThrough独立高亮；用户在穿透时再次点击穿透按钮，退出并恢复lastAnnotationMode记忆的Pen/Shape/Eraser，保留墨迹，不恢复旧Freeze。显式进入穿透的程序入口继续幂等；进入时关闭Freeze。记忆最近Pen/Shape/Eraser顶层模式，具体子工具/笔型/属性留在既有字段。穿透时点Freeze恢复该模式并开启Freeze，墨迹不动，不恢复旧Freeze。
- backend selectionMode、呈现/输入/设置owner按电脑操作判定处理两工具，但主栏按实际枚举分别高亮。已接受/迟到回调沿既有修订保护。
- Pierce运行时固定留在Freeze前，shared_ptr稳定持有；不可用时仅hide，不从主栏运行时列表抽离/重插。隐藏按钮保持布局动画定位且不命中，避免显示时从右侧飞入。无需持久化布局注册，不改变A2用户顺序和旧Pierce迁移行为。
- 常规页标题下/自启区前显示说明卡片和combo，说明在上；复用PPT信息卡片样式，按实际字体/换行测量高度。三段使用单换行以收紧行距，中文工具名使用「选择」/「選擇」/「穿透」；分离式说明仅称选择/Select，不加清空括注。短按钮选择(清空)/Select (Clear)文案独立保持。
- Separate非Selection文案为“选择(清空)”/“選擇(清空)”/两行Select与(Clear)；Selection显示普通选择。英文穿透可两行Click-与through，仅该标签局部适配字号，不改全局网格。目标为两行时在透明中点换字前先缩到9 DIP；换回短标签时保持小字号到文字替换，再恢复标准字号。
- SelectClearLabel与ClickThroughLabel的排版换行由各语言资源提供，主栏不比较英文译文字面量。i18n检查仅对这两个最多两行的短标签允许各语言0或1个换行，超出仍拒绝；其余文案继续严格校验与基准相同的换行数。

### 4. Validation & Error Matrix
|条件|行为|
|---|---|
|缺失、非int、超范围或超宽整数模式|Combined|
|Combined|Pierce隐藏，可用Freeze 2×2|
|Separate且非空页Selection或其他工具|Pierce上2×1+Freeze下2×1|
|空页Selection|Pierce隐藏，可用Freeze 2×2|
|Freeze不可用/PPT|Freeze隐藏，可用Pierce 2×2|
|白板|不提供Pierce|
|空页Selection的主栏Clear|Disable统一灰色，不发命令|
|空页绘制或ClickThrough的主栏Clear|仍可进入Selection，不能提前禁用|
|首击Clear被拒绝|不切工具，后续继续清空|
|设置写盘失败|运行时已选模式生效，下次按磁盘恢复|

### 5. Good / Base / Bad Cases
- Good：Pen→Pierce→再次点击Pierce恢复Pen，再次点击可重入；Eraser/Shape同样恢复。Pierce→Freeze恢复记忆工具并开启Freeze，退出穿透按钮则保持Freeze关闭。
- Base：旧配置无模式字段，Combined行为保持；PPT隐藏Freeze。
- Bad：把Pierce重新写进A2、将空画布绘制态Clear禁用、在所有Selection系统入口附加Clear。

### 6. Tests Required
- 真实配置读写：缺失/非法/0/1、大整数、Mini/复制/默认值，不上传；三语sync/check。
- 早期无窗口生产状态测试：电脑操作bridge投影、模式双向映射、显式进入穿透幂等、用户按钮入/出/重入及Freeze互斥/三类工具与属性恢复、内容历史保持、Clear拒绝。
- Headless覆盖A2布局矩阵、清空二击与Disable边界；离屏覆盖Pierce稳定列表身份及反复隐藏/显示的布局位置和命中。空页Selection优先隐藏Undo/Redo，不受历史保护计时影响。DWrite实际测量三语短标签，两种尺寸不截断；说明卡三语换行测高与紧凑行距一致。
- 完整Debug|ARM64 solution和Headless --no-window。真实点击/触摸/PPT/设置视觉需独立人工证据，不用CLI替代。

### 7. Wrong vs Correct
```cpp
// Wrong：把“非Selection”直接当绘图，会令Pierce拦截底层输入。
state.selectionMode = mode == StateModeSelectEnum::IdtSelection;
// Correct：复用原穿透呈现链，同时保持独立UI工具身份。
state.selectionMode = IsComputerInteractionMode(mode);
```

## 主栏图标顺序与展开方向合同

### 1. Scope / Trigger
适用于UI3主栏横向图标顺序、展开侧策略、常规设置两个下拉及对应配置；不改按钮ID/持久化A1/A2序列或上下翻转。

### 2. Types / Configuration
- UI.Bar.IconOrder: IdtAtomic<int>=0、NoUpload；0跟随主按钮、1跟随反向、2固定LTR、3固定RTL。
- UI.Bar.ExpandDirectionAdjustment: IdtAtomic<int>=1、NoUpload；0中轴线、1仅当空间不足。缺失、非法类型/越界/超宽整数回各自默认，不能窄化误读。
- Setting仅真正变化后写原子字段、UpdateRendering(false)唤醒、QueueConfigWrite冻结保存；不修改工具/Draw3。

### 3. Contracts
- 主栏列表始终正序，y从上到下；横坐标镜像表follow=!opensRight、reverse=opensRight、LTR=false、RTL=true。同列2×1不颠倒，Pierce上Freeze下、Redo上Undo下，Divider仍原网格封列。排序不修改列表/A1/A2/按钮身份。
- 只改排序时不改父栏宽度、展开侧及主按钮位置；排序变化纳入布局批次/缓存/damage/命中和稳定几何版本，隐藏锚点同帧正确。内部finite签名保持原字段/sizeof72/协议版本，复用flags bit8..9编码IconOrder、bit10编码ExpandDirectionAdjustment，producer/frame捕获和合法mask一致；bit7仍未知非法，不将配置变化证明成旧几何。
- 空间不足模式保持当前侧，当前完整目标包络不fit且对侧完整fit才换；两侧不足不换、不平移/自动缩放。包络含主按钮半宽、10DIP间隔、完整可见网格宽及描边，使用active monitor与zoom，不能看动态ULW包络/Back/pulse实际宽度。
- 先计算完整目标宽度再判侧再设动画。首次沿startup规则，完整宽可用后评估，fold仍按下一次展开宽。输入无变化时不重复目标或重启动画；切换后fit则保持，收窄和越过中轴均不自动回翻。
- 中轴线模式保留x<=中心右展、else左展；整体居中和白板首次专用dock锁优先。非居中dock适用设置，primaryBar上下逻辑不改。
- 两个新卡紧随选择与穿透，自启前，三语最长选项测宽，标题不得重叠；不追加额外说明块。

### 4. Validation Matrix
| 场景 | 行为 |
| --- | --- |
| 当前侧足够，包括两侧都fit | 保持侧 |
| 当前侧不足且另一侧fit | 换一次后稳定 |
| 两侧不足 | 保持原侧，允许越界 |
| 居中/白板锁或无效显示参考 | 保持侧，不影响上下 |
| 两固定排序 + 两方向 | 屏幕LTR/RTL固定，列表与y不变 |
| 动画/pulse/回弹，目标几何无变 | 不因临时尺寸换边 |

### 5. Good / Bad
Good：正序算格、仅X镜像、纯空间策略用稳定目标。Bad：逆转buttonList导致上下颠倒；旧PositionUpdate每帧按中轴覆盖新策略；两侧不足来回切。

### 6. Tests Required
真实配置读写/严格类型/默认/复制/NoUpload；三语sync/check及实际控件回放；Headless四排序×左右/空间矩阵/边界/负屏坐标/DPI/连续输入；真实Bar离屏排序/隐藏hit/重复动画、底栏居中与白板锁回归；完整Debug|ARM64及Headless --no-window，GUI视觉独立人工记录。

### 7. Data flow
Settings原子配置→frame配置快照→完整稳定宽度→受锁约束的side策略→独立IconOrder X镜像→统一布局动画/继承/hit/damage。工具和文档状态不变。

## Draw3 绘图设备与兆芯兼容偏好（2026-10-15）

- INKEYS_CONFIG_SCHEMA 新增 Experimental.Inkeys3.Draw3.EnableHardwareAcceleration=true、EnableZhaoxinCompatibility=true，均NoUpload严格bool。缺键/非法类型沿用schema默认true；不迁移传统deploy.json。
- 实验设置卡片名称为“启用绘图硬件加速”“为兆芯设备启用兼容模式”。仅保存到main.json，说明重启生效；不运行中重建设备、不改变UI RenderPipeline后端或DComp开关。
- IdtMain 使用configOnce捕获到HostStartOptions，再按值转交TransparentPresentationOptions。初始设备、DComp失败替换与device-lost恢复必须复用同一启动快照，不读取设置页刚写的实时config。
- hardware=false直接WARP；true先Hardware，失败才WARP。compat=true仅实际Hardware+adapter VendorId0x1D17启用DEFAULT上传，false/其他GPU/WARP使用普通动态路径。
- GUI前 --draw3-device-policy-test 覆盖生产设备选择helper的调用顺序/回退/失败；--config-boundary-test覆盖默认/四组合真实读写/ReadMini/赋值/NoUpload/非法类型。两入口均不创建HWND。

## UI3 三态缩放基准与倍率新键（2026-10-20）

- `UI.Bar.Zoom2` 取代活动 `Zoom`，默认 `1.0`、NoUpload；旧 `Zoom` 不迁移、不回读。AutoClean=false 时旧键可以按未知数据保留，但初始化、启动 ReadMini、设置、自动 fit 和冻结配置均只使用新键。
- `UI.Bar.UseTaskbarHeightAsScaleBase` 为三态整数、NoUpload：`0`关闭、`1`任务栏基础量、`2`任务栏真实量；仅接受整数0..2，旧JSON bool按false→0、true→2兼容，不能把旧true窄化为1。缺省/非法类型/越界/重置按启动冻结的设备默认分类决定：大屏为1，其余为0。分类沿用 READY 触控且主屏物理面积>=1200cm²或物理信息不可用的现有大屏规则，不读取用户保存或运行中覆盖的 PaintDevice。Config schema 只读已冻结缓存，不在静态初始化查询硬件；构造、Mini、All、Reset 共用此默认。设置页使用三项下拉框，模式与UI倍率改变时同时唤醒Bar和四分页，复用设置写盘队列；传统settingGlobalScale与PPT各自倍率不变。
- 关闭模式使用真实DPI/96；任务栏基础量固定为`48/80 × DPI/96`，UI倍率100%时80DIP设计主栏的实际高度为48DIP，不读取实际任务栏厚度，Shell缺样时也保持此语义。任务栏真实量延续旧开启行为：本屏任务栏完整厚度/80，上下取高、左右取宽；无本屏任务栏时按主屏厚度×本屏DPI/主屏DPI折算，无可信样本时回退DPI/96。Display的强类型UiScaleBaseMode用于主栏、启动Mini/预览与分页的统一解析，禁止将模式再转bool；只对用户倍率应用既有50%–200%限制，不裁剪基准。
- 配置验证须包含旧bool兼容、三态整数往返、Mini/全量、冻结默认/复制/重置、非法类型/越界和NoUpload；缩放验证须覆盖多DPI、固定48DIP独立任务栏样本及真实量原有回退。Codex沙箱默认TEMP可能禁止MoveFileEx原子替换（ERROR_ACCESS_DENIED）；可为测试进程将TEMP/TMP指向仓库内scratch，不能修改产品写盘器绕过环境限制。

## 白板新进程默认主题记忆

- `Drawing.Whiteboard.DefaultTheme` 属于 `Inkeys/Config/main.json`，为 NoUpload；`BackgroundRgb` 为 `0..0xFFFFFF` 整数，`Pattern` 为 `0..2` 整数（空白/点阵/网格）。两字段作为同一个原子值发布及冻结，避免异步快照混配。
- 缺失、缺项、类型或范围非法时整组回退 `#123B32/空白`；不把布尔、浮点或宽整数强制窄化。ReadMini 只影响被选中的路径，Reset 恢复 schema 默认值。
- 仅有效当前白板页真正接受的主题修改更新偏好；初始化、冷恢复、重复/stale/错页/加载中请求不写回。新进程首张页读取偏好，同进程新页继续继承当前页。
- 配置经现有 Setting FIFO 异步写盘，跨生产者的完整配置捕获与入队必须在同一短临界区保持同序，锁内不执行文件 I/O；Draw3 不做磁盘 I/O；正常退出先停止 Draw3 再排空配置队列，Window Service 与 RenderPipeline 在 Setting 释放后停止。
- 回归使用真实配置 roundtrip、新实例重读及 Controller 同源初始化/主题命令门禁；GUI 重启观感需另行明确授权。

## 场景：白板快捷方式图标资源（2026-11-04）

### 1. Scope / Trigger

修改白板快捷方式图标或资源 ID 时适用；图标设计来自用户最终确认的蓝色点阵卡片与白色书写线。

### 2. Signatures

源 `Inkeys/src/UI/whiteboardLnk.svg`，产物 `Inkeys/src/whiteboard.ico`；`IDI_ICON2=332`。主图标 `IDI_ICON1=191` 保持最小图标组 ID。

### 3. Contracts

ICO 包含16/20/24/32/40/48/64/128/256的32位 PNG 帧，RC 注册第二图标，vcxproj/filters 同步登记 ICO 和 SVG。ShellLink 使用负资源 ID `-332`，不依赖图标序号。原 `icon.ico` 字节不修改，不引入产品构建依赖。

### 4. Validation & Error Matrix

缺帧、组 ID 不符、内嵌 PNG 与 ICO 帧不一致或负 ID 无法提取均视为资源验证失败；图标设计须先经过用户预览确认。

### 5. Good / Base / Bad Cases

Good：第二图标固定资源 ID、主图标原样；Base：应用链接仍取原图标；Bad：新增比191更小的图标 ID，改变 Shell 默认程序图标。

### 6. Tests Required

任务 `research/icon-resource-check.py` 对最终 EXE 无窗口读取图标组、逐帧比对 PNG，并按负 ID 提取各尺寸；记录完整 Solution 构建与运行证据，不启动产品 GUI。

### 7. Wrong vs Correct

Wrong：把第二图标资源排成最小 ID 或只验证 ICO 文件存在。
Correct：保留191为最小组，白板用332，并验证最终内嵌资源和负 ID 的实际提取。
