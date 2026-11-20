# C3 设计：设置页主栏预览与通用拖放

> 设计：Claude（主 Agent）；实现：Astra（大规模，跨设置渲染、UI3 与 Bar 实例）；审查：Sol。依据父任务 `prd.md` R1/R2、父 `design.md` §5，C1（`BarLayoutModel`、`CanPlace/Move/Insert/Remove`、`ApplyBarLayout`）与 C2（`BarContentInstance`）的现有接口。行号以当前工作树为准，开工前核实。

## 1. 模块与文件

| 新/改 | 文件 | 内容 |
| --- | --- | --- |
| 新 | `Inkeys/Inkeys/UI/UI3/UI3.DragSession.cppm`（+ 必要时 `.cpp`） | 通用拖放状态机，不认识主栏或 ImGui |
| 改 | `Inkeys/Inkeys/UI/UI3/UI3.cppm` | 导出新分区/模块（按 UI3 现有组织方式） |
| 改 | `Inkeys/Inkeys/UI/Bar/Bar.ContentInstance.cppm/.cpp` | 增加“幽灵按钮 + 跟随提示”绘制（§4） |
| 新 | `Inkeys/Inkeys/UI/Setting/Setting.ComponentEditor.h/.cpp` | 设置页宿主：纹理、合成、输入映射、拖放会话、反馈、应用 |
| 改 | `Inkeys/Inkeys/UI/Setting/Setting.cpp` | 仅在 `tabComponent` 顶部（`PageHeader` 之后、快捷方式行之前）调用宿主；向宿主提供写盘回调（`QueueConfigWrite`）与滚动所需信息 |
| 改 | `Inkeys/additional/imgui/imgui_impl_dx11.h/.cpp`（若需要） | 预乘 alpha 混合回调，按现有 `OpacityGroup` 特殊回调值的写法增加一对（Begin/End） |
| 改 | 工程文件、filters | 登记新文件 |
| 改 | `Inkeys/Inkeys/UI/Bar/Bar.Host.Test.cpp` 或 headless 测试 | 测试（§7） |

C4 的卡片与条目是外部拖动源，C4 会调用宿主的 `BeginLibraryDrag`（§5.3），C3 必须提供该入口并实现完整反馈；卡片侧的接线由 C4 完成。

## 2. UI3 通用拖放 `UI3::DragSession`

```cpp
enum class DragPointerKind { Mouse, Touch, Pen };
enum class DragPhase { Idle, Pressed, Dragging };
struct DragPayload { std::uint64_t token; };   // 由宿主解释（预览中的 Position 或外部组件 ID）
class DragSession
{
public:
	// 阈值：鼠标 4 DIP，触摸/笔 10 DIP（以宿主传入的 DIP→像素比例换算）。
	void Press(std::uint32_t pointerId, DragPointerKind kind, D2D1_POINT_2F point, DragPayload payload, float pixelsPerDip);
	// 外部源直接进入 Dragging（组件库卡片已在宿主侧越过阈值）。
	void BeginDragging(std::uint32_t pointerId, DragPointerKind kind, D2D1_POINT_2F point, DragPayload payload);
	// 返回本次是否刚越过阈值进入 Dragging。
	bool Move(std::uint32_t pointerId, D2D1_POINT_2F point);
	// Dragging 中抬起 → 返回 Drop 结果；Pressed 中抬起 → 返回 Click（宿主可按点击处理，如点开 More）。
	enum class ReleaseResult { None, Click, Drop };
	ReleaseResult Release(std::uint32_t pointerId, D2D1_POINT_2F point);
	void Cancel();      // 失焦、Esc、页面离开、宿主重置
	DragPhase Phase() const; DragPayload Payload() const; D2D1_POINT_2F Point() const; D2D1_POINT_2F Origin() const;
	std::uint32_t PointerId() const; DragPointerKind Kind() const;
};
```
- 只接受当前接触 ID 的后续事件；第二根手指/其他指针忽略（不切换、不取消）。
- 纯数据，无窗口、无时间依赖，可无窗口单测。

## 3. 宿主 `ComponentEditorHost`（`Setting.ComponentEditor.*`）

