# Draw3 产品光标合同

当前规则：2026-11-03 按设备（鼠标/触摸/屏幕笔/数位板）独立选择光标、小圆点、笔设备高亮模式与专页；2026-10-19 的“固定边缘”被高亮模式取代，首次三场景独立高亮属于历史，不能继续作为产品选项。

## 1. Scope / Trigger

修改设置页“绘制→光标”、Mouse/Pen 类型、按输入来源固定边缘、鼠标荧光笔预览或触摸系统光标显隐时适用。补充 [input-and-ink.md](input-and-ink.md) 的 Touch 归属/过滤/诊断合同，不改变橡皮响应、墨迹几何或历史层。

## 2. Signatures

- `Draw3.CursorSettings.h`：`MouseCursorStyle { System=0, Drawing(画笔图标)=1, Circle(小圆点)=2, HighlightDot=3 }`、`PenCursorStyle { Circle(小圆点)=0, PenIcon=1, HighlightDot=2 }`、`TouchCursorStyle { None=0, Circle=1, PenIcon=2, HighlightDot=3 }`、`PenHighlightMode { Off=0, DigitizerOnly=1, Always=2 }`、`CursorSettings{mouseStyle,screenPenStyle,tabletStyle,touchStyle,penHighlight,injectedMouseAsTablet}`、`Restore*`、`RestoreDevicePenCursorStyle(device,legacyPen)`、`PackCursorSettings/UnpackCursorSettings`（mouse2|screenPen2|tablet2|touch3|highlight2|injected1|valid1 位域）、`CursorDevice`、`IsCursorEdgeHighlighted(device,mode,penIcon)` 与 `kCursor*Dip` 共用常量（实际渲染与设置页预览共用，避免漂移）。界面上的“小圆点”就是随笔宽的圆形预览，没有另一种固定尺寸圆点；“高亮小圆点”仅描边固定高亮。
- `Bridge::ProductState::cursorSettings` 是纯值快照；`Bridge::ResolveMouseCursorStyleForTool(Tool,MouseCursorStyle)` 给出当前工具的有效鼠标策略，`Bridge::ResolveCursorSettingsForTool(Tool,CursorSettings)` 按值生成有效快照并保留独立 PenStyle；原始选择不得被该临时结果覆盖。
- `DrawingController::SetCursorSettings(const CursorSettings&) noexcept` 只由 Host owner 消费；`ResolvePrimaryDrawingCursorVisual(..., const CursorSettings* productSettings=nullptr, bool penIconAllowed=false, float dpiScale=1.0f)` 在显式接入后应用产品规则。
- `FilterMouseCursorMessage(const MouseCursorMessageFilterInput&) noexcept`的输入携带当前持久`pointerAuthority`（默认Unknown）；Window RTS光标分支从既有`cursorOwner_`读取，不新增计时/归属状态。
- `DrawingCursorShape::PenIcon`、`DrawingCursorAppearance::{outlineOpacity,centerOffsetX,centerOffsetY}` 保留既有shader/dirty合同。`WindowController::RequestDrawingCursorRender() noexcept` 只发合并请求和wake，不操作HWND/GPU。

## 3. Contracts

### 配置与线程

唯一持久化位置 `Inkeys/Config/main.json` 的 `Drawing.Cursor`，均为 `NoUpload`：

| 字段 | 合法值 | 默认 |
| --- | --- | --- |
| `MouseStyle` | `0` 系统光标、`1` 画笔图标、`2` 小圆点、`3` 高亮小圆点 | `0` |
| `PenStyle`（旧版单一“笔”，保留作回落） | `0` 小圆点、`1` 画笔图标 | `0` |
| `ScreenPenStyle` / `TabletStyle` | `-1` 沿用旧 `PenStyle`，`0` 小圆点、`1` 画笔图标、`2` 高亮小圆点 | `-1` |
| `TouchStyle` | `0` 无光标、`1` 小圆点、`2` 画笔图标、`3` 高亮小圆点 | `0` |
| `PenHighlightMode` | `0` 关闭、`1` 仅数位板、`2` 始终 | `1` |
| `InjectedMouseAsTablet`（bool） | 把驱动注入的鼠标当作数位板 | `false` |

各设备选择独立往返；校验先验 JSON 整数类型与宽度，非法值只恢复该键默认，不窄化、不影响其它键。`ScreenPenStyle/TabletStyle` 仅在键为 `-1` 时继承旧 `PenStyle`；用户在专页改动某设备才写该设备键。`MouseHighlightEdge`、`ScreenPenHighlightEdge`、`DigitizerHighlightEdge` 不读/不输出；下一次写盘显式清理三键，即使 AutoClean=false，其它未知键依旧保留。

