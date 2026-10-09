# Research: settingui2 按住内容拖动原实现与当前滚动窗口部署

- Query: 精确定位用户所称 settingui2 的旧拖动实现，核对输入、触发、控件占用、逐帧调用及当前全部滚动窗口；保持原算法而仅迁移调用上下文。
- Scope: internal
- Date: 2026-10-09
- Task: `.trellis/tasks/10-09-setting-scrollbar-drag-scroll`，父任务 `09-06-setting-winui3-design-rebuild`。
- Boundary: 本研究只写此文件；未改源码、规范、任务元数据，未运行 Git、构建、GUI 或 Computer Use。

## Findings

### 1. 原源与版本证据

直接读取的本地旧工作区原源：

| 文件 | 作用 |
| --- | --- |
| `D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/UI/Setting/Setting.Base.cppm:163` | 完整 `ScrollWhenDraggingOnVoid` 定义。 |
| `D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/UI/Setting/Setting.cpp` | 15 处每帧反向 `MouseDelta` 调用，页面主内容末尾、`EndChild()` 之前。 |
| `D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/UI/Setting/Setting.Widgets.cpp:171` | 旧 Combo 的 Begin/Selectable/End；End 只 `EndCombo` 和恢复样式，无拖动 helper。 |

主会话只读 Git 交叉核对结果（研究角色未操作 Git）：

- `e6c729d2`（2026-09-20，`refactor(settings): clean up obsolete options`）是主会话只读历史中减少/删除旧页面调用点的变更；该版本附近 helper 定义仍位于 Setting.Base.cppm，不能将调用点清理说成定义删除。
- 其父版本完整 SHA：`7218d7191c5edd52f7b37a864ad035ce4625c65d`。
- 该版本 `Inkeys/Inkeys/UI/Setting/Setting.Base.cppm` helper 与上述本地旧工作区逐行一致，包含 ID、KeepAliveID、ButtonBehavior 参数、fallback 和 SetScrollX/Y。
- 同版本 `Setting.cpp` 有 15 处 `(0.0f, -MouseDelta.y)` 调用，位置同为页面内容提交后、滚动 child 的 `EndChild` 之前。
- 当前分支树和 `bugfix/settingui` / `25075fe7` 的文件树未发现名为 `SettingUI2` 的文件；旧代码实际位于 `UI/Setting/`，不能把用户口头名称伪造为源码路径。

旧源码全文如下，实施时保留函数正文与参数原语义（只允许必要命名空间/内部声明迁入）：

```cpp
void ScrollWhenDraggingOnVoid(const ImVec2& delta, ImGuiMouseButton mouse_button)
{
	ImGuiContext& g = *ImGui::GetCurrentContext();
	ImGuiWindow* window = g.CurrentWindow;
	bool hovered = false;
	bool held = false;
	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);

	ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, mouse_button);
	if (!held && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseDown(mouse_button))
		held = true;

	if (held && delta.x != 0.0f) ImGui::SetScrollX(window, window->Scroll.x + delta.x);
	if (held && delta.y != 0.0f) ImGui::SetScrollY(window, window->Scroll.y + delta.y);
}
```

旧页面末尾调用原文：

```cpp
{
	ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
	ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y), ImGuiMouseButton_Left);
}
```

本地旧 `Setting.cpp` 的 15 个调用行：1944、2179、2857、3719、3971、4239、5168、5381、5577、6522、7273、7363、7598、7666、7851。示例 `1944` 是语言页主滚动 child 的尾部；`2179` 是配置保存页；`2857` 是版本页。原实现没有 frame-end 全窗口遍历，调用目标就是当时 `g.CurrentWindow`。

### 2. 原行为，不能自行增加的手势规则