### 3.1 生命周期与渲染
- 宿主持有一个 `BarContentInstance`（预览）以及两张纹理：**预览纹理**（覆盖主栏预览区域，含阴影外扩与展开 More 的包络）和**浮层纹理**（幽灵按钮 + 跟随提示，跟随指针，在页面最上层合成）。
- 纹理：设置页 D3D11 设备上的 `DXGI_FORMAT_B8G8R8A8_UNORM`、`D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE`；同设备 `ID2D1DeviceContext` + `CreateBitmapFromDxgiSurface`（预乘 alpha）→ `UI3::FrameTarget(ExternalContext)`。按设备代次（`DeviceEpoch`）与像素尺寸重建；设置会话结束时释放（解绑 renderer 后再释放目标，按 C2 设计 §6 的合同）。
- D2D 设备与 DWrite 资源取 RenderPipeline 当代资源（参照 C2 测试与 Bar 宿主的取法），不新建第二个 D3D 设备。
- 每帧：组装 `PreviewInputs`（时间、可用宽、目标高度、尺寸模式 `SizeMode`、`IconOrder`、动画/光效开关、本地化排版、指针）→ `Advance` → 必要时调整纹理尺寸 → BeginDraw/透明清屏 → `Draw` → EndDraw → `NotifyExternalEndDraw` → `ConfirmFrame`。`PreviewFrame.active` 或 `hintDeadline` 未到时请求下一帧（使用设置页现有的“请求重绘/动画中”机制，不新建计时器）。
- ImGui 合成：`AddImage` 前后插入预乘混合回调（`ONE, INV_SRC_ALPHA`），之后恢复默认。

### 3.2 尺度与布局
- 目标高度 `targetHeight` 用设置页布局单位：`Design::Pixels(64)`（随设置窗口 DPI，不受主栏 `configZoom` 与任务栏基准影响）；大/小尺寸都按该高度渲染；可用宽度为内容区宽，窄时由实例等比缩小（C2 已实现）。
- 预览块在内容区水平居中，块高度 = 实例返回的 `extent` 高度（含阴影与 More 展开包络的上下留白）；块下方留 `RowGap`。
- 方向：`IconOrder` 原样传入，由 C2 处理（2/3 固定方向，其余从左到右）。

### 3.3 输入映射
- 从 ImGui IO 读取鼠标/触摸（`io.MouseSource` 区分 Mouse/TouchScreen/Pen；接触 ID：鼠标用 0，触摸用 1），只在预览块区域被悬停（且没有其他 ImGui 项占用）时开始交互；拖动开始后宿主独占指针直到抬起或取消（`ImGui::SetActiveID` 或等效方式，保证跨 child、拖出窗口区域仍收到移动/抬起）。
- 指针坐标换算到预览纹理局部像素后传给实例（悬停、点光、提示）；离开区域传 `nullopt`。
- 点击（未越过阈值）在 More 入口上 → `SetMoreExpanded(!expanded)`；点击其他按钮不执行任何动作。
- `Esc`、窗口失焦、离开组件页 → `Cancel`：实例 `CancelLift()`，主栏动画展开回原状。

## 4. Bar 实例扩展：幽灵按钮与跟随提示

在 `BarContentInstance` 增加（签名可调整，但能力必须齐全）：
```cpp
// 预览中抓起的条目，或外部来源的组件 ID（含 UserDivider），以当前尺寸模式样式单独绘制（无底板），
// 中心位于 point（目标局部像素）；hint 非空时在其附近绘制小尺寸样式的跟随提示（复用 DrawContentHint）。
struct GhostRequest { std::variant<Position, std::string> source; D2D1_POINT_2F point; std::wstring hint; bool danger = false; };
PreviewFrame AdvanceGhost(const PreviewInputs& inputs, const GhostRequest& request);
HRESULT DrawGhost(UI3::FrameTarget& target, const PreviewFrame& frame);
```
- 幽灵按钮使用实例自己的克隆（外部 ID 时按注册原型新建），浮起效果：比原尺寸放大约 5%、带材质阴影；`danger`（悬停垃圾桶）时降低不透明度。
- 提示文案由宿主给出（§5.2）。

## 5. 拖放语义与反馈

