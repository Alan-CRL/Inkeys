# Draw3 产品光标合同

当前规则：2026-10-19 用户修订后的鼠标/笔两类型选择与固定设备边缘策略。首次三场景独立高亮属于历史，不能继续作为产品选项。

## 1. Scope / Trigger

修改设置页“绘制→光标”、Mouse/Pen 类型、按输入来源固定边缘、鼠标荧光笔预览或触摸系统光标显隐时适用。补充 [input-and-ink.md](input-and-ink.md) 的 Touch 归属/过滤/诊断合同，不改变橡皮响应、墨迹几何或历史层。

## 2. Signatures

- `Draw3.CursorSettings.h`：`MouseCursorStyle { System=0, Drawing=1 }`、`PenCursorStyle { Circle=0, PenIcon=1 }`、`CursorSettings`、两个 `Restore*CursorStyle(int)`。
- `Bridge::ProductState::cursorSettings` 是纯值快照；`Bridge::ResolveMouseCursorStyleForTool(Tool,MouseCursorStyle)` 给出当前工具的有效鼠标策略，`Bridge::ResolveCursorSettingsForTool(Tool,CursorSettings)` 按值生成有效快照并保留独立 PenStyle；原始选择不得被该临时结果覆盖。
- `DrawingController::SetCursorSettings(const CursorSettings&) noexcept` 只由 Host owner 消费；`ResolvePrimaryDrawingCursorVisual(..., const CursorSettings* productSettings=nullptr, bool penIconAllowed=false, float dpiScale=1.0f)` 在显式接入后应用产品规则。
- `DrawingCursorShape::PenIcon`、`DrawingCursorAppearance::{outlineOpacity,centerOffsetX,centerOffsetY}` 保留既有shader/dirty合同。`WindowController::RequestDrawingCursorRender() noexcept` 只发合并请求和wake，不操作HWND/GPU。

## 3. Contracts

### 配置与线程

唯一持久化位置 `Inkeys/Config/main.json` 的 `Drawing.Cursor`，只含两个 `NoUpload` 整数字段：

| 字段 | 合法值 | 默认 |
| --- | --- | --- |
| `MouseStyle` | `0` 默认光标（系统）、`1` 画笔光标 | `0` |
| `PenStyle` | `0` 默认光标（圆形）、`1` 画笔光标 | `0` |

Mouse/Pen 选择独立往返，不保存高亮。`MouseHighlightEdge`、`ScreenPenHighlightEdge`、`DigitizerHighlightEdge` 不读/不输出；下一次写盘显式清理三键，即使 AutoClean=false，其它未知键依旧保留。合法已存类型保留，不强制重置用户选择。

设置只含“鼠标”“笔”两个类型下拉，选项都叫“默认光标”“画笔光标”。渲染作用域继续配置→deferred `SyncDraw3State()`→同一FIFO的冻结 `QueueConfigWrite()`，不得在 ImGui 锁内访问Controller/HWND。Product merge 必须完整复制 `cursorSettings`。

### 工具、来源与外观

| 入口 | 类型选择作用域 | 固定边缘规则 |
| --- | --- | --- |
| Mouse Pen/HardPen | MouseStyle | 使用画笔光标时始终高亮 |
| Pen Pen/HardPen | PenStyle（默认圆形） | IntegratedPen且非Conflict不高亮，其余Pen高亮 |
| Mouse Highlighter | 不受两类型选择影响，始终矩形粗细预览 | 始终高亮 |
| Pen Highlighter | 不受两类型选择影响，保留矩形 | 同固定Pen来源规则 |
| Mouse Shape | 始终System，不受MouseStyle影响 | 不新增主光标 |
| Pen Shape | 保持原圆形，不应用PenStyle | 同固定Pen来源规则 |
| Eraser/Laser/Touch | 既有专用策略，不应用两类型选择 | 专用alpha和生命周期保持 |