- 输入来自 ImGui 已有 `IO.MouseDelta` / 左键状态。此函数不读 TouchMode、RTS、WM_TOUCH、WM_POINTER、MouseSource，也不要求新的 Win32 输入转换。
- 左键常量 `ImGuiMouseButton_Left` 的值为 0，被原样传入 `ButtonBehavior` 的 flags 参数；库在未指定 mouse button mask 时默认左键。不要借此次迁入修正为新的 flags 表达式。
- `ButtonBehavior(window->Rect(), overlayId, ...)` 取得 ImGui 原有 held；`KeepAliveID` 维持活动 overlay 身份。
- fallback 使用 `IsWindowHovered(ChildWindows)` 与左键按住，因此悬停在下属非交互内容 child 上也可滚父级窗口；已按住后移入仍以原ImGui输入所有权、HoveredWindow及ActiveId规则为前提，helper没有记录必须从该窗口按下的起点。整个ImGui区域外按下时，原库MouseDownOwned=false并抑制移入时的HoveredWindow，不能将其理解为无条件支持外部起拖。
- 每个非零 delta 帧设置 `window->Scroll + delta`，向上拖动使垂直 scroll 增加。无距离/时长阈值、无 `GetMouseDragDelta` 累计值、无惯性、无速度/弹性、无倍率二次换算。
- `SetScrollY(window, value)` 实際只写 `ScrollTarget.y`，center ratio/edge snap 设为 0；下次 Begin 消费、像素取整并夹到 `[0, ScrollMax.y]`。证据：当前 `imgui.cpp:11907`、`11766`、`7927`、`7930`。不应新增同帧直接写 `window->Scroll.y` 的路径。
- helper 没有显式清除其他控件 ActiveId，也没有“滑动后取消原按钮点击”状态。原名称 OnVoid 是真实行为边界：一般活跃按钮、slider、textbox、selectable仍拥有自身交互，不承诺从任意控件表面起拖会变成滚动。
- 当前 ImGui 的 `ItemHoverable` 因其他 `HoveredId` / `ActiveId` 拒绝 overlay 抢占（`imgui.cpp:4915`）；`IsWindowHovered` 默认也因其他非重叠 ActiveId 拒绝 fallback，但允许 hovered window 自身 MoveId（`imgui.cpp:8445`）。因此保留原机制通常保留控件操作，但不能把这解释成额外实现过的误触过滤。
- scrollbar 活跃 ID 同样应由库保留，不能用 `AllowWhenBlockedByActiveItem` 或手动取消捕获让内容拖动抢占它。
- helper 不清理 `g.MovingWindow` / MoveId，不专门区分标题栏。原页调用未覆盖根标题栏；当前 Win32 标题栏系统拖动仍由已有 hit test 路由处理。

### 3. 当前文件和全部滚动所有者