### 5.1 落点判定
- 每帧根据指针在预览局部坐标调用 `InsertionGaps(point)`，按 C1 `CanPlace(model, source, gap, catalog)` 过滤，取最近的允许缝隙为候选；无允许缝隙时显示“不可放置”。
- 源：预览内抓起的条目 → `ItemSource{Position}`；外部卡片 → `ItemSource{componentId}`。
- More：指针停在 More 入口上约 0.5 秒自动 `SetMoreExpanded(true)`（宿主计时，用帧时间）；在 More 入口上松手 → 插入 More 末尾（`Position{More, size}`），须先通过 `CanPlace`。More 面板内的缝隙由 `InsertionGaps` 提供，面板内可排序、可拖出。
- 垃圾桶：拖动开始时在页面可视区右下角出现（淡入/上移动画，遵守动画开关），指针进入其扩展热区时变红并放大；仅对“可删除”的源（C1 约束：组件、用户分割线、外部来源）显示为可用，不可删除的源（默认 A1/A2 按钮、Setting）显示禁用外观且不可落下。外部来源落在垃圾桶 = 取消添加。

### 5.2 反馈
- 抓起：`Lift(source, revision)`，主栏动画缩短；幽灵按钮跟随指针。
- 插入光标：在预览纹理之上用 ImGui 绘制候选缝隙 `start→end` 的强调色圆角竖线（宽 3 DIP），随候选变化平滑移动（遵守动画开关）。
- 跟随提示文案（新增 i18n 键，zh-CN 为准）：`DragInsert`“放到这里”、`DragStore`“收纳到更多”、`DragDelete`“松开以删除”、`DragCancelAdd`“松开以取消添加”、`DragNotAllowed`“此按钮只能在原区域内调整位置”、`DragFixed`“固定分割线不能移动”（点击/按住交界线时短暂提示）。
- 放下：
  - 允许缝隙 → 预览内源：`Move(model, from, gap)`；外部源：`Insert(model, id, gap)` → `ApplyBarLayout(expected, next)` → `Applied` 时调用宿主写盘回调 → `Place(accepted, gap, revision)` 让实例动画展开到新布局；之后以 `ReadBarLayout()` 结果 `SetLayout`。
  - 垃圾桶 → `Remove(model, from)` → 同上应用。
  - 不允许/无候选/取消 → `CancelLift()`，回到原位（动画）。
  - `ApplyBarLayout` 返回 `Stale` → `CancelLift()` 并重新 `SetLayout(ReadBarLayout())`。
- 外部（真实主栏或其他入口）改了布局：宿主每帧比较 `ReadBarLayout()` 与实例当前版本，变化时 `SetLayout`（拖动中则先取消拖动）。

### 5.3 外部源入口（供 C4）
```cpp
// C4 的卡片/条目在 ImGui 中越过拖动阈值后调用；componentId 可为 UserDivider。
void BeginLibraryDrag(const char* componentId, ImVec2 screenPoint, bool touch);
bool DragActive() const;
```
宿主接管该指针直到抬起；卡片侧不再处理该次拖动。

### 5.4 边缘自动滚动
拖动中，指针进入设置页内容滚动区可视范围上/下边缘 48 DIP 内时，按深入程度线性加速滚动（最大约 1200 DIP/s，按帧时间积分），滚到头停止；使用页面滚动 child 的 `ImGui::SetScrollY`（宿主需拿到该 child 窗口；由 `Setting.cpp` 传入或在宿主内部查找当前内容窗口）。滚动期间持续更新候选与幽灵位置。

## 6. 性能与兼容
- 无动画、无悬停、无拖动时不重绘预览纹理（`PreviewFrame.active=false` 且无提示期限），只复用上次纹理合成。
- 不在设置渲染回调中写盘（只调用写盘回调入队）。
- Win7 SP1：仅 D3D11 FL 11.0 + D2D 1.1 互操作（与现有管线一致），无 DirectComposition、无共享句柄。

## 7. 测试
- `DragSession` 无窗口单测：阈值（鼠标 4、触摸 10 DIP）、点击 vs 拖动、其他接触 ID 被忽略、取消、外部 `BeginDragging`。
- 宿主落点逻辑可脱离 ImGui 测试的部分（候选选择、源/垃圾桶/More 规则 → 生成的模型）以纯函数实现并单测：A1 按钮拖到 B1 被拒、组件拖到 A1 被拒、用户分割线可入 A1、组件拖到 More 入口追加、外部组件拖入垃圾桶不改模型、Setting 拖入垃圾桶被拒。
- 离屏：幽灵按钮在大/小 × 深/浅下可绘制；预乘合成后的像素与直接绘制一致（抽样）。
- 全部 C1/C2 门禁保持：golden 逐字节、第二宿主、属性脏区、eraser、几何、headless。
