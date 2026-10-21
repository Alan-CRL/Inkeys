# Rendering and UI

本页区分 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。图形实现是按窗口分流的；“仓库用了 D3D11”不等于所有窗口或 ImGui 都使用 D3D11。

## UI 路由与后端分工

| 区域 | 当前选择关系 | 图形/呈现链 | 直接证据 |
| --- | --- | --- | --- |
| `Inkeys.UI.Bar` | `【直接确认】` `IdtMain.cpp::wWinMain` 无条件启动 UI3；不存在 UI2/UI3 运行时分支 | `Inkeys.UI.RenderPipeline` 的共享 D3D11 WARP epoch 提供 DXGI/D2D device；Bar 是独立客户端，经自己的 device context、GDI interop 和 `UpdateLayeredWindowIndirect` 呈现 | `IdtMain.cpp`、`Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.*`、`Bar.RenderLoop.cpp` |
| 传统 `IdtFloating` | `【历史/兼容】` 源码暂存但在 `Inkeys.vcxproj` 中为 `None`，生产代码不得 include | 不参与产品编译 | `Inkeys.vcxproj`、`Inkeys.vcxproj.filters` |
| 设置窗口 | `【直接确认】` 当前主工程编译的唯一 ImGui renderer 是 DX11 | Dear ImGui Win32 + 共享 WARP D3D11 device/immediate context；Setting 独占传统 discard swap chain、RTV、SRV 和 ImGui session | `Setting.Base.cppm::CreateDeviceD3D`、`Setting.cpp` 中 `RenderSettingFrame`、`Inkeys.vcxproj` |
| 主画板 | `【直接确认】` Draw3 已接管 Window Service 的主 Drawpad；选择态使用同一 Host 的 presentation-only 辅助表面 | Draw3 独立 D3D11.1 device、单一 swap chain/final backbuffer，以及主 DComp/DWM/ULW + 辅助 ULW target | `Draw3.Host.*`、`Draw3.TransparentPresentation.*`、`draw3-integration.md` |
| PPT / Whiteboard 分页控件 | `【直接确认】` `Inkeys.UI.PageControl` 独占四个 owned layered HWND；`PptBottomLeft/PptBottomRight` 在 PPT 与 Whiteboard 间连续切换布局 | 与 Bar/Setting 共享 D3D11 epoch；PageControl 独占四套 BarPageSurface/device context/target/GDI interop，PPT 与 Whiteboard 只发布状态和业务回调 | `Inkeys/Inkeys/UI/PageControl/PageControl.*`、`Inkeys/Inkeys/UI/Ppt/Ppt.*`、`Inkeys/Inkeys/UI/Whiteboard/Whiteboard.*` |
| 冻结帧、放大镜等 | `【直接确认】` Window Service 统一创建，图像承载使用 `DibSurface` | GDI/GDI+、Magnification API | `IdtFreezeFrame.cpp`、`IdtMagnification.cpp` |

`Experimental.Inkeys3.UI3` 名下的 Animation、EdgeLighting 和 Debug 仍是 UI3 功能配置，不是路由开关。旧路由 JSON key 只能清理，不能恢复读取或写入。

## D3D11 WARP、D2D 与 DWrite

`【直接确认】` `Inkeys/IdtMain.cpp::wWinMain` 调用 `Inkeys::UI::RenderPipeline::Initialize()`。`Inkeys.UI.RenderPipeline` 依次：

1. 创建 multithreaded `ID2D1Factory1`；
2. 创建 shared `IDWriteFactory`；
3. 默认以 `D3D_DRIVER_TYPE_WARP`、`D3D11_CREATE_DEVICE_BGRA_SUPPORT` 和 feature level 11.1/11.0 创建 D3D11 device；Windows 7 运行时以 `E_INVALIDARG` 拒绝包含 11.1 的列表时仅重试 11.0；
4. 取得 `IDXGIDevice`；
5. 由 D2D factory 创建 `ID2D1Device`。

共享对象使用 `Microsoft::WRL::ComPtr`。默认 epoch 为 WARP；`PrepareBackend`/`CommitPreparedBackend` 为以后显式选择 Hardware 提供帧边界切换入口。只有唯一渲染线程可以发布新 epoch；初始化失败路径逆序 reset 已创建对象。

`【直接确认】` 消费者并不相同：

- `DeviceEpoch` 由 Bar、PageControl 四个分页客户端、Whiteboard Freeze 和 Setting 共用；每个客户端仍持有自己的 D2D device context、target bitmap 和 GDI interop，共享底部 HWND 只有 PageControl 一个呈现所有者；
- `SharedAssets` 中的 D2D 1.1 factory、DWrite factory/font collection 被 Bar/PPT 文字资源使用；
- Setting 借用同一 D3D11 device/immediate context，但独占 discard swap chain、RTV、纹理 SRV 和 ImGui backend/session；
- 主画板不是 D2D target。

`【直接确认】` `IdtD2DPreparation.*`、旧图形全局和 `Inkeys.UI.RenderScheduler` 已从产品工程移除。新增 Bar/PPT/Setting/Whiteboard UI 图形资产必须沿用 RenderPipeline 的共享生命周期；Draw2、legacy Freeze 和其他背景窗口不因本合同自动迁移。

## `Inkeys.UI.Bar`

`【直接确认】` `Inkeys/Inkeys/UI/Bar/` 按 `Bar.State`、`Bar.Atomic`、`Bar.Animation`、`Bar.Theme`、`Bar.UI`、`Bar.RenderingAttribute`、`Bar.Button`、`Bar.Layout`、`Bar.Rendering`、`Bar.RenderLoop`、`Bar.Interaction`、`Bar.Initialization`、`Bar.Main`、`Bar.Zoom`、`Bar.Format` 等文件拆分状态、主题、布局/动画、渲染、交互和窗口初始化逻辑。

`Bar.RenderLoop.cpp::BarUISetClass::Rendering` 把 Bar 注册为共享 UI3 调度器客户端。Bar 从共享 D2D device 创建自己的 device context 和 GDI-compatible、BGRA premultiplied target bitmap；单帧入口经 `ID2D1GdiInteropRenderTarget::GetDC` 和 `UpdateLayeredWindowIndirect` 提交计算出的脏区，idle 等待与最多 60 FPS 节拍由共享调度器统一负责。

### 公共按钮、固定面板与多目标事务

- 主栏 Main/MainMore 与分页分别持有 `BarButtonListClass`；通过稳定 `Snapshot`、`ResolveBarButtonListLayout`（Main/GroupedGrid/SingleRow）、`ConfigureBarButtonVisualTargets`、`AdvanceBarButtonVisual` 和 `DrawBarButtonVisual` 消费同一套生产能力。固定属性保留字段/数组，使用 `BarButtonVisualParts` 和固定按钮 helper；不引入通用节点树。
- `BarAttributePanelMotion`、`ResolveBarAttributePanelFrame` 管理既有属性与白板两个面板的目标、回弹及父级继承；同批次使用 `FrameContext::frameTime`，同一按钮每帧只推进一次。Idle 后首帧 dt=0，后续沿共享 `FrameAnimationClock` 推进。同布局的页码/主题更新只处理对应字段、缓存测量与脏区，不重新设置其余按钮或重置进行中的布局目标；同透明度目标保持幂等，显式零时长仍可完成未结束的运动。
- `BarUIRenderingServices` 显式提供字体与必要状态，分页 renderer 不构造伪 `BarUISetClass`。每个目标保留独立 context/bitmap/SVG/字体缓存，共享设备 epoch。固定面板透明度使用有界 `PushPanelOpacity`，不为每个子按钮叠整幅透明层。
- `BeginFrameDraw` 在 Clear/绘制前设置 dirty clip；`FinishFrameDrawCommands` 在 GetDC 前退出裁剪；GetDC、ULW、ReleaseDC、`EndFrameDraw` 及必要 HWND 几何交接全部成功后才消费 damage、发布命中和映射。资源/容量重建必须完整重画当前 viewport，不能沿旧局部脏区向空位图提交全窗。
- 公共 Shape/Word/标准按钮可以在当前设备脏区之外保守跳过绘制；范围必须包含当前变换、缩放、按压及光效边缘。未知自定义内容、无效范围和需要完整 SVG 验证的路径保留原调用。真实部分/完整 BGRA 对照与生产 cull 计数必须同时证明正确性和实际生效。
- backing capacity 可以保留历史峰值；菜单关闭后 drawClip、清理和提交按当前实际区域裁剪。稳定帧不持续唤醒，不重复创建 SVG/字体/圆角资源。光照通知只唤醒受到旧/新光源影响的目标，订阅者销毁与通知采样必须共用生命周期保护。
- 既有主栏交互 worker 统一消费主栏及分页有界 FIFO。Move 可相邻合并，Down/Up/Cancel 保序；捕获身份含窗口/指针/按钮。手势等待为可续接状态，不运行嵌套阻塞泵。输入只发布反馈，动画由 render owner 应用；COM、文件和确认对话框走业务续接。
- 原生 Down 必须在标准化入队前建立捕获并冻结唯一票据；逻辑手势按已准入的 Down 处理，属性 Start 不因原生 Up 提前到达而拒绝快速点击，也不重新夺取捕获。延迟 Stop/取消/释放只能携带本手势票据，由 owner 核对释放。原生已处理的取消只清逻辑状态，不再次投递捕获释放。
- 队列溢出保留各注册目标最后准入的精确捕获元数据，取消屏障完成前拒绝新事件；被拒的 Down 由 owner 立即释放自己的票据。尚未被 worker 消费的 Down、卸载注册、异常回调及 Cancel 后新接触均需回归；不能用通配释放清理后来取得的捕获。
- 共享输入业务队列有界且有序，阻塞组件由单一 STA drain 执行；退出先请求可取消对话框的 `std::stop_token`，再排空已接受的保存，取消尚未启动的外部动作。可取消对话框按隐藏、恢复 owner、join 清理；不得用分离线程或丢弃请求来绕过退出屏障，可取消请求失败不能转为无法取消的系统 fallback。
- 命中只读取最近成功呈现的不可变快照。主栏按钮/固定形状与底栏映射纳入同一 serial 提交；分页命中与 workspace/session/page 门禁同批发布。退场菜单保持绘制但禁用按钮，已呈现快照区分主体与 popup 背景；PPT 背景拖动只接受主体，仍可见的 popup 继续阻挡墨迹。业务身份变化但视觉没有变化时，仍通过现有 forceFullPresentation 完成真实且可重试的提交；不能无提交便确认新身份，也不从 render 额外安排冗余 wake。失败帧保留旧快照和待绘制 dirty，不得提前允许新页交互。

### 定格状态机合同