| 当前文件/窗口 | 现状及部署边界 |
| --- | --- |
| `Setting.cpp:2289`，`##setting-content` | 所有页面、通用子页及插件详情的真正正文滚动所有者；尺寸为 shell content geometry，默认允许滚轮与滚动条。页面与详情用原稳定 ID 保存 scroll。 |
| `Setting.cpp:2303`，`##setting-page-content` | `AutoResizeY`、无额外高度约束的正文 body；正常内容自动完整增高，不应成为第二层滚动所有者。既有 EDID 实际控件回归明确检查 body `ScrollMax.y=0`、outer有range（`setting_design_tests.cpp:2277`、`2323`）。首帧/内容剧变仍应在新部署回归中核对。 |
| `Setting.Shell.cpp:234`，`##settings-nav-list` | 侧栏中段滚动所有者；当前 `NoScrollbar` 但保留滚轮，待另一研究的统一 scrollbar 方案。 |
| `Setting.Shell.cpp:328`，`##settings-general-children` | 展开动画的裁剪容器，`fixedFlags = NoScrollbar | NoScrollWithMouse`；半展开时可因裁剪存在 range，但不允许独立滚动，应透过 child hover 滚上层 nav-list。 |
| `Setting.Shell.cpp:506`，`##settings-general-flyout` | 客户区夹紧高度的四子项模态导航浮层；正常高度可能无range，极短窗实际内容可超过固定高度；凡实际有range应纳入旧 helper 部署。关闭淡出期间存在输入屏障；caller不得将关闭浮层当开放交互区。 |
| `Setting.Controls.cpp:462` → `imfluent.cpp:4320`，Combo popup | 实际 Combo 内部 popup 的高度按最大项数约束，长列表有 scroll；不属于外层正文 child，无法只调用正文 helper 就覆盖。默认最大项数及 popup style 由现有 ImFluent 决定，禁止复制 Combo 控件另写一套。 |
| `Setting.Pages.cpp:223/252`，`##home-common` / `##home-author-section` | 无约束 `AutoResizeY` 的主页版式子区，通常无range；正文 outer统一滚动。若实际首次测量产生range，应由运行期候选判断而非名字猜测。 |
| `Setting.Controls.cpp` → ImFluent BeginCard，EDID card | 内容完整高度卡片，已有实际回归检查 `ScrollMax.x/y=0`、无内部scrollbar。保持只由outer滚动，不恢复EDID内部滚动。 |
| `Setting.Shell.cpp:197/203/392`，pane/top/footer | 固定布局 host、顶部、底部；均 `NoScrollbar | NoScrollWithMouse`，不可纳入拖动所有者，即使过短窗裁剪导致range。 |
| `Setting.cpp:2137/3830`，root/nav-scrim | root禁用scrollbar和滚轮；scrim是关闭overlay的固定输入屏障。排除拖动，保留既有点击。 |
| ImFluent/ImGui tooltip | 非设置内容滚动目标且通常 `NoInputs`，排除；本轮不改变 tooltip 输入。 |

现有产品 `Setting.cpp` 和当前 `Setting.Base.cppm` 已没有旧 helper 或 MouseDelta 调用，不能只补一个未使用的定义。

### 4. frame末统一部署：最小调用边界及准确风险

为了覆盖内部 Combo popup而不改vendor，可把上述原 helper 移入已有 Design/Controls 内部绘制层，并在本设置 session 每帧全部 UI 提交后、`ImGui::EndFrame/Render` 前部署调用。产品具体空隙为 `Setting.cpp:3860` 附近正文和导航均提交之后，root `End` 之后至 `EndFrame` 之前；也可在 root End之前完成全部UI后调用。无窗口测试应调用同一内部部署函数，不能复制算法充当验证。

**这不是旧代码原来的全窗口遍历。** 旧 helper正文必须原样；新增的是覆盖当前新增窗口/内部 popup 的调用部署。caller应限制为当前设置 context、本帧实际打开可见且可滚窗口，不能将所有window无差别逐个运行。

候选规则和边界（仍需实施测试验证）：

1. `window->Active && !Hidden && !SkipItems` 证明本帧实际提交且可交互；`WasActive` 只是上帧状态，不可用它替代 Active纳入旧页面/隐藏popup。
2. 按实际 `ScrollMax.y > 0` 判可竖直滚动，排除 `NoMouseInputs` / `NoScrollWithMouse` / tooltip。以上 fixed child名单通过flags排除，不依赖本地化名字或窗名substring。
3. popup即使本帧已经绘制，可能在选项选择后关闭但 Active仍true；需用其 `PopupId` 与 `IsPopupOpen(id, ImGuiPopupFlags_AnyPopupLevel)` 核对仍在开放popup stack，不能误用 `AnyPopupId` 让别的popup替它通过。关闭中的导航浮层还须沿用既有状态屏障。
4. 同一指针位于父子两个有range的窗口时，选择最近的合格滚动所有者；不要同时向二者提交相同delta。固定裁剪 child不合格时沿其祖先找到outer/nav-list；通常 auto body的range0自然让outer成为目标。此选择属于部署目标仲裁，不能修改 helper内部held或MouseDelta算法。
5. 已由旧 overlay捕获的滚动 owner需要持续运行 helper到释放，避免指针离开后KeepAlive丢失而改变原held capture行为；释放后仍交库处理ActiveId生命周期。需要覆盖越界与跨区域连续帧，而不能仅按当前hover决定调用。
6. helper初次 `ButtonBehavior` 可 `FocusWindow`，会重排 `g.Windows`（`imgui.cpp:12924/12979`）。不能一边遍历同一vector一边调用；先收集稳定候选指针或先选单一owner。