设置页“绘制→光标”只有一个入口，进入 `DrawingSubpage::Cursor` 专页（`RenderCursorPage`）：四个设备行（鼠标 4 项：系统光标/画笔图标/小圆点/高亮小圆点；触摸 4 项：无光标/小圆点/画笔图标/高亮小圆点；屏幕笔、数位板各 3 项：小圆点/画笔图标/高亮小圆点；“无光标”只属于触摸）各带内联预览，另有“笔设备高亮模式”和“识别驱动注入的数位板”开关（后者在无 `GetCurrentInputMessageSource` 的系统上置灰）。下拉索引与枚举值一一对应。渲染作用域继续配置→deferred `SyncDraw3State()`→同一FIFO的冻结 `QueueConfigWrite()`，不得在 ImGui 锁内访问Controller/HWND。Product merge 必须完整复制 `cursorSettings`。

### 工具、来源与外观

| 入口 | 类型选择作用域 | 外描边规则 |
| --- | --- | --- |
| Mouse Pen/HardPen | MouseStyle（系统/画笔图标/小圆点/高亮小圆点） | 画笔图标始终高亮；小圆点普通边；高亮小圆点固定高亮 |
| Pen Pen/HardPen | 屏幕笔行或数位板行（Unknown/Conflict 与开关开启的注入鼠标归数位板行） | 小圆点/画笔图标按 `PenHighlightMode`：屏幕笔仅“始终”高亮，数位板“仅数位板/始终”高亮；高亮小圆点恒高亮 |
| Touch Pen/HardPen | TouchStyle，仅 contact 期间逐接触各一个 | 小圆点/画笔图标不高亮；高亮小圆点高亮 |
| Mouse/Pen/Touch Highlighter | 不受任何设备样式影响，保留矩形粗细预览；触摸荧光笔无光标 | 沿用旧规则：非屏幕笔高亮，屏幕笔不高亮，不受高亮模式影响 |
| Mouse Shape | 始终System，不受MouseStyle影响 | 不新增主光标 |
| Pen Shape | 保持原圆形，不应用设备样式 | 沿用旧规则：非屏幕笔高亮 |
| Eraser/Laser | 既有专用策略，不应用设备样式 | 专用alpha和生命周期保持 |

- Host只传原始设置；Controller按实际 EffectiveDrawingCursorTool 进行生产工具投影：Pen/HardPen返回保存的MouseStyle，Highlighter固定Drawing，其余System。Window系统箭头和resolver使用同一有效策略。活动contact锁存原工具时不能按新选择state.tool覆盖投影；抬起后重新投影，原始偏好始终保留。`penIconAllowed`（仅 Pen/HardPen）同时门控画笔图标与小圆点。
- Unknown/Conflict Pen 使用数位板行，视觉上按高亮模式（默认“仅数位板”即高亮），不改输入身份或橡皮模型。笔刚进入时 Pointer 路径发布的样本无来源，`WindowController::PublishPenCursorSample` 沿用最近一次已知来源（`lastKnownPenSource_`），避免错误设备行的瞬时闪现；换设备时由随后到达的 RTS 样本纠正。
- 数位板驱动（如 GAOMON M6 的 Windows Ink 虚拟 HID 设备）常被系统同时以 `POINTER_DEVICE_TYPE_INTEGRATED_PEN` 与 `THWC_Integrated` 报告为屏幕笔，但其 `POINTER_DEVICE_INFO.monitor` 为空；真实屏幕笔（Surface 内置笔）monitor 恒有效。`ClassifySource` 在指针设备已匹配、两路 Integrated 证据一致、已读到映射矩形且 monitor 为空时归为 `ExternalPen`；矩形读取失败、能力位与指针类型冲突或多重匹配仍按原规则（Unknown/Conflict）。依据为单机 Surface Laptop + GAOMON M6 的 2026-11-03 日志（见 11-03-tablet-recognition-investigation/research），其它数位板需设备验收。
- 小圆点即现有随笔宽的圆形预览（热点为圆心、沿用 Pen/HardPen 的 CursorAppearance），不新增 GPU type；高亮描边复用既有 1.25 DIP/0.96 灰白常量。触摸光标由 `MakeTouchPenDrawingCursorVisual` 按各 Touch runtime 的最新位置生成（缩放取该 runtime 的 visualStyle），Up/Cancel 随 runtime 消失，不改变系统触摸箭头始终隐藏与 Touch owner 合同。
- 非 Windows Ink 数位板以 `device=IMDT_MOUSE`、`origin=IMO_INJECTED` 的 WM_MOUSE* 到达（真鼠标为 `IMO_HARDWARE`）。仅当 `InjectedMouseAsTablet` 开启且来源 API 可用时，Window 把该样本标记 `SourceKind::ExternalPen`；resolver 据此走数位板行，`ShouldHideSystemDrawingCursor(…, mouseSampleIsTablet)` 隐藏系统箭头。默认关闭，因为远程桌面、AutoHotkey 等注入软件会被误判；Win7 无 API 时开关无效。
- PenIcon 仍24 DIP，左上尖端为输入热点、笔尾向右下；仅笔侧/笔尾四个非尖端角做内切圆角，目标半径3 DIP并等比缩放，切线长度各不超过相邻短边2/5以保留间隔；不能整体圆化而钝化笔头。偏移仅影响瞬态中心/quad/dirty，不移动 sample、不外扩外框，复用GPU type21。
- Highlighter矩形复用当前 `CanvasDiameterForTool` 和同一长宽比，填充有效alpha仍0.35；显式不透明高亮（outlineOpacity=1、GPU编码uv.x=2）时左右边缘为原宽度一半，上下保持原宽度，外框不动。ScreenPen非高亮与legacy默认编码0保持原公式，小尺寸inner coverage须非负且不超过outer；独立边缘alpha不改变工具墨迹。`outlineOpacity=-1` 保留旧整体opacity语义。
- 显式productSettings=nullptr时保留旧独立API行为；Pen Contact开关、倒转橡皮、Laser及逐Touch保持。瞬态视觉不进入L0/L1/L2、文档或Undo。
- 穿透/选择清应用主光标，由当前命中窗口拥有系统光标；保持WindowFromPoint保护，不用全局ShowCursor计数或强制箭头覆盖其它应用。

