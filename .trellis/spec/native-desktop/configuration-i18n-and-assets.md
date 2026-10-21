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

## 设置页废弃配置清理合同

### 1. Scope / Trigger

删除传统 Setting 条目或停止兼容旧 `deploy.json` / PPT 配置字段时适用；被工程标记为 `None` 的 Draw2 源文件不因此重新进入活动实现范围。

### 2. Signatures

- `ReadSetting()`：不再读取已废弃键。
- `CaptureSettingJson()`：不再输出已废弃键，并在返回 JSON 前显式 `removeMember`。
- `PptComReadSetting()` / `CapturePptComSettingJson()`：PPT 配置只读写仍有活动消费者的字段。

### 3. Contracts

- `Regular.AvoidFullScreen`、`PointAdsorption`、`SmoothWriting`、`HideTouchPointerBeta`、`Performance`、`Preset` 已退出活动配置合同；保存时必须清除，即使 `Config.AutoClean=false`。退休 `Preset` 的宽度/记忆字段可按 [pen-width.md](pen-width.md) 仅为保存兼容数据解码、保留迁移证据；当前发布启动读取/恢复已注释禁用，残留旧default/Startup/memory不能覆盖Medium，不得恢复旧设置页或重复按px解释新DIP。
- PPT `FixedHandWriting` 已退出活动配置合同；PPT 捕获函数构造新对象，省略该字段即完成下一次写回清理。
- `Save.Enable` 不是废弃键：它继续控制 Draw3 桌面画布在清空和退出时保存非空画布，设置文案必须说明该真实语义。
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

## UI3 脏区调试与帧率显示配置合同

### 1. Scope / Trigger

修改 UI3 脏区红框、下方帧率文字、设置页实验室卡片或其启动同步时适用；传统 `IdtFloating` 不在此合同内。

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

- `Enable` 只控制实际业务脏区红框；设置卡片名称为“脏区调试”。
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
- 内置组件注册 ID 使用非 `Inkeys.` 的稳定点分形式，例如 `Component.ShortcutButton.Appliance.Explorer`。注册项同时保存旧字段字符串、无参数开关读取器、设置页分类/名称、PNG 资源、栏内短文字和点击动作；注册顺序单独保存，不依赖 `unordered_map` 遍历顺序。
- 设置页修改旧组件开关时，先更新 `setlist` 并执行 `WriteSetting()`，再在 UI3 下调用 `SyncLegacyExtensionButtons()` 和 Bar 渲染唤醒入口。打开开关按固定列表位置加入 B，关闭开关立即移除，其余组件相对顺序不变；UI2 的首个有效组件规则保持不变。
- 运行时按钮列表整体替换为注册表持有的 `shared_ptr` 序列；两条交界 Divider 使用长期持有的独立对象。组件按钮各自使用本地状态，避免多个扩展按钮共享按压/选中状态。
- 按钮图标载荷可为 SVG 或 PNG。SVG 继续走主题着色和内容切换；PNG 复用 SVG 的布局/透明度动画状态并由 `BarUiPNGClass` 绘制。两者的 D2D 位图都属于当前 device generation；设备 epoch 切换或 `D2DERR_RECREATE_TARGET` 时，必须通过 `BarUIRendering::DiscardDeviceResources()` 重置 `svgMap`、`pngMap` 及注册按钮的 SVG/PNG 缓存，再由下一帧按原始 SVG 文本或 PNG 解码像素重新上传。
- **按钮 ID 命名**：
  - 官方按钮必须以 `Inkeys.` 开头，当前形如 `Inkeys.Bar.Select`。
  - 扩展/插件/组件按钮**不得**使用 `Inkeys.` 前缀，且必须为点分 ID：至少两段，形如 `xxx.xxx` 或 `xxx.xxx.xxx`（不允许首尾 `.` 或空段）。
  - `RegisterButton` 按分区强制上述规则；Extension 仅额外允许已注册官方实体 `Inkeys.Bar.Setting`、`Inkeys.Bar.ViewAdjust`，布局标识必须走 `RegisterLayoutMarker`；B 区规范化时丢弃未注册官方前缀 ID 与非法点分格式。
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
- B 区：顺序 + Visible；符合扩展 ID 规则的未知/已卸载插件 ID **永久保留**且不渲染；未注册的 `Inkeys.*` 与非法 ID 格式误入 B 时剔除；已注册官方 Setting、ViewAdjust、MoreBoundary 和扩展单例只取第一条。
- 旧 `UI.Bar.ButtonLayout` 单数组：仅当三个新字段都缺失时拆分迁移；其中 Divider 丢弃不迁入；迁移后 A 仍走严校验，B 保留扩展项 Visible。
- A1 仅将 `Select, Draw, Eraser, Geometry, Recall, Clean` 这一精确旧默认顺序迁移为当前默认；其他 required 集合的合法自定义排列保持原顺序。
- 发版新增 A 区 required 官方按钮：旧配置缺新 ID → 该 A 区整区重置默认；不猜测新按钮插入点。
- `ConfigSequence<T>` 快照在共享锁下生成；JSON 先完整解析到临时集合，成功后才在独占锁下整体替换。
- `BarButtonClass::IsVisible()` 是唯一可见性消费入口。主栏注册/配置的基础尺寸与用户可见性独立于运行时有效 `size/hide`；`Load()` 只更新基准和成员，不能把配置默认写回已显示的动态尺寸。所有主栏有效尺寸、显隐及业务高亮/禁用统一由 `StateUpdate` 的最终计算/应用入口派生，A2几何不受文字/内容styleKey短路。组件、历史、计时器与外部工具刷新先更改数据状态并走统一刷新，不直接覆盖有效属性。构造默认、独立分页/属性按钮、指针反馈和渲染SetTar不属于该主栏业务写入口。