`Inkeys.UI.Freeze` 是定格业务状态的唯一所有者。普通绘制、选择和工具按钮切换不清除或恢复定格；独立ClickThrough与Freeze互斥，进入穿透关闭定格；再次点击穿透按钮保留墨迹并恢复记住的Pen/Shape/Eraser，定格继续关闭。穿透时点定格则恢复记忆工具后开启定格，不恢复旧定格状态。PPT或Whiteboard进入时关闭并标记不可用；不可用定格按钮从主栏隐藏，退出工作区只恢复可用性，不恢复进入前状态。合并式/分离式的尺寸与显隐见[选择与穿透合同](configuration-i18n-and-assets.md#选择与穿透操作模式合同)。主栏Clear空页且已Selection时使用同一 `Disable` 和 `BarButtonDisabledContentOpacity`，不参与悬停/按压/点击；空页仍在绘制或穿透工具时保持可点击以进入Selection。

以下仅是对现有 Bar 的 `【合理推断】` 修改约束，不适用于所有窗口：

- 保持 target/current 动画状态与最终绘制值的现有分工；
- 状态变化需要触发现有渲染唤醒/脏区路径，静止时避免无意义刷新；
- 维持 premultiplied alpha 语义；
- `GetDC/ReleaseDC`、`BeginDraw/EndDraw` 在成功与失败路径成对；
- 不在没有线程安全依据时跨线程传递 device context/target。

### 主栏按钮统一状态派生合同

- 范围为主栏A1/B/A2与运行时Pierce/More/Divider。配置/注册基础尺寸、用户可见性、成员顺序是输入；运行时有效size/hide/业务Selected/Disable与相关内容由StateUpdate单一计算/应用入口给出。不同业务事件不能直接覆盖有效字段，组件Load后所有特殊/普通按钮均一致化，不能只修Freeze或只补Recall。
- 工具、内容、Whiteboard/PPT/Freeze可用性、RecallView、fold/More状态按同组快照计算。内容缓存只抑制重复内容动画，不抑制尺寸/显隐重投影；重复同状态不重启内容或几何动画。 Recall的view与所选锚点可见性在网格锁内捕获到帧对象，主栏颜色和历史属性Advance显式使用同份帧值，不在解锁后重新读全局view或有效hide拼接旧目标。
- Recall保持3秒steady_clock保护、接受命令前事实、灰色点击延期及popup身份；灰色表观不能以通用Disable拦截所有点击而丢弃延期资格。事件持Recall锁只更新state/deadline，锁外请求；不能在该锁或Draw3回调内盲目同步整套UI更新。
- 列表重建与投影使用短而明确的同一串行边界。不得持该锁跨D2D呈现、Window Service/SetWindowPos/owner等待；UpdateRendering(false)轻请求与timer不能等待可能同步等待owner的渲染锁。面板Close归位等可重入副作用与纯投影分开，避免锁内Close重新取得Recall/更新锁。
- 主栏布局失效必须比较稳定成员身份/顺序、有效size/IsVisible以及隐藏成员锚点语义，不能只比较总宽度。等宽的两枚2×1与单枚2×2、成员替换/顺序变化仍进入现有统一批次；扩张曲线继续由实际宽度决定。Selected/Disable、文字/颜色、hover/emph/pressScale/动画val不属于几何指纹。
- RenderLoop继续持有像素目标SetTar、current/target、CanJoin与换边后半程重开、隐藏锚点、命中和旧新dirty；统一业务入口不得SetDirect跳过动画。主按钮脉冲/停靠及独立Surface/属性按钮保持原所有权，不扩大为全UI属性框架。
- 真实Bar无窗口回归须调用RegisterBuiltInComponents/SyncLegacyExtensionButtons/Load/UpdateRendering与生产timer/state入口：相同业务styleKey下Separate绘制增删组件仍为Pierce上2×1/Freeze下2×1；覆盖0/1/2/3+组件及overflow、模式/内容/PPT/Whiteboard、历史期限与精简态、等宽转换/动画中途/重复刷新。源码合同只作补充，不以复制逻辑或字符串替代真实生产调用；未启动Draw3 Host/HWND时不能声称验证Accepted命令或系统WM_TIMER投递。

### Undo/Redo 属性与 3 秒布局保护合同

#### 1. Scope / Trigger

修改 UI3 主栏 Recall/Redo、属性弹窗命令身份、Draw3 历史可用性显示或相关布局/颜色动画时适用。

#### 2. Signatures

`Inkeys.UI.Bar.RecallChrome` 提供生产纯策略：

~~~cpp
RecallChromeState ApplyRecallChromeCommandResult(RecallChromeState state,
    bool redo, RecallHistoryAvailability clickAvailability, bool accepted) noexcept;
bool RecallChromeNeedsIdleRestart(RecallChromeState before, RecallChromeState after) noexcept;
RecallChromeView ResolveRecallChrome(RecallChromeState state, bool compactSelection = false) noexcept;
// 历史按钮复用 RetargetBarButtonInteractionVisual 的主题与统一禁用透明度。
~~~

`BarUISetClass` 的 `NoteRecallHistoryAvailability`、`PollRecallChromeIdleTimer`、`AdvanceRecallChromeIdle` 和既有 `ArmRecallChromeIdleDeadline/Timer` 负责发布与唤醒；`RetargetBarButtonInteractionVisual` 可接收 `std::optional<COLORREF>` 最终内容颜色，省略时保留原主题行为。

#### 3. Contracts

- Draw3 真实 `canUndo/canRedo` 只控制即时可用性与颜色。当前 `layout` 独立锁存，静止目标为两者可用时上 Redo/下 Undo 的 2×1、仅 Redo 可用时 2×2 Redo、Redo 不可用时 2×2 Undo。
- Recall业务事件/计时器只更新历史状态、期限与通知；Undo/Redo最终有效size/hide由主栏统一状态派生入口应用，不得在Load、事件、idle或PresetHoming中另套第二投影。主栏 2×2 Redo 点击没有新增按钮，应保持原单列宽度。该入口使用同组工具/内容/工作区与RecallView，非白板空页Selection精简门禁优先隐藏两按钮，不能被3秒保护、待调整或timer重新显示；不清除历史保护。到期结算与唯一投影必须在该帧目标宽度/布局批次扫描前，不能动画末尾再改变有效尺寸。
- 主栏 2×1 点击进入所点命令的 2×2 交互及相反命令弹窗。已有展开交互内的命令点击保持主栏/弹窗身份，并延长最后一次点击后的 3 秒保护。
- 属性面板的同一 `RecallChromeView` 快照须选择当前 2×2 主栏 Redo/Recall；所选按钮同时用于 `IsVisible()` 关闭门禁、几何继承和弹窗锚点。隐藏的另一命令不能触发 `Close`。
- 点击前在原互斥边界取可用性快照；发布命令后只在 `CommandResult::Accepted` 时确认展开。异步执行末步导致可用性耗尽，不能反过来拒绝本次已接受点击的反馈。
- `Dismiss/ReturnToDesktop/SuppressPopup` 只关闭面板，保留布局与期限。没有点击的可用性变化也保存 `pendingLayout`，从目标变化起等待 3 秒；同目标逐帧读取不得重启期限。目标回到当前布局时可取消无点击等待。
- 期限使用 `steady_clock`，主栏 idle 时以既有 `WM_TIMER` 唤醒；有 hold 和无 hold pending 都须被唤醒。不得累加会在 idle 停止的动画 dt 来判定 3 秒。
- 禁用或发布失败不发送成功点击反馈；若已有待调整布局，点击仍延长保护，保持关闭面板的关闭状态。无 pending 时无效点击不启动新交互。
- 主栏 Redo 时属性按钮必须为 Undo，反向对称；属性按钮不在主栏注册表，必须自行推进 icon/name 的 `AdvanceContentTransition` 并保持续帧。身份切换以已有 `InitializationFromResource`（SVG val/tar 同步、取消旧内容动画、缓存失效）和 `SetStringImmediate` 同帧提交显示及 preset，避免旧内容与新动作混配。同身份普通内容更新继续使用原动画。
- 主栏 Undo/Redo 与弹窗历史按钮的不可用效果统一使用主题 `TextPrimary` 和 `BarButtonDisabledContentOpacity=0.30`，与 Clear/Freeze 相同；可用性仅从当前透明度双向过渡，保护时序、点击决策与布局不变。各尺寸分支与交互 helper 每帧提交同一最终目标，不能先回到完整透明度再降低。

#### 4. Validation & Error Matrix

| 场景 | 必须行为 |
| --- | --- |
| 最后一次 2×1 Redo 在 Publish 后耗尽 | 仍为 2×2 Redo + 弹窗 Undo，Redo 变灰，3 秒空闲后才调整 |
| 展开的主栏/弹窗连续交替点击 | 单步命令，位置身份保持，计时从最后点击开始 |
| 灰色按钮继续连点 | 不发布禁用命令，仅延期已有保护，不新开弹窗 |
| 无 hold 的历史可用性变化 | 即时变色，保存待调整目标并等待 3 秒 |
| Escape/其他属性/绘制/工作区切换 | 可立即收面板，不能立即更换受保护几何 |
| 相同透明度目标逐帧重复设置 | 动画正常收敛，反向从当前显示透明度开始 |
| 空页Selection且主栏收窄，历史仍有hold/pending | Undo/Redo隐藏且不可命中；计时器到期不重新显示，退出精简态仍沿原历史布局 |

#### 5. Good / Base / Bad Cases

Good：末步前快照认可已接受点击，后续耗尽只改颜色；Base：灰色 2×2 Undo 在无待调整目标时点击无新交互；Bad：关闭面板直接清 hold，或 `Resolve` 从实时 availability 推导几何，或同帧正常色/灰色反复 retarget。

#### 6. Tests Required

`InkeysHeadlessTests --no-window` 中既有 Recall 测试须覆盖 2.99/3 秒、异步末步事件顺序、popup 身份、连续/灰色点击延期、无 hold pending 的重定向与取消、关闭/绘制/工作区保护；透明度须使用统一禁用常量和真实动画，断言中间帧、反向连续与终态，并确认弹窗接入公共视觉 helper。既有测试另检查 `RecallAttribute::Advance` 生产源码的单快照、当前主栏锚点门禁及几何接线，需从仓库源码可访问的位置运行。完整 `InkeysRepo.sln Debug|ARM64` 验证生产入口与 helper 编译接线；纯策略、源级断言与编译均不替代实际 D2D/HWND 定时唤醒及交互视觉验收。

#### 7. Wrong vs Correct

Wrong：`Publish(Redo) -> Close/Refresh -> 读取已耗尽 canRedo 拒绝展开`；Correct：保存点击前可用性，发布成功后用该快照确认交互，再以最新事实更新颜色。Wrong：一帧先正常色、后禁用色；Correct：先解析最终目标，所有调用路径提交同一目标。

### UI3 idle 唤醒动画时钟合同

#### 1. Scope / Trigger

修改 Bar 渲染线程的 idle wait、唤醒、失败退避或动画 `dt` 计算时适用。该合同防止把真实休眠时长计入唤醒后的第一帧动画。

#### 2. Signatures

~~~cpp
class FrameAnimationClock
{
public:
	double Tick(Clock::time_point now = Clock::now()) noexcept;
	void Rebase(Clock::time_point now = Clock::now()) noexcept;
	void SuspendForIdle(Clock::time_point now = Clock::now()) noexcept;
};
~~~

#### 3. Contracts

- 动画时钟使用单调时钟；`Tick()` 对非有限值或负值返回 `0`，并将活动帧 `dt` 限制到 `[0, 0.05]` 秒。
- Bar 回调确认无可见工作并返回 `Idle` 前调用 `SuspendForIdle()`；下次实际回调的 `Tick()` 先记录原始间隔用于诊断，但动画 `dt=0`，再下一帧按实际间隔推进。共享 Scheduler 统一等待，不能把其他客户端的回调当成 Bar 动画帧。
- `BarUiAdvanceAnimation` 对合法活动 Value/Color/Pct 的 `dt=0` 保留当前值、目标和活动状态；显式 `forceReplace`、Once、无效数据及关闭动画的即时替换仍优先。SVG/Word 内容关键帧在动画开启且 `dt=0` 时保留目标，在动画关闭时即使 `dt=0` 也要同帧提交目标，不能留一帧旧内容。
- present/device 失败的 `WaitUntilGenerationChange()` 退避、普通 60 FPS pacing 和连续动画帧不得 rebase，否则会冻结或缩短仍需推进的动画。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 长时间 idle 后收到 UI 请求 | 首次回调 `dt=0` 且目标保持活动；第二次回调正常推进，不把休眠计入动画 |
| idle wait 因退出信号结束 | 直接停止，不再推进动画 |
| present/device 失败退避结束 | 不 rebase；保留失败等待期间的动画时间语义 |
| 时钟差为负或非有限 | 本帧 `dt=0` |
| 活动线程异常停顿超过 50 ms | 本帧 `dt` 上限为 50 ms |

#### 5. Good / Base / Bad Cases

- Good：静置数小时后点击按钮，动画从接近起点开始，不会首帧直接跳过约 50 ms。
- Base：连续 60 FPS 动画仍按相邻活动帧间隔推进。
- Bad：只依赖 `clamp(dt, 0, 0.05)`；长时间 idle 后首帧仍会固定跳过 50 ms。

#### 6. Tests Required

- headless 用可注入时间点模拟活动帧、长时间 idle、`SuspendForIdle()` 和唤醒后两帧；Value/Color/Pct 分别断言 `dt=0` 不完成，关闭动画时仍即时完成。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`；手工检查长时间静置后的首次展开、属性面板和粗细预览动画。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：报告 Idle 时只重基准，下一次回调仍把休眠限幅成 50 ms。
clock.Rebase();
return FrameResult::Idle;

// Correct：记录 Idle 边界，让首次真正回调消耗休眠时间而不推进动画。
clock.SuspendForIdle();
return FrameResult::Idle;
~~~

### UI3 共享串行调度器合同

#### 1. Scope / Trigger

新增或修改 Bar、PageControl 四个分页 HWND、Whiteboard Freeze、Settings、请求唤醒、动画续帧、target/device 失败恢复或渲染线程退出时适用。当前固定顺序为 Bar、PPT 底部左、PPT 底部右、中部左、中部右、Settings、Whiteboard Freeze。

#### 2. Signatures

~~~cpp
enum class Client : std::uint8_t {
	Bar, PptBottomLeft, PptBottomRight, PptMiddleLeft,
	PptMiddleRight, Settings, WhiteboardFreeze, Count
};

enum class FrameResult : std::uint8_t {
	Idle, Continue, Retry, DeviceLost, Stop
};

bool Scheduler::Start(ContextProvider, DeviceRecoveryCallback, ControlCallback);
void Scheduler::Stop() noexcept;
bool Scheduler::Register(Client, RenderCallback);
void Scheduler::Unregister(Client) noexcept;
void Scheduler::Request(Client) noexcept;
void Scheduler::Request(ClientMask) noexcept;
bool Scheduler::PostControl(ControlTask);
~~~

#### 3. Contracts

- 每个客户端占一个原子请求位；`Request(mask)` 使用 `fetch_or` 合并并设置唯一 manual-reset event。idle 边界必须遵循 `ResetEvent -> TakeRequested -> 重查 stop -> wait`，不能丢失发生在 reset 前后的请求。
- 每帧只回调请求位、上一帧 `Continue` 或 `Retry` 的客户端。固定顺序不得依赖注册顺序；帧截止时间以 `steady_clock` 控制到最多 60 FPS，pacing 期间追加的请求可并入下一批但不能提前越过期限。
- `Idle` 不自动续帧；`Continue` 只续当前客户端；`Retry` 只重试当前客户端；`DeviceLost` 调用进程级恢复回调并请求全部已注册槽；`Stop` 只用于显式进程级管线退出。普通客户端观察到 `offSignal` 时返回 `Idle` 并等待主线程同步注销，不得抢先停止共享线程。
- `Unregister` 返回前必须等待该客户端正在执行的回调退出。不得从客户端自己的回调内注销自身；释放 per-window target/context 前必须先同步注销。
- 单窗 `D2DERR_RECREATE_TARGET`、ULW 或资源创建失败返回 `Retry` 并仅丢弃该窗 target。`DXGI_ERROR_DEVICE_REMOVED/RESET/DRIVER_INTERNAL_ERROR` 返回 `DeviceLost`，共享 device epoch 只能由唯一调度线程切换。
- 设备恢复失败时保留 recovery pending 状态，后续 16,666,667 ns 节拍只重试恢复；恢复成功前禁止调用任何客户端或向旧 epoch 提交。`Stop()` 后清空请求位、控制位和 event 状态，Scheduler 重启不得回放旧请求。
- `PostControl` 用于必须在渲染线程执行的会话销毁等生命周期任务；FIFO 控制任务在每轮设备恢复重试前执行，因此 recovery pending 不得阻塞 Setting teardown。停止接收任务后投递必须失败，调用者不得无限等待未入队任务。
- 所有客户端共享 D3D11/D2D device，但各自持有 device context、target bitmap、GDI interop 和 ULW 状态。COM、配置写盘、模态确认或其他可能阻塞的业务回调必须投递到业务线程，不能在调度回调内直接执行。客户端也不得调用 `HighPrecisionWait`、`Sleep` 或条件变量做本地帧等待；最多 60 FPS 的节拍只由共享调度器负责。
- `PptPageMask()` 固定只包含四个 PageControl 槽；`WhiteboardMask()` 包含 Freeze 与两个底部 PageControl 槽。结束放映是 Bar A2 的业务按钮，不得重新增加独立 RenderPipeline client。
- 主按钮直拖激活时，Bar 仍须推进并呈现已有动画；直接 `SetWindowPos` 与 ULW 提交共用同一几何锁，交互线程只能 `try_lock`，不得等待可能包含 `GetDC/ULW` 的慢提交。拖动累计屏幕位移必须同时叠加到 ULW `pptDst`，允许动画帧改变 viewport 大小而不把窗口拉回；松手后由渲染线程把位移吸收到主按钮布局并重基准脏区快照，吸收前后的屏幕到布局转换继续扣除尚未接管的位移，保证快速下一段拖动命中和坐标连续。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 同一窗口并发多次请求 | 位合并且至少执行一次，不为请求次数重复计算 |
| 多窗口同帧请求 | 按稳定客户端顺序串行执行，每窗只计算一次 |
| 回调返回 `Continue` / `Retry` | 只在下一帧保留该客户端，并继续受 60 FPS 限制 |
| 单窗 target/ULW 失败 | 仅该窗重试；其他客户端可进入 idle |
| 共享 device 丢失 | 唯一线程恢复 device epoch，随后所有已注册客户端重建自己的 target |
| 共享 device 首次恢复失败 | 后续节拍继续恢复，成功前客户端回调计数不增加 |
| 客户端因租约或局部条件暂不能提交 | 返回 `Continue` / `Retry`，不得在回调内等待下一帧期限 |
| 请求落在 idle reset/wait 边界 | event 或二次 `TakeRequested` 至少有一路保留请求 |
| 注销时回调仍在执行 | `Unregister` 阻塞到回调退出，再允许释放资源 |
| Scheduler 停止后重启 | 旧请求/control/event 不得触发回调；新请求仍可唤醒 |
| `offSignal` 在 Setting 可见时生效 | Bar 返回 `Idle`；主线程先 drain/unregister Setting，再 join Bar/PPT 并停止管线 |
| Bar HWND 直移 | Bar 动画和 resize 继续；交互线程不等待慢 ULW，PPT 请求继续处理，松手位移由下一帧无跳变接管 |

#### 5. Good / Base / Bad Cases

- Good：Settings 可见时独自连续返回 `Continue`，未变化的 Bar/PPT 不进入回调；隐藏后全部 idle，唯一事件无限等待。
- Base：页码变化只请求四个分页窗；A2 结束放映按钮随 Bar 状态刷新，不占独立客户端。
- Bad：Bar 观察到进程退出就返回 `Stop`，导致渲染线程先退出而 Setting shutdown 永久等待 session drain。

#### 6. Tests Required

- Headless 断言请求位合并、固定顺序、并发请求不丢失、Settings 独自 60 FPS/隐藏 idle、`Continue/Retry` 局部续帧、`DeviceLost` 全槽请求、恢复失败重试、同步注销、restart 清理和相邻帧不少于约 16 ms。
- 窗口测试断言 Bar/四个 PageControl HWND 生命周期、共享底部角色和 Z 序；完整 Solution `Debug|ARM64` 构建验证 module/project 登记。
- 手工验证静止休眠、Bar 直移期间 PPT 更新、设备/DPI 切换、PowerPoint/WPS 长按与拖动；自动测试不能替代真实 Office/WPS 和显示器切换。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：一次状态变化唤醒后计算所有 UI3 窗口。
Request(AllClientMask);

// Correct：生产者只标记确实受影响的客户端。
Request(Mask(Client::PptBottomLeft) | Mask(Client::PptBottomRight));

// Wrong：客户端回调自行等待帧期限，会推迟同一批次后续 PPT 回调。
HighPrecisionWait(elapsedMilliseconds, 60.0);
return FrameResult::Retry;

// Correct：立即交还控制权，由共享调度器完成统一 pacing。
return FrameResult::Retry;

// Wrong：释放 target 后再异步等待旧回调结束。
target.Reset();
scheduler.Unregister(client);

// Correct：同步 drain 后才能释放回调使用的资源。
scheduler.Unregister(client);
target.Reset();

// Wrong：单个客户端提前终止共享线程，其他客户端无法同步清理。
if (offSignal) return FrameResult::Stop;

// Correct：客户端退出只停止自身续帧，由主线程统一注销并 Shutdown。
if (offSignal) return FrameResult::Idle;
~~~

### UI3 PageControl 与 Bar 按钮单一来源合同

#### 1. Scope / Trigger

修改 Main Bar 按钮、PPT/Whiteboard 共享分页窗口、PPT 页状态发布、分页背景、PageControl 输入/动画/碰撞或相关 headless 测试时适用。这里的“复用 Bar”表示 Main Bar 与 PageControl 调用同一行为实现；共享数值、底层 Shape/SVG/Word 或复制状态机均不满足合同。

#### 2. Signatures

`【直接确认】` Bar 层提供 Main Bar 与 PageControl 共同消费的边界：

~~~cpp
BarButtonVisualMetrics ResolveBarButtonVisualMetrics(
    BarButtonVisualLayoutKind) noexcept;
BarButtonVisualPoint ResolveBarButtonChildTopLeft(
    double parentX, double parentY,
    double parentWidth, double parentHeight,
    double childOffsetX, double childOffsetY,
    double childWidth, double childHeight) noexcept;
BarButtonVisualInheritance PrepareBarButtonVisualInheritance(
    BarButtonClass&, const BarUiInheritClass&,
    BarUiWordClass* secondary = nullptr) noexcept;
bool StartBarButtonHoverVisual(BarButtonClass&) noexcept;
bool StopBarButtonHoverVisual(
    BarButtonClass&, bool immediate, bool preserveVisual = false) noexcept;
bool UpdateBarButtonHoverVisual(
    BarButtonClass&, bool visible, bool hoverAllowed,
    double fadeDurationSeconds) noexcept;
void SetBarButtonPressedVisual(BarButtonClass&, bool pressed) noexcept;
void RetargetBarButtonInteractionVisual(
    BarButtonClass&, bool visible, bool enabled, bool selected,
    double durationSeconds) noexcept;
bool DrawBarButtonVisual(
    BarUIRendering&, ID2D1DeviceContext*, BarButtonClass&,
    const BarUiInheritClass&, const BarButtonDrawOptions& = {});
bool DrawBarBackgroundVisual(
    BarUIRendering&, ID2D1DeviceContext*, BarUiShapeClass&,
    const BarUiInheritClass&, RECT* targetRect = nullptr, bool clip = false);
bool BarUiRoundedRectContainsPoint(
    int mx, int my, double zoom, double leftDip, double topDip,
    double widthDip, double heightDip, double radiusXDip,
    double radiusYDip, double epsilon = 1e-6) noexcept;

void NotifyBorderCursorSurfacePointerEntered() noexcept;
void NotifyBorderCursorSurfacePointerLeft() noexcept;
void PublishBorderCursorSurfaceBounds(
    unsigned int index, const RECT&, bool visible) noexcept;

WorkspaceMode ResolveWorkspaceMode(
    Surface, const PptState&, const WhiteboardState&) noexcept;
WorkspaceInputPolicy ResolveWorkspaceInputPolicy(WorkspaceMode) noexcept;
bool IsPptEndPage(int currentPage, int totalPage) noexcept;
DirectionContentPolicy ResolveDirectionContentPolicy(
    WorkspaceMode, bool next, bool nextIsAdd,
    bool nextIsEndShow = false) noexcept;
double ResolveDirectionIconAngle(
    Surface, WorkspaceMode, bool next, bool nextIsAdd,
    bool nextIsEndShow = false) noexcept;
PptState ResolveRuntimePageControlLayout(
    const RECT& monitor, float dpiScale, PptState) noexcept;
ResolvedSurfaceLayout ResolveSurfaceLayout(
    Surface, const RECT& monitor, float dpiScale,
    const PptState&, const WhiteboardState&) noexcept;
PptDragPresentationTarget ResolvePptDragPresentationTarget(
    const ResolvedSurfaceLayout& previous,
    const ResolvedSurfaceLayout& candidate,
    LONG presentationOutsetPixels) noexcept;
bool IsPageControlFrameRevisionCurrent(
    uint64_t frameRevision, uint64_t currentRevision) noexcept;

struct PptKeyboardRepeatTiming {
    std::chrono::milliseconds initialDelay;
    std::chrono::milliseconds repeatInterval;
};
PptKeyboardRepeatTiming ResolvePptKeyboardRepeatTiming(
    unsigned int keyboardDelay, unsigned int keyboardSpeed) noexcept;
bool ShouldTriggerPptLongPressRepeat(
    bool repeatEnabled, std::chrono::steady_clock::duration held,
    bool hasRepeated, std::chrono::steady_clock::duration sinceLastRepeat,
    const PptKeyboardRepeatTiming& timing) noexcept;
std::chrono::steady_clock::time_point ResolvePptLongPressRepeatAnchor(
    std::chrono::steady_clock::time_point lastRepeat,
    std::chrono::steady_clock::time_point now, bool hasRepeated,
    const PptKeyboardRepeatTiming& timing) noexcept;

inline constexpr std::uint32_t PageControlTabletGestureStatusFlags =
    0x00000001 | 0x00000008 | 0x00000100 |
    0x00000200 | 0x00010000;

struct PageControlTouchLockState {
    std::uint32_t id;
    bool active;
    bool primary;
};
enum class PageControlTouchMessage : std::uint8_t {
    None, Down, Move, Up
};
struct PageControlTouchDecision {
    PageControlTouchLockState state;
    PageControlTouchMessage message;
    bool cancelPrevious;
    bool fallbackLocked;
};
PageControlTouchDecision ResolvePageControlTouchSample(
    PageControlTouchLockState current, std::uint32_t id,
    bool down, bool move, bool up, bool primary,
    bool batchHasPrimary, bool fallbackLocked) noexcept;

bool ShouldContinuePageControlFrame(
    bool targetVisible, bool transitionDeadlineActive,
    bool boundsAnimationActive, bool sceneAnimationActive,
    bool longPressActive) noexcept;
~~~

EndShow 生产状态补齐后的 `Inkeys.UI.Ppt` 实施合同为：

~~~cpp
struct PageStatePublication {
    int currentPage = -1;
    int totalPage = -1;
};
constexpr PageStatePublication ResolvePageStateForPublication(
    int readyCurrentPage, int readyTotalPage,
    int observedCurrentPage, int observedTotalPage) noexcept;
enum class PptDirectionAction : std::uint8_t {
    PreviousPage, NextPage, EndShow
};
constexpr PptDirectionAction ResolvePptDirectionAction(
    bool next, int currentPage, int totalPage) noexcept;
constexpr bool IsPptDirectionActionRepeatable(
    PptDirectionAction action) noexcept;
~~~

`ResolveRuntimePageControlLayout` 不得接收 Bar/MainButton 障碍或 Whiteboard 位置状态。PageControl 可以提供 surface 内控件拓扑和锚点，但标准按钮内部几何、交互、动画、绘制和 damage 只能来自上述 Bar 边界。

#### 3. Contracts

- Main Bar 必须先迁移到共享 Bar 按钮入口，随后 PageControl 才能接入。若 Main Bar 仍使用 RenderLoop/Interaction 局部算法而 PageControl 单独使用新 helper，仍是两套实现。
- 每个分页 HWND 独立持有 D2D target/`BarUIRendering`、Previous/Page/Next `BarButtonClass` 和输入状态；独立资源是正确边界，不允许复制按钮行为。Bottom 的 Previous/Page/Next/More 实例及列表成员跨 `PptCompact/WhiteboardExpanded` 稳定，More 使用共享隐藏布局与绘制层级收拢到 Next 下方并渐隐，不能清空目标矩形；退场不命中且反向重入续接当前呈现值。Middle 只切 `Hidden/PptCompact`。
- `BarPageSurface` 只保存每窗分页列表、固定属性数组、PPT-only DragHandle 和公共指针状态的路由；不得保留动态 Widget 构建或独立视觉算法。标准按钮内部位置、hover/press、内容转换、draw、圆角 hit 和动画值调用上述 Bar 入口，适配器只把共享结果映射到本窗 damage/present。
- 分页标准按钮的 SVG/PNG 位图与 renderer 字体/遮罩缓存按目标及设备代次持有，不构造完整主栏 owner。显式 `ReleaseDeviceResources()` 与成功的 epoch/target 隐式重建须清理相应设备资源；创建失败保留旧资源和待绘制 damage。只有内容、尺寸、颜色或设备域变化才重建对应资源，普通帧不得反复失效。
- PPT 横纵分页默认统一使用白板同源的 `70x70 DIP` `2x2` 按钮：Bottom主体 `240x80 DIP`，Middle主体 `80x240 DIP`，保留 `10 DIP` Drag slot及 `5 DIP` 外边距/按钮间距。标准按钮尺寸与槽位来自共享 Bar 尺寸源；当前可见项及 Drag 槽的列表布局共同决定按钮目标和背景目标宽度，背景与按钮使用同一布局批次推进。分页仅在拓扑/尺寸目标变化时重算列表并缓存隐藏优先顺序；PageControl 通过非分配几何读取跟随背景当前尺寸，不维护独立宽高动画；`BarPptCompactPaginationEnabled=false` 禁用原32.5DIP路径，原紧凑布局、字体与分支继续保留。Drag 是 divider lane，与相邻 Arrow 直接相接、不增加第三个间距。Whiteboard 主体左侧 `305x80 DIP`（Previous/Page/Next/More）、右侧 `230x80 DIP`（Previous/Page/Next）；左侧菜单另按当前动画向上扩展同一 HWND 包络。
- 分页背景的主题、圆角、边框、第一/第三光源、draw 与 dirty 直接复用 Main Bar 背景实现；PageControl 只提供子控件联合外框目标。外框必须读取 `BarMainBarCornerRadiusDip`，按钮必须读取 `BarButtonCornerRadiusDip`，边框和内边距也读取主栏单一来源（当前为 `8/4/1/5 DIP`）；禁止 PageControl 镜像常量。分页外框和 Main Bar 外框都保持背景 Shape 默认 `frameCursorLightIntensityScale = 1.0`，不得误用按钮的 `BarButtonCursorLightIntensity`。第三光源必须消费 Main Bar 发布的同一屏幕坐标、半径、强度和可见性快照；Surface 不得从本地 `WM_MOUSEMOVE` 创建淡入/淡出状态。
- `DrawBarButtonVisual` 的显式 `inherit` 是按钮父坐标唯一真值。入口必须先同步 `button.inhX/inhY`，再通过 `ResolveBarButtonChildTopLeft` 解析 SVG、主文字和次文字；Main Bar dirty 与最终 draw 调用同一继承准备入口。禁止读取默认或上一帧父缓存后再把显式 `inherit` 只用于背景 Shape。
- 四个 PageControl HWND 与 Bar HWND 共同构成第三光源的实际消息接收窗口集合。真实鼠标进入分页 HWND 时只通知 Main Bar 激活既有状态机；离开时由 Main Bar 根据 `WindowFromPoint` 决定 `Inside/Grace`。Raw Input 和 5 秒 timer 始终以 Bar HWND 为 owner；分页只发布成功呈现的屏幕边界用于 240px 邻域裁剪，隐藏或提交失败不得发布假边界。
- Page 在三种形态中是同一实例。PPT横纵分页与Whiteboard默认均使用 `PageTwoTwo` 标准 `2x2` 双行槽；原紧凑横向整体测量及竖向页码分支保留，由同一紧凑开关禁用。PPT 当前页或总页为负数时显示 `-`/`/-`，Bottom/Middle 分别限制显示到 `9999/999`。禁止分页专用绝对 offset。Page 数值使用公共即时内容更新：取消旧文字 transition、同帧替换主/次字符串，不调用 `TransitionToString`；PPT/WB 模式变化中的文字槽、字号与按钮几何仍按共享动画从当前值连续重定向，不能因替换字符串而跳到终态。PPT Page 在系统拖动阈值内短按并仍命中时调用既有 `ViewShow` 打开预览，Whiteboard Page 保持 no-op；两者都使用标准 hover/press。Arrow/Add SVG 与语义标签继续使用共享内容转换，不得因即时数字策略被关闭动画。
- 普通 Previous/Next 始终保留同一 `barMore` SVG 对象，只动画尺寸/位置/角度及标准按钮标签；不得为同资源启动替换。`barMore` 的 `0°` 基准朝上，`MiddleLeft/MiddleRight` 必须统一使用 Previous `0°`（向上）、Next `180°`（向下），不得按左右侧镜像；Bottom PPT/Whiteboard 继续使用 Previous `-90°`、Next `90°`，Whiteboard Add 使用 `0°`。只有 Whiteboard Next 的 Arrow/Add 语义真实变化时，才用共享内容转换切换 `barMore`/`barAdd` 与“右翻页”/“加页”，并与几何同批并行推进。
- EndShow A2 与 PageControl 必须共用主栏 `UI/barEndShow` SVG；深色主题由 `TextPrimary` 着色为白色，资源本身遵循主栏 `24x24` 画布、圆角端点/连接和相近线宽，不得继续用固定黑色 `ppt3` PNG。PageControl 仅在 UI 发布态 `totalPage > 0 && currentPage < 0` 时视为结束放映页：Bottom/Middle 的稳定 Next 实例通过现有 Animated 中点内容转换把 `barMore` 切为正向 `0°` 的 `barEndShow`，状态离开结束页后反向切回各 surface 的箭头角度；未知 `-1/-1` 不得误判。同一快照必须让 `ResolvePptDirectionAction` 把结束页 Next 解析为 `EndShow`，并通过 `PptCallbacks::endShow` 直接向 PPT 业务线程退出队列投递，不经过主栏确认 dispatcher；仅主栏 EndShow 点击确认。有效页仍解析为 `NextPage`。会话身份/失败/输入门禁见 [PPT 当前合同](../ppt-interop/native-session-ui3.md)。
- DragHandle 是 PageControl 自有且仅 PptCompact 存在的 shape/hit region，不属于 Bar 按钮，无 hover/press/click/selected/content 视觉。PPT 成对拖动可从 DragHandle、Page 和未被 Previous/Next 占用的真实圆角背景开始；透明 presentation margin、圆角外像素和 diffuse 光晕继续 `HTTRANSPARENT`，不能把整个矩形 HWND 当作背景。Previous/Next 永远是纯按钮。Page 按下先产生标准 press，超过 `SM_CXDRAG/SM_CYDRAG` 对应系统阈值后取消 press 并转为拖动，阈值内且仍命中 Page 时抬起调用 `ViewShow`，不写位置。Up 必须先验证来源归属，再使用该事件的完整屏幕点按同一阈值采纳最终候选；只有 Up 才越阈值的 Page/background 也按最终拖动处理，不能误触预览。Whiteboard 不创建 DragHandle 或 drag candidate。
- Bar 与 PageControl 的原生 Down/Move/Up 均按值进入唯一交互 worker；HWND owner 固化采样时间、来源与精确捕获票据，PageControl 只读取成功呈现快照完成准入，业务由 worker 推进。只有连续/拖动路径准备私有 Raw 补充，原生 Up 与 Raw Up 按同一归属去重；Raw 不可用不能阻断普通点击，未过阈值的 Raw Up 不能点击。来源/失败/时序合同见 [ui-pointer-session.md](ui-pointer-session.md)。
- PPT 普通 Arrow 的 Pointer Down 立即投递一次 Previous/Next，并在该次 Down 边界通过 `SPI_GETKEYBOARDDELAY` / `SPI_GETKEYBOARDSPEED` 快照系统键盘重复设置；一次按压中途不得重新读取或改变节奏。`keyboardDelay` 先限制到 `0..3`，首次重复延迟为 `250ms * (delay + 1)`；`keyboardSpeed` 先限制到 `0..31`，按 Windows 文档给出的 `2.5..30 次/秒` 线性换算并取最近毫秒作为后续重复间隔。任一 SPI 查询失败时仅该字段回退到 `delay=1` / `speed=31`，按压仍可继续。只有 PPT 快照发布的 `longPressEnabled=true`、指针仍命中同一 Arrow 且所属指针会话有效时才重复。首次实际重复以当前帧锚定，保证第二次至少等待完整 interval；第二次起以计划 deadline 推进 repeat anchor，避免 60 FPS 把 `34ms` 等间隔量化成固定 `50ms`。若实际帧已落后计划 deadline 至少一个完整 interval，则只触发一次并以当前时刻重新锚定，不追赶积压。既有 COM outstanding gate 继续合并未完成普通页命令。结束页 Next 在 Down 时只调用一次 EndShow dispatcher，不建立 repeat timing；普通 Next 按住期间若 UI 状态进入结束页，则下一次合法 repeat 改投一次 EndShow 并立即清除 tracking。移出 Arrow、Pointer Up、会话/capture cancel 或 workspace 切换立即停止。Whiteboard 不继承 long-press；键盘 Hook 与 wheel 不得合成 Arrow pressed 闪按，真实 Pointer 的标准 hover/press 保留。
- 每个有效 PPT Pointer Down 都调用 `PromotePptWindow(surfaceRole)`，把最近交互窗放到其他 PPT 窗口之上、Bar 正下方，不激活窗口；纯平移继续使用 `SWP_NOZORDER`。
- Enter 开始即 cancel capture、关闭 DragHandle hit；Window Service 撤销必须广播 Drawpad、四个 PageControl HWND 与 Bar。PageControl 的 `WM_CANCELMODE` 在 `renderTransactionMutex` 内清 pointer/drag/touch 状态，释放该锁后才调用可能重入 `WM_CAPTURECHANGED` 的 `ReleaseCapture`。Down 的 `SetCapture` 同样必须在该非递归锁外：先完成旧 capture 的同步取消，再在短锁内复核本次 gesture/session/epoch/mode 与命中并建立新 tracking；长按依赖的 `GetCapture` 判断位于捕获完成之后，不能用递归锁掩盖旧 Cancel 覆盖新 tracker。背景、三枚按钮、Drag slot/opacity、SVG/文字和 HWND bounds 使用同一批次从当前成功呈现值重定向。PPT 底栏可见时重定向到 Whiteboard 可见描边外沿距屏幕底边 `5 屏幕 DIP` 的固定角落并形变；同屏默认位置和局部倍率为 1 时保持底部 Y；不可见时直接用最终几何渐显。Exit 反向返回 PPT 最新运行时位置，稳定 PptCompact 成功呈现后才恢复 DragHandle hit。
- Surface 首次在 Hidden 配置时必须把当前/目标透明度直接提交为 `0`，不能只留下尚未推进的零时长 target；随后显示必须形成真实 `0 -> 1` 渐显。可见或退场 Surface 的续帧由 Scene/bounds 实际动画和待绘制 damage 决定，不增加截止时间锁存；`inputLocked` 逐帧消费实际布局动画及工作区切换事实。布局结束帧自行解除布局锁，各动画与 damage 收敛后进入 Idle，不得依赖 Bar 鼠标、共享光源或其他客户端消息再次唤醒。
- 四个 PageControl HWND 创建时继续统一执行 `RegisterTouchWindow(hwnd, 0)` 与 `DisableEdgeGestures(hwnd, true)`；WndProc 还必须对 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回与 Bar 一致的 press-and-hold、pen tap feedback、Touch UI 和 flick 禁用标志。`WM_TOUCH` 每批优先锁定带 `TOUCHEVENTF_PRIMARY` 的触点；整批没有 primary 标志时锁定第一个 DOWN 作为 fallback。同批处理必须先保序消费 primary、再消费 non-primary，防止旧 fallback 的 Up 在新 primary 替换 cancel 前触发 click。活动触点的 id、是否 primary 和最后 client 坐标必须锁存；新合格 DOWN 替换旧 id 时先取消旧 press/capture（等价于 cancel up，且不得触发 click），再建立新 Down。只有锁定 id 的 Move/Up 可转译，Up/cancel/workspace 切换必须清空锁存；系统生成的兼容 mouse 副本仍由现有来源签名过滤。
- PPT 碰撞只包含 bottom/middle pair 与屏幕越界。手动拖动不推动另一 pair；自动纠偏 bottom 优先、middle 最近位置/极端运行时缩放回退，且不写保存配置。Bar HWND、MainButton、主栏移动和 Whiteboard 均不参与求解或唤醒。
- 同一 PPT publication/direct-move/display/device 代的四个 PageControl surface 必须消费同一保守呈现预算：已启用 surface 的最大真实 Scene outset 与最小 bitmap limit。各自旧 Scene scale/context/backing 不能让同一 pair 得出不同最终倍率；旧 backing 超出当前设备上限应缩到上限，若目标完整包络仍超限则拒绝该帧并保留重试，不能静默裁掉左/右控件。合法 Side 屏外入场起点只在动画结束后检查最终主体是否入屏；自动适配不写用户位置/倍率偏好。
- `ConfigureSurface` 的 Scene 配置、内容更新和 bounds 均成功后才推进 observed revision；ULW/资源/GetDC/ReleaseDC/EndDraw 失败在原调用点保存 HRESULT/`GetLastError`，同阶段重复失败限频并有界退避，恢复无需新的鼠标输入。PPT Window Service 的 Show/Hide/SetBounds 成功必须读回真实可见性/RECT；任一必需 surface 未完成呈现与窗口提交，PageControl UI-ready mask 不能提前确认半对。`INKEYS_PAGECONTROL_PRESENT_TRACE=1` 仅在排障时开启同代四窗阶段与 HWND 日志，默认关闭。
- PageControl 继续拥有 stable backing、logical/presentation 映射、direct-move revision、ULW/Window 提交与调试覆盖层事务。共享 Bar 运行时返回 animation/damage，不直接调用 Window Service；隐藏生命周期不得被光源动画无限延长。渲染线程可在 `presentationMutex` 内同步等待 Window Service owner，因此 owner WndProc 绝不能阻塞等待该锁。每次拖动采样必须先按原始 drag 起点计算并发布 latest-wins 绝对候选；发布本身不得请求 pair。直移目标必须分别解析上一 feasible layout 与当前 candidate layout，确认尺寸、scale、mode 仅发生平移，再由 candidate logical bounds 加减当前不变的 presentation outset 得到绝对 HWND 目标；不得依赖上一候选已成功提交，也不得为取得目标调用 `ApplySceneBounds`。
- `presentationMutex.try_lock()` 成功后，owner 依次对两个 HWND 调用 `SetWindowPos`；第一窗失败直接保留 pending，第二窗失败必须把第一窗回滚到 original，并记录 first/second/rollback 阶段及 Win32 error。普通最终目标两窗成功后才对两个 `SurfaceState.bounds` 调用 `SetBoundsDirect`、提交 mailbox/direct-move revision，并立即发布两个 cursor-light 接收边界；mailbox/layout/bounds payload 必须先完整写入，再以 release store 发布单调且由 `dragCommitMutex` 串行的 revision。普通完整直移热路径不得调用 `ApplySceneBounds`、产生 Scene damage 或请求 pair。吸附动画的中间直移只推进 direct-move 过期帧屏障，不确认最终 pair，并显式请求 pair 续帧。普通拖动只有锁竞争、非纯平移或窗口移动失败走 pair render fallback。较新的候选可以覆盖旧候选，但 fallback 必须保留最新候选，不能依赖另一条可能被合并的 `WM_MOUSEMOVE`。渲染帧以 acquire 读取 revision，取得 `renderTransactionMutex` 后、`ConfigureSurface/PresentScene` 前必须复核，窗口提交后继续第二次复核；任一过期帧均返回 `Retry`，不得先以旧 ULW 拉回 HWND。松手在合法终点采样后只 `RequestAll` 一次让 Scene/layout 吸收最终位置，并且不得在最新候选成功提交或明确回滚前清除 pending 所有权。正在拖动及 released pending 的同 pair 非吸附纯位置 fallback 必须直接落定，包含同组未设 `dragging` 的另一面；目标相同也必须结束旧位置缓动。只有明确的中轴吸附 Y 修正允许有界短动画，不能恢复普通拖动的旧位置缓动。render 只有实际 logical/presentation 像素几何与本帧拟合目标一致才推进该面 commit，不能以动画中间帧凑满两面 mask；正常显隐、倍率和工作区形变仍沿用原动画。采集 pair ownership 与快照时保持 `dragCommitMutex -> snapshotMutex`，禁止新增反向锁序。
- 中间竖向 pair 的主体中心吸附屏幕水平中线；`middlePairHeight=0` 即居中，不另加边距/阴影补偿。复用 `BarBottomDockThresholdDip` 的20屏幕DIP，同时要求 `abs(middlePairHeight)` 与 `middlePairWidth` 均不超过阈值，含等号；后者已是主体距对应屏幕侧边扣除既有缩放间隙后的额外距离，不读取阴影/outset，也不强制 X 归零。阈值仅乘同一布局屏幕的归一化DPI，不乘控件倍率。先裁剪原候选，再尝试中心候选；中心碰撞保留可行原候选，否则上一 feasible。仅吸附 Y 修正复用现有 UI3 PageControl 曲线/RenderPipeline 做140ms过渡，两面共享起点时刻和单调采样进度，owner采样已推进后不得被较早的RenderPipeline批次frameTime回退；X及时跟随；重复 Move、页码发布和 Up 不重启，同目标完成后不重播，released pending等两面真实几何到位才提交。过渡不新增 inputLocked，离开任一阈值、真正Cancel、新gesture/session/epoch及模式/显示布局失效可打断；Up后无活动pointer的capture取消不能清掉刚释放的pending吸附。只在用户拖动/合法Up采样吸附，普通页码发布不得重写已存偏移。
- 当前 COM→Draw3→页码提交→输入开放、session/target revision、采样/唤醒、位置持久化及有效缩放合同见 [native-session-ui3.md](../ppt-interop/native-session-ui3.md)。Ppt 生命周期不得再由 presentationVisible 派生；白板最小化按钮的禁用视觉与业务门禁必须使用独立活动会话事实，不能把白板期间隐藏分页当作放映结束。图标/文字沿共享 UI3 0.30 禁用目标过渡，禁用时无 hover/press/click，活动会话变化须请求分页刷新；成功呈现前不发布 ready，成功页码提交前不开放新页输入。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Main Bar 与 PageControl 对相同 button request/state/time 结果不同 | 测试失败；修共享实现，禁止在 PageControl 加补偿常量 |
| PPT Arrow 进入 Whiteboard Arrow | 保留同一 `barMore` SVG与标准2×2按钮，从当前尺度/位置连续重定向，标签不重建按钮 |
| Whiteboard Next Arrow↔Add | 同一按钮在同一批次转换 SVG、标签与几何；语义未变不重启 |
| raw COM 为 `-1/有效总页数`、ready buffer 为 `-1/-1` | UI 解析为 `-1/有效总页数`；四个 surface 的 Next 动画切为 `barEndShow`/`0°`，动作解析为单次 EndShow，buffer 不变 |
| raw COM 恢复有效、Draw3 尚未 ready | UI 先离开 EndShow 语义并切回各 surface 的 `barMore` 角度；页码保持未知直到 ready |
| raw COM/发布态为 `-1/-1` 或 `0/有效总页数` | 不进入 EndShow 语义；不得沿用上一次 EndShow 目标 |
| Enter 时 DragHandle 正在 capture | 立即 cancel、关闭 hit；slot/opacity 从当前值收拢 |
| Exit/Enter 中途反向 | 背景、按钮、内容、DragHandle 和 HWND bounds 从当前插值值重定向 |
| Whiteboard 收到 drag/wheel/long-press | 不捕获、不持久化、不投递对应业务；普通 click/tap 保留 |
| 用户拖动 pair 接近另一 pair | 命中 pair 停在最近可行位置；另一 pair 不移动 |
| 显示空间不足 | bottom 保留；middle 先找位置、再仅降低运行时 scale；保存状态不变 |
| 同一发布代左右旧 Scene outset/context/backing 不同 | 四窗从同一 group budget 求解，左右最终倍率与主体位置镜像一致；合法 Side 入场中间帧不作终态错误 |
| 一侧 ULW/资源/SetBounds/Show 暂时失败 | 该侧不提交 UI mask；记录原始阶段/错误并有界重试，解除故障后无需用户输入自行完成，另一侧成功不冒充整对成功 |
| Main Bar 移动或尺寸变化 | 不请求 PageControl 碰撞重算，不改变分页位置 |
| PPT 当前页或总页未知/超出显示上限 | 使用 `-`/`/-`；Bottom/Middle 分别限制到 `9999/999`，不改变业务页码 |
| PPT 页码按钮点击 | 产生标准 press，并在阈值内且仍命中时调用 `ViewShow`；不写位置 |
| Whiteboard 页码按钮点击 | 只产生标准 press，业务 no-op |
| Page 按下后在系统阈值内抬起 | PPT 调用预览，Whiteboard no-op；两者都不写位置 |
| PPT 普通 Arrow 按下并持续 | Down 立即一次；配置开启时按该次 Down 快照的系统键盘 delay/rate 重复；移出/Up/cancel 后停止，不补发迟到节拍 |
| 结束页 Next 按下或普通 Next hold 后进入结束页 | 直接投递带会话的退出业务，不弹主栏确认；Down 单次且不 repeat，hold 转态下一次合法 repeat 投递一次后停止 |
| SPI 返回失败或越界键盘值 | 失败字段分别回退 `delay=1` / `speed=31`，输入值限制到 Windows 合法范围；不得恢复 `400ms + 15ms` 固定节拍 |
| Page 或非箭头背景移动超过系统阈值 | 取消 Page press 并开始成对拖动；Previous/Next 永不转换 |
| 指针位于透明 margin 或背景圆角外 | 返回 `HTTRANSPARENT`；不得启动背景拖动或截获下层窗口输入 |
| 纯平移且呈现锁与两窗直移均成功 | 两窗顺序直移；只更新 bounds/mailbox/revision/接收边界，不调用 `ApplySceneBounds`、不产生 damage、不请求 pair |
| render 正持有呈现锁并同步等待 owner | owner 不等待；保存最新绝对拖动候选并请求命中 pair，锁释放后即使没有新的 `WM_MOUSEMOVE` 也必须收敛到该候选 |
| 第一窗移动失败 | 保留 pending 并请求 pair；日志包含 first 阶段和 Win32 error |
| 第二窗移动失败 | 第一窗回滚 original，保留 pending 并请求 pair；日志包含 second/rollback 阶段及两个 error |
| COM 页状态已变化、Draw3 尚未到达 | 请求绝对页并等待 runtime revision；PageControl 继续显示上一 buffer 页码 |
| Draw3 到达目标页 | runtime revision 唤醒状态线程，同轮推进 buffer 并发布即时数字文本 |
| render 已取 frame revision、随后 owner 完成直移 | 取得 render transaction 锁后、`ConfigureSurface/PresentScene` 前即 `Retry`；窗口提交后的第二次 revision gate 仍保留 |
| 目标隐藏但光源仍动画 | 固定退场后隐藏 HWND，不能由光源延长生命周期 |
| 启动时 Hidden Surface 随后首次显示 | 透明度从真实 `0` 渐显；transition deadline 内持续续帧并锁输入，到期后无外部 Bar 消息也自行解锁 |
| `WM_TOUCH` 批次没有 primary 标志 | 只锁定第一个 DOWN，后续非锁定触点不产生 mouse 消息 |
| 新 primary DOWN 替换活动 id | 同批先处理 primary；旧 press/capture 先 cancel 且不触发 click，再对新 id 发送一次 Down；迟到的旧 fallback Up 被忽略，后续使用新触点最后坐标；无 primary 时的额外 fallback DOWN 继续忽略 |
| 分页按钮位于非零 Surface 原点 | 按钮背景、SVG、主文字和次文字从同一显式父坐标解析，不得回落到 `(0,0)` 或上一帧缓存 |
| 鼠标从分页 HWND 移到画布 | 进入同一个 `Grace`，区域外坐标继续由 Bar Raw Input 发布；绝对 5 秒截止不被移动重置，随后从当前强度淡出 |

#### 5. Good / Base / Bad Cases

- Good：Main Bar 与 PageControl 各自拥有按钮实例/target，却共同调用一套 layout/interaction/advance/draw/hit/damage；PPT→Whiteboard 只改变稳定实例目标。
- Good：连续纯平移只顺序移动两个 HWND 并发布一条候选日志；Scene 不产生整窗 damage，松手时才由一次 `RequestAll` 吸收最终布局。
- Good：唯一一条 `WM_MOUSEMOVE` 到达时呈现锁正被占用；WndProc 保存最新绝对候选并唤醒 pair，锁释放后无需第二条移动消息也能完成直移。
- Good：用户键盘设置为 delay 1/speed 31 时，触摸或鼠标长按在约 `500ms` 后首次重复，后续约 `33ms` 一次；轻微帧量化保留计划相位，渲染或 COM 严重迟到只跳过该次机会，不追赶积压。
- Good：最后一页进入真实 EndScreen 时先为同文稿附加页取得独立 target/ready，成功 Present 后才发布 `-1/12`；PageControl 的稳定 Next 转为 EndShow，并直接投递原有非确认退出业务。主栏 EndShow 点击才进入 A2 确认。返回真实页同样等待对应画布 ready 再发布数字和开放输入。
- Base：PPT横纵使用三枚标准 `2x2` 按钮及原拖动槽；Whiteboard左侧四枚、右侧三枚 `2x2`，两工作区分页UI均跟随主栏全局尺度。普通Arrow复用同一图标，Add/EndShow语义变化才转换内容。
- Bad：`BarSurfaceScene` 继续维护 local hover/pressed，PageControl 设置 `18 DIP` icon 或 `±13 DIP` text offset，或者复制主栏曲线后声称“复用”。
- Bad：把 Whiteboard 拖动条叠在 Page 上、让 Whiteboard 继承 PPT wheel/long-press/persist，或把 Bar HWND 传入碰撞求解。
- Bad：`presentationMutex.try_lock()` 失败后直接丢弃本次候选，并假定下一条 `WM_MOUSEMOVE` 一定会补偿；消息合并或松手会让控件保持原位。
- Bad：每个 candidate 先 `SetBoundsDirect + ApplySceneBounds` 再求 HWND 位置，或直移成功后固定请求 pair；两者都会把纯平移放大成整窗 damage 与约 60 FPS render fallback。
- Bad：Scene 动画提前报告 inactive 后返回 Idle，但 `inputLocked` 仍等未来 deadline；只有 Bar 下一次鼠标消息碰巧唤醒 PageControl 才恢复输入。

#### 6. Tests Required

- 共享 Bar 等价测试必须让 Main Bar 与 PageControl 直接调用同一导出入口，逐项断言 `oneOne/twoOne/twoTwo` 外框、内容槽、hover/press 时间样本、press transform、hit 和 damage；禁止在测试中复制产品公式。
- `ppt_ui_tests` 必须直接调用生产 `ResolvePageStateForPublication`，覆盖有效 ready 页、结束页、未知态、非法零页、结束页恢复但 Draw3 未 ready、恢复 ready；PageControl headless 再覆盖稳定实例 ID、横/竖页码测量、背景合同、普通 Arrow SVG 身份不变、Arrow/Add 与 Arrow/EndShow 同批中点转换目标及四 surface 资源/角度矩阵、`PreviousPage/NextPage/EndShow` 动作解析、EndShow 不可重复、DragHandle 时序、首次渐显、反向重入和有限退场。
- 输入矩阵覆盖 PPT DragHandle/Page/非箭头背景 drag、Arrow 拒绝 drag、系统阈值、Page 预览、long-press/wheel/persist，以及 Whiteboard 对 drag/wheel/long-press/persist 的负向断言和普通 click/tap 正向断言。长按测试必须覆盖 delay `0..3`、speed `0/31` 和越界限制、默认回退 `500ms/约33ms`、Down 立即一次、首次 deadline 前无重复、deadline 到达后一次、后续 interval、`34ms` 在 60 FPS 下保留计划相位、落后一个 interval 时跳过积压、配置关闭与移出/Up/cancel 停止；静态确认每次有效 Arrow Down 只快照一次 SPI、不存在 keyboard/wheel 合成 press，并确认有效 PPT Down 调用 `PromotePptWindow`。触摸纯测试覆盖 primary、无 primary fallback、多指忽略、活动 id 替换 cancel、新 id Move/Up、替换后旧 fallback Up 被忽略和 cancel 清理；静态确认同批先保序处理 primary、四窗注册触摸、禁用边缘/Tablet 手势且兼容 mouse 副本仍被过滤。拖动提交仲裁必须断言 publication 不自动请求 pair、纯平移由 candidate logical bounds 与稳定 outset 生成绝对 target，并可注入一次呈现锁竞争，确认 fallback 显式请求 pair且只保存最新候选；还要覆盖两窗 commit、第二窗失败的第一窗回滚、松手 ownership，以及进入 `ConfigureSurface/PresentScene` 前和窗口提交后的两道 stale revision gate。
- 生命周期 headless 必须让 Hidden→Visible 在没有任何 Bar/光源外部请求时推进到 deadline 后一帧，断言 deadline 活跃期间续帧且输入锁定、到期自行 Idle/解锁；源码审查初始 Hidden 使用直接透明度 `0` 而不是未消费的零时长动画 target。
- Animation headless 通过生产共用 `ApplyBarImmediateContentUpdate` 覆盖旧 content transition 取消、current/target/pending 同事务替换及取消后不发生旧关键帧回写；实际离屏回归确认模式切换同一时间戳页码 glyph 区域不跳变，后续几何连续推进，Arrow/Add 仍走公共内容转换。
- Draw3 headless 通过 Host 实际持有的 `HostRuntimeRevisionSignal` 覆盖 current page/page count 变化推进 revision、唤醒 waiter、稳定值不唤醒及 stop 释放等待；源码审查两个 document observer 都调用该入口。放映期 native 有界 16ms 观测缓存状态、旧 DLL 兼容路径 50ms；正常页及独立 EndScreen 都等待 Draw3 成功 Present 和同目标 UI 回执，`-1` 仅是显示哨兵。
- PageControl 输入测试覆盖圆角背景门禁策略；背景命中必须读取最近成功呈现的不可变快照，并区分主体与退场 popup。Window Service capture 撤销包含四个 PageControl 角色，原生释放由窗口 owner 按精确票据执行，不在呈现锁内释放。
- 碰撞覆盖手动 pair 最近可行位置、bottom 优先/middle 回退、极小屏运行时 scale、输入/保存快照不变，并静态断言无 Bar obstacle 参数/查询/通知。
- Headless 直接断言非零父原点下的标准按钮子内容坐标，以及屏幕光源点到 Surface presentation 点的映射；静态审查分页不再调用本地 cursor-light prepare/reset，且四个 HWND 的进入/离开与成功呈现边界只通知 Main Bar 全局状态机。
- 回归 EndShow/A2、四窗口生命周期、owner/Z 序、旧 JSON、COM/WPS、`PptInfoStateBuffer` 与页级墨迹；执行完整 `Debug|ARM64` Solution 构建和 ARM64 `--no-window` 测试。
- 大倍率四窗回归须用独立屏外 HWND 入口执行生产 `RenderSurface→ULW→Window Service`，在 DPI 96/144/192/240、bottom-only/side-only/both、倍率大→小→大、白板覆盖/返回下检查最终主体像素/命中/真实 HWND bounds；单侧注入失败时验证未 ack、撤销故障后自行恢复。无 HWND Scene 离屏与同一公式纯函数测试不能替代该事务。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：PageControl 复制一份主栏按钮行为，并用专用数值修视觉差异。
widget.hover = hit;
widget.button.pct.SetTar(widget.hover ? 0.18 : 0.0, 0.24);
widget.icon.SetWH(18.0, 18.0);
ppt = ResolveRuntimePageControlLayout(monitor, dpi, ppt, whiteboard, &barBounds);

// Correct：surface 只给目标/事件，Main Bar 与 PageControl 调用同一 Bar 运行时。
const auto metrics = ResolveBarButtonVisualMetrics(layoutKind);
StartBarButtonHoverVisual(button);
SetBarButtonPressedVisual(button, pressed);
RetargetBarButtonInteractionVisual(
    button, visible, enabled, selected, durationSeconds);
DrawBarButtonVisual(renderer, context, button, inherit, drawOptions);
ppt = ResolveRuntimePageControlLayout(monitor, dpi, ppt);

// Wrong：Surface 从本地鼠标消息创建第三光源，离窗后失去全局坐标和 5 秒期限。
renderer.PrepareSurfaceCursorLight(dt, localCursor, hoveredButton);

// Correct：分页只登记接收窗口和呈现边界，绘制消费 Main Bar 的最终共享光源快照。
NotifyBorderCursorSurfacePointerEntered();
PublishBorderCursorSurfaceBounds(index, presentedBounds, true);
scene.SetSharedLightingSubscribed(true);

// Wrong：非阻塞抢锁失败后丢弃唯一一次移动采样。
std::unique_lock lock(presentationMutex, std::try_to_lock);
if (!lock.owns_lock()) return;

// Correct：先发布 latest-wins 绝对目标；只有 fallback 请求 pair。
pendingDrag.Publish(candidateLayout, dragRevision);
if (!lock.owns_lock()) {
    RequestPair(pair);
    return;
}
if (MovePairWindowsSequentially(candidatePresentationBounds)) {
    SetBoundsDirect(candidateLogicalBounds);
    CommitLatestPendingDrag();
    PublishBorderCursorSurfaceBounds();
}

// Wrong：把旧 UI2 的 COM 后等待间隔当成用户可见重复速度。
if (held >= 400ms && sinceLastRepeat >= 15ms) InvokeDirection();

// Correct：Down 时快照系统键盘设置，一次按压只消费该 timing。
const auto timing = ResolvePptKeyboardRepeatTiming(delay, speed);
if (ShouldTriggerPptLongPressRepeat(
        enabled, held, hasRepeated, sinceLastRepeat, timing)) InvokeDirection();

// Wrong：Scene 动画提前结束便休眠，deadline 到期后无人重算输入门禁。
return scene.AnimationActive() ? FrameResult::Continue : FrameResult::Idle;

// Correct：transition deadline 本身就是本客户端的续帧理由。
return ShouldContinuePageControlFrame(visible, now < transitionUntil,
    bounds.active, scene.AnimationActive(), longPressActive)
        ? FrameResult::Continue : FrameResult::Idle;

// Wrong：仅凭 COM EndScreen 状态就发布 -1/N，而画布仍是最后一张真实页。
PublishPageState(-1, observedTotalPage, 0);

// Correct：独立 EndScreen target 已成功 Present 后，才带该 target revision 发布显示哨兵。
if (ready.presentationReady == ReadyIdentityFor(endTarget))
    PublishPageState(-1, endTarget.totalPages, endTarget.targetRevision);

// Wrong：图标切为 EndShow，点击仍固定进入会丢弃 currentPage == -1 的 Next。
callback = pptCallbacks.nextPage;

// Correct：同一发布快照同时决定视觉与业务动作，并复用 A2 dispatcher。
switch (ResolvePptDirectionAction(next, ppt.currentPage, ppt.totalPage)) {
case PptDirectionAction::NextPage: callback = pptCallbacks.nextPage; break;
case PptDirectionAction::EndShow: callback = pptCallbacks.endShow; break;
default: callback = pptCallbacks.previousPage; break;
}
~~~

### UI3 基于变化的脏区事务合同

#### 1. Scope / Trigger

修改 UI3 Bar 的动画推进、动态 HWND、输入坐标、自绘控件、动态光、调试覆盖层、D2D dirty clip 或 `UpdateLayeredWindowIndirect` 提交时适用。Bar 的 D2D target 是可复用资源容量，真实 HWND 只覆盖当前 viewport；两者不得重新合并为显示器尺寸的单一状态。

#### 2. Signatures

~~~cpp
using BarDirtyVisualKey = std::uint64_t;

class BarDirtyRegionTracker
{
public:
	void BeginFrame(const RECT& windowBounds);
	void Observe(BarDirtyVisualKey key, const RECT& currentBounds);
	void MarkChanged(BarDirtyVisualKey key);
	bool ShouldObserve(BarDirtyVisualKey key) const noexcept;
	bool HasOnlyChangedKeys(BarDirtyVisualKey first,
		BarDirtyVisualKey second) const noexcept;
	RECT ResolveDamage(bool requireFallback);
	void CommitPresented();
	void RetainForRetry(bool forceFullDamage);
};

RECT ResolveBarLightBorderDamage(
	const RECT& outerBounds,
	const RECT& contentBounds,
	const RECT& lightInfluenceBounds) noexcept;

RECT ResolveBarScaledDirtyBounds(
	double left, double top, double right, double bottom,
	double pivotX, double pivotY, double scale, double zoom,
	LONG padding) noexcept;

BarDebugDamageResolution ResolveBarDebugDamage(
	const RECT& businessDamage,
	const RECT& previousTextBounds,
	const RECT& previousFrameBounds,
	const RECT& currentTextBounds,
	bool debugEnabled,
	bool finalIdleFrame = false) noexcept;

BarWindowCapacityDecision ResolveBarWindowCapacity(
	SIZE currentSize,
	POINT anchor,
	const RECT& currentContentBounds,
	const RECT& layoutBounds,
	LONG padding) noexcept;

BarWindowViewportDecision BarWindowViewportController::Resolve(
	const RECT& currentContentBounds,
	const RECT& predictedEnvelope,
	const RECT& layoutBounds,
	LONG padding,
	bool settleToCurrent,
	POINT committedTranslation = {}) noexcept;

struct FrameRateAverages
{
	double actualFramesPerSecond;
	double unlimitedFramesPerSecond;
	bool updated;
};

class OneSecondFrameRate
{
public:
	FrameRateAverages Tick(
		Clock::duration activeFrameTime,
		Clock::time_point now = Clock::now()) noexcept;
	void Reset(Clock::time_point now = Clock::now()) noexcept;
};

class DebugFrameSleepLatch
{
public:
	bool Update(bool enabled, bool hasActiveRendering) noexcept;
	bool IsPending() const noexcept;
	bool IsPresented() const noexcept;
	bool CommitPresented() noexcept;
};

struct BarPresentMappingTuple
{
	POINT source;
	SIZE windowSize;
	SIZE targetCapacity;
	std::uint64_t deviceGeneration;
};

BarPresentMappingMode BarPresentMappingTracker::Resolve(
	const BarPresentMappingTuple& candidate) const noexcept;
void BarPresentMappingTracker::CommitPresented(
	const BarPresentMappingTuple& candidate) noexcept;
bool ShouldForceBarFullWindowReplacement(
	bool viewportMappingChanged,
	BarPresentMappingMode presentMappingMode,
	bool rootLayoutChanged = false) noexcept;

class BarWindowPresentationTransaction
{
public:
	BarWindowPresentationTransaction(std::mutex& mutex, bool& boundsReady);
	void WindowUpdated() noexcept; // ULW 成功后使直移缓存失效
	void Commit() noexcept; // 完整成功快照发布后恢复直移资格
};

class BarPresentDecision
{
public:
	constexpr void RequireVisualRetry() noexcept;
};

void SetDebugOptions(bool enable, bool showFrameRate);
~~~

#### 3. Contracts

- 标准 Shape/SVG/PNG/Word 使用稳定对象键记录边界；父布局、粗细/色板/弹窗等自绘内容使用稳定功能组键。变化项的 damage 是上次成功呈现边界与本帧边界的并集，覆盖移动、缩放、出现和消失；所有 damage 最终合并、裁剪为一个 `RECT`。
- 每帧顺序固定为：推进动画并 `MarkChanged` → 完成继承布局并 `Observe` 当前边界 → `ResolveDamage` → 解析容量/viewport → 加入调试文字和红/绿/蓝框旧新边界 → 用同一 `presentDamage` 约束 D2D clip/Clear 与稳定映射帧的 `UPDATELAYEREDWINDOWINFO::prcDirty`。首次呈现，或 `pptSrc`、`psize`、target capacity、device generation 任一字段相对上次成功 tuple 改变时，必须清除完整候选 HWND 范围并令 `prcDirty=nullptr` 执行整窗替换；viewport 相对 committed anchor 的解释变化必须与 present-mapping tracker 合并为同一个 `forceFullWindowReplacement`，同一布尔值控制业务/调试 damage、`presentDirty` 和 `prcDirty`。只有两类映射都稳定的帧允许局部 dirty。居中重排改变主按钮根节点时，capacityOrigin 与 viewport 可以同向平移而保持 source/size 数值不变；该类真实布局移动仍必须整窗替换，清除被新缓冲区原点重新解释的旧右端像素。纯直移吸收继续沿用既有成功快照重基准并复用容量；用本帧稳定居中根节点实际派生变化标识触发此项整窗替换，不能把普通直拖误判为居中重排。
- `BarUiAdvanceAnimation` 的 `changed || active` 必须标记所属控件或功能组。直接拖动、保持环、色板/粗细自绘等绕过标准动画的路径必须显式标脏；存在非调试呈现请求却没有分类 damage 时必须回退全窗口。
- 主光和鼠标光必须独立报告变化，静止的一路不得因另一路移动而被标脏。每路先计算包含径向半径、`pointLightDiffuseExtraWidth * zoom` Gaussian 外扩和抗锯齿余量的影响矩形，再只与实际可见 `PointLight` 边框的上/下/左/右影响带求交；光圈内部没有边框像素贡献的区域不得进入 damage。关闭光影时当前边界为空，旧边界仍参与清除。
- Tracker 为稳定视觉键复用记录，并复用变化键/观察键容器；普通帧只通过 `ShouldObserve()` 采集变化项、所需功能组和光源的边界，成功后只推进本帧实际观察记录。全可见内容边界只在首帧、DPI/容量纪元、顶层外框动画、整栏拖动和最终 idle 帧重算，普通 hover/按压/光影帧复用缓存。禁止逐帧清空并重建哈希节点、复制完整快照，或为动态缩窗在普通高频帧遍历全部 SVG/PNG/Word 内容。
- D2D target 容量以主按钮为稳定锚点，启动、DPI/显示器纪元或真实内容突破容量才重建；整栏拖动只平移 `capacityOrigin` 和 viewport，同尺寸不重建 target。容量突破要保守对称扩容并强制全脏，不得为追求立即缩小而频繁重建。
- 只有可改变顶层外框的动画批次才在首帧扩展 viewport 并保留保守扫掠包络；批次内不缩放，最终 idle 帧只收缩一次。普通 hover、按压、帧率文字和光影不得将 HWND 扩到整个容量。预留包络要预先内缩 viewport padding，保证解析后的 `pptSrc + psize` 始终位于 target 内。
- 粗细 Slider/FineDial 连续手势的完整交互域必须复用 `GetBarThicknessSliderRange(currentPenMode, dpiZoom).max`，在按下/捕获首帧按最大端滑块位置计算完整 Preview Popup。包络同时覆盖 DPI 换算后的最大圆、数字从圆外迁入圆内的最宽 Surface、Slider/FineDial 两个目标位置、Popup Back 极值，以及实际可见描边、PointLight `pointLightDiffuseExtraWidth` 和抗锯齿外扩；捕获、拖动与 FineDial 物理期间保持该预约，候选粗细逐帧增长不得再次 resize。
- Popup 已可见时，粗细快捷按钮或切换笔型产生的程序化动画必须在首个变化帧按 `drawAttributePenThickness.tar`、滑块归一化目标和 FineDial 目标位置预留紧致包络，并覆盖当前到目标的动画段、数字内外迁移与 Popup 回弹；不得等动画结束后才按实际内容追扩，也不得因此退化为预留当前笔型完整量程。
- 绘制使用布局坐标，D2D 帧 transform 统一平移 `-capacityOrigin`；ULW 在同一次调用中提交 `pptDst/psize/pptSrc/prcDirty`。Bar 原生鼠标消息必须在窗口线程入队时就用当次 Win32 消息的屏幕位置固化为 monitor-local layout 坐标，然后丢弃 HiMsg 默认 client 副本；触摸/原生消息必须保留采样时的完整屏幕位置；Raw Input 私有样本和计时器重新命中通过统一入口转换到同一 layout 空间。禁止在交互线程出队时再读取新 viewport 解释旧 client 坐标，否则 resize 恰好夹在入队/出队之间时会出现一次命中跳变。
- 保持单次 GDI interop 链：`GetDC(D2D1_DC_INITIALIZE_MODE_COPY) → UpdateLayeredWindowIndirect → ReleaseDC`。不得在没有端到端数据的情况下加入 CPU staging bitmap、DIB Section、`CopyFromBitmap`、`Map` 或脏行 `memcpy`；这些会引入额外拷贝和更复杂的持久像素一致性。
- 装饰租约跳帧只延迟提交，不能清除变化键或累计 damage。设备 generation 变化、资源重建失败或呈现事务任一阶段失败都强制下一次全窗口恢复。
- 只有 `GetDC → UpdateLayeredWindowIndirect → ReleaseDC → EndDraw` 全部成功才可推进业务 damage、viewport controller、mapping tuple、交互命中边界和调试覆盖快照；失败时保留请求并强制下一帧全脏。ULW 已成功但后续阶段失败时，真实 HWND 已经改变，但任何内部呈现快照仍不推进，下帧继续按旧成功 tuple 判定整窗替换并重新对齐。输入消息已在窗口线程入队时固化为 layout 坐标，不依赖异步 viewport 快照发布。`directWindowDragMutex` 必须覆盖 ULW、EndDraw 到成功窗口边界、direct translation 与底栏快照发布；交互侧不能在 resize 已发生但缓存仍旧时使用旧边界 `SetWindowPos`。ULW 成功而后续阶段失败时，旧成功快照保留，但其窗口位置不可用于直移，直至完整恢复呈现成功。
- `Experimental.Inkeys3.UI3.Debug.Enable` 同时控制脏区框和 HWND 框；活动帧 damage 用红框，idle 前最后一帧将上一帧红框原位改为绿框，当前真实 HWND 边界用蓝框。`Debug.ShowFrameRate` 只在前者开启时控制下方帧率文字；文字不得显示“休眠”，也不得单独形成持续呈现需求或维持 60 FPS。
- FPS 文字只随真实 UI、光影、一次性刷新和失败重试帧进入 damage。真实活动结束后由 `DebugFrameSleepLatch` 请求唯一最终帧，用于收缩 viewport 和把旧红框重绘为绿框；完整呈现事务成功后关闭锁存，失败或租约跳帧继续保留，直至下一次真实活动重新武装。两项帧率按同一个完整 1 秒桶锁存，一秒内文字数值不变；实际值只统计成功呈现帧，并在 idle 后恢复真实活动时重建桶，禁止把 idle 等待计入墙钟分母。无限制值以同批有效帧数除以进入 60 FPS pacing 等待前累计的工作时长；只有帧率锁等待被排除。
- 红/绿框在业务 damage 解析后生成；蓝框跟随 candidate viewport。绿框使用上次成功呈现的红框边界，不重新扩大 damage。蓝框的旧新边界只在 viewport 或调试开关变化时进入 damage，稳定帧不得因蓝框而每帧全窗刷新。关闭任一覆盖层时，用成功呈现的旧快照清除遗留像素。
- 使用显式 D2D pivot/scale 变换绘制的内容，damage 必须用同一变换解析实际呈现边界；仅在绘制阶段调用 `Inherit` 的子视觉必须在 dirty 采集前同步同一继承关系。功能组不能只依赖根面板兜底：凡是动画中可能越过根边界的子视觉，都要在 `ResolveDamage()` 前按实际绘制层级同步；Main/More 注册按钮固定为 `button ← MainBar`、`icon/name ← button`。同步只在对应功能组需要观察时执行，纯光源帧不得为此遍历图标和文字。禁止直接读取未参与本帧绘制的默认或上一帧 `inhX/inhY`；绘制后的 `state.current` 更新也不能补救已经用于 Clear/D2D/ULW 的本帧 dirty。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 首次呈现或 device generation 改变 | 全窗口 damage；成功后才建立快照 |
| target 尺寸改变或内容突破容量 | 重建容量并按新 HWND 全脏；失败保留旧资源语义并重试 |
| 顶层外框动画 | 批次开始最多扩窗一次，批次中不收缩，idle 最终帧收缩一次 |
| 粗细 Slider/FineDial 按下后拖到当前笔型最大值 | 按下首帧即包含最大 Preview Popup；候选值增长和 FineDial 惯性不触发第二次扩窗 |
| 整栏拖动 | 平移 HWND 与容量原点；尺寸不变时不重建 target |
| 首次呈现，或 viewport/source/window size/target capacity/device generation 改变 | 清除完整候选 HWND，`pptDst/psize/pptSrc` 同次提交且 `prcDirty=nullptr`；`pptSrc + psize` 不得越出 target |
| 成功 tuple 稳定且没有真实根布局重排 | 允许以同一 damage 约束 D2D clip/Clear 和非空 `prcDirty` |
| 单控件移动/缩放 | 提交旧边界与新边界的并集 |
| 控件出现或消失 | 空边界与非空边界按同一变化键解析 |
| 更多/绘制属性快速交替后主栏立即收缩 | Main/More 组先同步本帧按钮组合坐标，再把成功呈现的旧组边界与当前新组边界合并；最左旧像素不得漏算 |
| 装饰帧租约跳过 | 保留变化键与累计 damage，后续继续提交 |
| 任一 GetDC/ULW/ReleaseDC/EndDraw 失败 | 保留请求并强制全窗口重试 |
| 未分类的非调试呈现请求 | 安全回退全窗口，不允许空提交 |
| 调试模式关闭 | 并入上次文字、红/绿框与蓝框边界完成清除，不绘制新覆盖层 |
| 脏区调试开启、显示帧率关闭 | 活动帧绘制红色 damage 框和蓝色 HWND 框；idle 最终帧把旧红框改为绿色后进入 wait |
| 显示帧率开启但没有真实呈现需求 | 最终帧保持帧率文字不变、绿框标记旧 damage，成功后进入 wait；不显示“休眠” |
| idle 后出现真实呈现需求 | 恢复红色 damage 框并以当前活动帧起点重建统计桶，idle 时长不得进入实际帧率 |
| 一秒统计桶尚未结束 | 真实呈现帧继续绘制已锁存文字，`updated=false`，显示值不变化 |
| 一秒统计桶结束 | 同时发布实际/无限制平均值并开始新桶；无限制分母不包含 pacing 等待 |
| 仅鼠标光位置变化 | 只标记鼠标光键；按其旧/新实际受光边框合并，不并入静止主光 |
| 光源影响矩形完全位于控件内部且未触及边框 | 该控件不贡献光源 damage，不得用整个光圈矩形代替 |
| 显式缩放文字或延迟继承的色轮子视觉 | 按实际 pivot/scale 或父子 Inherit 解析当前边界；隐藏/无效变换返回空矩形 |

#### 5. Good / Base / Bad Cases

- Good：顶层展开动画前一次扩展 HWND，批次内不再 resize，idle 最终帧一次贴合内容；活动 damage 是红框，最后一帧原位变绿，真实 HWND 始终是蓝框。
- Base：普通 hover/光影不扩窗；静止后没有呈现请求；首次帧、target/设备切换和失败恢复帧允许按当前 HWND 全脏。
- Bad：将 HWND 与 target 每帧缩到当前 damage，或者为每次缩窗重建 target；前者会跳变并频繁 resize，后者把节省的覆盖成本换成资源重建成本。
- Bad：每帧把所有可见内容边界并入 damage，或 ULW 成功但 `EndDraw` 失败后仍推进快照；前者丢失优化，后者会在重试时漏掉旧像素。
- Bad：粗细预览数字用显式变换绘制，却仍以从未更新的 `word.inhX/inhY` 采集边界；结果是脏区无故从 `(0,0)` 开始。
- Bad：先用上一帧的按钮继承坐标提交功能组，再在绘制阶段更新 `state.current`；当前帧清除和 ULW 已经使用较窄 dirty，旧边缘仍会暂留。

#### 6. Tests Required

- Headless 覆盖首次全脏、单键变化、旧/新并集、出现/消失、多键合并、窗口裁剪、提交后推进、跳帧保留、失败全脏、未分类回退、调试文字/红/绿框关闭清除，以及光圈位于内部无边框交集、单边/拐角交集和稳定记录复用。
- Headless 覆盖 client/layout/surface 坐标往返、动画批次扩展/中间帧不 resize/idle 一次收缩、普通 hover 不预留整容量、整栏拖动保持尺寸、容量突破扩容，以及 `pptSrc + psize` 始终位于 target 内。另覆盖居中根移动但 source/size 不变时完整擦除旧缓冲区尾部、纯直移吸收排除重复清除，以及 ULW 成功/后续失败时的直移门禁；呈现锁到成功快照发布的完整范围由源码审查和构建验证。
- Headless 覆盖首次 tuple、viewport、source、window size、target capacity 和 device generation 变化均选择整窗替换，稳定 tuple 选择局部 dirty；失败候选不得推进成功 tuple，成功重试后才允许局部 dirty。
- Headless 覆盖显式 pivot/scale 变换后的实际矩形和隐藏 `scale=0` 空边界，断言结果不回落到默认原点。
- Headless 覆盖功能组在中间帧未提交时继续保留最外层 pending damage，成功提交后只推进最终观察边界；RenderLoop 的继承顺序另由静态审查和完整构建约束。
- Headless 覆盖完整一秒前不发布、桶结束同时发布两个平均值、一秒内保持锁存、无限制分母排除 pacing 等待、Reset 和非单调时间重建统计桶；另覆盖最终 idle 帧单次请求、失败保留、成功关闭和真实活动重新武装。
- 使用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，并运行 `InkeysHeadlessTests`。
- 手工覆盖悬停/按压、属性栏与更多面板、换边、粗细/色板弹窗、整栏拖动、主光/鼠标光、动画开关、调试开关、DPI/zoom 和设备资源重建；不允许残影或漏刷。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：所有可见内容每帧都成为脏区，并在 ULW 后提前推进快照。
dirty = Union(lastPresentedBounds, visibleContentBounds);
UpdateLayeredWindowIndirect(hwnd, &ulwi);
tracker.CommitPresented();
EndDraw();

// Correct：只解析变化项；完整事务成功后再提交快照。
tracker.MarkChanged(controlKey);
tracker.Observe(controlKey, currentBounds);
RECT dirty = tracker.ResolveDamage(unclassifiedDemand);
auto result = PresentThroughGdiInterop(dirty);
if (result.GetDcOk && result.UlwOk && result.ReleaseDcOk && result.EndDrawOk)
	tracker.CommitPresented();
else
	tracker.RetainForRetry(true);
~~~

~~~cpp
// Wrong：文字并未通过 Inherit 绘制，默认坐标会污染脏区。
dirty = GetWeigetRect(previewNumber); // inhX/inhY 仍为 0

// Correct：复用实际绘制的 pivot/scale 解析呈现边界。
dirty = ResolveBarScaledDirtyBounds(
	left, top, right, bottom, pivotX, pivotY, scale, zoom, padding);
~~~

~~~cpp
// Wrong：组边界先读取上一帧继承坐标，绘制时同步已经太晚。
tracker.Observe(mainGroupKey, CollectRegisteredButtonBounds());
DrawMainBar(); // 此处才执行 button/icon/name.Inherit(...)

// Correct：复用绘制层级先同步，再观察并解析本帧 damage。
SyncRegisteredButtonPresentedBounds();
tracker.Observe(mainGroupKey, CollectRegisteredButtonBounds());
RECT dirty = tracker.ResolveDamage(false);
DrawMainBar();
~~~

~~~cpp
// Wrong：业务 damage 已因 viewport 解释变化全脏，但 ULW 仍按另一布尔值提交局部 dirty。
businessDirty = candidateViewport;
ulwi.prcDirty = mappingTrackerChanged ? nullptr : &localDirty;

// Correct：viewport 与呈现 tuple 共用唯一整窗替换决策。
const bool forceFullWindowReplacement =
	ShouldForceBarFullWindowReplacement(viewportMappingChanged, mappingMode);
businessDirty = forceFullWindowReplacement ? candidateViewport : businessDirty;
ulwi.prcDirty = forceFullWindowReplacement ? nullptr : &localDirty;
~~~

~~~cpp
// Wrong：每帧改动显示值，并用含 60 FPS 等待的墙钟时长推算“无限制”值。
actualFps = rolling.Tick(now);
unlimitedFps = actualFps;

// Correct：完整一秒才发布；无限制分母只累计 pacing 前的工作时长。
const auto averages = frameRate.Tick(frameWorkEnd - frameWorkStart, frameEnd);
if (averages.updated)
	UpdateDebugText(averages.actualFramesPerSecond,
		averages.unlimitedFramesPerSecond);
~~~

### UI3 共享鼠标光照通知贡献合同

修改 `BarPageSurface::PublishSharedLighting`、光照到局部 damage 的映射或分页订阅时适用。目标是光源与窗口无交集时不唤醒窗口，同时保留擦除旧光的必要帧。

- `SetSharedLightingSubscribed(bool)` 管理目标订阅；`PublishSharedLighting(const BarSurfaceSharedLighting&)` 保存全局最新值与 revision。注册、移动、Reset 和销毁使用同一注册表锁，锁外只调用复制的 hooks，不能保留裸 impl 指针。
- `Impl::NeedsLightingUpdate(before, after)` 使用最近成功呈现的可见面板和倍率，检测旧/新 primary/cursor 范围与真实边缘 damage 的交集；已有业务 dirty 不属于本次光照贡献。只有相关变化才收集 hooks，不因其他目标动画唤醒远处目标。
- Render owner 在共享帧 Advance 中应用最新光照。cursor damage 取旧/新贡献并集；移出、隐藏或强度归零仍擦旧光。mapping/primary 变化按公共规则重绘，同时更新 cursor 历史范围。
- 布局、业务、动画 Continue、设备重建和失败 Retry 保持各自请求；光照过滤不提前消费失败 damage。无 damage 的稳定回调不执行 GetDC/提交。
- 实际 `--bar-eraser-offscreen-test` 覆盖远处光移动不唤醒、进入/离开窗口唤醒、Reset 退订与真实呈现事务；多目标基准验证稳定后不持续回调。headless 纯数学测试不能替代这些集成检查。

| 情况 | 必须行为 |
| --- | --- |
| cursor 旧、新范围都不影响本目标 | 更新全局快照，不发该目标 wake |
| cursor 从目标内移到外部 | 旧光贡献进入 damage，安排擦除 |
| 同一区域亮度变化、已有业务 dirty | 有效光变化仍通知；无效变化不借旧 dirty 唤醒 |
| primary 与 cursor 同时变化 | 按公共规则重绘，同时推进 cursor 历史范围 |
| 布局/epoch/显隐、动画或失败重试 | 独立请求与成功呈现合同保持 |
| 订阅目标同时 Reset/移动/销毁 | 注册表保护生命周期，锁外不解引用已移走的 impl |

### UI3 Bar 底栏二维吸附事务合同

#### 1. Scope / Trigger

修改 UI3 Bar 的底栏捕获、主按钮直拖、水平居中、弹性映射、成功呈现快照或吸附提示时适用。水平居中是竖向 `Floating / BottomDocked` 的正交状态，不得把两轴合并成互斥枚举。

#### 2. Signatures

~~~cpp
enum class BarBottomDockCenterMode : std::uint8_t { Free, Centered };

struct BarBottomDockHorizontalMapping;
struct BarBottomDockPresentedSnapshot;
bool ShouldDeferBarBottomDockReleaseHandoff(
	bool dragActive, bool directDragStillOwnsWindow,
	bool directTranslationPending) noexcept;
POINT ResolveBarBottomDockFrameTranslation(
	unsigned long long frameTransitionSerial,
	unsigned long long observedSerialBefore,
	unsigned long long observedSerialAfter,
	POINT latestTranslation, POINT presentedTranslation) noexcept;
BarBottomDockSpringResult AdvanceBarBottomDockSpring(
	BarBottomDockSpringState& state, double targetDip, double dtSeconds,
	bool animationsEnabled = true, bool preservePresentedOffset = false) noexcept;
struct BarBottomDockFramePresentationDecision
{
	POINT translation;
	bool deferred;
};
BarBottomDockFramePresentationDecision ResolveBarBottomDockFramePresentation(
	unsigned long long frameTransitionSerial, POINT frameTranslation,
	unsigned long long observedSerialBefore, unsigned long long observedSerialAfter,
	unsigned long long deferredTransitionSerial, unsigned long long presentedTransitionSerial,
	POINT latestTranslation, POINT presentedTranslation) noexcept;
void BeginBarBottomDockTransition(std::atomic<unsigned long long>& serial) noexcept;
bool TryBeginBarBottomDockFrameTransition(
	std::atomic<unsigned long long>& serial,
	unsigned long long consumedSerial, bool dragActive) noexcept;
unsigned long long FinishBarBottomDockTransition(
	std::atomic<unsigned long long>& serial,
	std::atomic<unsigned long long>& deferredSerial, bool deferWindowMove) noexcept;
bool SeedBarBottomDockCaptureBottom(
	BarBottomDockSpringState& spring,
	BarBottomDockMode presentedMode, BarBottomDockMode nextMode,
	double presentedBottomScreenY, double nextBaseBottomScreenY,
	double zoom, bool animationsEnabled) noexcept;
BarBottomDockVerticalMapping ResolveBarBottomDockVerticalMapping(
	double baseTopDip, double baseBottomDip, double elasticOffsetDip,
	double captureBottomOffsetDip = 0.0, bool preserveCaptureBottomOffset = false) noexcept;
BarBottomDockVerticalMapping ResolveBarBottomDockRecoveringVerticalMapping(
	double baseTopDip, double baseBottomDip, double elasticOffsetDip,
	double captureBottomOffsetDip = 0.0, bool preserveCaptureBottomOffset = false) noexcept;
RECT ResolveBarBottomDockCapacityEnvelope(
	const RECT& elasticBaseBounds, double zoom,
	double horizontalOutsetDip = BarBottomDockVisualLimitDip,
	double verticalOutsetDip = BarBottomDockVisualLimitDip) noexcept;
~~~

#### 3. Contracts

- 水平捕获目标是当前显示器 `monitorBounds` 的水平中点；参与居中的几何只包含主按钮、主栏及可见描边的联合外框。扩展面板不参与中心计算或拉伸，只按其按钮锚点的二维映射差值刚性跟随。
- 水平捕获只在竖向已 `BottomDocked`、主栏展开且联合外框中心进入 `BarBottomDockCenterThresholdDip = 40 DIP` 时生效；边界值允许捕获，严格越界立即脱离。竖向继续使用 `BarBottomDockThresholdDip = 20 DIP`；折叠或竖向脱离必须结束水平捕获。
- 横纵 mode、phase、elastic offset、直接窗口位移和显示环境共用 `bottomDockTransitionSerial` 的同一发布事务。交互线程必须先计算完整两轴候选，再发布偶数稳定 serial；渲染线程不得提交只包含一轴新状态的帧。 deferred barrier 与屏幕抓手 X/Y 也必须在偶数 serial 发布前写入；首次抓取、普通采样和 DPI/显示屏障遵守同一顺序，不能在稳定 serial 之外补写其中一轴或 barrier。
- 输入、折叠和白板请求的写者必须先通过 `BeginBarBottomDockTransition` CAS 独占奇数 serial，禁止多个 `fetch_add` 写者重叠形成伪稳定偶数。渲染自动写回必须用 `TryBeginBarBottomDockFrameTransition` 同时满足未抓取且 serial 仍等于本帧已消费版本；资格失效就丢弃候选并保留 dirty 重试，不得替旧位图升级到更新输入的 serial。在 PrepareLightingAndDemand 之前作废候选时，必须调用 `BarPresentDecision::RequireVisualRetry()` 同时补 visual demand 和 full dirty；仅 RetainForRetry/RequireFullDirtyRetry 不会令 ShouldPresent 为真，不能保证静止后的正确帧上屏。写事务仅包含短暂本地/原子更新，不跨 D2D 呈现或外部锁。
- 这两组偶数/奇数版本快照（期望 `bottomDockTransitionSerial` 与已呈现 `bottomDockPresentedMappingSerial`）的写者在领取奇数后、写 relaxed 载荷前设置 release fence，写完后以 release 发布偶数。读者先 acquire 读偶数，读取全部 relaxed 载荷后设置 acquire fence，再重读版本并仅在相等时接受。新增读取点或写者必须保持这个配对；只靠末尾 acquire load 不能保证载荷先于校验。运行期形态变化仍用原有 deferred barrier 处理，不能把未来 barrier 当成成功呈现。
- 按住时 Free/Dragging 阶段由输入 tracker 拥有，渲染不得强制写成 Stable 或程序化居中；不一致的折叠/居中候选需等完整新 tuple。显示位置的拖动所有权也读取同帧 `bottomDockDragActive`，不另读实时 direct phase 拼接帧状态。
- 松手发布 `bottomDockDragActive=false` 后，`directWindowDragPhase` 仍可能短暂为 `Dragging`。只要仍有待吸收直移，渲染线程在取得稳定 release tuple 后必须返回 Retry；下一帧先把 phase 原子切到 `Absorbing`，在 `directWindowDragMutex` 内吸收 translation、重基准成功快照并执行 `PositionUpdate()`，然后才允许释放态布局和既有换向动画。
- 交互重基准、直接 `SetWindowPos` 失败回滚和下一手势起点只读取最后成功呈现快照。水平 tracker 的输入必须是指针驱动、未形变的主体中心；形态呈现 barrier 只能确认窗口位移已提交，不得用视觉主体中心改写抓取偏移或 tracker 基准。显示环境重基准也不能用当前 pointer 减已限位/滞后的呈现中心重抓；抓取偏移保持原逻辑点，仅通过 `ScaleBarDragGrabOffsetForZoom` 随实际呈现 zoom 比例缩放。水平捕获与脱离首帧必须从已显示像素播入恢复平移，不能把逻辑锚点切换表现为 HWND 跳变。
- D2D 几何计算完成后到 ULW 之间，交互线程仍可发布采样。最终目的地必须在 `directWindowDragMutex` 内读取同一稳定偶数 serial 下的目标/实际 translation、最新 deferred barrier 和成功呈现 serial：当前帧已包含尚未上屏的 barrier 时，位图必须与帧内 translation 成对提交，后续普通采样不能迫使首次吸附饥饿；若更新的形态/显示 barrier 晚于当前帧，或提交前读到发布中/不一致 tuple，则放弃 ULW、配对 EndDraw 并保留完整重试，不推进成功快照或伪报 ULW 失败。barrier 已成功呈现后的普通过期帧继续使用实际已呈现 translation，避免拉回已直移 HWND。
- 主按钮及 Logo 使用水平刚性抓手映射并保留既有竖向果冻；主栏背景、普通按钮、图标和文字使用独立二维主体映射。主栏近端随抓手移动，远端吸附稳定居中边界：右向展开从中心左侧进入时拉伸、越过中心后压缩，左向展开镜像；捕获、脱离和恢复中重新捕获的远端都必须从上一成功像素按新旧 HWND 位移反推。绘制、dirty、viewport/capacity、PointLight 逆映射、第三光源接受区和命中必须按刚性抓手/主体映射分类；竖向普通抓手输入保留 24 DIP 视觉保护；水平 40 DIP 捕获带内的刚性抓手偏移以及从成功像素反推的远端初值必须精确保留，不能按 24 DIP 截断而造成捕获或脱离跳帧。水平弹簧须允许连续从超出常规保护范围的有效初值恢复，保持原频率、阻尼与动画开关语义。水平捕获带、实际抓手/远端恢复范围、Gaussian 外扩和抗锯齿余量都进入保守包络。
- 纵向捕获只由最后成功呈现的 Floating -> BottomDocked 识别，并以旧 `monitorOrigin + visualBottomDip * zoom + directTranslation` 和新基准底边反推 capture-bottom 初值；不得按上一候选 mode 消费捕获。首次成功捕获帧不推进该弹簧，跳帧或失败需从同一旧成功像素重新播入。20 DIP 输入阈值不能约束上一画面距 dock 的距离，故捕获底端初值可超出 24 DIP；生产 Docked/Recovering 映射须启用 `preserveCaptureBottomOffset`，弹簧用 `preservePresentedOffset` 连续恢复，并把实际捕获范围与原抓手范围纳入 capacity/viewport。
- 场景入口由渲染线程的 `BarWorkspaceSceneState` 统一处理：首次布局完成后，Desktop 首次进入 PPT/Whiteboard 才捕获位置、显示器、两种展开方向、fold、dock 和 center 基线。仅已展开 BottomDocked 贴目标屏幕底边 `5 DIP`，保留 Centered/Free；浮动或折叠不改变形态。PPT↔Whiteboard 不覆盖桌面基线，最终离开恢复基线；原屏幕消失时按显示快照回退并钳制可见位置。此合同替代旧 PPT 进入/退出强制居中以及白板强制展开/退出折叠策略，用户拖动的水平捕获规则不变。
- 启动时已有的展开中置底栏发布为稳定居中。折叠时退出居中但不移动主按钮；底栏重新展开时只在最终展开联合外框仍位于 40 DIP 阈值内时无提示捕获。桌面首次放置必须在首次方向分类前初始化为向右展开（主按钮居左），白板入口保持既有方向。稳定居中展开时保持渲染线程当前布局方向，不得因动态 HWND 包络重新换边；离开居中后普通换向仍按既有关键帧执行。
- 底栏 `PositionUpdate()` 只能把最后成功呈现的水平模式作为方向分类门禁：`Centered` 时保持当前布局方向；非居中时遵守 `UI.Bar.ExpandDirectionAdjustment`，中轴线模式只有有限且大于零的显示参考宽度才可按中轴分类，空间不足模式在完整稳定目标宽度可用后由主栏布局策略判定。首帧零宽、无效宽度或动态 HWND 包络都不得自行产生新方向。
- `BottomDocked + Centered + Stable + Expanded` 且无拖拽、水平弹簧或显示切换时，主按钮 X 是稳定居中的唯一根位置。渲染线程必须在主栏、主按钮和按钮动画值推进后，同时在 Popup、颜色面板、粗细面板等下游绝对几何派生前，以主按钮、主栏及两者当前可见描边的联合外框反推 `mainButton.x`，同步 `displayCenterX`，再重新执行 `MainBar.Inherit(Center, MainButton)`；整个既有继承树必须在同一帧消费新根节点。主栏动画不得因每帧居中派生而重启，属性面板、More、Popup 和提示框不得参与中心计算。实际新的主栏布局批次必须同步重定向在途子按钮的位置动画，即使该子按钮目标未变，也不能继续旧批次的进度或曲线而与新父布局叠加成往返；独立按压、悬停与内容动画仍保持各自所有权。捕获、拖拽、脱离、恢复、Free、浮动、折叠、白板放置和显示切换继续由原状态机持有根位置。
- 稳定居中不得再维护逐帧 layout correction、补偿平移、pending/in-flight rebase、方向锁存或遗留换向清理状态。viewport 预测必须从主栏 `x/w` 和描边的完整动画 range 保守反推根节点 X range，并把该 range 传播给主按钮、主栏及继承子视觉；禁止用全局 correction outset 扩张包络来代替真实父几何范围。
- 一次二维主体命中必须先捕获一个成功呈现快照，再由同一 tuple 同时逆映射 X/Y；禁止两个轴分别读取 `BottomDockPresentedSnapshot()`，否则并发发布可能组合出从未成功呈现的映射。普通主栏按钮使用主体 X/Y，More 等刚性覆盖层继续使用原始刚性坐标，不能为复用而原地改写消息。
- 提示资格在手势内锁存，Desktop/PPT/Whiteboard 共用且不按白板场景抑制：浮动主栏真实进入底栏，或 `Free / Centered` 发生任意双向切换时建立；底栏内起拖但未发生转换时不显示。资格建立后，Free 阶段显示“底栏模式”，Centered 阶段显示“底栏模式 · 居中”；只有松手、折叠、取消或竖向脱离时清除，同一手势重新进入底栏可以再次建立。文案变宽必须先完成外框扩宽再交换文字，变窄必须先完成文字交换再缩窄外框；命中仍只发布成功呈现的实际外框。首次激活帧即使缩放仍为零，也必须把上次成功外框、当前外框和覆盖 Back 峰值、描边、第一/第三光源、Gaussian 与抗锯齿的完整包络并入 damage；失败呈现保留该 damage。
- 指示器的锚点是底栏二维映射的下游几何。映射变化时，只要指示器正在显示、退场或仍有上次成功边界，就必须标记其稳定视觉键并观察同源完整光影包络；不得因指示器自身文字、样式和显隐进度未变而跳过旧新边界 damage。
- 点击后的悬停抑制以物理屏幕坐标为准：窗口重基准产生的同坐标相对消息不得重新悬停；同一识别区域内只要收到一次真实屏幕坐标变化的 `WM_MOUSEMOVE` 就必须解除抑制，点击不是恢复条件。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 同一采样同时进入竖向和水平捕获带 | 两轴与窗口位移一次发布，首个成功帧连续 |
| release tuple 已稳定，但直移 phase 仍为 `Dragging` 且 translation 非零 | 当前帧 Retry；下一帧吸收并 `PositionUpdate()` 后才执行释放布局 |
| 浮动旧帧后夹入新捕获/按下/释放，再尝试渲染自动写回 | CAS 失败，旧帧不得确认新 barrier；后续微小 X 移动不能把未形变旧位图直移到 dock |
| 持续抓取的 Free/Dragging 帧 | 保留 tracker 阶段，不能逐帧 Stable 写回或自动居中 |
| 首次捕获候选未上屏或上一成功底边在 24 DIP 外 | 按成功像素重新播入精确底端；首帧不提前积分或裁短初值 |
| 当前帧已包含未上屏 barrier，后续普通采样推进 serial | 位图与 frame translation 成对提交，保持首次形变连续 |
| 最新 barrier 晚于当前帧，或提交时 tuple 仍在发布 | 跳过 ULW，配对 EndDraw 并保留完整重试；不推进成功快照 |
| barrier 已成功呈现后的普通过期帧 | 使用锁内实际 HWND translation，不回退帧内旧位移 |
| 居中拖动严格超过 40 DIP | 水平进入 Free/Detaching，竖向底栏可保持 |
| 竖向脱离或主栏折叠 | 水平停止捕获并连续恢复，不保留不可见约束 |
| 底栏起始手势 `Free → Centered → Free → Centered` | 提示矩形持续可见，文案在普通/居中之间按防裁切时序双向切换 |
| 呈现或直接移动失败 | 两轴成功快照都不推进，下帧从旧 tuple 完整恢复 |
| 稳定居中主栏 `x/w` 正在变化 | 每帧从当前值反推主按钮根节点，联合可见中心保持显示器中点 |
| 捕获、拖拽、弹簧或显示切换 | 禁止根节点反推，原位置所有者继续工作 |
| 水平 24..40 DIP 捕获以及 39 -> 41 DIP 脱离 | 主按钮保持抓取点；远端从上一成功像素连续恢复，不能截断初值 |
| 新主栏布局批次遇到目标不变的在途隐藏按钮 | 子位置动画加入同一新批次；父子位移抵消时不能出现额外往返 |
| 居中态或窗口宽度为 0/非有限 | `PositionUpdate()` 保持当前布局方向，不创建换向批次 |
| 呈现快照在一次 hover 换算期间更新 | 本次 X/Y 仍全部使用已捕获的旧成功 tuple；下一消息再使用新 tuple |
| ULW/EndDraw 失败 | 现有 dirty/present 事务保留重试；不存在额外居中 rebase 状态 |

#### 5. Good / Base / Bad Cases

- Good：在 40 DIP 捕获边缘主按钮仍跟随屏幕抓取点，远端从上次成功位置弹到居中边界；同目标隐藏按钮与新布局共享进度。 浮动旧帧被新捕获赶超时不能获得写回资格，成功捕获从原显示底边连续启动。
- Base：竖向弹簧继续使用默认 24 DIP 保护；稳定居中只派生根节点，普通直移沿用成功快照吸收。
- Bad：水平抓取偏移或远端初值按 24 DIP 截断；只验证主栏联合中心而不检查子按钮屏幕轨迹。 另一个错误是给旧浮动画面补写新 serial，使输入误以为捕获已经显示。

#### 6. Tests Required

- Headless 覆盖水平 `-40/0/+40 DIP` 边界、严格越界、高速跨带、横纵同帧捕获、折叠门禁、展开自动捕获、左右展开联合外框和 100%/150% DPI；竖向 20 DIP 边界另行保持。
- Headless 覆盖稳定居中根节点所有权矩阵、左右展开、可见描边、无效几何、Draw → Selection 宽度单调与逐帧联合中心不变量、桌面/白板初始方向、零宽/非有限宽度方向保持、主体 X/Y 非恒等映射往返、扩展面板锚点、首次显现完整光影包络、失败快照、release handoff 门禁、当前/过期/发布中帧的实际 HWND translation 解析，以及浮动进入/底栏双向切换/未切换手势的提示资格。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`；手工检查横纵果冻叠加、脏区调试、动画关闭和多显示器切换。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：分别发布横纵模式，渲染线程可能观察到半个吸附转换。
bottomDockMode.store(nextVertical);
bottomDockCenterMode.store(nextHorizontal);

// Correct：先 CAS 独占写者，再发布完整两轴 tuple；渲染写回还须验证已消费 serial。
BeginBottomDockTransition();
PublishBottomDockAxes(nextVertical, nextHorizontal, translation);
FinishBottomDockTransition(true);
~~~

~~~cpp
// Wrong：先平移视觉映射补偿中心，再等待未来成功帧把补偿吸收进根节点。
mapping = Translate(mapping, center - VisualBodyCenter(mapping));
QueueCenteredLayoutRebase(mapping.translation);

// Correct：主栏动画先正常推进，稳定居中态直接从当前子几何反推根节点。
const auto placement = ResolveCenteredRoot(
	monitorCenter, buttonVisibleWidth, mainBar.x.val, mainBarVisibleWidth);
mainButton.x.SetDirect(placement.mainCenterDip);
mainBar.Inherit(Center, mainButton);

// 二维命中仍必须从一次成功快照同时逆映射两轴。
const auto presented = BottomDockPresentedSnapshot();
const POINT body = UnmapBodyPoint(msg.x, msg.y, presented);
~~~

~~~cpp
// Wrong：release tuple 先进入布局，或过期帧用自己的旧位移覆盖实际 HWND。
LayoutReleasedState();
const POINT destination = frameTranslation;

// Correct：release 先等待直移接管；首张形态位图再与其帧内位移配对。
if (ShouldDeferBarBottomDockReleaseHandoff(
	frame.dragActive, directPhase == Dragging, translationPending))
	return FrameResult::Retry;
const auto presentation = ResolveBarBottomDockFramePresentation(
	frame.serial, frame.translation, serialBefore, serialAfter,
	deferredSerial, presentedSerial, latestTranslation, presentedTranslation);
// deferred 时不调用 ULW；仍配对 EndDraw、保留完整重试，不能推进成功快照。
const POINT destination = presentation.translation;
~~~

### UI3 共享设备、串行帧与光影缓存契约

#### 1. Scope / Trigger

新增或修改 Bar、PPT、Setting 图形客户端，或修改 WARP/Hardware 选择、Bar 光影、脏区与分层窗口提交时，必须遵守本节。该契约不把暂存的 `IdtFloating`、Draw2 `DibSurface`、白板或定格自动迁入共享设备。

#### 2. Signatures

~~~cpp
enum class Backend : std::uint8_t { Warp, Hardware };
DeviceEpoch GetDeviceEpoch();
SharedAssets GetSharedAssets();
HRESULT PrepareBackend(Backend backend);
bool CommitPreparedBackend() noexcept;
void BarUIRendering::SetFrameDiffuseMaskGeometryScale(double scale);
~~~

每个 epoch 至少发布 `backend`、单调递增的 `generation`、实际 `featureLevel`、`ID3D11Device`、可选 `ID3D11Device1`、immediate context、DXGI factory/device 与 `ID2D1Device`。

#### 3. Contracts

- 启动默认创建 WARP epoch，请求 FL11.1/11.0；仅在 `E_INVALIDARG` 时用 FL11.0 重试。Hardware 只能显式准备；失败时不得破坏当前 WARP。
- 只有 RenderPipeline 线程能发布新 epoch 并串行调用客户端。客户端从本次 `FrameContext` 读取 epoch，在 `generation` 变化时于 `BeginDraw` 前重建自己的 context、target、GDI interop、RTV/SRV、brush/effect/mask 等设备相关资源。
- 共享 D3D11 device/immediate context、DXGI/D2D device 和 factories，不共享 Bar/PPT 的 D2D context/target 或 Setting 的 swap chain/RTV/SRV。每个回调覆盖该客户端完整绘制和呈现区间，禁止另起渲染线程或跨回调缓存 `FrameContext*`。
- Bar 主帧只保留一组 `BeginDraw/EndDraw`，`GetDC` 已承担必要提交，前面不得再调用显式 `Flush`。Windows 7 Platform Update 路径在 `GetDC` 前必须弹出所有 clip/layer。
- Bar/PageControl 在 DC 只作 ULW 的 `hdcSrc` 且没有 GDI 绘制时，`ReleaseDC` 传入空 `RECT`，准确描述源 DC 未被 GDI 修改。保留 `GetDC(COPY)`；不得以 D2D 或 ULW 的 dirty 代替 GDI 修改范围，也不得据此承诺特定驱动上的拷贝耗时。
- 动态光只长期缓存颜色停靠点/画刷和几何的 A8 预模糊遮罩；画刷位置、半径和透明度每帧更新。禁止缓存快速变化的最终光影帧或冻结布局状态。
- A8 遮罩按几何参数量化且有容量上限。生成时使用同一 D2D device 上的专用 device context，先用一组 `BeginDraw/EndDraw` 写 source target，再把 source 作为 Gaussian 输入，用第二组 `BeginDraw/EndDraw` 写 output；禁止在同一 draw span 中把仍绑定为 target 的 bitmap 当作输入。
- PointLight 圆角矩形正在等比动画时，必须通过 `SetFrameDiffuseMaskGeometryScale()` 把缓存查询归一到完整尺寸；实际动画几何继续使用稳定遮罩的分段落点，Gaussian 外扩宽度不得随圆角段一起压缩。禁止通过动画期间关闭第三光源 diffuse 来规避缓存创建，这会造成亮度变化和动画结束时的单帧创建卡顿。
- 稳态帧不得创建 Gaussian effect、command list、渐变停靠点、solid brush 或重新生成已有遮罩。`FillOpacityMask` 前临时切为 `D2D1_ANTIALIAS_MODE_ALIASED`，结束后恢复。
- 光源与控件扩展边界不相交时必须裁剪该光源的 diffuse/hard-light 绘制；D2D 全局 dirty clip 和 layered-window dirty rect 必须使用旧边界与新边界的并集。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Windows 7 运行时以 `E_INVALIDARG` 拒绝 feature level 11.1 列表 | 只用 11.0 重试；仍失败则保留初始化失败语义 |
| Hardware 准备失败 | 当前 WARP epoch 和全部客户端资源保持可用；不得发布半成品 |
| `generation` 变化后客户端资源重建失败 | 跳过该帧；同一 generation 错误限频，不能用旧 device 的资源向新 epoch 提交 |
| 客户端本帧无变化 | 返回 `Idle`；不得为探测状态扫描其他窗口 |
| A8、Effect 或遮罩专用 context 失败 | 本设备会话停用 diffuse mask；保留基础灰边和硬光，不得退回逐帧实时 Gaussian 或逐帧重试 |
| PointLight 等比几何动画中 | 查询归一化后的稳定 diffuse mask；第一、第三光源、基础边框和硬光全程保持，不得在动画终点集中创建遮罩 |
| 主 `EndDraw` 返回 `D2DERR_RECREATE_TARGET` | 丢弃客户端设备资源并在下一帧按当前 epoch 重建 |
| 主 `EndDraw` 暴露刚创建遮罩的延迟错误 | 清空遮罩缓存并将本设备会话标为不可用，避免错误循环 |

#### 5. Good / Base / Bad Cases

- Good：WARP 中展开属性栏或弹性提示浮窗时始终绘制相同亮度的第三光源，圆角和描边按完整尺寸归一后复用同一 A8 mask；浮窗文字复用完整字号格式并通过 D2D 变换缩放，Bar 主上下文每帧仅一次提交。
- Base：后台准备 Hardware 成功，渲染线程控制点发布新 generation；Bar/PPT/Setting 下一帧先重建各自资源，再开始绘制。
- Bad：切换全局 device 指针后让旧 Bar context 继续一帧，或为避免重建而跨 device 复用 bitmap/brush；这会造成设备域错配、空白帧或设备丢失错误。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`，必须使用 ARM64 host MSBuild。
- 在 WARP 上分别测光影关、仅主光、主光+动态光：属性栏展开/收起、主栏状态切换、鼠标第三光和长时间静止；记录 CPU、帧时间、遮罩 cache miss 和提交次数。
- 反复展开/收起绘制属性和两个提示浮窗；同一完整几何变体在动画期间最多产生一次遮罩 cache miss，动画终点不得新增 miss；第三光源 diffuse 亮度必须连续，第一光源全程保持原效果。
- Windows 7 SP1 + KB2670838 需单独实机验证 feature level 回退、A8 target、Gaussian、`FillOpacityMask`、传统 discard swap chain 和 layered-window 脏区；仅完成静态审计时必须明确未做此项。
- 在支持设备上循环执行 WARP → Hardware → WARP 帧边界切换，覆盖动画中、资源重建失败与 Hardware 准备失败；断言旧 epoch 在发布前始终可用。
- 后续每接入一个共享设备客户端，都要并发触发其交互与 Bar 帧，断言帧串行、交互优先、无自旋和跨 device 资源复用。
- 视觉对比基础灰边、硬光、圆角九宫格遮罩接缝和超椭圆量化伸缩；允许经产品确认的轻微 diffuse 像素差异，不允许边缘断裂或残影。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：客户端另起线程读取全局 epoch 并并行使用 immediate context。
auto epoch = GetDeviceEpoch();
std::thread([epoch] { DrawWith(epoch); }).detach();

// Correct：只在管线回调中读取 FrameContext，并在 BeginDraw 前处理 generation。
if (context.epoch.generation != localGeneration)
    RecreateClientResources(context.epoch);
BeginDrawAndPresent();
~~~

~~~cpp
// Wrong：刚把 bitmap 设为 target，就在同一 BeginDraw 中作为 effect input。
context->SetTarget(source);
context->BeginDraw();
DrawSource();
effect->SetInput(0, source);
context->DrawImage(effect);

// Correct：先结束 source 提交，再在独立 draw span 中生成 output。
maskContext->SetTarget(source);
maskContext->BeginDraw();
DrawSource();
maskContext->EndDraw();
maskContext->SetTarget(nullptr);
effect->SetInput(0, source);
maskContext->SetTarget(output);
maskContext->BeginDraw();
maskContext->DrawImage(effect);
maskContext->EndDraw();
~~~

~~~cpp
// Wrong：动画中关闭第三光源 diffuse，结束后再创建稳定遮罩。
lighting.SetCursorDiffuseSuppressed(geometryTimeline.IsActive());
DrawAnimatedPointLightFrame();

// Correct：全程绘制光影，仅把缓存键归一到完整几何尺寸。
lighting.SetFrameDiffuseMaskGeometryScale(1.0 / geometryScale);
DrawAnimatedPointLightFrame();
lighting.SetFrameDiffuseMaskGeometryScale(1.0);
~~~

### UI3 第三鼠标光休眠契约

#### 1. Scope / Trigger

当修改 UI3 Bar 的第三鼠标光、全局鼠标跟踪或画布落笔通知时，必须保持 `Dormant → Inside → Grace` 状态机；传统 `IdtFloating` 不属于该契约。Draw2 与 Draw3 的鼠标、笔和触摸都必须在各自统一的绘制活动边界复用同一通知接口。

#### 2. Signatures

~~~cpp
namespace Inkeys::UI::Bar
{
	export void NotifyCanvasDrawingStarted();
	export void NotifyCanvasDrawingEnded();
	export void NotifyBorderCursorSurfacePointerEntered() noexcept;
	export void NotifyBorderCursorSurfacePointerLeft() noexcept;
	export void PublishBorderCursorSurfaceBounds(
		unsigned int index, const RECT& bounds, bool visible) noexcept;
}
~~~

画布只能成对调用通知接口，不得从画布线程直接修改 Bar、D2D 或 Raw Input 状态。Draw2 在每个真实笔迹线程入口建立 RAII guard；Draw3 以全部 physical contact 的聚合状态发布 `0→1` Started 与 `1→0` Ended。所有正常、提前返回、Host stop 和异常退出路径都必须保证最终补齐 `Ended`。

#### 3. Contracts

- `Dormant`：第三光源目标透明度为 0，停止光效观察/渲染唤醒；交互所需 Raw Input 注册继续保留；仅 Bar 或四个 PageControl HWND 自然收到真实 `WM_MOUSEMOVE` 才能进入 `Inside`。触摸/笔生成的兼容 mouse 不得激活。
- `Inside`：复用唯一 Bar HWND 的共享鼠标 `RIDEV_INPUTSINK` 注册并启用光效观察；首次离开 Bar 与四个 PageControl 的实际接收消息区域集合时进入 `Grace`，并记录 `GetTickCount64() + 5000` 的绝对截止时间。PageControl 不得注册第二份 Raw Input 或第三光源 timer；活动输入会话的取消/取队列计时器不属于光源生命周期。
- `Grace`：区域外移动不得重置截止时间。第三光源使用独立的 `240 × barStyle.zoom` 径向渐变；已发布 UI 外框只用于判断光圈是否可能命中 UI，从而裁剪渲染唤醒，不得作为全局亮度乘数。
- 同一控件的同一边框像素上，第三光源贡献只由光标到该像素的距离、生命周期强度和控件固定比例决定；光标是否位于接受消息区域、主栏或其他可见区域内不得改变该贡献。第一光源可独立影响最终合成结果，不属于该一致性契约。
- `Grace → Inside`：重新进入实际接收消息区域时取消定时器；仅回到 240px 邻域不能从 `Dormant` 唤醒。
- `Grace → Dormant`：绝对截止时间到达，或画布开始真实绘制时，停止光效观察并从当前强度平滑淡出，不注销交互注册。落笔时若光标仍在 UI3 接收区，区域内后续移动不得重新激活；必须先收到离开，再由下一次自然进入激活。
- 5 秒等待使用窗口定时器，不得新增轮询线程或靠持续渲染计时。
- Main Bar 每个实际光源帧以屏幕物理像素发布第三光源位置、`240 × barStyle.zoom` 半径、当前生命周期强度和可见性；PageControl 只映射到各自 presentation target。分页本地 hover 与第三光源是两条状态：hover 可快速退出，不能据此清零或重新创建第三光源。
- 第三光源输入的屏幕物理像素点是跨坐标重基准的唯一位置来源。`PrepareFrameLighting` 必须用当前帧布局屏幕原点（同帧 `monitorOrigin + bottomDockFrameTransitionTranslation`）重新换算局部物理像素点；该 translation 来自布局已捕获的同一稳定偶数 tuple，不另读 live/最后成功呈现位移，也不额外除以 zoom。松手吸收直移、取消/回滚或显示原点改变时，即使 screenPoint 和输入 serial 不变也必须正确换算；不能直接沿用输入线程缓存的旧 layoutPoint、逐帧轮询光标或在吸收分支竞争写回输入缓存。生命周期、共享屏幕快照、主体/刚性逆映射与旧新光源 damage 继续由原入口持有。
- PageControl 只有在完整 present 与 `SetBounds/Show` 成功后才发布该 HWND 的屏幕边界；成功隐藏发布空边界。该列表只参与 Grace 邻域唤醒裁剪，不能改变亮度、截止时间或 `WindowFromPoint` 的实际接收判断。
- 并发笔迹由原子 activity count 合并：`0 → 1` 才向 Bar 窗口线程发送 Started。Draw3 controller 必须对聚合 bool 去重，多 contact 不重复发送；Host stop/异常时若仍 active 必须补 false。Ended 只负责把 activity count 安全归零，不维持绘图静默状态。
- Bar 窗口线程收到有效 Started 时必须无条件让第三光源进入 `Dormant` 并停止光效观察，保留交互 Raw Input 注册。若系统光标仍位于 Bar 或 PageControl 实际接收 HWND，必须建立 wait-for-leave 门禁：区域内后续移动不能重新激活，真实离开后才允许下一次自然进入。后续绘制过程和抬笔不再持续控制光影。
- 首次 Started 复用 Bar 的辅助界面收起路径，关闭绘制/几何属性、更多、笔型、粗细、颜色与提示浮层；不得改变主栏 `fold` 或隐藏 PageControl。多 contact 与后续 Started 不重复执行可见状态转换。
- 落笔通知不得参与 `BarUiEdgeLightingEnabled`、第一光源、光色过渡或普通 UI 动画门禁；画布线程仍只能通过通知接口请求第三光源休眠，不得直接写 UI、D2D 或 Raw Input 状态。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `RegisterRawInputDevices` 注册失败 | 第三光源保持隐藏；错误至多记录一次；触摸、主光及原生/HiMsg 普通点击保持原路径；拖动补充不可用，不回退到按键轮询 |
| `RIDEV_REMOVE` 注销失败 | 逻辑状态仍进入 `Dormant`，迟到的 `WM_INPUT` 必须被忽略 |
| 窗口定时器创建失败 | 立即进入 `Dormant`，不得无限保留全局跟踪 |
| 动画关闭 | 立即隐藏第三光源并请求休眠 |
| 触摸模拟鼠标消息 | 不得激活第三光源；画布休眠仅由 Draw2/Draw3 统一绘制活动边界通知，不由模拟鼠标消息重复通知 |
| 鼠标从 PageControl 移到画布 | 由 Bar HWND 的同一 timer 进入 Grace；Raw Input 继续发布屏幕点，5 秒后淡出 |
| PageControl HWND 移动、隐藏或提交失败 | 只发布最后成功可见边界；失败候选不得进入邻域列表 |
| 笔迹在取得 Canvas 前提前返回 | RAII guard 仍必须发送 Ended，activity count 最终回到 0 |
| 多指笔迹交错结束 | activity count 最终回到 0；不得因任一笔仍活动而持续压制第一光源或 UI 动画 |
| Started 窗口消息迟到 | 窗口线程以当前原子 count 复核；计数已归零时忽略过期消息 |
| Started 时光标仍在 Bar/PageControl 内 | 立即进入 Dormant 并 wait-for-leave；区域内移动不得重新激活 |
| Draw3 多 contact 或 Host active 状态停止/异常 | 聚合只发布一次 Start/End；停止/异常补 End，activity count 不泄漏 |

#### 5. Good / Base / Bad Cases

- Good：无论光标是否仍在 UI 接收区，落笔都让第三光源一次性休眠；区内落笔等待真实离开，第一光源、光色过渡与普通 UI 动画在整笔期间继续正常工作。
- Base：宽限期内返回 UI，取消休眠并从当前透明度继续淡入。
- Bad：把 activity count 或绘图静默状态乘到总光影开关上；这会让第一光源和第三光源在整笔期间一起消失。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工验证 UI 外启动、进入 UI、离开后 5 秒内返回、超过 240px、5 秒超时、休眠后仅靠近 240px、Draw2/Draw3 鼠标/笔/触摸落笔、落笔时仍位于接收区和动画关闭。
- 对同一边框像素分别从接受消息区域内外取等距离光标位置，隔离第一光源后确认第三光源贡献一致。
- 性能验证至少比较 `Dormant` 与持续全局移动时的 CPU；`Dormant` 中不得出现由第三光源导致的持续渲染唤醒。
- 分别在 UI 接收区外和 Bar/PageControl 接收区内落笔，确认第三光源立即休眠；区内落笔还必须验证区域内移动不能重新激活、真实离开后下一次自然进入才可激活。整笔持续期间移动并抬笔，第一光源、光色过渡和普通 UI 动画始终不受影响。
- Headless 覆盖 Draw3 聚合状态 `false→true→true→false` 只发送一次 Start/End、多 contact 去重，以及 Host stop/异常 active 清理补 End；多指和快速连续短笔迹下 activity count 最终归零且不存在绘图静默门禁。
- Headless 覆盖屏幕点到 PageControl presentation 点的坐标映射；生产静态审查四窗只调用通知/边界发布接口，且不存在 Surface 本地 cursor-light prepare/reset。
- 无窗口 Bar 离屏回归必须调用真实 `PrepareFrameLighting`：保持屏幕点与 serial 静止，切换非零直移、吸收后的零位移及实际残差原点，断言局部点加同帧原点恒等于屏幕点，光源半径/强度/可见性与分页共享快照不跳变。覆盖正负两轴、负显示器原点、实际 zoom、连续静止帧与原有光照门禁；坐标改变沿既有 lightChanged/damage 链生效。修复前应能确认缓存旧局部点导致失败，修复后通过；不得用手工设置最终光照快照代替真实 Prepare 入口证明修复。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：区域外每次移动都会把休眠延后 5 秒。
deadline = GetTickCount64() + 5000;

// Correct：只在 Inside → Grace 的首次转换设置绝对截止时间。
if (state == TrackingState::Inside)
{
	state = TrackingState::Grace;
	deadline = GetTickCount64() + 5000;
}
~~~

~~~cpp
// Wrong：任意 UI 的最近距离会同时改变所有边框的第三光源亮度。
cursorIntensity = lifecycleIntensity * nearestVisibleRegionIntensity;

// Correct：区域距离只裁剪唤醒；第三光源贡献由自身 240px 径向画刷决定。
cursorIntensity = lifecycleIntensity * controlIntensityScale;
cursorRadius = 240.0 * zoom;
~~~

~~~cpp
// Wrong：PageControl 用本地 hover 位置创建另一束鼠标光，离开 HWND 后即失去全局更新。
surfaceRenderer.PrepareSurfaceCursorLight(dt, localCursor, hoveredButton);

// Correct：分页只通知唯一状态机，并消费 Main Bar 发布的最终屏幕光源快照。
NotifyBorderCursorSurfacePointerEntered();
PublishBorderCursorSurfaceBounds(index, presentedBounds, true);
scene.SetSharedLightingSubscribed(true);
~~~

~~~cpp
// Wrong：把整段绘图活动当成总光影静默，连第一光源也一起关闭。
edgeLightingEnabled = BarUiEdgeLightingEnabled && !canvasDrawingQuiet;

// Correct：Started 无条件休眠第三光源；光标仍在接收区时等待真实离开。
CanvasDrawingActivityGuard guard;
RunStroke();
if (started)
	SuspendBorderCursorTracking(hWnd, true);
~~~

### UI3 边缘光影实验开关契约

#### 1. Scope / Trigger

修改 UI3 边缘点光、第三鼠标光的设置或 Inkeys3 配置时，必须同时核对 Setting、`Inkeys.Other.Config` schema、Bar 运行时门禁与启动同步；传统 `IdtFloating` 不在该契约内。

#### 2. Signatures

~~~cpp
namespace Inkeys::UI::Bar
{
	export void SetEdgeLightingOptions(bool enable, bool dynamic);
}
~~~

持久化路径为：

~~~text
Experimental.Inkeys3.UI3.EdgeLighting.Enable  : bool = true
Experimental.Inkeys3.UI3.EdgeLighting.Dynamic : bool = true
~~~

#### 3. Contracts

- `Enable=false`：第一主光、第三鼠标光和 Gaussian 柔光均不绘制；基础灰边继续绘制；停止仅服务于光影的动画推进，并请求第三光源进入 Dormant、停止光效观察；保留共享交互注册。
- `Enable=true, Dynamic=false`：第一主光和 Gaussian 柔光保持；只有第三鼠标光关闭，禁止由 Raw Input 推进该光源；共享交互输入仍继续处理。切换关闭时可以沿既有 300ms 淡出收尾。
- `Enable=true, Dynamic=true`：保持第三光源 `Dormant / Inside / Grace` 状态机；重新开启不主动全局取鼠标，等待自然进入 UI3。
- Setting 只在 UI3 开启时显示总开关，只在总开关开启时显示动态开关；隐藏动态项不得覆盖其持久化值。
- schema 是字段声明、默认值和 JSON 读写的唯一来源；不得在 `Other.Config.cpp` 为两个 bool 添加特判。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 旧配置缺少新字段 | schema 默认值均为 `true`，升级后视觉保持不变 |
| 关闭任一动态门禁 | 立即阻止新的激活/位置处理，并向 Bar 窗口投递既有休眠消息 |
| 配置写入失败 | 本次运行时状态仍即时生效；不得直接从 Setting 线程修改 D2D 或 Raw Input |
| 总开关关闭期间工具或颜色变化 | 不因第一光源位置、颜色或过渡继续产生光影专用渲染帧 |

#### 5. Good / Base / Bad Cases

- Good：关闭动态开关后主光仍可见，第三光源完成收尾后不再因 Raw Input 唤醒渲染，交互注册继续保留。
- Base：关闭总开关后只剩基础灰边；重新开启后恢复主光，并保留此前动态开关选择。
- Bad：把动态开关乘到全部光影或基础灰边上，导致关闭第三光源时第一主光也消失。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工验证四种组合：总开关关、总开关开且动态关、两者全开、动态关后重启。
- 检查 `main.json` 两个路径、旧配置缺失字段默认值、设置卡片条件显示与容器高度。
- 在动态关闭和总开关关闭时观察光效停止唤醒但共享 Raw Input 注册保留，以及静止/移动 CPU，确认第三光源不再唤醒渲染。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：动态开关错误地关闭所有边缘光影。
drawPrimaryLight = edgeLightingEnabled && dynamicEdgeLightingEnabled;

// Correct：动态开关只进入第三光源的激活与可见性条件。
drawPrimaryLight = edgeLightingEnabled;
desiredCursorLightVisible = edgeLightingEnabled && dynamicEdgeLightingEnabled
	&& cursorInputAvailable;
~~~

### UI3 按钮边框光影约定

按钮未选中时通常保持背景透明，但仍可能需要显示鼠标第三光源。此时不得复用按钮本体 `pct` 作为边框光影透明度；应使用 `BarUiShapeClass::frameLightPct` 独立控制光影，保持 `framePct = 0`，并配置：

~~~cpp
shape.frameRendering = BarUiFrameRenderingEnum::PointLight;
shape.framePrimaryLightEnabled = false;
shape.frameCursorLightIntensityScale = buttonIntensity;
~~~

按钮的 `buttonIntensity` 应由颜色边框的第三光源强度按产品要求折算；本项目当前按钮使用色块边框强度的一半。按下时同时缩放按钮和 PointLight 边框，并降低 `frameLightPct`；PointLight 的 Gaussian 漫反射继续由 `BarRenderingAttribute::pointLightDiffuseExtraWidth` 覆盖脏区。

错误做法是把 `frameLightPct` 绑定到按钮本体 `pct`，这会让未选中按钮的透明背景同时隐藏其光影；正确做法是让 `Shape()` 在本体 `pct == 0` 且 `frameLightPct > 0` 时仅绘制 PointLight 边框。

### UI3 按钮悬停与按压衔接约定

主栏按钮的 `5s` 表示指针仍停留时，悬停背景完成快速显现后自然淡出的时长；它不是鼠标移出的退出时长。鼠标移出继续使用 `BarButtonHoverExitDur` 快速退出，避免残留多个按钮背景或在布局更新中出现长时间闪烁。

复用主栏悬停状态机的独立控件必须同时遵守：

- `Showing` 完成后才进入 `5s` 的 `Fading`，同一次进入不得反复重启计时；
- 鼠标移出调用普通非立即 `StopHover`，不得为独立控件另传 `5s`；
- 鼠标按下先建立按压状态，再以 `preserveVisual=true` 结束悬停，使透明度和填充色从当前值连续过渡到按下态；禁止先 `SetDirect(0)` 再升到按下透明度；
- 隐藏、失效或选择状态不再允许悬停时仍可立即清零，不能误用按下的保留视觉路径；
- 属性栏批次同步不得覆盖标记为 `animateWhenDisabled` 的悬停时长。

手工验证至少覆盖：指针驻留至自然淡出、淡出中移出、显现中按下、淡出中按下、拖出取消，以及抬起后未移动指针时不重新触发悬停。

### UI3 复合视觉切换状态机合同

#### 1. Scope / Trigger

修改 Bar 的笔型、粗细预览、Laser 外套/内芯、粗细快捷按钮或笔型扩展入口时适用。一次工具切换可能同时改变几何、颜色、内容语义和交互资格，这些维度必须从同一组当前动画值派生。

#### 2. Signatures

~~~cpp
BarThicknessPreviewMorphSample ResolveBarThicknessPreviewMorph(double progress) noexcept;
BarThicknessPresetVisualKind ResolveBarThicknessPresetVisualKind(
	BarThicknessPreviewVisualKind penKind) noexcept;
BarThicknessPresetOpacitySample ResolveBarThicknessPresetOpacity(
	double numberProgress) noexcept;
BarPenTypeExtensionPresentation ResolveBarPenTypeExtensionPresentation(
	bool targetInteractive, double visualProgress, double contentOpacity) noexcept;
BarLaserPreviewPhase ResolveBarLaserPreviewPhase(
	BarLaserPreviewPhase phase, bool targetLaser,
	bool coreAtLaserEndpoint, bool coreAtNonLaserEndpoint,
	bool shellHidden, bool shellExpanded) noexcept;
BarLaserPreviewTargetPolicy ResolveBarLaserPreviewTargetPolicy(
	BarLaserPreviewPhase phase) noexcept;
double ResolveBarLaserPreviewEnvelopeThickness(
	double coreThickness, double outerThickness,
	double shellProgress) noexcept;
BarLaserPreviewLayerGeometry ResolveBarLaserPreviewLayerGeometry(
	double layerThickness, double animatedOuterDiameter,
	double sliderProgress, double sliderTrackThickness) noexcept;
~~~

#### 3. Contracts

- Laser 预览固定使用 `NonLaserStable -> EnteringCore -> EnteringShell -> LaserStable -> LeavingShell -> LeavingCore`。进入时芯宽、颜色、曲率/圆角和荧光渐变先在 `0.4s` 内连续到达白色芯端点，随后彩色外壳再用 `0.4s` 从芯宽展开；退出时先收外壳，再改变 semantic core。
- `EnteringShell` 与 `LeavingShell` 对 core thickness、outer thickness、morph 和 white mix 使用 `Hold` 目标策略，不得用已经切换的逻辑笔宽重新提交 target。只有彩色外壳完全隐藏并进入 `LeavingCore` 后，才允许芯层转向非 Laser 目标；反向切换继续使用锁存的 Laser target。
- 彩色外壳先绘制、semantic core 后绘制。外壳 RGB 必须直接读取 `stateMode.Pen.Laser.color`，默认红色；Laser 模式下预设色或自定义颜色提交后，下一帧预览使用新 RGB。切换到其他笔时，正在退场的外壳仍读取 Laser 独立颜色，不能改用新工具颜色。预览包络使用 `max(coreThickness, currentShellThickness)` 约束曲线振幅和裁剪，但 semantic core 的实际绘制宽度仍只读取 core thickness，不能被外壳宽度替代。
- 白芯与彩色外壳的曲线/胶囊两端圆心必须共享阶段化 outer thickness。曲线路径以 `outer / 2` 计算端点，圆角矩形按 `(outer - layerThickness) / 2` 水平收进；因此壳进度为零时外壳层被白芯完全覆盖，壳展开时只改变 stroke width，不移动两端圆心。Slider 展开时 endpoint diameter、core thickness 和当前 shell thickness 必须一起连续趋向 track thickness。
- `GetFrameSolidColorBrush` 返回帧内复用的可变 solid brush；后续调用会原地改色。不得跨另一处 `GetFrameSolidColorBrush` 调用保留画刷颜色假设；彩色外壳绘制后必须紧邻 semantic core 绘制重新提交 `previewColor`，否则白芯会继承外壳颜色。
- 颜色、曲率、圆角矩形进度、外套宽度和内芯宽度均从当前值续接，中途反向不得回到任一端点；Laser 稳态的 `3/5/7 DIP` 切换同时 retarget 芯宽和外宽。
- Circle/Number 切换时锁存 outgoing 内容：Circle -> Number 先保持旧圆直径并淡出，再显示已锁存的新数字；Number -> Circle 先保持旧数字并淡出，再显示圆。Circle -> Circle 才允许直径连续 retarget；切换中点旧新内容透明度均为零。
- 工具失去扩展资格时，命中区和按压状态立即失效，arrow/divider 的视觉则按 current progress 退场；其几何锚点和颜色必须派生 selected button 的当前 `x/y/frame`，不能读取新目标位置或首帧直接改 Accent。
- 重新进入 Pen 面板时按当前工具直接初始化稳定 Circle/Number 语义；不得从离开前工具制造一次假的内容切换。动画中的状态不得仅按 `laserActive`、`ModeSelect` 等目标布尔量选择互斥绘制分支。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Highlighter -> Laser | 渐变、圆角、颜色和芯宽先连续到白芯端点，之后所选颜色的外壳才出现 |
| Laser -> Highlighter | 彩色外壳完全隐藏前 core/outer/morph/white target 保持 Laser 端点；之后才连续恢复渐变矩形 |
| Pen <-> Laser 中途反向 | `EnteringCore <-> LeavingCore` 从当前芯值反向；`EnteringShell <-> LeavingShell` 只反向 shell progress，并保留锁存 target |
| Laser 稳态切换粗细 | core 与 outer 同时连续 retarget，shell 保持完全展开 |
| 细笔/粗笔 -> Laser | outer 的当前动画值分别增大/减小，白芯水平 span 连续收窄/拓宽；彩色外壳与白芯端点圆心始终一致 |
| Laser 预览 -> Slider | endpoint diameter、core 和当前 shell 同步 morph 到 track thickness，不在交接帧改变端点 |
| 彩色外壳与白芯同帧绘制 | 外壳以 `Pen.Laser.color` 先画；其后重新以 `previewColor` 配置帧内 solid brush，再画白芯或渐变失败 fallback |
| Circle <-> Number 中途反向 | 从当前透明度继续；outgoing 内容和尺寸保持锁存 |
| Laser 使扩展入口失效 | 命中立即归零；arrow/divider 平滑退场且不残留 |
| 离开 Pen 后以 Highlighter 重进 | 首帧直接为稳定数字，不出现圆形过渡 |

#### 5. Good / Base / Bad Cases

- Good：阶段 helper 决定 `Hold/Laser/NonLaser`，`LeavingShell` 只提交 shell target；彩色外壳隐藏的交接帧进入 `LeavingCore` 后才提交新笔型语义。
- Base：稳态 SoftPen/HardPen/Laser 为 Circle，Highlighter 为 Number；超出按钮内框的 Circle 仍按既有规则显示真实数字。
- Bad：目标工具一改变就在 `LeavingShell` 使用新 `penThickness` 计算芯壳目标，或在彩色外壳改色前缓存共享 solid brush 并用于后绘白芯；前者造成跳宽，后者使白芯继承外壳颜色。

#### 6. Tests Required

- Headless 覆盖 0/25/50/75/100% 正反切换、六阶段首帧/中间帧/交接帧/终点、`Hold/Laser/NonLaser` 目标策略、锁存 target、芯/壳包络、共享端点圆心、细笔/粗笔切入 span 和 Slider endpoint morph、颜色端点/反向、Circle/Number 锁存、Circle -> Circle 直径、四种工具初始化语义和扩展入口资格/当前锚点/当前颜色。帧内 D2D brush 身份由绘制顺序静态审查和完整构建验证。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 与 `Release | ARM64`；受限环境只运行 `InkeysHeadlessTests.exe --no-window`，不得启动 GUI。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：LeavingShell 读取新工具宽度，彩色外壳退场期间白芯提前改变。
core.SetTar(penThickness / 3.0, duration);
outer.SetTar(penThickness, duration);

// Correct：Hold 阶段只改变 shell；LeavingCore 才提交非 Laser 语义。
auto policy = ResolveBarLaserPreviewTargetPolicy(phase);
if (policy.core != BarLaserPreviewSemanticTarget::Hold)
	SubmitCoreTarget(policy.core);
shell.SetTar(policy.shellExpanded ? 1.0 : 0.0, duration);

// Wrong：保存芯层 brush 后，外壳调用把同一帧内 brush 原地改成所选颜色。
auto coreBrush = GetFrameSolidColorBrush(previewColor, opacity);
DrawLaserShell();
DrawCore(coreBrush);

// Correct：彩色外壳画完后紧邻芯层重新配置共享 brush。
DrawLaserShell();
auto coreBrush = GetFrameSolidColorBrush(previewColor, opacity);
DrawCore(coreBrush);
~~~

### UI3 展开按钮点击合并合同

- `BarToggleClickCoalescer::TryBegin(channel, now)` 对每个 `BarToggleChannel` 独立记录最近一次成功 toggle；第一击立即通过，距上次同通道操作小于 300ms 时返回 `false`，达到 300ms 边界后重新通过。该窗口用于覆盖触摸误双击，不跟随可能过长的系统双击时间。
- 门控只能放在业务代码已经确定本次操作是展开/收起之后。绘制和几何按钮不在目标工具状态时，第一击只切换工具且不得写入门控；已经处于目标工具时，再由 DrawAttribute / GeometryAttribute 通道决定是否 toggle 属性面板。
- `ResolveBarDrawButtonToggleDecision(drawAttributeOpen)` 只在 DrawAttribute 门控成功后执行。面板打开和关闭都只能改变自身显隐，不得改写 Laser、软笔、硬笔或荧光笔记忆；离开 Pen 到选择、橡皮或图形后再返回绘制，也必须恢复离开前笔型。门控拒绝的点击不得提前改变任何面板状态。
- 主按钮、更多、粗细调节三角和笔属性三角分别使用独立通道；普通命令、粗细预设、FineDial 提交、显式关闭、浮层外点击关闭以及 Win32/HiMsg 原始消息不进入门控。
- Headless 测试必须覆盖首次立即执行、299ms 合并、300ms 边界通过、不同通道互不阻塞、DrawAttribute 开/关不改变笔型，以及“记忆 Laser + Pen/非 Pen 顶层模式”的激活矩阵；按钮调用点另以静态审查和完整 Solution 构建确认面板 toggle 不包含笔型副作用。

错误做法是在 `WM_LBUTTONDOWN` / HiMsg 队列层全局丢弃第二击，这会破坏“选择状态双击绘制：第一击切换工具、第二击展开属性”。正确做法是保留两次输入，仅合并第二次真正重复的展开/收起动作。

### UI3 动画批次加入与关键帧中点

`【直接确认；设计约定】` `BarUiTimelineClass::CanJoin(double maxProgress = 0.5)` 是主栏布局变化和绘制属性加入主栏批次的统一判断入口；无效参数同样回退到 `0.5`，边界判断为 `GetProgress() <= maxProgress`。

| 线性时间轴进度 | 契约 |
| --- | --- |
| `progress <= 0.5` | 新目标可以复用当前批次的剩余时长，并继续共享原截止时间。 |
| `progress > 0.5` | 新目标必须创建完整的新批次；旧的单中间关键帧不再继承。 |

该边界与现有唯一中间关键帧的 `0.5` 时刻一致。若重新把加入阈值设到中点之后，会产生“关键帧已经过去、但新目标仍沿用旧批次语义”的区间，可能表现为重复收拢或后半程被过度压缩。修改批次阈值时应搜索全部 `CanJoin()` 调用，并手工覆盖中点前、中点附近和中点后的目标变化。

### UI3 复合面板收起与换边关键帧

计算 UI 的循环会每帧重复提交布局目标。一个动画属性必须先在局部变量中算出包含方向、行距和边距的最终值，再调用一次 `SetTar()`；禁止先提交基础坐标，随后通过 `value.tar + offset` 再次提交。同一帧的不同目标会反复执行 `startV = val` 和 `progress = 0`，导致宽高继续缩放而位置停在展开值。

颜色块 ColorSelect1..11 的 `ft` 同样遵守此约束：选中状态分支只处理勾号，描边宽度由末尾几何循环按展开/紧凑比例提交。禁止先 `ft.SetTar(1)` 再 `ft.SetTar(compactScale)`；即使随后同步批次剩余时长，Back 也会重复采样初段，使归一描边变粗并污染柔光遮罩键。保留批次同步及可见性/换边的一次性重建，不用逐帧 `SetDirect` 取消正常缩放。

收起终点和换边中点的根面板均以锚点为中心，并限制为固定紧凑尺寸。直属 Shape 的隐藏 `x/y`、宽高、圆角和边框必须由最终展开目标乘固定展开到紧凑倍率；嵌套 SVG/Word 的局部坐标和尺寸也使用同一倍率。根面板负责对齐锚点，内部控件保留完整布局的等比微缩关系。隐藏中点不得使用运行中的子对象 `val` 乘“紧凑宽度 / 当前面板宽度”，因为位置动画被打断或重启时，两者不再保持同一比例，会放大旧的展开坐标。

直接绘制、没有独立 Shape 承载的几何也必须加入同一动画契约。若其布局依赖上下方向，不得在换边开始时直接读取已经切换的方向布尔值；应从正在动画的承载 Shape 推导连续位置，或为该几何提交同批次关键帧，否则它会在其他控件收缩前瞬间跳到新布局。

粗细墨迹预览必须从动画中的粗细区域和调节按钮当前位置推导当前预览侧，不能直接读取已经倒转的 `primaryBar` 目标。硬笔振幅、真实设备像素粗细和避让中心偏移都要限制在当前预览高度内；提示徽标换边时，中心偏移使用同一连续方向量反转。最终绘制还必须裁剪到粗细区域边框内侧，作为动画中断、超限粗细和浮点误差的兜底，不能只依赖稳态几何计算保证不越框。

由面板当前几何派生的徽标、提示图标等控件，显隐和透明度必须读取承载内容的当前动画值（`val`），不能直接读取 `fold`、`drawAttribute` 等目标状态后立即置零；交互门禁可以立即关闭，但视觉应继续等比收拢到动画终点。

可关闭浮窗在点击关闭后可以立即撤销固定态和交互命中，但叉号等随浮窗收起的子视觉必须锁存到浮窗当前进度归零，再随浮窗的当前透明度与几何一起消失。禁止用 `pinned ? opacity : 0` 直接控制叉号，否则目标状态切换会让子视觉先于承载浮窗瞬间消失；正确做法是让 `pinned` 只开启锁存，收起进度到达终点后才清除。

手工验证至少覆盖面板在主栏上方和下方的展开/收起，以及上到下、下到上的换边。检查颜色块、区域背景、按钮 Shape 及其 SVG/Word；不允许出现尺寸已缩小但局部 `y` 仍停留在展开值，或隐藏点落在 `60×30` 面板之外。

## 设置窗口与 ImGui

`【直接确认】`：

- `Setting.Base.cppm::CreateDeviceD3D` 借用 `FrameContext::epoch` 的共享 WARP device/immediate context，并通过传统 `IDXGIFactory::CreateSwapChain` 创建 Setting 独占的 discard swap chain 和 RTV；
- `Setting.cpp` 的持久协程 session 调用 `ImGui_ImplWin32_Init`、`ImGui_ImplDX11_Init/NewFrame/RenderDrawData/Shutdown`；`WM_SIZE` 只排队宽高，渲染线程释放 RTV、`ResizeBuffers` 后重建，遮挡时用 `DXGI_PRESENT_TEST`；
- 设置图片使用 `ID3D11ShaderResourceView*` 作为 `ImTextureID`；`DibSurface` 的 BGRA 字节上传为 `DXGI_FORMAT_B8G8R8A8_UNORM` immutable texture/SRV；
- `Inkeys/Inkeys.vcxproj` 编译 `additional/imgui/imgui_impl_win32.cpp` 与 `imgui_impl_dx11.cpp`，仓库不再随附 ImGui DX9 backend；
- DX11 backend 不在运行时调用 `D3DCompile`：`IDR_SHADERS2` 是 VS、`IDR_SHADERS1` 是 PS，两个预编译 CSO 位于 `UI/RenderPipeline/Assets/ImGui` 并由 `Inkeys.rc` 嵌入 EXE。

`【合理推断】` 设置 UI 的局部改动应复用 `Setting.Widgets`、`Setting.Wrap` 及现有字体/纹理路径；普通设置功能不得顺带更换 device、swap-chain、shader 或 SRV 所有权。

### Setting 外观条件卡片约定

- “动画速率”只在 `Experimental.Inkeys3.UI3.Animation.Enable=true` 时显示；“动态边缘光影”只在 `Experimental.Inkeys3.UI3.EdgeLighting.Enable=true` 时显示。隐藏只影响布局，不得覆盖 `SpeedRate` 或 `EdgeLighting.Dynamic` 的持久化值。
- 外观容器高度和两个条件卡片的渲染门禁必须读取同一份帧开始快照，按 `5 + showAnimationRate + showDynamicEdgeLighting` 张卡片计算（含任务栏缩放基准三项下拉框）；不得分别读取切换前后的 bool，否则同一帧会出现空白或裁剪。
- 用户可见标题走 i18n；改标题时保持不可见 ImGui ID 稳定，避免无必要地重置控件身份。

### Setting 统一渲染与业务队列合同

#### 1. Scope / Trigger

修改 Setting 显隐、WndProc、ImGui session、swap chain/RTV/SRV、配置持久化、模态动作或退出顺序时适用。

#### 2. Signatures

~~~cpp
bool Setting::Initialize();
void Setting::Shutdown() noexcept;
void Setting::Show();
void Setting::Hide();
void Setting::Toggle();
bool Setting::IsVisible() noexcept;
WNDPROC Setting::WindowProc() noexcept;
FrameResult RenderSettingFrame(const FrameContext&);
~~~

#### 3. Contracts

- HWND 所属线程只处理 capture/cursor/IME、生命周期和 `ImGui_ImplWin32_WndProcHandlerEx`；专用 recursive mutex 串行访问 ImGui IO，因为 Win32 backend 的 `SetCapture`/`ReleaseCapture`/IME 调用可能同步重入同一 WndProc。ImGui context、DX11 backend、NewFrame/draw/Present 只由 RenderPipeline 线程使用。
- Setting 可见且可呈现时返回 `Continue`，因此独自按统一 16,666,667 ns 上限连续绘制；隐藏时逆序释放 backend、SRV、swap chain/RTV，返回 `Idle`。resize、遮挡和 epoch 变化由 `SessionState` 决策，不在 WndProc 直接操作 D3D。
- Setting 隐藏在 HWND owner 只释放自己的 capture；逻辑/原生 Hide 锁存输入会话失效，Show 和旧 CommitEpoch 不清除，原渲染 owner drain 后才建立新会话；早期消息与迟到 native Hide 合同见 [F4](canvas-freeze-boundaries.md#f4设置隐藏必须结束原生捕获和旧-imgui-输入会话)。
- 文件写盘、Shell、模态确认、重启和 DDB 操作进入单一 FIFO。配置命令在生产者线程冻结 JSON 或 `Inkeys::Config` 副本；配置 I/O 命令的 worker 不读取实时 `setlist`、`pptComSetlist` 或 `Inkeys::config`；绘图状态同步命令按下文专用合同读取最新运行时状态。停止时禁止新命令，并按 FIFO 排空已接收命令。
- 自动更新是既有长期网络服务；FIFO 只串行化其启动命令，不把长期下载循环占用为业务 worker 本体。
- 退出顺序固定为：停止显隐/输入生产者，隐藏并请求 Settings，渲染线程 drain session，`Unregister(Settings)`，排空业务 FIFO，join Bar/PPT，停止 Window Service，最后 `RenderPipeline::Shutdown()`。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `Present` / `ResizeBuffers` 返回 device removed/reset/internal error | 返回 `DeviceLost`，由唯一管线线程恢复 epoch |
| 普通 Present/resize 失败 | 只返回 `Retry` 并重建 Setting session，不影响 Bar/PPT |
| `DXGI_STATUS_OCCLUDED` | 后续帧用 `DXGI_PRESENT_TEST` 探测，成功后恢复绘制 |
| 隐藏或 generation 变化 | 在渲染线程逆序释放旧 session；可见时按新 epoch 重建 |
| 业务命令完成 | 发布不可变 completion snapshot 并只请求 Settings |
| 退出时 FIFO 尚有配置写盘 | 排空后 join，不得 request_stop 后清空未执行命令 |
| Bar 在 Setting drain 前观察到 `offSignal` | Bar 返回 `Idle`，不得以 `Stop` 提前结束共享线程 |

#### 5. Good / Base / Bad Cases

- Good：Setting 连续绘制时 Bar/PPT 保持 idle；隐藏后 session 释放且管线无限等待，最后一次配置仍写盘。
- Base：show→resize→occluded→visible→hide→show，epoch 不变时复用共享 device 并重建 Setting 私有呈现资源。
- Bad：在渲染回调直接 `Write()`/`MessageBox`，或 worker 到退出时清空尚未执行的 FIFO；前者阻塞所有 UI，后者丢配置。

#### 6. Tests Required

- Headless 覆盖显隐、resize、occlusion、generation、业务完成快照和 device loss 分类；WARP 初始化断言 FL11.0+、context、DXGI/D2D/DWrite 资产有效。
- 完整 Solution `Debug|ARM64` 构建，静态审计旧 hardware device、24 FPS、`SettingMain`、`test.select`、运行时 `D3DCompile` 和 flip/DirectComposition 均不存在于活动路径。
- GUI 受限任务只运行 `InkeysHeadlessTests.exe --no-window`。Win7 SP1 + KB2670838 只可声明传统 CreateSwapChain/discard/FL11.0 fallback 的静态兼容，未经实机不得声称已验证。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：渲染线程同步写盘并从 worker 延迟读取可变全局配置。
Inkeys::config.Write();
queue.push({ .kind = WriteConfig });

// Correct：入队前冻结值，worker 只消费 owned payload。
command.configSnapshot = std::make_shared<Inkeys::Config>(Inkeys::config);
queue.push(std::move(command));
~~~

## Whiteboard 分页与 workspace 生命周期合同

> 2026-10-15 初版扩展以 [whiteboard.md](whiteboard.md) 为准：左栏含 More 为 305×80 DIP，右栏仍为 230×80 DIP；页面主题和存储独立，退出恢复桌面基线而非强制折叠。以下共享 HWND、呈现事务与窗口组资源合同继续适用。

### 1. Scope / Trigger

修改 PageControl 的 Whiteboard 布局、页码发布、Draw3 workspace 切换、辅助面板收起、taskbar/activation 或退出恢复时适用。该功能横跨 `IdtState`、Draw3、Bar、PageControl 与 Window Service，任何一层都不得自行推导另一层尚未确认的稳定状态。

### 2. Signatures

~~~cpp
PageStateTransaction::Publish(int currentPage, int totalPage, bool switching)
    -> PageState;
Whiteboard::PublishPageState(int currentPage, int totalPage, bool switching)
    -> void;
Whiteboard::PublishExpandedLayoutTarget(bool expanded) -> void;
PageControl::PublishWhiteboardState(const WhiteboardState&) -> void;
PageControl::ResolveWorkspaceMode(Surface, const PptState&,
    const WhiteboardState&) -> WorkspaceMode;
Bar::CollapseAuxiliaryPanels(bool cancelCapture = true) -> void;
Window::Service::EnterWhiteboardWindowMode() -> bool;
Window::Service::LeaveWhiteboardWindowMode() -> bool;
Window::Service::MinimizeWhiteboardWindowGroup() -> bool;
Window::Service::RestoreWhiteboardWindowGroup() -> bool;
~~~

### 3. Contracts

- 白板左侧分页主体为 `305x80 DIP`，在 Next 右侧增加 More；右侧仍为 `230x80 DIP`。Previous/Page/Next 跨工作区保持稳定实例，左侧 More 和菜单按钮也使用共享 Bar `twoTwo`/菜单视觉行为。按钮布局、SVG、颜色、光效、交互、动画和 visual damage 来自共享 Bar 运行时；PageControl 拥有拓扑、独立资源与 ULW present 映射。页码按钮保持标准交互视觉，业务 no-op。
- `PptBottomLeft/PptBottomRight` 是 Presentation 与 Whiteboard 共用的唯一左右底部分页 HWND；不得恢复 `WhiteboardLeft/WhiteboardRight` 窗口角色、窗口规格或句柄。`Inkeys.UI.PageControl` 是两种工作区唯一的 surface host、输入路由和呈现所有者，PPT/Whiteboard 只发布状态与业务回调；Whiteboard 不创建 DragHandle，也不继承 PPT 的拖动、滚轮、长按、缩放或位置持久化。
- PageControl 的稳定态为 `Hidden / PptCompact / WhiteboardExpanded`。Bottom 的放映布局资格使用 `presentationActive && showBottomPair`，Middle 仍使用 `presentationVisible && showMiddlePair`，Fit/Clamp/组碰撞预算使用同源资格；白板最小化继续单独覆盖为 Hidden。进入/退出白板的分步发布不能使原本应连续可见的 Bottom 经 Hidden、整栏淡出和重新入场；业务输入继续沿原可见性、事务与会话门禁。`WhiteboardState::expandedLayoutTarget` 对底部 surface 的优先级高于 PPT 布局资格，并隐藏两侧；背景 `active` 只表示 Draw3 Whiteboard 首帧已就绪后的 Freeze 目标。两者不一致时 PageControl 必须锁定输入，不再使用 Presentation/Whiteboard 双 renderer owner 或 `TransitionHidden` 中间 owner。
- PageControl 的 render/present 事务锁只保护 D2D、GDI interop、共享 Bar visual state 与 present 快照；同步 `Window::Service::Hide/SetBounds/Show` 必须在该锁外执行。由于只有一个呈现所有者，迟到帧必须消费最新快照后重新定向，不能靠调用方抢先隐藏共享 HWND 仲裁。
- Enter 先撤销 capture、关闭 PPT DragHandle 命中并进入 Whiteboard window mode；PPT 可见状态保持当前 COM 事实。窗口模式事务成功后立即发布 expanded 目标，使已显示 PPT 底栏从当前实际位置重定向到可见描边外沿距屏幕底边 `5 屏幕 DIP` 的 Whiteboard 角落、收拢 Drag slot 并连续形变；同屏默认偏移/局部倍率时主体和箭头的 Y 保持不变；原本未显示时直接使用左 `305x80 DIP`、右 `230x80 DIP` 的最终主体位置/几何渐显。Whiteboard 背景 active 仍须等待 Draw3 Whiteboard 首帧就绪。Exit 先发布 compact 目标并关闭背景 active，反向回到 PPT 最新运行时位置；不得在交接前后显式隐藏两个共享底部 HWND。Exiting 期间反向重入从当前背景、按钮、内容、DragHandle 与 HWND 插值值重新定向，再等待 Draw3/Freeze readiness，期间保持输入锁定。
- `totalPage` 至少为 1，`currentPage` 限制到 `[1, totalPage]`。首次发布若直接为 `switching=true`，稳定基线必须来自该次真实输入。
- 进入翻页事务时锁存上一稳定帧的 Previous enabled 与 Add/Arrow；事务中锁定分页和菜单交互，未变化的 SVG 和文字不得重启动画。普通 Arrow 跨工作区保留同一 `barMore` 对象并只缩放；追加页事务全程保持 Add，只有稳定语义真实发生 Arrow/Add 变化时才运行一次同批 SVG/文字转换。
- `Inactive / Entering / Active / Exiting` 的每次 workspace 转换都先调用 `CollapseAuxiliaryPanels(true)` 并撤销 Whiteboard capture。进入稳定白板时绘制属性、几何属性、更多、笔菜单和粗细预览均保持关闭。
- More 在左下同一 HWND 向上展开关闭白板、最小化白板、主题样式；主题内联展开六色与空白/点阵/网格预览。左/底主体锚点固定，窗口大小、命中、阴影及 damage 使用同帧几何；不得新增 popup HWND。菜单外点击、Escape、翻页、最小化和工作区切换关闭菜单。Hook 只异步投递收起请求，排除左侧控件本身，原画布点击继续正常处理。
- 白板主栏 A2 按钮恢复；有效放映存在时为“返回放映”，否则为“关闭白板”，后台 PPT 结束会同步文案；白板组最小化时主栏优先显示 `barWhiteboard` 和“返回白板”/`Resume`，点击仅恢复原窗口组，最小化/恢复边沿立即刷新图标和文字；主栏非最小化关闭与菜单关闭共用 `RequestWhiteboardActive(false)`。
- 每页主题及笔迹/历史自动保存到进程会话下的多页 `.uink`，不受桌面自动保存开关影响。新页继承当前主题，主题修改不进墨迹撤销栈。色板、纹理、冷页加载和页级内存合同见 [whiteboard.md](whiteboard.md) 与 [draw3-integration.md](draw3-integration.md#whiteboard-页级-uink-与编辑历史合同)。本次替代此前无保存、固定墨绿的初始范围；仍不支持平移、缩放和历史浏览。
- Freeze 是 Whiteboard mode 的唯一 `WS_EX_APPWINDOW`、任务栏和 activation anchor；Drawpad 可激活但保留 `WS_EX_TOOLWINDOW`，其余辅助窗口不进入任务栏。整个 owner group 保持 `HWND_NOTOPMOST`，最小化/恢复只恢复此前可见成员。
- 退出必须恢复 `DrawpadPresentation` click-through 并释放白板 dock request/lock；有有效 PPT 则返回放映，没有则回桌面。最终回桌面恢复首次进入前的主栏基线，不再强制 `fold=true`。窗口顺序保持 `LeaveWhiteboardWindowMode()` 后 `SetOverlayTopmost(true)`，不得让 style 事务覆盖恢复后的 Z 序。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `3/3 -> 3/4 -> 4/4` 且 switching | Next 始终为 Add；分页及菜单按钮锁输入，保留既有内容语义 |
| 普通翻页进入末页 | 事务中保持 Arrow，稳定后才转 Add |
| 首次发布即 switching | Previous/Add 基线来自真实页码，不从内部 `1/1` 泄漏 |
| Enter/Exit 中途反向请求 | 先切换 expanded 目标并回到完整的另一状态事务；PageControl 从当前插值值重新定向且共享 HWND 不隐藏，背景 active 不提前发布 |
| `EnterWhiteboardWindowMode()` 失败 | Window Service 回滚 Presentation style；PPT 可见性按当前 COM 页数恢复 |
| PageControl present 与 `PublishActive(false)` 交错 | present 先结算 D2D 事务；下一帧按最新快照反向重定向，且不在 Scene 锁内同步调用 Window Service |
| Whiteboard group 最小化/恢复 | Freeze 作为组锚点；仅恢复快照中可见的成员 |
| 窗口状态调用失败 | phase 不发布假稳定态，下一轮继续收敛或回滚 Presentation |

### 5. Good / Base / Bad Cases

- Good：追加页时只改变真实变化的当前页/总页数，Add 和未变化文字不闪回 Arrow 或重启动画。
- Base：第一页 Previous 禁用，离开第一页的事务结束后变为启用。
- Bad：把 `switching` 映射到 `enabled=false` 或依据每个中间 snapshot 重算 Add/Arrow，导致文字、图标和尺寸反复切换。

### 6. Tests Required

- Headless 覆盖页码归一化、追加页、到末页、离开第一页、首次 switching、按钮 no-op、稳定实例、普通 Arrow SVG 身份、Arrow/Add 同批转换、PPT/Whiteboard 双向位置/布局、DragHandle 门禁、Whiteboard drag/wheel/long-press/persist 负向断言、反向重入、首次渐显、输入门禁与窗口 activation style/group 状态；静态审查 workspace 交接不显式隐藏共享底部 HWND。
- 使用 ARM64 MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`，并执行 ARM64 `InkeysHeadlessTests.exe --no-window`。
- GUI 环境另行执行连续 50 次 Enter/Exit、任务栏最小化/恢复、桌面首次点击穿透和辅助面板默认关闭验收；无窗口测试不得冒充这些结果。

### 7. Wrong vs Correct

~~~cpp
// Wrong：翻页中间快照改变稳定视觉语义。
state.nextIsAdd = currentPage >= totalPage;
state.nextEnabled = !switching;

// Correct：事务只关闭输入，边界视觉来自上一稳定帧。
state = transaction.Publish(currentPage, totalPage, switching);
// state.nextInteractive == false while switching
~~~

~~~cpp
// Wrong：双 renderer 交接靠隐藏共享 HWND 仲裁，连续布局必然断帧。
ppt.PublishVisible(false);
service.Hide(WindowRole::PptBottomLeft);

// Correct：单一 PageControl 根据两份快照从当前帧重定向布局。
pageControl.PublishWhiteboardState({
    .expandedLayoutTarget = true,
    .active = false, // Draw3/Freeze 尚未就绪，输入继续锁定。
});
~~~

## Win32 Window、DibSurface 与 HiMsg 合同

### 1. Scope / Trigger

创建或操作 Mag、Freeze、Drawpad/DrawpadPresentation、四个 UI3 PageControl 窗口（其中底部两个由 Whiteboard 复用）、UI3 Bar、Setting、DisplayObserver，或迁移 Draw2 图像/消息路径时适用。HiEasyX/EasyX 已从源码、工程和链接中删除，不得重新引入。

### 2. Signatures

~~~cpp
Window::Service::Start(std::vector<WindowSpec>) -> bool;
Window::Service::SetBounds(WindowRole, RECT) -> bool;
Window::Service::SetDrawpadSurfaceVisibility(DrawpadSurfaceVisibility) -> bool;
Window::Service::SetClickThrough(WindowRole, bool) -> bool;
Window::Service::EnterWhiteboardWindowMode() -> bool;
Window::Service::LeaveWhiteboardWindowMode() -> bool;
Window::Service::MinimizeWhiteboardWindowGroup() -> bool;
Window::Service::RestoreWhiteboardWindowGroup() -> bool;
Window::Service::RequestTopmostRefresh() -> bool;
Window::Service::SetOverlayTopmost(bool) -> bool;
Window::Service::SetOverlayFullscreen(bool) -> bool;
Window::Service::SetSettingOwnedByDrawpad(bool enabled) -> bool;
Window::Service::PromotePptWindow(WindowRole) -> bool;
Window::Service::Enqueue(WindowRole, Message::Message) -> bool;
Window::Service::StopAndJoin() noexcept;
UI::Setting::Toggle() -> void;
UI::Freeze::StateSnapshot { uint64_t revision; bool active; bool available; };
UI::Freeze::Snapshot() noexcept -> StateSnapshot;
UI::Freeze::SetStateObserver(StateObserver) noexcept -> void;
UI::Freeze::DeactivateIfRevision(uint64_t revision) noexcept -> bool;
UI::Freeze::Toggle() -> void;
StopMagnifierCoordinator() noexcept -> void;

Graphics::DibSurface(int width, int height);
Graphics::DibSurface::dc() -> HDC;
Graphics::DibSurface::pixels() -> std::span<std::uint32_t>;
~~~

### 3. Contracts

- Window Service 的受管线程拥有 Mag host/child、Freeze、DrawpadPresentation、Drawpad、四个 PageControl HWND、Bar、Setting 和 DisplayObserver；Whiteboard 复用两个 `PptBottom*` HWND，不再创建独立左右窗口。创建结果通过 promise/future 返回，stop callback 用事件唤醒 `MsgWaitForMultipleObjectsEx`。Setting 仍是普通 app window，但不再自带绘制线程。
- style、owner、显隐、bounds、click-through、HiMsg bind/unbind 和销毁必须投递到 HWND 所属线程。`UpdateLayeredWindowIndirect`、D3D present 和明确要求 HWND 的外部 API 是受控跨线程例外。
- 四个分页 HWND 的业务显隐只由 PageControl 提交：`Acquire()` 注册客户端后以 `RequestAll()` 唤醒，成功呈现后 `SetBounds/Show`，Hidden 目标使用 `Hide`。`TopWindow()` 不得无条件显示分页窗；`IdtWindowsIsVisible.pptWindow` 只表示客户端注册就绪，否则尚未布局的 `(0,0)` 初始矩形可能滞留左上角并干扰新接触。
- 基础 overlay owner 链只在创建时建立：`Mag -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar`；Mag 缺失时 Freeze 为根。Drawpad 是 DrawpadPresentation 的顶层 owned popup，不是 `WS_CHILD`；该传递链必须保证 Bar/PPT 高于两套画布表面。Presentation mode 中 overlay 保持 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`。Whiteboard mode 是显式例外：Freeze 切为唯一 `WS_EX_APPWINDOW`、可激活和任务栏锚点；Drawpad 清除 `WS_EX_NOACTIVATE` 但保留 `WS_EX_TOOLWINDOW`；其他成员仍为非任务栏辅助 UI。Bar 必须高于所有 PPT；共享底窗或其他 PPT show、`PromotePptWindow` 只把目标窗放到 Bar 正下方。置顶刷新只对链根调用一次 `HWND_TOPMOST` 或 `HWND_NOTOPMOST`，且 Whiteboard mode 强制 NOTOPMOST。Win32 会把根的 topmost band 变化传播给 owned popup；刷新后非根出现 `WS_EX_TOPMOST` 不能证明代码对它执行了独立置顶，必须审查 `SetWindowPos` 调用点。白板期间对 Freeze 调用 `ITaskbarList2::MarkFullscreenWindow`，退出和销毁前清除。
- PPT 可见性 `false -> true` 发布完成后立即请求一次根置顶刷新；成功后连续可见状态去重，失败时保留 pending 并由既有 500ms 发布节拍重试，离开放映取消 pending。PageControl 的 present 成功不代表 HWND 提交完成：`SetBounds/Show/Hide` 任一步失败都返回 RenderPipeline `Retry`。Draw3 surface 切换失败同样保留 reconciliation pending，由既有 250ms 状态节拍重试；只有窗口提交成功后才更新 drawpad ready 事实。
- 主栏定格按钮在 `Freeze::Toggle()` 前后比较 `IsActive()`；只有 `false -> true` 转换立即显式调用一次 `RequestTopmostRefresh()`。`true -> false` 和 PPT/白板导致的不可用点击不提交。Bar 入口不得持有 HWND 或直接调用 `SetWindowPos`；立即刷新失败不回滚定格状态，由既有周期性根刷新继续收敛。
- `Inkeys.UI.Freeze` 把业务 flags 与单调 `revision` 放在同一原子状态字中，每次真实 Toggle 或 PPT/Whiteboard 工作区切换发布完整 `StateSnapshot`。observer 只能把 `{revision, active}` 投递到 latest-wins mailbox 并唤醒 `MagnifierThread`，不得直接操作 HWND；迟到通知按 revision 丢弃，失败回退只能调用 `DeactivateIfRevision(request.revision)`，禁止无条件 `Toggle()`。
- `MagnifierThread` 是桌面定格唯一的 Magnifier 更新/显示协调入口。一次有效 active revision 依次校验 Window Service 当前 Host/Child、保持 Host 透明并显示 Host/Child、从当前角色 HWND 重建并提交排除列表、提交一次 source、invalidate/redraw，最后再校验 revision 与当前句柄后揭开 Host；Reveal 后仍需复查，过期结果必须立即 conceal 且不得提交 applied/visible。过滤、source 或必要显示步骤任一失败都停止成功链、条件撤销对应 Freeze revision，并允许后续新 revision 重试。inactive、工作区切换、停止和退出只透明并隐藏 Magnifier Host/Child，不隐藏 Bar/Drawpad，不改变 owner/topmost/focus/taskbar。`FreezeFrameWindow` 只保留 Freeze/PPT/Recall surface 绘制，不得调用 Magnifier 更新、显隐或 alpha API。
- 桌面定格不得调用 `SetOverlayFullscreen`：Magnifier 和 Freeze surface 都继续使用显示器高度减 1 像素，Shell fullscreen 标记只属于白板工作区。Window Service 重建 Magnifier 生命周期时先停止旧 coordinator，再在新 `PrepareMagnifierWindow` 生命周期 reset；每次事务重新读取当前 Host/Child 和完整排除集合，不能缓存旧 HWND。
- Setting 创建时及电脑操作状态（`IdtSelection`/`IdtClickThrough`）的 owner 必须为 null；绘制状态通过 `SetSettingOwnedByDrawpad(true)` 把 Drawpad 设为 owner，回到电脑操作状态通过 `SetSettingOwnedByDrawpad(false)` 清除 owner 并以 `HWND_NOTOPMOST` 退出画布置顶链。owner 修改必须投递到 Setting 所属线程，重复请求幂等；目标 HWND 缺失、Win32 调用失败或最终 `GW_OWNER` 不符合请求时返回 false，部分修改必须回滚原 owner。`SyncDraw3State()` 保存带版本的最新期望值，提交失败后由 `StateMonitoring()` 的 250ms 节拍重试；提交与 applied 写回必须串行，并在持锁后丢弃过期版本，避免旧命令覆盖新模式或产生假收敛。该动态 owner 是 Setting 唯一受控例外，不得直接从工具按钮、快捷键或渲染线程修改。
- Setting owner attach 与 owned Show 必须立即完成根/相对层级并核验位于两块画布上方，不能只设置 GW_OWNER 后等周期刷新。不独立执行 HWND_TOPMOST；根策略造成的 WS_EX_TOPMOST 传播允许变化，其余应用样式保持。重复 owner且实际层级正确时幂等；刷新失败返回 false 保留重试。Setting owner-thread 不得同步等待 overlay 命令造成 USER32 互等，正常业务/状态协调线程在 Setting 命令完成后进行根刷新。
- Setting 始终是可激活的顶层 owned/unowned popup，而不是真正的子窗口：style 固定为 `WS_POPUP | WS_CLIPCHILDREN`，不得包含 caption/thickframe/minimize/maximize/system-menu；ex-style 包含 `WS_EX_APPWINDOW` 且排除独立 topmost/layered/noactivate/toolwindow。owner 切换不得改变 style、除根传播 TOPMOST 外的 ex-style、图标、窗口线程或任务栏按钮；显示时由所属窗口线程主动 restore/show 并请求 foreground/active/focus；`WM_GETMINMAXINFO` 把最小/最大 track size 固定为配置尺寸。
- Setting 自绘标题栏的可拖动空白区必须在 ImGui 消息转发前处理 `WM_NCHITTEST` 并返回 `HTCAPTION`，由 Win32 非客户区移动循环负责拖窗；当前几何为缩放后的 `0 <= y < 32 DIP` 且 `0 <= x < 914 DIP`。右侧 `914..960 DIP` 关闭按钮和正文返回 `HTCLIENT`，继续由 ImGui 处理隐藏与内容交互。该命中合同不得因 Setting 是否拥有 Drawpad owner 而分支，`WM_MOVE` 继续记录最终位置。
- 主栏设置按钮调用统一 `UI::Setting::Toggle()`：隐藏时显示并激活；已显示但真实 Setting HWND 不是前台线程的焦点窗口时，只重新提交 Setting `Show` 命令以 restore/foreground/active/focus；仅在已显示且前台窗口与真实焦点 HWND 都是 Setting 时隐藏。Bar 保持 `WS_EX_NOACTIVATE`，只读取焦点状态，不得从 Bar 输入线程直接调用 `ShowWindow`、`SetForegroundWindow`、`SetActiveWindow` 或 `SetFocus`。
- `DibSurface` 是 top-down 32-bit BGRA DIB Section。HDC、HBITMAP、旧选入对象和像素地址由 RAII 管理；复制为深拷贝，移动为 `noexcept`，resize 先成功创建新资源再交换。
- HiMsg 成功 `Get/TryGet` 即消费；合成输入通过 `Enqueue` 原样进入同一队列。触摸转单指的 mouse message、坐标、按键状态和 marker 字段不得丢失或重新解释。
- HiMsg 默认接受 Win32 系统生成的触摸兼容 mouse；这是公共库行为。只有已经自行处理 `WM_TOUCH` 并合成单指输入的 Inkeys Bar/PPT binding 才设置 `WindowSpec::messageCallback`，在 HiMsg subclass 自动入队前对 `IsTouchGeneratedMouseMessage(message, GetMessageExtraInfo())` 返回 `Action::Discard`。该 callback 仍继续原 WndProc；真实鼠标和不带 touch flag 的笔兼容 mouse 必须保留。
- 所有使用 HWND 的渲染/交互 `jthread` 必须先 stop/join，最后才调用 Window Service `StopAndJoin()` 逆序销毁窗口。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `beforeCreate`、注册类、CreateWindow、HiMsg bind 或 `created` 失败 | 回滚 HWND、channel、class、thread id 和已激活 lifecycle；optional role 不拖垮同组 |
| 动态重建窗口 | 当前 `activeSpec` 决定 cleanup；不得调用旧 spec 的 `destroyed` |
| Mag 创建失败 | 跳过 Mag child，Freeze 成为 overlay root |
| 静态或动态创建双画布 | `GW_OWNER(DrawpadPresentation)=Freeze`，`GW_OWNER(Drawpad)=DrawpadPresentation`；两者仍为顶层 popup |
| Whiteboard window mode 切换失败 | 回滚已修改成员的 style/visibility，不发布稳定 workspace 状态 |
| Whiteboard group 收到最小化/恢复 | 保存成员可见性；恢复时只显示此前可见成员，不激活辅助窗 |
| Setting 创建规格传入 overlay ex-style 或 owner | Service 强制归一化为普通 app window 且初始 owner=null；运行时 owner 只能由 `SetSettingOwnedByDrawpad` 修改 |
| `SetSettingOwnedByDrawpad(true)` 缺少 Setting 或 Drawpad HWND | 返回 false，不改变现有 owner/style/Z 序 |
| Setting owner 写入失败或最终 `GW_OWNER` 不匹配 | 回滚调用前 owner 并沿用 Window Service 命令失败日志 |
| Setting owner 同步提交失败 | 保留 desired/applied 版本差异，按 250ms 状态节拍重试直至最新模式生效；失败日志由 Window Service 去重 |
| owner 提交等待期间发布了新模式 | 旧请求持锁后若已过期则不提交；若调用期间才过期，记录实际应用的旧版本，使下一轮继续收敛到新版本 |
| `SetSettingOwnedByDrawpad(false)` 成功 | owner=null，执行 `HWND_NOTOPMOST`；窗口仍保留 `WS_EX_APPWINDOW`、激活能力与图标 |
| Setting `WM_NCHITTEST` 位于缩放后的标题栏空白区 | 在 ImGui 前返回 `HTCAPTION`，进入系统 move loop |
| Setting `WM_NCHITTEST` 位于关闭按钮或正文 | 返回 `HTCLIENT`，不得截断既有 ImGui 点击/隐藏路径 |
| 主栏设置按钮：Setting 隐藏 | 提交 `Show(Setting)`，显示并激活 |
| 主栏设置按钮：Setting 可见但未拥有前台焦点 | 再次提交 `Show(Setting)` 恢复并聚焦，不隐藏 |
| 主栏设置按钮：Setting 可见且拥有前台焦点 | 提交 Hide，关闭设置窗口 |
| Bar/PPT 收到系统触摸兼容 mouse | HiMsg callback 不入队但继续 WndProc；业务 WndProc 同样返回 0，自定义 `WM_TOUCH -> Enqueue` 是唯一单指来源 |
| PPT hide 后重新 show 或交互前置 | owner 仍为 Drawpad，目标位于 Bar 正下方，且前台/焦点窗口不变化 |
| PPT 进入放映时根刷新失败 | 保留一次 refresh pending；后续状态发布继续请求根刷新，成功或离开放映后清除 |
| 定格按钮从 inactive 切换为 active | 在按钮入口立即提交一次 `RequestTopmostRefresh()`；不等待定格线程轮询 |
| 定格按钮从 active 切换为 inactive，或 Toggle 因工作区不可用而 no-op | 不提交立即置顶刷新 |
| 定格立即置顶请求失败 | 保留已切换的定格状态，由后续周期性根刷新收敛；不得用 fullscreen 标记补偿层级 |
| 同一 active revision 被接受执行 | 完整排除列表和 source 各提交一次；成功完成必要重绘、Reveal 与末次版本/句柄检查后才发布 applied/visible |
| 排除列表提交失败 | 本 revision 不调用 source、不 Reveal、不提交 applied；仅在 revision 仍匹配时清除 Freeze active，下一次有效 revision 可重试 |
| source、invalidate、redraw、show 或 alpha 失败 | 停止成功链并 conceal；内部 visible/applied 只在实际窗口操作成功后更新，不能把 API 提交称为像素或 GPU 首帧已验证 |
| active 执行期间发布关闭、PPT/白板切换或更新版本 | 旧事务在下一阶段门禁或 Reveal 后复查时失效并 conceal；旧结果不得覆盖新请求 |
| 进程停止或 Magnifier 生命周期重建 | stop 唤醒协调线程并使旧请求失效；窗口销毁前完成 conceal/join，新生命周期 reset 后只使用 Window Service 当前 HWND |
| PageControl 的 bounds/show/hide 失败 | 当前 surface 返回 `FrameResult::Retry`，不回滚目标可见性或改用节点级 `HWND_TOPMOST` |
| Drawpad surface 显隐提交失败 | 不发布假完成的 ready 事实；即使 Draw3 runtime revision 不变也按 250ms 节拍继续收敛 |
| 未配置上述 callback 的其他 HiMsg binding | 保持库默认行为，系统触摸兼容 mouse 正常入队 |
| 队列满或 shutdown | `Enqueue` 返回 false；队列满增加 dropped count，shutdown 不再接收 |
| DIB 创建或 resize 失败 | 原 surface 保持有效，临时 GDI 资源全部释放 |

### 5. Good / Base / Bad Cases

- Good：Draw3 绘制线程只向已请求且就绪的 target present；双窗尺寸与互斥显隐通过 Window Service；Drawpad 以顶层 owned popup 挂到 DrawpadPresentation，根刷新整体抬升单条 owner 树；桌面定格激活发布一次立即的 service refresh，并由唯一 Magnifier coordinator 对该 revision 提交一次减 1 像素 source，不向 Shell 声明全屏。
- Base：隐藏根也能通过 `RequestTopmostRefresh()` 越过同桌面的外部 topmost HWND；绘制态 Setting 作为 Drawpad owned popup 随链位于画布之上，选择/穿透态清除 owner 后回到普通窗口层级；owner 提交短暂失败时保持最新期望并周期重试；两种状态下标题栏拖动与主栏按钮的失焦恢复行为一致。Win32 传播后的非根 topmost style 是 owner 树状态，不是节点级调用证据。
- Bad：把 DrawpadPresentation 和 Drawpad 都直接挂到 Freeze，会让 Bar 与 selection-only 表面缺少可传递的 Owner 层级保证；定格按钮直接 `SetWindowPos(Freeze, HWND_TOPMOST, ...)` 或把置顶延迟到定格画面准备完成，会分裂链根置顶所有权或让用户误以为定格加载很慢。

### 6. Tests Required

- ARM64 host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64 /m:1`。
- Headless 覆盖 Surface 创建/复制/移动/resize/合成/加载保存/失败路径和 GDI handle 压力；HiMsg 覆盖过滤、clear、capacity、dropped、shutdown、并发及合成触摸字段往返。
- Message 测试需覆盖 touch signature + touch flag、真实鼠标、笔兼容 mouse、wheel/hwheel 和 XButton；Window 测试需覆盖线程 ID、owner/style、动态创建失败回滚与 stop 后无 HWND/jthread。禁止创建 HWND 的环境使用 `InkeysHeadlessTests.exe --no-window`，Window 合同仅做编译和静态检查。
- Window 测试还需覆盖持久 `SetOverlayTopmost`、白板专用 `SetOverlayFullscreen`、Whiteboard activation style 和 group minimize/restore；fullscreen 不得自行改变 topmost 位，退出白板或 `StopAndJoin` 前必须清掉 Freeze 全屏标记。静态审查需确认 `IdtFreezeFrame` 的桌面定格路径不调用 `SetOverlayFullscreen`。
- Window 测试必须分别断言静态与动态创建的 `Freeze -> DrawpadPresentation -> Drawpad -> Bar/PPT` Owner 链，并在 Presentation/Primary 互斥显隐切换后断言 Bar 保持可见且 Z 序高于两个 Drawpad HWND。
- 若主栏定格点击建立稳定测试接缝，必须通过 topmost refresh observer 断言 inactive->active 立即发布一次，active->inactive 和 unavailable no-op 发布零次；在当前 Bar 生产文件未进入 headless target 且无注入边界时，以生产分支静态审查和完整 Solution 构建验证，不得只为计数测试扩大公共 API。
- Magnifier coordinator headless 测试必须复用生产事务边界，覆盖单次 source、filter/source/invalidate/redraw/reveal 失败阻断、失败后新 revision 恢复、开启→关闭→开启、准备中取消、Reveal 后迟到取消、PPT/Whiteboard 状态切换、停止/重建生命周期以及 conceal 失败不产生假 visible/applied。排除列表测试需覆盖 null、销毁、跨进程、`WS_CHILD`、重复 HWND、可选 Setting 缺失和下一请求读取新 HWND。
- Window 测试还需覆盖 Setting 初始 owner=null、attach/detach 与重复请求幂等、缺失 HWND 安全失败、独立窗口线程不变、style/ex-style/icon 不变；detach 后断言 owner=null、退出 topmost 链并落到独立 topmost 竞争窗之下。
- Setting owner 状态同步需覆盖失败后 desired/applied 不相等并由 250ms 状态节拍重试，以及“旧请求等待/执行期间发布新模式”最终仍以最新模式为准；若没有稳定失败注入边界，必须以并发路径静态审查、Window Service 失败测试和完整集成构建共同验证，不得扩大公共 API 只为测试内部 token。
- Setting WndProc 可被测试目标链接时，需覆盖缩放后的标题中央为 `HTCAPTION`、`x=914 DIP` 起的关闭按钮及 `y=32 DIP` 起的正文为 `HTCLIENT`，并验证 owned/unowned 返回一致；未建立稳定链接边界时不得只为该断言扩大公共 API，改由完整构建、静态审查和 GUI 拖窗验收覆盖。
- Setting session-state 测试需覆盖主栏按钮三态决策：隐藏状态不受残留 focus 标记影响且总是 ShowAndActivate；可见未聚焦为 Activate；可见已聚焦为 Hide。窗口集成审查需确认两种显示动作都复用 owner-thread `Show(Setting)`。
- 允许创建隐藏 HWND 时，Window 测试需创建一个 ownerless 外部 topmost 竞争窗：先确认它位于完整 owner 树之上，再刷新根并确认每个 overlay popup 都越过竞争窗；同时断言根保持隐藏、Bar 位于目标 PPT 之上且前台/焦点不变化。禁止用“刷新后非根没有 `WS_EX_TOPMOST`”判断独立置顶，因为该位可由 Win32 owner 传播。
- RenderPipeline 测试需保留 `Retry` 会再次调度的合同；若没有稳定的 Win32 失败注入边界，PageControl/Draw3 的失败映射通过生产分支静态审查和完整集成构建验证，不得为单测扩大 module 公共 API。
- 手工 Z 序、Setting 任务栏/激活、Draw2/PPT/Freeze/Mag/DPI 回归必须在允许 GUI 的独立阶段执行，不能用静态构建冒充。白板全屏必须确认任务栏按普通全屏窗让出，且已展开底栏及白板分页栏底边距屏幕底边 `5 DIP`；浮动和折叠主栏保持进入前形态。

### 7. Wrong vs Correct

~~~cpp
// Wrong：逐节点进入 topmost band，并把传播后的 ex-style 当成调用证据。
SetWindowPos(bar, HWND_TOPMOST, 0, 0, 0, 0, flags);
assert((GetWindowLongPtrW(drawpad, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);

// Correct：外部层级只刷新根；Bar/PPT 仅维护 owner 树内顺序。
service.RequestTopmostRefresh();
SetWindowPos(bar, HWND_TOP, 0, 0, 0, 0, flags);
SetWindowPos(ppt, bar, 0, 0, 0, 0, flags);
~~~

~~~cpp
// Wrong：双画布互为 Freeze 的兄弟，Bar 只能保证高于主 Drawpad。
CreateOwnedPopup(drawpadPresentation, freeze);
CreateOwnedPopup(drawpad, freeze);

// Correct：Drawpad 仍为顶层 popup，但通过 Owner 传递使 Bar 高于两个表面。
CreateOwnedPopup(drawpadPresentation, freeze);
CreateOwnedPopup(drawpad, drawpadPresentation);
~~~

~~~cpp
// Wrong：业务入口直接操作 Freeze HWND，且定格关闭也重复置顶。
Freeze::Toggle();
SetWindowPos(freeze, HWND_TOPMOST, 0, 0, 0, 0, flags);

// Correct：只在 inactive->active 后立即提交链根刷新。
const bool wasActive = Freeze::IsActive();
Freeze::Toggle();
if (!wasActive && Freeze::IsActive())
    (void)Window::GetService().RequestTopmostRefresh();
~~~

~~~cpp
// Wrong：桌面定格表面刻意少 1 像素，却仍通知 Shell 它是全屏窗口。
service.SetOverlayFullscreen(true);

// Correct：桌面定格只提交画面和统一根置顶；仅白板工作区设置 fullscreen 标记。
// Bar 的 inactive -> active 入口：
service.RequestTopmostRefresh();
// MagnifierThread 唯一入口：
coordinator.Publish({ snapshot.revision, snapshot.active });
~~~

~~~cpp
// Wrong：FreezeFrameWindow 与 MagnifierThread 各自抓取并无条件揭开 Host。
UpdateMagWindow();
SetLayeredWindowAttributes(host, 0, 255, LWA_ALPHA);
RequestUpdateMagWindow = 1;

// Correct：业务状态只发布版本；唯一 coordinator 执行并贯穿检查结果。
Freeze::SetStateObserver(PublishFreezeState);
const auto result = ExecutePresent(coordinator, request, operations);
if (result.stage == Stage::Applied)
    coordinator.CommitApplied(request);
else
    Freeze::DeactivateIfRevision(request.revision);
~~~

~~~cpp
// Wrong：从 Drawpad 渲染线程直接修改所属线程状态。
SetWindowLongPtrW(drawpad, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);

// Correct：主 Drawpad 样式不变；目标内容就绪后由 Window Service 互斥切换 surface。
Inkeys::Window::GetService().SetDrawpadSurfaceVisibility(
    Inkeys::Window::DrawpadSurfaceVisibility::Presentation);
~~~

~~~cpp
// Wrong：在各工具入口直接改 owner，或忽略统一同步点的一次提交失败。
SetWindowLongPtrW(setting, GWLP_HWNDPARENT,
    reinterpret_cast<LONG_PTR>(drawpad));

// Correct：统一发布最新期望；同步失败由状态线程持续重试到 applied 版本追上。
const auto desired = PublishSettingOwnerDesiredState(
    !IsComputerInteractionMode(stateMode.StateModeSelect));
ApplySettingOwnerDesiredState(desired);
~~~

~~~cpp
// Wrong：先交给 ImGui，再碰运气处理系统标题栏命中。
if (ImGui_ImplWin32_WndProcHandlerEx(hwnd, msg, wParam, lParam, io)) return true;
if (msg == WM_NCHITTEST) return HTCAPTION;

// Correct：系统拖窗命中先于 ImGui；关闭按钮和正文仍返回 HTCLIENT。
if (msg == WM_NCHITTEST)
    return InSettingTitleBarDragRegion(point) ? HTCAPTION : HTCLIENT;
~~~

~~~cpp
// Wrong：窗口可见就关闭，用户无法从其他窗口把 Setting 召回前台。
if (Setting::IsVisible()) Setting::Hide();
else Setting::Show();

// Correct：只有真实 Setting HWND 已拥有前台焦点时才关闭。
switch (ResolveBarButtonClickAction(visible, focused)) {
case Hide: Setting::Hide(); break;
case Activate:
case ShowAndActivate: Setting::Show(); break;
}
~~~

~~~cpp
// Wrong：在 HiMsg 公共实现中全局丢弃系统触摸转译，其他 consumer 会失去默认输入。
if (IsTouchGeneratedMouseMessage(message, GetMessageExtraInfo())) return;

// Correct：仅在自行转译 WM_TOUCH 的 Bar/PPT binding 上选择丢弃系统副本。
spec.messageCallback = [](HWND, UINT message, WPARAM, LPARAM) {
    return IsTouchGeneratedMouseMessage(message, GetMessageExtraInfo())
        ? Message::Reply{ Message::Action::Discard, 0 }
        : Message::Reply{};
};
~~~

低级鼠标 Hook 必须在创建 Hook 的受管 `std::jthread` 中卸载。停止端设置事件，Hook 线程用 `MsgWaitForMultipleObjectsEx` 同时等待事件和消息，不得使用 detached thread 或只依赖 `PostThreadMessage(WM_QUIT)`。

`BarUiSVGClass` 的原始尺寸 `rW/rH` 必须默认初始化为 `0.0`。默认构造或资源解析失败后，单边 `SetWH` 应稳定返回失败，不能读取未初始化尺寸来推导宽高。

Bar、PPT 与 Setting 共享 RenderPipeline device epoch 和调度线程；Bar/PPT 不得共享 per-window device context/target/ULW，Setting 仍独占 ImGui DX11 swap-chain/RTV/SRV。Draw2 的 `DibSurface` 仍是独立生命周期，不能套用共享 D2D target 规则。

## Win32、DPI 与坐标

`【直接确认】` `Inkeys.Window` 统一维护窗口线程、owner、style、消息与置顶状态；旧 `IdtWindow.cpp/.h` 仅暂存不编译。`IdtMain.cpp`、`IdtDisplayManagement.cpp` 仍初始化系统版本、DPI 和显示器状态。

`【合理推断】` 修改尺寸、命中或窗口显示行为时，应追踪目标窗口实际使用的逻辑/物理坐标、显示器原点、DPI 缩放、分层窗口脏区及 mouse/touch/RTS 坐标转换；不要仅在创建点修改 style 后假设维护线程不会覆盖它。

## 建议验证范围

以下是由当前多后端结构推导的建议，不代表仓库已有正式测试政策：

- 目标窗口创建、显示/隐藏、缩放、透明边缘和退出；
- Bar 的脏区、静止 CPU 与 D2D 失败路径；
- 设置窗口的 DX11 resize/遮挡、hide/show/stop 资源清理，以及 SRV 图片颜色/透明度；
- 画板的基础层、活动笔画、撤销/恢复和 PPT 换页合成；
- UI3 Bar 是唯一悬浮栏路径；不得把 UI2 源码重新加入产品回归矩阵。


## Setting 绘制期间的 Draw3 同步合同

### 1. Scope / Trigger
设置会话在 ImGui 临界区内直接或经橡皮等业务 setter 调用 SyncDraw3State 时适用。2026-10-13 卡死源于渲染线程持 ImGui 锁等待 Setting HWND 命令，而 HWND WndProc 正等待同一把锁。

### 2. Signatures
- `Setting::DeferDraw3SyncIfRendering() -> bool`。
- `SetListStruct.paintDevice: IdtAtomic<int>`，`CaptureSettingJson`显式load并保持PaintDevice整数；每次Sync只load一次用于比较和赋值。
- `SettingRenderScope` 标记当前线程正在设置会话 Resume；`settingRenderingOnCurrentThread` 为 thread_local。
- `SyncDraw3State()` 入口先调用 defer guard；业务 FIFO 增加 `SettingBusinessKind::SyncDraw3State`，worker 调用全局生产 Sync。

### 3. Contracts
- 设置绘制作用域内 guard 返回 true 并转交现有 FIFO，调用方立即返回；不能持 ImGui 锁同步等 HWND。
- 非设置绘制线程 guard 返回 false，保持原有同步语义。thread_local 防止 worker 再次延后自己。
- 选项先更新当前配置，Sync 命令随后按最新状态应用；配置写盘仍在生产者线程冻结 payload，并排在同一 FIFO 中的 Sync 之后。
- Setting owner 的 HWND 线程所有权、desired/applied 版本与 250ms 重试保持不变，不用 try_lock 丢弃同步或跨线程直接改 HWND。
- 停止生产后排空已接受命令；停止期拒绝入队沿用现有队列语义，不能回退到锁内执行 Sync。guard 的 true 表示已拦截同步，不等于窗口事务已经应用成功。

### 4. Validation & Error Matrix
| 条件 | 行为 |
| --- | --- |
| Render 持 ImGui 锁，模拟 HWND 正等锁 | 同步提交立即返回；释放 render 锁后 worker 能完成 |
| 非 render 调用 | 原同步路径执行 |
| 快速连续修改 | FIFO 保持顺序，应用最新运行时状态，写盘消费各次已冻结 payload |
| 停止队列 | 已接受命令排空，新命令按既有停止语义拒绝，不在 render 锁内执行 |
| 窗口 owner 应用失败 | 保留既有 desired/applied 差异及重试 |

### 5. Good / Base / Bad Cases
- Good：模式下拉或橡皮 setter 发起 Sync 后渲染继续；worker 执行实际 HWND 调度。
- Base：状态监控、非设置工具入口继续同步应用。
- Bad：只换 Combo/Radio 控件，仍在 ImGui 锁内等待 Window::Submit 的 future；或只用计数式 Sync 桩测试就宣称卡死已修复。

### 6. Tests Required
- 使用生产业务 FIFO、render 标记及 defer guard 的无窗口线程回归：锁内提交不阻塞、worker 竞争锁后完成、非render语义、FIFO顺序、重复修改/停止排空。
- 控件回放另验证三语下拉与信息卡片缩放几何；它不能代替线程调度测试。
- 完整 ARM64 solution 与 Headless --no-window；受限环境出现已知 PDB/文件权限失败时区分环境并按权限规则复核。

### 7. Wrong vs Correct
```cpp
// Wrong：设置会话持 ImGui 锁时进入同步 HWND 等待。
void SyncDraw3State() { ApplySettingOwnerDesiredState(...); }
// Correct：只延后设置渲染发起的同步，worker 和其他线程仍走原生产路径。
void SyncDraw3State() {
    if (Inkeys::UI::Setting::DeferDraw3SyncIfRendering()) return;
    // 原有状态/owner/呈现同步。
}
```

## 2026-10-15 白板截图反馈修订

白板最新 UI/最小化合同见 [whiteboard.md](whiteboard.md)，覆盖此前同页的旧行为：更多和主题为同 HWND 内分开的上方/右侧矩形，采用共享属性 Back 动画；2×2 分页标签使用主文字槽。最小化隐藏画布和分页但保留主栏，已有展开底栏回到任务栏居中位置；PPT 活动时拒绝最小化。居中白板空间不足换向按屏幕边缘计算。Freeze 使用国际化 Inkeys 标题与 exe 图标。

白板 UI3 统一轮使用公共固定属性面板计算、动态 presentation crop、140×32.5 操作行与完整 2×2 主题按钮；已移除独立 popup clock、递归 pose 和旧 springLayout。尺寸、选中 SVG、同帧坐标及 opacity 目标防振荡合同见 [whiteboard.md](whiteboard.md#ui3-统一菜单与主题合同覆盖截图轮旧尺寸)。

## P2 图形属性面板

- 图形面板只扩展原geometry子树，上方最多6列36类别（11已实现、25禁用占位）；中间按需参数区；底部样式|线型|粗细，不增加分类导航。全部控件使用BarButtonClass共享按压/回弹/光源/Advance/Draw和成功呈现hit。参数区高度/位置/透明度连续，动画未稳不接收对应新命中。
- 圆单入口按圆心/外接两SVG切换；正多边形3–12边按钮，未选中/稳定展开/动画开启每1.8s SVG轮播，选中固定实际边数。轮播仅视觉，不写业务参数；按下/隐藏/选中/关闭动画/退出撤销timer，沿已有WM_TIMER只唤醒到期帧的生命周期。
- 复用既有异步memory适配，控件点击不得同步磁盘写入阻塞UI3 worker。CLI离屏fixture隔离偏好路径，验证完整生产主栏子树及真实像素/呈现hit，不能只测试新面板的独立绘制。
- Geometry主栏长标签仍使用原70 DIP槽，仅该按钮按当前/待切换目标文字测宽调整字号；透明中点换字前须完成缩小，不得每帧先发默认字号再覆盖目标。`BarUiWordClass::TargetContent()`在同一content timeline锁中只读目标；三语完整主栏测试逐帧验证字宽。Shape帧公共光源颜色取同帧图形颜色，Pen保留原Brush记忆槽。

- 按用户修正：图形粗细只保留原斜向、无数字的三档按钮，无精调三角/滑块；线型也用斜向图标。关闭旁不加颜色入口，不借用DrawAttribute/Slider/FineDial/ColorPicker子树；类别持有独立list并使用图形自己的GeometryGrid排列，不套用More的GroupedGrid及关闭栏尺寸；按钮复用UI3公共配置、绘制、推进与呈现命中能力。
- 图形底部属性区独立持有attributeList_，按样式/分隔线/线型/分隔线/粗细解析SingleRow目标；业务显隐与退场绘制分离，局部横坐标通过共享BarUiAdvanceAnimation推进，不逐帧SetDirect覆盖重排动画。底行锚定当前panel.bottom - 5 DIP - buttonHeight；布局及父高度动画中底部净距都保持5 DIP（fit/面板pose缩放后乘相同scale）。隐藏目标立即禁用输入，但保留当前透明度直至退场结束；自定义粗细内容也消费共享当前opacity。属性/参数行在位置或显隐透明度未稳定时禁用本行新命中，hover/press反馈不纳入此门禁；Tab/箭头跳过disabled项，Enter/Space同样验证已呈现hit资格。命中只发布成功呈现的当前矩形。

## 后续图形主栏目录与局部滚动（A 方案）

- `ShapeCategoryCatalog()` 是 Bar 层只读目录，36 类各有稳定 UI 键、标签与 SVG；只有现有 11 类带真实 `PresetKind`。未实现项用空映射，不扩业务工具枚举、几何或保存格式。所有控件偏移由 `Geometry*` 常量派生，共 59 个控件，不能继续硬编码 P2 的 34/33 索引。
- 禁用项仍走共享标准按钮 Configure/Retarget/Advance/Draw，以公共 0.30 内容透明度显示。state、visual enabled、press/hover、hit、键盘与 Execute 资格同源；完全裁剪外的 hit 同时不可见、不可用。面板内部的禁用区域仍消费指针，避免落到画布。
- 类别 list 按最多六列排列；正常空间显示完整六行，小空间仅类别区使用裁剪视口和共享 `BarUiValueClass` 滚动。根据同帧主栏上下可用侧决定展开区，至少保留一行；更极小空间才 fit。参数、独立属性 list 和关闭不随类别滚动，底部仍为 5 DIP × pose/fit scale。拖栏锁冻结侧向/位置跟随，不冻结已经变化的面板宽高；slots/clip 从同帧实际 panel 派生。
- 滚轮沿现有消息入队链，滚动条有 32.5 DIP 触控热区；键盘跳过禁用项并将真实类别滚入视口。worker 只发布原子滚动意图，唯一 render owner 推进共享动画；禁止新增滚动线程、独立时钟或逐帧重载 SVG/测字。
- Draw 的 categoryClip 与已成功呈现 hit 的裁剪区相同。滚动未稳或请求尚未成功呈现时禁止类别动作，参数/属性沿各自门禁；不能用旧成功快照接收新滚动位置的点击。thumb 拖动在失捕、取消、revision/工作区/轨道/倍率变化时结束，失败呈现保留原快照。工作区事实必须进入同批呈现快照与 changed 判定：即使轨道位置恰好不变，工作区变化也应在下一次成功提交后使旧拖动失效，不能只比较几何结果。
- 新增 25 份 SVG 各自嵌入名称型 UI 资源，沿用 24×24、2.6 主描边与主题语义色；反比例/双曲线、正弦/余弦须有可辨的独立轮廓。无新增设置项。验收使用生产 ULW 源像素与空白负例、视口/滚动/禁用动作回放，不能仅靠目录计数或 XML 通过。

## UI3 任务栏基准与真实 DPI 隔离（2026-10-19）

- `dpiZoom` 保持真实屏幕 DPI/96，用于画笔宽度及屏幕DIP；界面基准独立保存为 `uiBaseZoom`，最终 `zoom=uiBaseZoom*configZoom`。初始化、显示过渡、底栏直移、命中和启动预览必须使用同一 UI 尺度；不把任务栏厚度塞进真实 DPI 字段。
- 主栏名义主体高取 `BarMainBarHeightDip=80`，不含阴影/透明HWND包络；100%时关闭模式主体为80DIP、任务栏基础量为固定48DIP、任务栏真实量匹配可信任务栏完整厚度，允许像素舍入。仅真实量在无可信样本时回退80DIP；保留启动空间不足时显式降低用户倍率的既有fit行为。
- Display snapshot包含逐屏 `taskbarThicknessPx`，厚度变化参与语义比较和serial；任务栏低频采样不重复EDID/拓扑枚举，不在渲染或Move热路径查询Shell。自动隐藏用完整矩形而非与屏幕交集或workArea缺口，临时失败仅复用同屏同DPI/几何下可信缓存。
- finite wire不增加字段：`configZoomBits`历史名称保留，值为`UI基准/真实DPI倍率*用户倍率`，Source以DPI×该值还原UI尺寸；不能再把它当仅用户倍率套用0.5..2裁剪。内部`barStyle.configZoom`仍是用户倍率。
- 任务栏Shell窗口类只是兼容采样策略；真实自动隐藏、Explorer重建、第三方Shell和跨屏视觉需独立实机证据，无窗口算法测试不能替代。

## 任务栏方向、显隐与主栏贴底（2026-10-19）

- Display 的逐屏 `MonitorInfo.taskbar` 独立发布方向、autoHide、规范化显隐、同屏同几何最近可信状态与稳定 dockTop；完整 `taskbarThicknessPx` 单独维护。隐藏触发条、动画中间矩形或 Unknown 不能当作完整厚度，也不能把主屏 UI 比例回退误作本屏存在任务栏。
- 桌面顶/左/右任务栏的主栏线为 monitor.bottom-round(5*真实DPI/96)；底部常驻或自动隐藏 Raised 为稳定 taskbar top；底部 autoHide Hidden 为 monitor.bottom-round(5*真实DPI/96)。Unknown 优先同屏同几何可信历史，首次失败按有效 workArea 保守回退。PPT/Whiteboard 原 scene 的 5 屏幕 DIP 优先，不更改业务进入/退出动作。所有线对齐主体描边外沿，不能按透明 HWND 包络计算间隙。
- 非注入 WinEvent 在既有 DisplayObserver owner 线程安装/卸载，回调只过滤缓存 PID/HWND/object 并排队，不持 refreshMutex、查询 Shell 或通知业务。事件合并 25ms；仅缓存底部 autoHide 存在时以 5 秒间隔轻量矩形兜底，结构复查也为 5 秒，负责设置/Explorer 重建。所有任务栏周期轮询间隔至少 5 秒，事件触发的即时刷新与单次合并计时不受此限制。快路径不枚举 EDID/拓扑/全部窗口或更新完整厚度；同值不发布新 generation。epoch 关门与 owner 清理对称，不从任意 Shutdown 调用线程卸载 hook。
- 任务栏归属的已知方向与兼容枚举共用确定性候选解析。常驻按对应 dock 外沿判定且距离须小于厚度，拒绝仅接触屏外边界的邻屏矩形（包括单屏 Mini 快照）；自动隐藏保留跨边界候选，不能仅按矩形交叠面积归屏。同距离歧义只使用同 HWND/非零 PID、同屏名称/几何/DPI 的可信旧归属，不覆盖更近的新几何；无可靠信息时返回 Unknown，不依赖屏幕/方向遍历顺序。当前 EX 接口优先，副屏可信逐屏 autoHide 缓存优先于弱全局模式提示。
- 完整厚度允许真实变矮：主任务栏位置查询的可信完整值可立即更新；兼容副栏完整对齐的结构样本连续确认后更新。禁止用只增不减缓存永久保留旧高度；快显隐采样不改 UI 缩放标尺。
- Bar 的 desktopDockLine/protectBottomEdge 和 bounds/workArea/DPI/UI基准一起受 pending/presented 偶数 serial 保护。渲染、输入 tracker/重基准/blocked release 及居中启动预览消费同代数据。仅同屏任务栏线变化不重算用户 X、不把浮动栏吸底；稳定底栏复用原 0.4 秒 Y 属性动画与配置，重复目标不重播，反向从最后成功显示 Y 开始。拖动/吸附交接/恢复与 PPT/Whiteboard 优先，不引入第二个 Y 写者。
- 桌面底部 autoHide 屏幕的 Bar 最终 viewport（包含最终直移量）下界 exclusive<=monitor.bottom-1，保护主体、描边、光影和回弹。source/size 改变用既有完整 ULW 替换清旧像素，稳定恢复 partial dirty；提交前位移改变须重新校验并保留重试。SetWindowPos 直移越界时保留最新输入目标、改走渲染提交，不能把旧位图直接移到底行。此条件策略与双画布 Desktop/PPT 恒定 1px 策略分别维护。
- 无窗口几何/状态机/真实动画类和失败路径测试不能代替实际 Explorer 动画时序、Shell 触发、UIAccess、多屏、Win7 或视觉性能验收；实际结果写任务 validation，不从配置存在推导支持结论。

## UI3 新按钮的笔震动接入（2026-10-20）

新增主栏注册按钮、形状/粗细表项或独立属性控件时，同时遵守 [pen-haptics.md](pen-haptics.md) 的独立偏好与业务接受点合同。公共按钮使用共享入口自动继承；独立状态机必须报告真实接受，不能把命中/Consumed 当作成功。保持 Pen 凭据、取消/禁用/去重、即时功能开关和可选设备回退，并补充相关回归。
