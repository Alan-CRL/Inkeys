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
  - `RegisterButton` 按分区强制上述规则；Extension 仅额外允许已注册官方实体 `Inkeys.Bar.Setting`，布局标识必须走 `RegisterLayoutMarker`；B 区规范化时丢弃未注册官方前缀 ID 与非法点分格式。
- A1 默认 required 与顺序：Select, Draw, Geometry, Eraser, Recall, Clean（**不含 Divider**）。
- A2 默认 required 与顺序：`Whiteboard/twoOne`、`Freeze/twoOne`、`EndShow/twoTwo`；Setting 属于 Extension 的显式 More 项。旧 A2 若恰好是 Whiteboard/Freeze 的任一相对顺序，则保留该顺序并在末尾追加 EndShow；其他缺项、多余项或错区 ID 仍按严校验整区回默认。`Pierce` 已退出产品合同，新 A2 配置不接受该 ID。
- A2 运行时投影固定为：桌面显示 Whiteboard `twoOne` + Freeze `twoOne`；PPT 放映显示 Whiteboard `twoTwo` + EndShow `twoTwo`；全屏 Whiteboard 只显示“关闭白板” `twoTwo`。Freeze 在 PPT/Whiteboard 隐藏，EndShow 在非 PPT 或 Whiteboard 隐藏；这些是运行时 `hide/size`，不改写 A2 持久化顺序。
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
- `BarButtonClass::IsVisible()` 是唯一消费入口：`userVisible && !hide`。`PresetHoming` 等运行时仍可临时改 Freeze 尺寸或上下文 `hide`。

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
| 旧 `ButtonLayout` 或 A2 含 `Inkeys.Bar.Pierce` | 迁移时丢弃；A2 最终按 Whiteboard/Freeze/EndShow required 集合严校验，不注册 Pierce 实体 |
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