### Pen离开后的主光标归属

- Pen Up/OutOfRange清Pen样本后仍保留Pen归属；Pen owner配空样本不能回退到旧Mouse样本。Controller的活动runtime只选择实际工具，不得代替新设备输入切换owner。
- API成功且`IMDT_UNAVAILABLE + IMO_SYSTEM`的`WM_MOUSEMOVE`在Pen归属时拒绝，不发布Mouse样本或改变owner；不限延迟、坐标和按键位。System样式原本不自绘，不能由默认光标正常推断Drawing不会错误回弹。
- 真实Mouse/TouchPad消息、显式按键Down和新Pen输入继续即时接管；API缺失/失败、未知来源及应用注入保留既有回退，独立Win32Mouse producer不套用该RTS光标过滤。

### Touch 与 Win7

- 主 Drawpad 的系统触摸反馈始终禁用，无 UI 开关、无持久化布尔值，不迁移/恢复 `HideTouchPointerBeta`。`TABLET_DISABLE_TOUCHUIFORCEOFF` 加入现有 Vista+ 固定策略，首次输入前的窗口属性和 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 使用同一标志。
- Touch 最后一指 Up/Cancel 及重复 Pen 清除都重新请求 UI owner 刷新系统光标；Touch suppression 保持到新的真实 Pen/Mouse 接管，不因终态恢复旧 Hover。
- 保留 `WindowFromPoint` 归属保护和既有来源过滤；Win7 无输入来源 API 不能成为拒绝一切未知 Mouse 的理由。不得以固定延时/全局 hook 替代来源判定。
- 关闭诊断时不采集/格式化系统光标信息。开启时记录 `GetCursorInfo` 实际 flags/handle 与策略决策，采样有界；不能把 SetCursor 调用或 headless 通过当成 Win7 桌面视觉验收。
- 新光标 shader 的正式 HLSL 与 Win7 完整 guarded PS 必须同步。保留已有 OS/driver 选择；FXC 编译成功不证明实际 Win7 WARP 已渲染。
- 新增 PenIcon GPU type `21` 后，guarded DefaultInk 正向门必须使用 `type == 0 || type == 3 || type < 0 || type > 21`，排除新已知提前返回类型。仅凭前部 return 不能证明 Win7 WARP 的尾部导数/控制流不影响该光标；生成器门限须随类型边界同步，未知 `>21`/`<0` 的旧输出继续保留。


## 4. Validation & Error Matrix