- Host只传原始两类型选择；Controller按实际 EffectiveDrawingCursorTool 进行生产工具投影：Pen/HardPen返回保存的MouseStyle，Highlighter固定Drawing，其余System。Window系统箭头和resolver使用同一有效策略。活动contact锁存原工具时不能按新选择state.tool覆盖投影；抬起后重新投影，原始偏好始终保留。
- Unknown/Conflict Pen 只在视觉上固定高亮，不改输入身份或橡皮模型。没有任何高亮开关，历史高亮值不能覆盖固定规则。
- PenIcon 仍24 DIP，左上尖端为输入热点、笔尾向右下；仅笔侧/笔尾四个非尖端角做内切圆角，目标半径3 DIP并等比缩放，切线长度各不超过相邻短边2/5以保留间隔；不能整体圆化而钝化笔头。偏移仅影响瞬态中心/quad/dirty，不移动 sample、不外扩外框，复用GPU type21。
- Highlighter矩形复用当前 `CanvasDiameterForTool` 和同一长宽比，填充有效alpha仍0.35；显式不透明高亮（outlineOpacity=1、GPU编码uv.x=2）时左右边缘为原宽度一半，上下保持原宽度，外框不动。ScreenPen非高亮与legacy默认编码0保持原公式，小尺寸inner coverage须非负且不超过outer；独立边缘alpha不改变工具墨迹。`outlineOpacity=-1` 保留旧整体opacity语义。
- 显式productSettings=nullptr时保留旧独立API行为；Pen Contact开关、倒转橡皮、Laser及逐Touch保持。瞬态视觉不进入L0/L1/L2、文档或Undo。
- 穿透/选择清应用主光标，由当前命中窗口拥有系统光标；保持WindowFromPoint保护，不用全局ShowCursor计数或强制箭头覆盖其它应用。

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
| Win7尚未实测 | 待硬件验收，不宣布零闪现/双光标消失 |

## 5. Good / Base / Bad Cases

- Good：Mouse默认系统但Highlighter仍预览；选择Mouse笔形不改变Shape；Pen选择笔形后按实际来源固定边缘，重启保留两份类型。
- Base：没有配置时Mouse软/硬笔系统、Pen软/硬笔圆形；ScreenPen不高亮，其余高亮；没有边缘UI选项。
- Bad：让MouseStyle关闭荧光笔预览；把PenStyle套到荧光笔矩形；仍从旧bool读取边缘；只删UI而保存旧开关；把Unknown改成别的设备。把四边统一减半或把笔尖一起圆化同样违反外观合同。

## 6. Tests Required

- 真实Config：两默认0、四组合独立往返/ReadMini/冻结副本、非法类型恢复0、旧高亮忽略/保存清理、未知键保留与NoUpload。
- Bridge：只改任一类型仍发布新revision、旧快照不变；调用生产工具helper覆盖两MouseStyle×全部Tool，不复制映射。
- Resolver：两PenStyle×ScreenPen/Digitizer/Unknown/Conflict，Mouse/Pen相互独立、圆/笔形热点DPI、固定edge与Highlighter填充、工具排除、Contact/Touch/Eraser/Laser回归。
- 完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`、i18n sync/check、编码/BOM/EOL与diff --check；GUI需用户明确授权。既有无窗口 `Inkeys.exe --draw3-pen-width-test` 覆盖真实未附着 Window 的活动工具锁存、选择切换与释放后原始偏好恢复，不以纯 helper 断言替代该接线验证。
- 修改光标shader数学时必须同步正式PS与完整Win7 guarded数组，并核对签名、原/还原DefaultInk尾部与正向guard；Win7实际可见行为不能沿用旧二进制或逻辑通过结论。
- 最终PE资源须逐字节等于项目实际生成的当前CSO；显式/O3参考与项目默认编译选项不同时不能强制同SHA。须区分身份，另核实际产品PS的签名/分支，不能仅靠参考ASM推断工程资源行为。

## 7. Wrong vs Correct

```cpp
// Wrong：把鼠标类型直接当作所有工具的系统光标策略。
window.SetMouseUsesSystemCursor(settings.mouseStyle == MouseCursorStyle::System);
// Correct：先按实际工具解析，荧光笔独立预览、形状固定系统。
auto effective = settings;
effective.mouseStyle = Bridge::ResolveMouseCursorStyleForTool(actualTool, settings.mouseStyle);
// 高亮依据实际来源固定，不从配置恢复三份bool。
```