**调用上下文恢复需要精准而克制：**

- `SetCurrentWindow` 在 `imgui.cpp:4593` 是文件内部 `static`，没有导出到 internal.h；不可写成不存在的 `ImGui::SetCurrentWindow` API。
- 库原函数会同步 CurrentWindow、StackSizesInBeginForCurrentWindow、CurrentTable、CurrentDpiScale、FontRasterizerDensity、字体大小和Nav状态。但这里旧 helper路径只读 CurrentWindow、window DC、全局item flags，调用KeepAlive/ButtonBehavior/IsWindowHovered/SetScroll；不需要改font/table/dpi或伪造Begin/End栈。最小部署可在严格局部scope暂存 `g.CurrentWindow`、切为目标、调用后恢复，须以真实回归确认其他context字段、栈长度及原LastItemData不变。
- 已执行 End的窗口已经PopClipRect（`imgui.cpp:8190/8213`）；此时其 `ClipRect`不再是原内裁剪。直接切CurrentWindow调用会让 helper `IsMouseHoveringRect`用已弹出的宽clip，可能误包含裁剪区或scrollbar。需要局部保存ClipRect、临时恢复已夹紧的 `InnerClipRect`、helper后原样还原。helper本身不draw，**只赋ClipRect是否已足够**是待回归验证的最小方案；没有证据前不要宣称已通过。替代Push/PopClipRect会新增绘制命令，需要更审慎的DrawData检查。不要把窗口InnerRect直接当InnerClipRect。
- 使用frame末调用时，原BeginDisabled已结束，因此关闭浮层/内容输入屏障应在候选层保证；不能假设g.CurrentItemFlags仍反映某个已经结束窗口的禁用状态。
- 原helper按窗口ID栈生成overlayID；frame末window IDStack仍应保持窗口根ID，必须核对原调用点是否有未Pop的业务PushID（旧15处均在页面业务ID结束后）。测试要验证稳定overlay identity与现有路由ID互不影响。

### 5. root移动与标题栏：当前证据及待测边界

- 当前实际root Begin（`Setting.cpp:2137`）有 `NoResize/NoSavedSettings/NoScrollbar/NoScrollWithMouse/NoTitleBar`，**没有 NoMove**；本地旧root同样无NoMove。
- 原helper在自己的窗口直接取得overlay时会占ActiveId；hover在嵌套内容child时的fallback不取得overlayActiveId。`UpdateMouseMovingWindowEndFrame`在空白点击且无活动item时可以 `StartMouseMovingWindow`（`imgui.cpp:5310`），后来IsWindowHovered对MoveId的例外又允许父级fallback继续scroll。
- 当前root每帧 `SetNextWindowPos({0,0})`、窗口原生移动由Win32 hit test处理；这不等于 `g.MovingWindow`不会出现，也不能证明需要立即新增NoMove。
- 若最终部署候选只运行真正scroll owner，不为range0子层设置overlay，空白嵌套child的MoveId风险仍与原fallback有关。新增NoMove只有在真实root flags回归显示必要时作为最小调用环境边界修正；不应预先写成已经必需/已经通过，也不改Win32 caption拖动/双击/贴靠命中。

### 6. 输入来源与现有无窗口验证入口