| 条件 | 行为 |
| --- | --- |
| 缺失类型/非法整数/超宽整数/bool/float/string/null | 对应恢复0；不把非法输入窄化/强转成笔形 |
| 仅改Mouse或Pen类型 | 只改变对应入口，旧快照不回写；静止时请求光标重绘 |
| MouseStyle=System，工具Highlighter | 仍矩形预览且隐藏系统箭头；填充0.35 |
| MouseStyle=Drawing，工具任一Shape | 仍系统默认；不产生Circle/PenIcon主光标 |
| PenStyle=PenIcon，工具Highlighter/Shape | 保留原矩形/圆形，不误用笔形 |
| 高亮矩形 / ScreenPen非高亮矩形 | 前者仅左右减半、上下不变；后者原公式保持；inner≤outer |
| PenIcon圆角 | 四个非尖端内切，尖端和外框不动，极小/非等宽高无非有限值或切点越界 |
| 旧高亮键任意值 | 读取忽略；写盘删除三键，不影响其它未知键 |
| Controller首次收到与默认相同偏好 | 仍同步Window策略；不能因等值早退漏接入 |
| Unknown/Conflict | 固定高亮；不伪造ScreenPen/ExternalPen身份 |
| Touch终态/重复Pen离开 | 仍请求隐藏刷新，不恢复旧hover；来源过滤保持 |
| Pen归属、样本清空、迟到/重复系统Move | 两MouseStyle均无主视觉；不复活旧Mouse mailbox |
| 真实Mouse/TouchPad移动或按键Down | 即时接管，Drawing恢复PenIcon，System沿既有系统策略 |
| 来源API缺失/失败或其它未知来源 | 不冒充已确认系统注入；保留Win7及既有回退 |
| Win7尚未实测 | 待硬件验收，不宣布零闪现/双光标消失 |
| 触摸 TouchStyle=None | 不生成 visual；荧光笔/激光触摸行为不变 |
| 高亮模式变化 | 只改屏幕笔/数位板的小圆点与画笔图标；鼠标、高亮小圆点、荧光笔/图形不变 |
| 旧配置仅有 PenStyle | 屏幕笔与数位板两行沿用它，其余设备取各自默认 |
| 注入鼠标开关关闭/真鼠标（HARDWARE） | 按普通鼠标，不走数位板行 |

## 5. Good / Base / Bad Cases

- Good：Mouse默认系统但Highlighter仍预览；选择Mouse笔形不改变Shape；Pen选择笔形后按实际来源固定边缘，重启保留两份类型。
- Base：没有配置时Mouse软/硬笔系统、Pen软/硬笔圆形；ScreenPen不高亮，其余高亮；没有边缘UI选项。
- Bad：让MouseStyle关闭荧光笔预览；把PenStyle套到荧光笔矩形；仍从旧bool读取边缘；只删UI而保存旧开关；把Unknown改成别的设备。把四边统一减半或把笔尖一起圆化同样违反外观合同。

## 6. Tests Required

- 真实Config：各设备键默认（Mouse/Touch 0、ScreenPen/Tablet -1、Highlight 1、injected false）、全部合法值与非法值（bool/float/字符串/超宽/越界）按键恢复默认、旧 `PenStyle` 回落、往返/ReadMini/冻结副本、旧高亮忽略/保存清理、未知键保留与NoUpload。
- Bridge：只改任一设备样式或高亮模式仍发布新revision、旧快照不变；调用生产工具helper覆盖 MouseStyle×全部Tool，不复制映射；`Pack/UnpackCursorSettings` 对全部合法组合往返。
- Resolver：屏幕笔/数位板/Unknown/Conflict/注入鼠标 × 4 样式 × 3 高亮模式，小圆点/笔形热点与DPI、荧光笔与图形不受设备样式及高亮模式影响、Touch 工厂（None 无 visual、逐接触样式、NaN 拒绝）、Contact/Eraser/Laser回归；`ShouldHideSystemDrawingCursor` 的 `mouseSampleIsTablet`。
- 完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`、i18n sync/check、编码/BOM/EOL与diff --check；GUI需用户明确授权。既有无窗口 `Inkeys.exe --draw3-pen-width-test` 覆盖真实未附着 Window 的活动工具锁存、选择切换与释放后原始偏好恢复，不以纯 helper 断言替代该接线验证。
- Pen→Mouse回归须使用生产filter、mailbox和visual resolver，参数化System/Drawing，覆盖旧Mouse→Pen Hover/Contact→清样本→约1秒/长间隔/重复系统Move仍隐藏，再覆盖真实Mouse/TouchPad/Down、新Pen恢复与API失败/Win7回退；固定tick只是测试输入，不是产品计时器。
- 修改光标shader数学时必须同步正式PS与完整Win7 guarded数组，并核对签名、原/还原DefaultInk尾部与正向guard；Win7实际可见行为不能沿用旧二进制或逻辑通过结论。
- 最终PE资源须逐字节等于项目实际生成的当前CSO；显式/O3参考与项目默认编译选项不同时不能强制同SHA。须区分身份，另核实际产品PS的签名/分支，不能仅靠参考ASM推断工程资源行为。

## 7. Wrong vs Correct

```cpp
// Wrong：把鼠标类型直接当作所有工具的系统光标策略。
window.SetMouseUsesSystemCursor(settings.mouseStyle == MouseCursorStyle::System);
// Correct：先按实际工具解析，荧光笔独立预览、形状固定系统。
auto effective = settings;
effective.mouseStyle = Bridge::ResolveMouseCursorStyleForTool(actualTool, settings.mouseStyle);
// 外描边由高亮模式 + 实际设备行决定（IsCursorEdgeHighlighted），不从配置恢复三份bool；预览与渲染共用该函数。
```