### 视图调整官方条目的窄迁移

2026-10-21滚动批注新增 `Inkeys.Bar.ViewAdjust`，Bar preset 在旧枚举尾部追加，不能重编号。默认B为MoreBoundary、ViewAdjust、Setting；运行时仍沿旧组件开关投影，explicitMore在Setting前加入ViewAdjust，不附带恢复整套B自定义。

配置读取与运行时投影职责不同：`Other.Config.cpp` 的 `TryAddMissingViewAdjustButton(Json::Value&)` 在共用ReadImpl原旧布局拆分后，使用生产sequence codec验证现有B，只把缺失ViewAdjust插在首个Setting前，没有Setting则追加。已有ViewAdjust完全不变，保留位置/Size/Visible/原字段；A1/A2和其它B原始项/顺序/未知插件额外字段不被规范化。非法B保持原codec回退；缺整个B继续schema默认，不为迁移额外造新字段。

ReadAll与ReadMini均覆盖，ReadMini未选中的运行时字段仍不读。迁移直接用既有原子写盘器写回原JSON文档，不能依赖configOnce差异或Bar后续Write；写失败沿原旧布局best-effort，后续读可重试。此处只允许新官方条目补缺，与Bar运行时不得写回整个B的合同相容。

资源为 `src/UI/BarViewAdjust.svg` / `BARVIEWADJUST UI`，主题占位沿原SVG，label `UI.Bar.MainButtons.ViewAdjustLabel` 为视图调整/視圖調整/Adjust View。三语源经i18n sync生成header/snapshot，不能手改生成键。实际按钮注册白名单、explicitMore和最终Selected/Disable由Bar唯一最终投影处理。