- `Setting.cpp:1222` 使用原 `ImGui_ImplWin32_WndProcHandlerEx(..., ImGui::GetIO())`，HWND线程在已有mutex下更新IO，渲染线程拥有context及UI；本轮保持不变。
- 当前 vendored `imgui_impl_win32.cpp:602` 通过现有 `GetMessageExtraInfo` 区分Mouse/TouchScreen/Pen；`:660/704/722` 将MouseSource及position/button入IO。旧helper无需辨别source，所以可同时复用来源为Mouse或TouchScreen的现有IO事件。测试模拟MouseSource只能验证已进入ImGui的行为，不能冒充真实桌面触屏/驱动验证。
- vendored版本为Dear ImGui `1.92.7`（`imgui.h:32`）；`Setting.Wrap.h`已有19270 static_assert，不升级依赖。
- `InkeysHeadlessTests/setting_design_tests.cpp:1492` 分层导航真实frame harness使用 `AddMousePosEvent` / `AddMouseButtonEvent`、真实Design::RenderNavigationPane、实际DrawData和窗口ScrollMax；`:1540` 识别真实nav-list并保存scroll。
- 同文件 `CheckNavigationScrolling:2340` 及既有scroll-sponsor回归覆盖固定top/footer、中段末项可达、蓝条与滚轮滚动同帧delta；应扩展真实拖动输入后继续保留这些回归，不复制坐标/手势公式。
- 同文件 `CheckEdidCard` 从`:2275`提交与产品同形的outer/body/card；已有body无range/card无range、outer长内容scroll及末行字形可见断言，适合接入原helper的正文target验证。
- Combo实际真实控件连续帧用例（`:1178`及后续）已可打开内部popup和采样DrawData；应增加足够长列表以产生range，而非使用无range的短选项假装覆盖。

建议新增回归只验证原有行为及部署边界：Mouse/TouchScreen来源的void Down→连续上/下移→Up；最小窗/nav三模式；嵌套非交互child滚outer；原overlay capture越界及松手；scrollbar活跃不触发导航；button/slider/textbox/selectable仍保留原操作；popup关闭/不在本帧Active无drag；range0和fixed子窗不drag；父子range不双重应用；真实root flags下MovingWindow/context/clip状态；多个倍率保持delta只使用物理MouseDelta一次。以上是待执行验证清单，研究未执行这些测试。

### 7. Related specs / local primary references

- `.trellis/workflow.md`：研究落盘与planning/implementation边界；本研究不读取implement/check JSONL。
- `.trellis/spec/native-desktop/index.md`：native桌面层边界和无窗口验证约束。
- `.trellis/spec/native-desktop/cpp-conventions.md`：minimal diff、原编码/换行、中文边界注释。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1451`：Setting统一session/render/FIFO/input所有权。
- 同规范 `:1938`：WinUI3 Design、固定与滚动导航分区、主题scrollbar、真正item登记、选中条随滚动平移、不新增触摸适配。
- 同规范 `:1974`：统一scrollbar 8DIP track/2DIP padding/透明背景/主题灰thumb；具体按钮减宽由独立sidebar研究负责。
- `Inkeys/additional/imgui/imgui.cpp` / `imgui_widgets.cpp` / `imgui_impl_win32.cpp` 为本地primary实现参考，无外部网络资料或版本猜测。

## Caveats / Not Found

- 没有名字明确为 `settingui2` 的文件；已找到并由主会话历史版本交叉核对的实际旧设置helper，历史commit和本地原源记录如上。
- 旧helper不是“从所有交互控件起拖”的新手势，也不包含阈值、惯性或额外取消点击逻辑；不能以产品方便为由发明这些规则。
- 旧15调用只覆盖其对应页面滚动child，旧Combo End不调用helper；覆盖当前Combo/flyout需扩大caller部署，文档必须如实区分算法复用与部署差异。
- frame末局部CurrentWindow/InnerClipRect恢复、候选仲裁与root NoMove必要性尚无实际测试结论；只提供精确源码依据和最小待测选择。
- 当前AutoResizeY body通常没有独立range，已有EDID回归支持此结论；不等同全页面/所有首次帧均已实测。
- 未运行真实触屏、鼠标GUI、HWND/DX11呈现、caption拖动或Computer Use；实施后完整solution/headless结果与人工验收应分别记录。