生产配置专测 `Inkeys::Tests::RunBarViewAdjustConfigTests()` 位于Other.Config.ViewAdjustTest.cpp/.h，只写仓库Build/ScrollFollow私有路径并恢复globalPath，不启动HWND；harness关闭返回2。严格早退CLI/工程登记由产品Core完成，真实二进制测试须统一构建执行，JSON fixture解析不能冒充C++专测通过。

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
| B 含未注册的 `Inkeys.*` 或非点分/空段 Id | 剔除；已注册 Setting/ViewAdjust/MoreBoundary 保留 |
| B 未知插件 Id（合法扩展点分格式） | 保留不渲染 |
| 旧 `ButtonLayout` 且无新字段 | 拆到 A1/B/A2；Divider 不迁入，交界运行时注入 |
| 旧 A2 恰好为 Whiteboard/Freeze（任一顺序） | 保留相对顺序，在末尾追加 EndShow 并规范化默认尺寸 |
| 旧 `ButtonLayout` 或 A2 含 `Inkeys.Bar.Pierce` | 迁移时丢弃；A2 最终按 Whiteboard/Freeze/EndShow required 集合严校验，不将 Pierce 注册为持久化布局实体 |
| 已有任一新字段 | 不再读旧 `ButtonLayout` |
| 旧组件全关 | 运行时仍显示 More；浮层显式组含 ViewAdjust、Setting，无横向分割线 |
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
- Base：无新区字段时 A1/A2 使用当前默认序；Geometry 注册默认可见并在选择模式运行时隐藏；旧组件开关全关时主栏无普通 B 实体，但 More 仍提供 ViewAdjust 与 Setting。
- Bad：A1 缺少 Geometry 或混入 Pierce → A1 整区回默认，A2/B 不动。
- Bad（并行期）：设置页只写旧开关却继续从持久化 `ExtensionButtons` 构建 B，导致 UI 与开关状态分叉。

### 6. Tests Required

- 验证 A 缺项/错区重置、Size 纠正、A 误带 Visible 剥离、B 未知 ID 保留、官方 ID 误入 B 剔除。
- 验证旧 `ButtonLayout` 迁移与 `configOnce = config` 后三区一致。
- 静态确认 16 个内置组件均已注册且顺序与设置页一致，16 个 toggle 写入后均触发 UI3 同步。
- 验证 UI3 启动和 toggle 同步均不读取、替换或写回 `UI.Bar.ExtensionButtons`。
- 手工验证 SVG/PNG 图标在 device epoch 重建后重新显示、PNG 透明图标、全部组件同时布局、toggle 即时增减，以及 UI2 首个有效组件行为。
- 手工验证 0/1/2/3+ 个旧组件的主栏容量、MoreBoundary 两组顺序、分割线条件、上下展开物理行方向、与绘制属性一致的时长及 Back/Sine 动画、隐藏态 Selected 青色同步、More 固定小三角、右上角 X 悬停/按压/拖出，以及 `closeMoreAfterAction=false` 保持打开。
- 手工验证主栏两条 Divider 保持 `oneTwo` 两行布局占用但只绘制垂直居中的 `1x50` DIP 纯 Shape，以及 `0.30` 填充/第三光强度、5 DIP 间隙居中、半列封列、左右镜像；指针经过/按下不产生背景、缩放或点击。
- 执行 `git diff --check` 和完整 Solution `Debug|ARM64` 构建；Headless/静态断言确认旧 Whiteboard/Freeze 顺序迁移、A2 三态可见性、EndShow 单次业务投递、旧 Pierce 被迁移且新配置不接受；无自动化 UI 测试时记录未做运行验证。

### 7. Wrong vs Correct

- Wrong：继续用单数组表达固定区+插件区，或 A 缺项时猜测插入点补洞。
- Correct：三区分存；A 非法整区重置；运行时始终 A1+B+A2；布局/命中统一 `IsVisible()`。
- Wrong（UI2/UI3 并行期）：组件 toggle 改变时写入或重排持久化 `ExtensionButtons`。
- Correct（UI2/UI3 并行期）：旧开关仍是唯一持久化来源，UI3 只重建当前进程的 B 序列；移除 UI2 后再恢复持久化 B 排序。

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
- 设置恢复Preset导航页，仅启动后位置说明和现有combo。旧deploy.json的Preset与画笔记忆继续退休，不恢复读写或旧条目；启动动画开关仍常规。
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
