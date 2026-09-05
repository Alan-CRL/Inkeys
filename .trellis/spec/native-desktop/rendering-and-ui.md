# Rendering and UI

本页区分 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。图形实现是按窗口分流的；“仓库用了 D3D11”不等于所有窗口或 ImGui 都使用 D3D11。

## UI 路由与后端分工

| 区域 | 当前选择关系 | 图形/呈现链 | 直接证据 |
| --- | --- | --- | --- |
| `Inkeys.UI.Bar` | `【直接确认】` `IdtMain.cpp::wWinMain` 无条件启动 UI3；不存在 UI2/UI3 运行时分支 | `Inkeys.UI.RenderPipeline` 的共享 D3D11 WARP epoch 提供 DXGI/D2D device；Bar 是独立客户端，经自己的 device context、GDI interop 和 `UpdateLayeredWindowIndirect` 呈现 | `IdtMain.cpp`、`Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.*`、`Bar.RenderLoop.cpp` |
| 传统 `IdtFloating` | `【历史/兼容】` 源码暂存但在 `Inkeys.vcxproj` 中为 `None`，生产代码不得 include | 不参与产品编译 | `Inkeys.vcxproj`、`Inkeys.vcxproj.filters` |
| 设置窗口 | `【直接确认】` 当前主工程编译的唯一 ImGui renderer 是 DX11 | Dear ImGui Win32 + 共享 WARP D3D11 device/immediate context；Setting 独占传统 discard swap chain、RTV、SRV 和 ImGui session | `Setting.Base.cppm::CreateDeviceD3D`、`Setting.cpp` 中 `RenderSettingFrame`、`Inkeys.vcxproj` |
| 主画板 | `【直接确认】` Draw3 已接管 Window Service 的主 Drawpad；选择态使用同一 Host 的 presentation-only sibling | Draw3 独立 D3D11.1 device、单一 swap chain/final backbuffer，以及主 DComp/DWM/ULW + 辅助 ULW target | `Draw3.Host.*`、`Draw3.TransparentPresentation.*`、`draw3-integration.md` |
| PPT / Whiteboard 分页控件 | `【直接确认】` `Inkeys.UI.PageControl` 独占四个 owned layered HWND；`PptBottomLeft/PptBottomRight` 在 PPT 与 Whiteboard 间连续切换布局 | 与 Bar/Setting 共享 D3D11 epoch；PageControl 独占四套 Scene/device context/target/GDI interop，PPT 与 Whiteboard 只发布状态和业务回调 | `Inkeys/Inkeys/UI/PageControl/PageControl.*`、`Inkeys/Inkeys/UI/Ppt/Ppt.*`、`Inkeys/Inkeys/UI/Whiteboard/Whiteboard.*` |
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

### 定格状态机合同

`Inkeys.UI.Freeze` 是定格业务状态的唯一所有者。绘制、选择和工具按钮切换不得清除或恢复定格；用户只能通过定格按钮切换状态。PPT 或 Whiteboard 工作区进入时必须关闭定格并禁用主栏定格按钮，退出工作区只解除禁用，不恢复进入前的定格状态。禁用按钮保留可见布局和内容，以现有 `BarButtonDisabledContentOpacity` 呈现，不参与悬停、按压或点击命中。

以下仅是对现有 Bar 的 `【合理推断】` 修改约束，不适用于所有窗口：

- 保持 target/current 动画状态与最终绘制值的现有分工；
- 状态变化需要触发现有渲染唤醒/脏区路径，静止时避免无意义刷新；
- 维持 premultiplied alpha 语义；
- `GetDC/ReleaseDC`、`BeginDraw/EndDraw` 在成功与失败路径成对；
- 不在没有线程安全依据时跨线程传递 device context/target。

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
};
~~~

#### 3. Contracts

- 动画时钟使用单调时钟；`Tick()` 对非有限值或负值返回 `0`，并将活动帧 `dt` 限制到 `[0, 0.05]` 秒。
- 只有渲染线程在无可见工作并从 `BarAtomic::wait.WaitAndConsume()` 真正唤醒后，才在处理下一轮状态前调用 `Rebase()`；退出信号已生效时直接停止。
- present/device 失败的 `WaitUntilGenerationChange()` 退避、普通 60 FPS pacing 和连续动画帧不得 rebase，否则会冻结或缩短仍需推进的动画。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 长时间 idle 后收到 UI 请求 | 唤醒点 rebase；下一轮只计算唤醒后的实际帧间隔 |
| idle wait 因退出信号结束 | 直接停止，不再推进动画 |
| present/device 失败退避结束 | 不 rebase；保留失败等待期间的动画时间语义 |
| 时钟差为负或非有限 | 本帧 `dt=0` |
| 活动线程异常停顿超过 50 ms | 本帧 `dt` 上限为 50 ms |

#### 5. Good / Base / Bad Cases

- Good：静置数小时后点击按钮，动画从接近起点开始，不会首帧直接跳过约 50 ms。
- Base：连续 60 FPS 动画仍按相邻活动帧间隔推进。
- Bad：只依赖 `clamp(dt, 0, 0.05)`；长时间 idle 后首帧仍会固定跳过 50 ms。

#### 6. Tests Required

- headless 用可注入时间点模拟活动帧、长时间 idle、`Rebase()` 和唤醒后首帧，断言休眠时长不进入结果。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`；手工检查长时间静置后的首次展开、属性面板和粗细预览动画。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：idle 等待结束后沿用等待前的动画时刻。
wait.WaitAndConsume();
const double dt = clock.Tick(); // 直接得到 50 ms clamp

// Correct：只在真正 idle 唤醒后重置基准。
wait.WaitAndConsume();
if (offSignal) return;
clock.Rebase();
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
- 每个分页 HWND 独立持有 D2D target/`BarUIRendering`、Previous/Page/Next `BarButtonClass` 和输入状态；独立资源是正确边界，不允许复制按钮行为。Bottom 三枚实例跨 `PptCompact/WhiteboardExpanded` 稳定，Middle 只切 `Hidden/PptCompact`。
- `BarSurfaceScene` 可以保存每窗稳定 `BarButtonClass`、PPT-only DragHandle 和 `hovered/pressed` id 路由，但不得定义另一套视觉算法或分页专用数值。标准按钮的内部位置、hover/press、内容转换、draw、圆角 hit 和动画值必须调用上述 Bar 入口；Scene 只把共享结果映射到本窗 damage/present。
- PPT 外框：Bottom `165x42.5 DIP`，Middle `42.5x165 DIP`；Drag slot `10 DIP`、Arrow `32.5x32.5 DIP`、Page 横向 `70x32.5 DIP`/竖向 `32.5x70 DIP`，两侧外边距与真实按钮间距 `5 DIP`。Drag 是 divider lane，与相邻 Arrow 直接相接、不增加第三个间距。Whiteboard 外框 `230x80 DIP`，三枚标准 `70x70 DIP` `2x2` 按钮。
- 分页背景的主题、圆角、边框、第一/第三光源、draw 与 dirty 直接复用 Main Bar 背景实现；PageControl 只提供子控件联合外框目标。外框必须读取 `BarMainBarCornerRadiusDip`，按钮必须读取 `BarButtonCornerRadiusDip`，边框和内边距也读取主栏单一来源（当前为 `8/4/1/5 DIP`）；禁止 PageControl 镜像常量。分页外框和 Main Bar 外框都保持背景 Shape 默认 `frameCursorLightIntensityScale = 1.0`，不得误用按钮的 `BarButtonCursorLightIntensity`。第三光源必须消费 Main Bar 发布的同一屏幕坐标、半径、强度和可见性快照；Surface 不得从本地 `WM_MOUSEMOVE` 创建淡入/淡出状态。
- `DrawBarButtonVisual` 的显式 `inherit` 是按钮父坐标唯一真值。入口必须先同步 `button.inhX/inhY`，再通过 `ResolveBarButtonChildTopLeft` 解析 SVG、主文字和次文字；Main Bar dirty 与最终 draw 调用同一继承准备入口。禁止读取默认或上一帧父缓存后再把显式 `inherit` 只用于背景 Shape。
- 四个 PageControl HWND 与 Bar HWND 共同构成第三光源的实际消息接收窗口集合。真实鼠标进入分页 HWND 时只通知 Main Bar 激活既有状态机；离开时由 Main Bar 根据 `WindowFromPoint` 决定 `Inside/Grace`。Raw Input 和 5 秒 timer 始终以 Bar HWND 为 owner；分页只发布成功呈现的屏幕边界用于 240px 邻域裁剪，隐藏或提交失败不得发布假边界。
- Page 在三种形态中是同一实例。横向页码对加粗当前页与常规 `/总页数` 做整体测量并居中；竖向使用上下行并整体居中；Whiteboard 使用标准 `2x2` 主内容/标签槽。PPT 当前页或总页为负数时显示 `-`/`/-`，Bottom/Middle 分别限制显示到 `9999/999`。禁止分页专用绝对 offset。Page 数值变化必须走共享 Scene 的即时内容策略：取消旧文字 transition、同帧替换主/次字符串，并直接应用本次重新测量的槽位；不得调用 `TransitionToString`。PPT Page 在系统拖动阈值内短按并仍命中时调用既有 `ViewShow` 打开预览，Whiteboard Page 保持 no-op；两者都使用标准 hover/press。Arrow/Add SVG 与语义标签继续使用共享内容转换，不得因即时数字策略被关闭动画。
- 普通 Previous/Next 始终保留同一 `barMore` SVG 对象，只动画尺寸/位置/角度及 Whiteboard 标签；不得为同资源启动替换。`barMore` 的 `0°` 基准朝上，`MiddleLeft/MiddleRight` 必须统一使用 Previous `0°`（向上）、Next `180°`（向下），不得按左右侧镜像；Bottom PPT/Whiteboard 继续使用 Previous `-90°`、Next `90°`，Whiteboard Add 使用 `0°`。只有 Whiteboard Next 的 Arrow/Add 语义真实变化时，才用共享内容转换切换 `barMore`/`barAdd` 与“右翻页”/“加页”，并与几何同批并行推进。
- EndShow A2 与 PageControl 必须共用主栏 `UI/barEndShow` SVG；深色主题由 `TextPrimary` 着色为白色，资源本身遵循主栏 `24x24` 画布、圆角端点/连接和相近线宽，不得继续用固定黑色 `ppt3` PNG。PageControl 仅在 UI 发布态 `totalPage > 0 && currentPage < 0` 时视为结束放映页：Bottom/Middle 的稳定 Next 实例通过现有 Animated 中点内容转换把 `barMore` 切为正向 `0°` 的 `barEndShow`，状态离开结束页后反向切回各 surface 的箭头角度；未知 `-1/-1` 不得误判。同一快照必须让 `ResolvePptDirectionAction` 把结束页 Next 解析为 `EndShow`，并通过 `PptCallbacks::endShow -> Bar::RequestEndShow` 复用 A2 的单请求 dispatcher、确认和 PPT 业务线程退出流程；有效页仍解析为 `NextPage`。
- DragHandle 是 PageControl 自有且仅 PptCompact 存在的 shape/hit region，不属于 Bar 按钮，无 hover/press/click/selected/content 视觉。PPT 成对拖动可从 DragHandle、Page 和未被 Previous/Next 占用的真实圆角背景开始；透明 presentation margin、圆角外像素和 diffuse 光晕继续 `HTTRANSPARENT`，不能把整个矩形 HWND 当作背景。Previous/Next 永远是纯按钮。Page 按下先产生标准 press，超过 `SM_CXDRAG/SM_CYDRAG` 对应系统阈值后取消 press 并转为拖动，阈值内且仍命中 Page 时抬起调用 `ViewShow`，不写位置。Whiteboard 不创建 DragHandle 或 drag candidate。
- PPT 普通 Arrow 的 Pointer Down 立即投递一次 Previous/Next，并在该次 Down 边界通过 `SPI_GETKEYBOARDDELAY` / `SPI_GETKEYBOARDSPEED` 快照系统键盘重复设置；一次按压中途不得重新读取或改变节奏。`keyboardDelay` 先限制到 `0..3`，首次重复延迟为 `250ms * (delay + 1)`；`keyboardSpeed` 先限制到 `0..31`，按 Windows 文档给出的 `2.5..30 次/秒` 线性换算并取最近毫秒作为后续重复间隔。任一 SPI 查询失败时仅该字段回退到 `delay=1` / `speed=31`，按压仍可继续。只有 PPT 快照发布的 `longPressEnabled=true`、指针仍命中同一 Arrow 且 capture 有效时才重复。首次实际重复以当前帧锚定，保证第二次至少等待完整 interval；第二次起以计划 deadline 推进 repeat anchor，避免 60 FPS 把 `34ms` 等间隔量化成固定 `50ms`。若实际帧已落后计划 deadline 至少一个完整 interval，则只触发一次并以当前时刻重新锚定，不追赶积压。既有 COM outstanding gate 继续合并未完成普通页命令。结束页 Next 在 Down 时只调用一次 EndShow dispatcher，不建立 repeat timing；普通 Next 按住期间若 UI 状态进入结束页，则下一次合法 repeat 改投一次 EndShow 并立即清除 tracking。移出 Arrow、Pointer Up、capture cancel 或 workspace 切换立即停止。Whiteboard 不继承 long-press；键盘 Hook 与 wheel 不得合成 Arrow pressed 闪按，真实 Pointer 的标准 hover/press 保留。
- 每个有效 PPT Pointer Down 都调用 `PromotePptWindow(surfaceRole)`，把最近交互窗放到其他 PPT 窗口之上、Bar 正下方，不激活窗口；纯平移继续使用 `SWP_NOZORDER`。
- Enter 开始即 cancel capture、关闭 DragHandle hit；Window Service 撤销必须广播 Drawpad、四个 PageControl HWND 与 Bar。PageControl 的 `WM_CANCELMODE` 在 `renderTransactionMutex` 内清 pointer/drag/touch 状态，释放该锁后才调用可能重入 `WM_CAPTURECHANGED` 的 `ReleaseCapture`。背景、三枚按钮、Drag slot/opacity、SVG/文字和 HWND bounds 使用同一批次从当前成功呈现值重定向。PPT 底栏可见时同时移动到 Whiteboard 固定 `5 DIP` 角落并形变；不可见时直接用最终几何渐显。Exit 反向返回 PPT 最新运行时位置，稳定 PptCompact 成功呈现后才恢复 DragHandle hit。
- Surface 首次在 Hidden 配置时必须把当前/目标透明度直接提交为 `0`，不能只留下尚未推进的零时长 target；随后显示必须形成真实 `0 -> 1` 渐显。只要 `layoutTransitionUntil` 尚未到期，无论 Scene/bounds 是否仍报告 active，可见或退场 Surface 都必须返回 `FrameResult::Continue` 并逐帧重算 `inputLocked`；到期帧自行解锁并进入 Idle，不得依赖 Bar 鼠标、共享光源或其他客户端消息再次唤醒。
- 四个 PageControl HWND 创建时继续统一执行 `RegisterTouchWindow(hwnd, 0)` 与 `DisableEdgeGestures(hwnd, true)`；WndProc 还必须对 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 返回与 Bar 一致的 press-and-hold、pen tap feedback、Touch UI 和 flick 禁用标志。`WM_TOUCH` 每批优先锁定带 `TOUCHEVENTF_PRIMARY` 的触点；整批没有 primary 标志时锁定第一个 DOWN 作为 fallback。同批处理必须先保序消费 primary、再消费 non-primary，防止旧 fallback 的 Up 在新 primary 替换 cancel 前触发 click。活动触点的 id、是否 primary 和最后 client 坐标必须锁存；新合格 DOWN 替换旧 id 时先取消旧 press/capture（等价于 cancel up，且不得触发 click），再建立新 Down。只有锁定 id 的 Move/Up 可转译，Up/cancel/workspace 切换必须清空锁存；系统生成的兼容 mouse 副本仍由现有来源签名过滤。
- PPT 碰撞只包含 bottom/middle pair 与屏幕越界。手动拖动不推动另一 pair；自动纠偏 bottom 优先、middle 最近位置/极端运行时缩放回退，且不写保存配置。Bar HWND、MainButton、主栏移动和 Whiteboard 均不参与求解或唤醒。
- PageControl 继续拥有 stable backing、logical/presentation 映射、direct-move revision、ULW/Window 提交与调试覆盖层事务。共享 Bar 运行时返回 animation/damage，不直接调用 Window Service；隐藏生命周期不得被光源动画无限延长。渲染线程可在 `presentationMutex` 内同步等待 Window Service owner，因此 owner WndProc 绝不能阻塞等待该锁。每次拖动采样必须先按原始 drag 起点计算并发布 latest-wins 绝对候选；发布本身不得请求 pair。直移目标必须分别解析上一 feasible layout 与当前 candidate layout，确认尺寸、scale、mode 仅发生平移，再由 candidate logical bounds 加减当前不变的 presentation outset 得到绝对 HWND 目标；不得依赖上一候选已成功提交，也不得为取得目标调用 `ApplySceneBounds`。
- `presentationMutex.try_lock()` 成功后，owner 依次对两个 HWND 调用 `SetWindowPos`；第一窗失败直接保留 pending，第二窗失败必须把第一窗回滚到 original，并记录 first/second/rollback 阶段及 Win32 error。两窗成功后才对两个 `SurfaceState.bounds` 调用 `SetBoundsDirect`、提交 mailbox/direct-move revision，并立即发布两个 cursor-light 接收边界；mailbox/layout/bounds payload 必须先完整写入，再以 release store 发布单调且由 `dragCommitMutex` 串行的 revision。该热路径不得调用 `ApplySceneBounds`、产生 Scene damage 或请求 pair。只有锁竞争、非纯平移或窗口移动失败走 pair render fallback。较新的候选可以覆盖旧候选，但 fallback 必须保留最新候选，不能依赖另一条可能被合并的 `WM_MOUSEMOVE`。渲染帧以 acquire 读取 revision，取得 `renderTransactionMutex` 后、`ConfigureSurface/PresentScene` 前必须复核，窗口提交后继续第二次复核；任一过期帧均返回 `Retry`，不得先以旧 ULW 拉回 HWND。松手只 `RequestAll` 一次让 Scene/layout 吸收最终位置，并且不得在最新候选成功提交或明确回滚前清除 pending 所有权。
- `PptInfoState` 是 COM 页状态事实，`PptInfoStateBuffer` 仍只在 Draw3 到达对应零基有效页后前进。`PptInfo` 必须通过 `Inkeys.UI.Ppt::ResolvePageStateForPublication` 解析 UI 状态：有效 COM 页返回 ready buffer；`observedTotalPage > 0 && observedCurrentPage < 0` 只向 UI 投影 `-1/observedTotalPage`，buffer 保持 `-1/-1`；总页无效或当前页为 `0` 返回 `-1/-1`。发布去重比较解析结果，使结束页进入/恢复都能请求 PageControl 内容转换。现有 PowerPoint/WPS COM ABI 不增加 native wait handle，因此 native 对共享 COM 状态使用不超过 `50ms` 的有界检查；检测到目标变化后发布 Draw3 绝对页，Draw3 在 current page/page count 真实变化时推进 `runtimeRevision` 并通知 `WaitForProductRuntimeRevision`。禁止继续用固定 `500ms` 睡眠等待 Draw3，也禁止为追求即时有效页数字而提前发布未 ready 的 COM 页码。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Main Bar 与 PageControl 对相同 button request/state/time 结果不同 | 测试失败；修共享实现，禁止在 PageControl 加补偿常量 |
| PPT Arrow 进入 Whiteboard Arrow | 保留同一 `barMore` SVG，只缩放/移动并显示标签 |
| Whiteboard Next Arrow↔Add | 同一按钮在同一批次转换 SVG、标签与几何；语义未变不重启 |
| raw COM 为 `-1/有效总页数`、ready buffer 为 `-1/-1` | UI 解析为 `-1/有效总页数`；四个 surface 的 Next 动画切为 `barEndShow`/`0°`，动作解析为单次 EndShow，buffer 不变 |
| raw COM 恢复有效、Draw3 尚未 ready | UI 先离开 EndShow 语义并切回各 surface 的 `barMore` 角度；页码保持未知直到 ready |
| raw COM/发布态为 `-1/-1` 或 `0/有效总页数` | 不进入 EndShow 语义；不得沿用上一次 EndShow 目标 |
| Enter 时 DragHandle 正在 capture | 立即 cancel、关闭 hit；slot/opacity 从当前值收拢 |
| Exit/Enter 中途反向 | 背景、按钮、内容、DragHandle 和 HWND bounds 从当前插值值重定向 |
| Whiteboard 收到 drag/wheel/long-press | 不捕获、不持久化、不投递对应业务；普通 click/tap 保留 |
| 用户拖动 pair 接近另一 pair | 命中 pair 停在最近可行位置；另一 pair 不移动 |
| 显示空间不足 | bottom 保留；middle 先找位置、再仅降低运行时 scale；保存状态不变 |
| Main Bar 移动或尺寸变化 | 不请求 PageControl 碰撞重算，不改变分页位置 |
| PPT 当前页或总页未知/超出显示上限 | 使用 `-`/`/-`；Bottom/Middle 分别限制到 `9999/999`，不改变业务页码 |
| PPT 页码按钮点击 | 产生标准 press，并在阈值内且仍命中时调用 `ViewShow`；不写位置 |
| Whiteboard 页码按钮点击 | 只产生标准 press，业务 no-op |
| Page 按下后在系统阈值内抬起 | PPT 调用预览，Whiteboard no-op；两者都不写位置 |
| PPT 普通 Arrow 按下并持续 | Down 立即一次；配置开启时按该次 Down 快照的系统键盘 delay/rate 重复；移出/Up/cancel 后停止，不补发迟到节拍 |
| 结束页 Next 按下或普通 Next hold 后进入结束页 | 复用 A2 EndShow dispatcher；直接按下单次投递且不建立 repeat，hold 转态只在下一次合法 repeat 投递一次并停止 |
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
- Good：最后一页进入结束页时 raw COM 从 `12/12` 变为 `-1/12`，UI 在不污染 ready buffer 的前提下驱动稳定 Next 转为 EndShow；点击经 A2 dispatcher 进入既有确认/退出流程，返回有效页先恢复 Arrow/NextPage，Draw3-ready 后再更新数字。
- Base：默认 DPI 下 PPT 为紧凑深色 Bar surface，Whiteboard 为固定三枚 `2x2`；普通 Arrow 只缩放，Add 才转换内容。
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
- Animation headless 通过生产共用 `ApplyBarImmediateContentUpdate` 覆盖旧 content transition 取消、current/target/pending 同事务替换及取消后不发生旧关键帧回写；Scene 源码审查确认即时 Page 槽位使用 `SetDirect`，而 Arrow/Add 仍走 Animated 中点转换。
- Draw3 headless 通过 Host 实际持有的 `HostRuntimeRevisionSignal` 覆盖 current page/page count 变化推进 revision、唤醒 waiter、稳定值不唤醒及 stop 释放等待；源码审查两个 document observer 都调用该入口，PPT 状态线程只以不超过 `50ms` 检查 COM，共享 buffer 仍等待 Draw3-ready，结束页仅经 UI 解析器投影。
- PageControl 输入测试覆盖圆角背景门禁策略；源码审查背景命中读取 Scene 当前动画 Shape，Window Service capture 撤销包含四个 PageControl 角色，且 `ReleaseCapture` 位于呈现锁外。
- 碰撞覆盖手动 pair 最近可行位置、bottom 优先/middle 回退、极小屏运行时 scale、输入/保存快照不变，并静态断言无 Bar obstacle 参数/查询/通知。
- Headless 直接断言非零父原点下的标准按钮子内容坐标，以及屏幕光源点到 Surface presentation 点的映射；静态审查分页不再调用本地 cursor-light prepare/reset，且四个 HWND 的进入/离开与成功呈现边界只通知 Main Bar 全局状态机。
- 回归 EndShow/A2、四窗口生命周期、owner/Z 序、旧 JSON、COM/WPS、`PptInfoStateBuffer` 与页级墨迹；执行完整 `Debug|ARM64` Solution 构建和 ARM64 `--no-window` 测试。

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

// Wrong：只发布已被结束页分支归一化的 buffer，PageControl 永远收不到 -1/总页数。
PublishPageState(buffer.CurrentPage, buffer.TotalPage);

// Correct：buffer 只表达 Draw3-ready；结束页在 UI 发布边界单独投影。
const auto publication = ResolvePageStateForPublication(
    buffer.CurrentPage, buffer.TotalPage,
    observedCurrentPage, observedTotalPage);
PublishPageState(publication.currentPage, publication.totalPage);

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
	BarPresentMappingMode presentMappingMode) noexcept;

void SetDebugOptions(bool enable, bool showFrameRate);
~~~

#### 3. Contracts

- 标准 Shape/SVG/PNG/Word 使用稳定对象键记录边界；父布局、粗细/色板/弹窗等自绘内容使用稳定功能组键。变化项的 damage 是上次成功呈现边界与本帧边界的并集，覆盖移动、缩放、出现和消失；所有 damage 最终合并、裁剪为一个 `RECT`。
- 每帧顺序固定为：推进动画并 `MarkChanged` → 完成继承布局并 `Observe` 当前边界 → `ResolveDamage` → 解析容量/viewport → 加入调试文字和红/绿/蓝框旧新边界 → 用同一 `presentDamage` 约束 D2D clip/Clear 与稳定映射帧的 `UPDATELAYEREDWINDOWINFO::prcDirty`。首次呈现，或 `pptSrc`、`psize`、target capacity、device generation 任一字段相对上次成功 tuple 改变时，必须清除完整候选 HWND 范围并令 `prcDirty=nullptr` 执行整窗替换；viewport 相对 committed anchor 的解释变化必须与 present-mapping tracker 合并为同一个 `forceFullWindowReplacement`，同一布尔值控制业务/调试 damage、`presentDirty` 和 `prcDirty`。只有两类映射都稳定的帧允许局部 dirty。
- `BarUiAdvanceAnimation` 的 `changed || active` 必须标记所属控件或功能组。直接拖动、保持环、色板/粗细自绘等绕过标准动画的路径必须显式标脏；存在非调试呈现请求却没有分类 damage 时必须回退全窗口。
- 主光和鼠标光必须独立报告变化，静止的一路不得因另一路移动而被标脏。每路先计算包含径向半径、`pointLightDiffuseExtraWidth * zoom` Gaussian 外扩和抗锯齿余量的影响矩形，再只与实际可见 `PointLight` 边框的上/下/左/右影响带求交；光圈内部没有边框像素贡献的区域不得进入 damage。关闭光影时当前边界为空，旧边界仍参与清除。
- Tracker 为稳定视觉键复用记录，并复用变化键/观察键容器；普通帧只通过 `ShouldObserve()` 采集变化项、所需功能组和光源的边界，成功后只推进本帧实际观察记录。全可见内容边界只在首帧、DPI/容量纪元、顶层外框动画、整栏拖动和最终 idle 帧重算，普通 hover/按压/光影帧复用缓存。禁止逐帧清空并重建哈希节点、复制完整快照，或为动态缩窗在普通高频帧遍历全部 SVG/PNG/Word 内容。
- D2D target 容量以主按钮为稳定锚点，启动、DPI/显示器纪元或真实内容突破容量才重建；整栏拖动只平移 `capacityOrigin` 和 viewport，同尺寸不重建 target。容量突破要保守对称扩容并强制全脏，不得为追求立即缩小而频繁重建。
- 只有可改变顶层外框的动画批次才在首帧扩展 viewport 并保留保守扫掠包络；批次内不缩放，最终 idle 帧只收缩一次。普通 hover、按压、帧率文字和光影不得将 HWND 扩到整个容量。预留包络要预先内缩 viewport padding，保证解析后的 `pptSrc + psize` 始终位于 target 内。
- 粗细 Slider/FineDial 连续手势的完整交互域必须复用 `GetBarThicknessSliderRange(currentPenMode, dpiZoom).max`，在按下/捕获首帧按最大端滑块位置计算完整 Preview Popup。包络同时覆盖 DPI 换算后的最大圆、数字从圆外迁入圆内的最宽 Surface、Slider/FineDial 两个目标位置、Popup Back 极值，以及实际可见描边、PointLight `pointLightDiffuseExtraWidth` 和抗锯齿外扩；捕获、拖动与 FineDial 物理期间保持该预约，候选粗细逐帧增长不得再次 resize。
- Popup 已可见时，粗细快捷按钮或切换笔型产生的程序化动画必须在首个变化帧按 `drawAttributePenThickness.tar`、滑块归一化目标和 FineDial 目标位置预留紧致包络，并覆盖当前到目标的动画段、数字内外迁移与 Popup 回弹；不得等动画结束后才按实际内容追扩，也不得因此退化为预留当前笔型完整量程。
- 绘制使用布局坐标，D2D 帧 transform 统一平移 `-capacityOrigin`；ULW 在同一次调用中提交 `pptDst/psize/pptSrc/prcDirty`。Bar 原生鼠标消息必须在窗口线程入队时就用当次 Win32 消息的屏幕位置固化为 monitor-local layout 坐标，然后丢弃 HiMsg 默认 client 副本；合成触摸、Raw Input 和计时器重新命中也必须在生产时转成同一 layout 空间。禁止在交互线程出队时再读取新 viewport 解释旧 client 坐标，否则 resize 恰好夹在入队/出队之间时会出现一次命中跳变。
- 保持单次 GDI interop 链：`GetDC(D2D1_DC_INITIALIZE_MODE_COPY) → UpdateLayeredWindowIndirect → ReleaseDC`。不得在没有端到端数据的情况下加入 CPU staging bitmap、DIB Section、`CopyFromBitmap`、`Map` 或脏行 `memcpy`；这些会引入额外拷贝和更复杂的持久像素一致性。
- 装饰租约跳帧只延迟提交，不能清除变化键或累计 damage。设备 generation 变化、资源重建失败或呈现事务任一阶段失败都强制下一次全窗口恢复。
- 只有 `GetDC → UpdateLayeredWindowIndirect → ReleaseDC → EndDraw` 全部成功才可推进业务 damage、viewport controller、mapping tuple、交互命中边界和调试覆盖快照；失败时保留请求并强制下一帧全脏。ULW 已成功但后续阶段失败时，真实 HWND 已经改变，但任何内部呈现快照仍不推进，下帧继续按旧成功 tuple 判定整窗替换并重新对齐。输入消息已在窗口线程入队时固化为 layout 坐标，不依赖异步 viewport 快照发布。
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
| 成功 tuple 稳定 | 允许以同一 damage 约束 D2D clip/Clear 和非空 `prcDirty` |
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
- Headless 覆盖 client/layout/surface 坐标往返、动画批次扩展/中间帧不 resize/idle 一次收缩、普通 hover 不预留整容量、整栏拖动保持尺寸、容量突破扩容，以及 `pptSrc + psize` 始终位于 target 内。
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
~~~

#### 3. Contracts

- 水平捕获目标是当前显示器 `monitorBounds` 的水平中点；参与居中的几何只包含主按钮、主栏及可见描边的联合外框。扩展面板不参与中心计算或拉伸，只按其按钮锚点的二维映射差值刚性跟随。
- 水平捕获只在竖向已 `BottomDocked`、主栏展开且联合外框中心进入 `BarBottomDockCenterThresholdDip = 40 DIP` 时生效；边界值允许捕获，严格越界立即脱离。竖向继续使用 `BarBottomDockThresholdDip = 20 DIP`；折叠或竖向脱离必须结束水平捕获。
- 横纵 mode、phase、elastic offset、直接窗口位移和显示环境共用 `bottomDockTransitionSerial` 的同一发布事务。交互线程必须先计算完整两轴候选，再发布偶数稳定 serial；渲染线程不得提交只包含一轴新状态的帧。
- 松手发布 `bottomDockDragActive=false` 后，`directWindowDragPhase` 仍可能短暂为 `Dragging`。只要仍有待吸收直移，渲染线程在取得稳定 release tuple 后必须返回 Retry；下一帧先把 phase 原子切到 `Absorbing`，在 `directWindowDragMutex` 内吸收 translation、重基准成功快照并执行 `PositionUpdate()`，然后才允许释放态布局和既有换向动画。
- 交互重基准、直接 `SetWindowPos` 失败回滚和下一手势起点只读取最后成功呈现快照。水平 tracker 的输入必须是指针驱动、未形变的主体中心；形态呈现 barrier 只能确认窗口位移已提交，不得用视觉主体中心改写抓取偏移或 tracker 基准。水平捕获与脱离首帧必须从已显示像素播入恢复平移，不能把逻辑锚点切换表现为 HWND 跳变。
- D2D 几何计算完成后到 ULW 之间，交互线程仍可直移 HWND。最终目的地必须在 `directWindowDragMutex` 内同时读取 transition serial、目标 translation 和 `directWindowPresentedTranslation`：帧仍属于当前偶数 serial 时可消费最新目标；serial 过期、发布中或提交前后不一致时必须使用实际已呈现 translation，禁止用帧内旧 translation 把 HWND 拉回一帧。
- 主按钮及 Logo 使用水平刚性抓手映射并保留既有竖向果冻；主栏背景、普通按钮、图标和文字使用独立二维主体映射。主栏近端随抓手移动，远端吸附稳定居中边界：右向展开从中心左侧进入时拉伸、越过中心后压缩，左向展开镜像；捕获、脱离和恢复中重新捕获的远端都必须从上一成功像素按新旧 HWND 位移反推。绘制、dirty、viewport/capacity、PointLight 逆映射、第三光源接受区和命中必须按刚性抓手/主体映射分类；两轴 24 DIP 视觉限值、Gaussian 外扩和抗锯齿余量都进入保守包络。
- 启动时已有的展开中置底栏发布为稳定居中。折叠时退出居中但不移动主按钮；底栏重新展开时只在最终展开联合外框仍位于 40 DIP 阈值内时无提示捕获。桌面首次放置必须在首次方向分类前初始化为向右展开（主按钮居左），白板入口保持既有方向。稳定居中展开时保持渲染线程当前布局方向，不得因动态 HWND 包络重新换边；离开居中后普通换向仍按既有关键帧执行。
- 底栏 `PositionUpdate()` 只能把最后成功呈现的水平模式作为方向分类门禁：`Centered` 时保持当前布局方向；非居中时只有有限且大于零的窗口宽度才允许按中轴重新分类。首帧零宽、无效宽度或动态 HWND 包络都不得自行产生新方向。
- `BottomDocked + Centered + Stable + Expanded` 且无拖拽、水平弹簧或显示切换时，主按钮 X 是稳定居中的唯一根位置。渲染线程必须在主栏、主按钮和按钮动画值推进后，同时在 Popup、颜色面板、粗细面板等下游绝对几何派生前，以主按钮、主栏及两者当前可见描边的联合外框反推 `mainButton.x`，同步 `displayCenterX`，再重新执行 `MainBar.Inherit(Center, MainButton)`；整个既有继承树必须在同一帧消费新根节点。主栏动画不得因居中而重启，属性面板、More、Popup 和提示框不得参与中心计算。捕获、拖拽、脱离、恢复、Free、浮动、折叠、白板放置和显示切换继续由原状态机持有根位置。
- 稳定居中不得再维护逐帧 layout correction、补偿平移、pending/in-flight rebase、方向锁存或遗留换向清理状态。viewport 预测必须从主栏 `x/w` 和描边的完整动画 range 保守反推根节点 X range，并把该 range 传播给主按钮、主栏及继承子视觉；禁止用全局 correction outset 扩张包络来代替真实父几何范围。
- 一次二维主体命中必须先捕获一个成功呈现快照，再由同一 tuple 同时逆映射 X/Y；禁止两个轴分别读取 `BottomDockPresentedSnapshot()`，否则并发发布可能组合出从未成功呈现的映射。普通主栏按钮使用主体 X/Y，More 等刚性覆盖层继续使用原始刚性坐标，不能为复用而原地改写消息。
- 提示资格在手势内锁存：浮动主栏真实进入底栏，或 `Free / Centered` 发生任意双向切换时建立；底栏内起拖但未发生转换时不显示。资格建立后，Free 阶段显示“底栏模式”，Centered 阶段显示“底栏模式 · 居中”；只有松手、折叠、取消或竖向脱离时清除，同一手势重新进入底栏可以再次建立。文案变宽必须先完成外框扩宽再交换文字，变窄必须先完成文字交换再缩窄外框；命中仍只发布成功呈现的实际外框。首次激活帧即使缩放仍为零，也必须把上次成功外框、当前外框和覆盖 Back 峰值、描边、第一/第三光源、Gaussian 与抗锯齿的完整包络并入 damage；失败呈现保留该 damage。
- 指示器的锚点是底栏二维映射的下游几何。映射变化时，只要指示器正在显示、退场或仍有上次成功边界，就必须标记其稳定视觉键并观察同源完整光影包络；不得因指示器自身文字、样式和显隐进度未变而跳过旧新边界 damage。
- 点击后的悬停抑制以物理屏幕坐标为准：窗口重基准产生的同坐标相对消息不得重新悬停；同一识别区域内只要收到一次真实屏幕坐标变化的 `WM_MOUSEMOVE` 就必须解除抑制，点击不是恢复条件。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 同一采样同时进入竖向和水平捕获带 | 两轴与窗口位移一次发布，首个成功帧连续 |
| release tuple 已稳定，但直移 phase 仍为 `Dragging` 且 translation 非零 | 当前帧 Retry；下一帧吸收并 `PositionUpdate()` 后才执行释放布局 |
| D2D 帧建立后 transition serial 改变或正在发布 | ULW 目的地保持锁内实际 HWND translation，不回退到帧内旧位移 |
| 居中拖动严格超过 40 DIP | 水平进入 Free/Detaching，竖向底栏可保持 |
| 竖向脱离或主栏折叠 | 水平停止捕获并连续恢复，不保留不可见约束 |
| 底栏起始手势 `Free → Centered → Free → Centered` | 提示矩形持续可见，文案在普通/居中之间按防裁切时序双向切换 |
| 呈现或直接移动失败 | 两轴成功快照都不推进，下帧从旧 tuple 完整恢复 |
| 稳定居中主栏 `x/w` 正在变化 | 每帧从当前值反推主按钮根节点，联合可见中心保持显示器中点 |
| 捕获、拖拽、弹簧或显示切换 | 禁止根节点反推，原位置所有者继续工作 |
| 居中态或窗口宽度为 0/非有限 | `PositionUpdate()` 保持当前布局方向，不创建换向批次 |
| 呈现快照在一次 hover 换算期间更新 | 本次 X/Y 仍全部使用已捕获的旧成功 tuple；下一消息再使用新 tuple |
| ULW/EndDraw 失败 | 现有 dirty/present 事务保留重试；不存在额外居中 rebase 状态 |

#### 5. Tests Required

- Headless 覆盖水平 `-40/0/+40 DIP` 边界、严格越界、高速跨带、横纵同帧捕获、折叠门禁、展开自动捕获、左右展开联合外框和 100%/150% DPI；竖向 20 DIP 边界另行保持。
- Headless 覆盖稳定居中根节点所有权矩阵、左右展开、可见描边、无效几何、Draw → Selection 宽度单调与逐帧联合中心不变量、桌面/白板初始方向、零宽/非有限宽度方向保持、主体 X/Y 非恒等映射往返、扩展面板锚点、首次显现完整光影包络、失败快照、release handoff 门禁、当前/过期/发布中帧的实际 HWND translation 解析，以及浮动进入/底栏双向切换/未切换手势的提示资格。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`；手工检查横纵果冻叠加、脏区调试、动画关闭和多显示器切换。

#### 6. Wrong vs Correct

~~~cpp
// Wrong：分别发布横纵模式，渲染线程可能观察到半个吸附转换。
bottomDockMode.store(nextVertical);
bottomDockCenterMode.store(nextHorizontal);

// Correct：在同一 transition serial 内发布完整两轴 tuple。
BeginBottomDockTransition();
PublishBottomDockAxes(nextVertical, nextHorizontal, translation);
EndBottomDockTransition();
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

// Correct：release 先等待直移接管；过期帧停在锁内实际 HWND 位移。
if (ShouldDeferBarBottomDockReleaseHandoff(
	frame.dragActive, directPhase == Dragging, translationPending))
	return FrameResult::Retry;
const POINT destination = ResolveBarBottomDockFrameTranslation(
	frame.serial, serialBefore, serialAfter, latestTranslation,
	presentedTranslation);
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

- `Dormant`：第三光源目标透明度为 0，鼠标 Raw Input 必须注销；仅 Bar 或四个 PageControl HWND 自然收到真实 `WM_MOUSEMOVE` 才能进入 `Inside`。触摸/笔生成的兼容 mouse 不得激活。
- `Inside`：唯一 Bar HWND 注册鼠标 `RIDEV_INPUTSINK`；首次离开 Bar 与四个 PageControl 的实际接收消息区域集合时进入 `Grace`，并记录 `GetTickCount64() + 5000` 的绝对截止时间。PageControl 不得注册第二份 Raw Input 或 timer。
- `Grace`：区域外移动不得重置截止时间。第三光源使用独立的 `240 × barStyle.zoom` 径向渐变；已发布 UI 外框只用于判断光圈是否可能命中 UI，从而裁剪渲染唤醒，不得作为全局亮度乘数。
- 同一控件的同一边框像素上，第三光源贡献只由光标到该像素的距离、生命周期强度和控件固定比例决定；光标是否位于接受消息区域、主栏或其他可见区域内不得改变该贡献。第一光源可独立影响最终合成结果，不属于该一致性契约。
- `Grace → Inside`：重新进入实际接收消息区域时取消定时器；仅回到 240px 邻域不能从 `Dormant` 唤醒。
- `Grace → Dormant`：绝对截止时间到达，或画布开始真实绘制时，注销 Raw Input 并从当前强度平滑淡出。落笔时若光标仍在 UI3 接收区，区域内后续移动不得重新激活；必须先收到离开，再由下一次自然进入激活。
- 5 秒等待使用窗口定时器，不得新增轮询线程或靠持续渲染计时。
- Main Bar 每个实际光源帧以屏幕物理像素发布第三光源位置、`240 × barStyle.zoom` 半径、当前生命周期强度和可见性；PageControl 只映射到各自 presentation target。分页本地 hover 与第三光源是两条状态：hover 可快速退出，不能据此清零或重新创建第三光源。
- PageControl 只有在完整 present 与 `SetBounds/Show` 成功后才发布该 HWND 的屏幕边界；成功隐藏发布空边界。该列表只参与 Grace 邻域唤醒裁剪，不能改变亮度、截止时间或 `WindowFromPoint` 的实际接收判断。
- 并发笔迹由原子 activity count 合并：`0 → 1` 才向 Bar 窗口线程发送 Started。Draw3 controller 必须对聚合 bool 去重，多 contact 不重复发送；Host stop/异常时若仍 active 必须补 false。Ended 只负责把 activity count 安全归零，不维持绘图静默状态。
- Bar 窗口线程收到有效 Started 时必须无条件让第三光源进入 `Dormant` 并注销 Raw Input。若系统光标仍位于 Bar 或 PageControl 实际接收 HWND，必须建立 wait-for-leave 门禁：区域内后续移动不能重新激活，真实离开后才允许下一次自然进入。后续绘制过程和抬笔不再持续控制光影。
- 首次 Started 复用 Bar 的辅助界面收起路径，关闭绘制/几何属性、更多、笔型、粗细、颜色与提示浮层；不得改变主栏 `fold` 或隐藏 PageControl。多 contact 与后续 Started 不重复执行可见状态转换。
- 落笔通知不得参与 `BarUiEdgeLightingEnabled`、第一光源、光色过渡或普通 UI 动画门禁；画布线程仍只能通过通知接口请求第三光源休眠，不得直接写 UI、D2D 或 Raw Input 状态。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `RegisterRawInputDevices` 注册失败 | 第三光源保持隐藏；错误至多记录一次，不影响主光和 UI 交互 |
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

- `Enable=false`：第一主光、第三鼠标光和 Gaussian 柔光均不绘制；基础灰边继续绘制；停止仅服务于光影的动画推进，并请求第三光源进入 Dormant、注销 Raw Input。
- `Enable=true, Dynamic=false`：第一主光和 Gaussian 柔光保持；只有第三鼠标光关闭，禁止注册/处理其 Raw Input。切换关闭时可以沿既有 300ms 淡出收尾。
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

- Good：关闭动态开关后主光仍可见，第三光源完成收尾后不再注册 Raw Input。
- Base：关闭总开关后只剩基础灰边；重新开启后恢复主光，并保留此前动态开关选择。
- Bad：把动态开关乘到全部光影或基础灰边上，导致关闭第三光源时第一主光也消失。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工验证四种组合：总开关关、总开关开且动态关、两者全开、动态关后重启。
- 检查 `main.json` 两个路径、旧配置缺失字段默认值、设置卡片条件显示与容器高度。
- 在动态关闭和总开关关闭时观察 Raw Input 注销及静止/移动 CPU，确认第三光源不再唤醒渲染。

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

- Laser 预览固定使用 `NonLaserStable -> EnteringCore -> EnteringShell -> LaserStable -> LeavingShell -> LeavingCore`。进入时芯宽、颜色、曲率/圆角和荧光渐变先在 `0.4s` 内连续到达白色芯端点，随后红壳再用 `0.4s` 从芯宽展开；退出时先收红壳，再改变 semantic core。
- `EnteringShell` 与 `LeavingShell` 对 core thickness、outer thickness、morph 和 white mix 使用 `Hold` 目标策略，不得用已经切换的逻辑笔宽重新提交 target。只有红壳完全隐藏并进入 `LeavingCore` 后，才允许芯层转向非 Laser 目标；反向切换继续使用锁存的 Laser target。
- 红壳先绘制、semantic core 后绘制。预览包络使用 `max(coreThickness, currentShellThickness)` 约束曲线振幅和裁剪，但 semantic core 的实际绘制宽度仍只读取 core thickness，不能被红壳宽度替代。
- 白芯与红壳的曲线/胶囊两端圆心必须共享阶段化 outer thickness。曲线路径以 `outer / 2` 计算端点，圆角矩形按 `(outer - layerThickness) / 2` 水平收进；因此壳进度为零时红层被白芯完全覆盖，壳展开时只改变 stroke width，不移动两端圆心。Slider 展开时 endpoint diameter、core thickness 和当前 shell thickness 必须一起连续趋向 track thickness。
- `GetFrameSolidColorBrush` 返回帧内复用的可变 solid brush；后续调用会原地改色。不得跨另一处 `GetFrameSolidColorBrush` 调用保留画刷颜色假设；红壳绘制后必须紧邻 semantic core 绘制重新提交 `previewColor`，否则白芯会继承红壳颜色。
- 颜色、曲率、圆角矩形进度、外套宽度和内芯宽度均从当前值续接，中途反向不得回到任一端点；Laser 稳态的 `3/5/7 DIP` 切换同时 retarget 芯宽和外宽。
- Circle/Number 切换时锁存 outgoing 内容：Circle -> Number 先保持旧圆直径并淡出，再显示已锁存的新数字；Number -> Circle 先保持旧数字并淡出，再显示圆。Circle -> Circle 才允许直径连续 retarget；切换中点旧新内容透明度均为零。
- 工具失去扩展资格时，命中区和按压状态立即失效，arrow/divider 的视觉则按 current progress 退场；其几何锚点和颜色必须派生 selected button 的当前 `x/y/frame`，不能读取新目标位置或首帧直接改 Accent。
- 重新进入 Pen 面板时按当前工具直接初始化稳定 Circle/Number 语义；不得从离开前工具制造一次假的内容切换。动画中的状态不得仅按 `laserActive`、`ModeSelect` 等目标布尔量选择互斥绘制分支。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Highlighter -> Laser | 渐变、圆角、颜色和芯宽先连续到白芯端点，之后红壳才出现 |
| Laser -> Highlighter | 红壳完全隐藏前 core/outer/morph/white target 保持 Laser 端点；之后才连续恢复渐变矩形 |
| Pen <-> Laser 中途反向 | `EnteringCore <-> LeavingCore` 从当前芯值反向；`EnteringShell <-> LeavingShell` 只反向 shell progress，并保留锁存 target |
| Laser 稳态切换粗细 | core 与 outer 同时连续 retarget，shell 保持完全展开 |
| 细笔/粗笔 -> Laser | outer 的当前动画值分别增大/减小，白芯水平 span 连续收窄/拓宽；红壳与白芯端点圆心始终一致 |
| Laser 预览 -> Slider | endpoint diameter、core 和当前 shell 同步 morph 到 track thickness，不在交接帧改变端点 |
| 红壳与白芯同帧绘制 | 红壳先画；其后重新以 `previewColor` 配置帧内 solid brush，再画白芯或渐变失败 fallback |
| Circle <-> Number 中途反向 | 从当前透明度继续；outgoing 内容和尺寸保持锁存 |
| Laser 使扩展入口失效 | 命中立即归零；arrow/divider 平滑退场且不残留 |
| 离开 Pen 后以 Highlighter 重进 | 首帧直接为稳定数字，不出现圆形过渡 |

#### 5. Good / Base / Bad Cases

- Good：阶段 helper 决定 `Hold/Laser/NonLaser`，`LeavingShell` 只提交 shell target；红壳隐藏的交接帧进入 `LeavingCore` 后才提交新笔型语义。
- Base：稳态 SoftPen/HardPen/Laser 为 Circle，Highlighter 为 Number；超出按钮内框的 Circle 仍按既有规则显示真实数字。
- Bad：目标工具一改变就在 `LeavingShell` 使用新 `penThickness` 计算芯壳目标，或在红壳改色前缓存共享 solid brush 并用于后绘白芯；前者造成跳宽，后者使白芯变红。

#### 6. Tests Required

- Headless 覆盖 0/25/50/75/100% 正反切换、六阶段首帧/中间帧/交接帧/终点、`Hold/Laser/NonLaser` 目标策略、锁存 target、芯/壳包络、共享端点圆心、细笔/粗笔切入 span 和 Slider endpoint morph、颜色端点/反向、Circle/Number 锁存、Circle -> Circle 直径、四种工具初始化语义和扩展入口资格/当前锚点/当前颜色。帧内 D2D brush 身份由绘制顺序静态审查和完整构建验证。
- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 与 `Release | ARM64`；受限环境只运行 `InkeysHeadlessTests.exe --no-window`，不得启动 GUI。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：LeavingShell 读取新工具宽度，红壳退场期间白芯提前改变。
core.SetTar(penThickness / 3.0, duration);
outer.SetTar(penThickness, duration);

// Correct：Hold 阶段只改变 shell；LeavingCore 才提交非 Laser 语义。
auto policy = ResolveBarLaserPreviewTargetPolicy(phase);
if (policy.core != BarLaserPreviewSemanticTarget::Hold)
	SubmitCoreTarget(policy.core);
shell.SetTar(policy.shellExpanded ? 1.0 : 0.0, duration);

// Wrong：保存芯层 brush 后，红壳调用把同一帧内 brush 原地改成红色。
auto coreBrush = GetFrameSolidColorBrush(previewColor, opacity);
DrawLaserShell();
DrawCore(coreBrush);

// Correct：红壳画完后紧邻芯层重新配置共享 brush。
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
- 文件写盘、Shell、模态确认、重启和 DDB 操作进入单一 FIFO。配置命令在生产者线程冻结 JSON 或 `Inkeys::Config` 副本；worker 不读取实时 `setlist`、`pptComSetlist` 或 `Inkeys::config`。停止时禁止新命令，并按 FIFO 排空已接收命令。
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

- 每侧分页控件固定为 `230x80 DIP`，使用三个稳定的真实 Bar `twoTwo` 按钮。按钮内部布局、SVG、颜色、光效、交互、动画和 visual damage 来自前述共享 Bar 运行时；PageControl 只拥有横向三按钮拓扑、独立资源与 ULW present 映射。页码按钮保持标准交互视觉，但业务命令为 no-op。
- `PptBottomLeft/PptBottomRight` 是 Presentation 与 Whiteboard 共用的唯一左右底部分页 HWND；不得恢复 `WhiteboardLeft/WhiteboardRight` 窗口角色、窗口规格或句柄。`Inkeys.UI.PageControl` 是两种工作区唯一的 surface host、输入路由和呈现所有者，PPT/Whiteboard 只发布状态与业务回调；Whiteboard 不创建 DragHandle，也不继承 PPT 的拖动、滚轮、长按、缩放或位置持久化。
- PageControl 的稳定态为 `Hidden / PptCompact / WhiteboardExpanded`。`WhiteboardState::expandedLayoutTarget` 对底部 surface 的优先级高于 PPT visible，并隐藏两侧；背景 `active` 只表示 Draw3 Whiteboard 首帧已就绪后的 Freeze 目标。两者不一致时 PageControl 必须锁定输入，不再使用 Presentation/Whiteboard 双 renderer owner 或 `TransitionHidden` 中间 owner。
- PageControl 的 render/present 事务锁只保护 D2D、GDI interop、共享 Bar visual state 与 present 快照；同步 `Window::Service::Hide/SetBounds/Show` 必须在该锁外执行。由于只有一个呈现所有者，迟到帧必须消费最新快照后重新定向，不能靠调用方抢先隐藏共享 HWND 仲裁。
- Enter 先撤销 capture、关闭 PPT DragHandle 命中并进入 Whiteboard window mode；PPT 可见状态保持当前 COM 事实。窗口模式事务成功后立即发布 expanded 目标，使已显示 PPT 底栏从当前实际 HWND 位置同时移动到固定 `5 DIP` Whiteboard 角落、收拢 Drag slot 并连续形变；原本未显示时直接使用 `230x80 DIP` 最终位置/几何渐显。Whiteboard 背景 active 仍须等待 Draw3 Whiteboard 首帧就绪。Exit 先发布 compact 目标并关闭背景 active，反向回到 PPT 最新运行时位置；不得在交接前后显式隐藏两个共享底部 HWND。Exiting 期间反向重入从当前背景、按钮、内容、DragHandle 与 HWND 插值值重新定向，再等待 Draw3/Freeze readiness，期间保持输入锁定。
- `totalPage` 至少为 1，`currentPage` 限制到 `[1, totalPage]`。首次发布若直接为 `switching=true`，稳定基线必须来自该次真实输入。
- 进入翻页事务时锁存上一稳定帧的 Previous enabled 与 Add/Arrow；事务中只把三个 `interactive` 置 false，未变化的 SVG 和文字不得重启动画。普通 Arrow 跨工作区保留同一 `barMore` 对象并只缩放；追加页事务全程保持 Add，只有稳定语义真实发生 Arrow/Add 变化时才运行一次同批 SVG/文字转换。
- `Inactive / Entering / Active / Exiting` 的每次 workspace 转换都先调用 `CollapseAuxiliaryPanels(true)` 并撤销 Whiteboard capture。进入稳定白板时绘制属性、几何属性、更多、笔菜单和粗细预览均保持关闭。
- Freeze 是 Whiteboard mode 的唯一 `WS_EX_APPWINDOW`、任务栏和 activation anchor；Drawpad 可激活但保留 `WS_EX_TOOLWINDOW`，其余辅助窗口不进入任务栏。整个 owner group 保持 `HWND_NOTOPMOST`，最小化/恢复只恢复此前可见成员。
- 退出必须恢复 `DrawpadPresentation` click-through、Bar `fold=true` 与 dock request/lock。顺序固定为 `LeaveWhiteboardWindowMode()` 后 `SetOverlayTopmost(true)`；不得让 style 事务覆盖恢复后的 Z 序。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `3/3 -> 3/4 -> 4/4` 且 switching | 右按钮始终为 Add；三个按钮只锁输入 |
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
Window::Service::PromotePptWindow(WindowRole) -> bool;
Window::Service::Enqueue(WindowRole, Message::Message) -> bool;
Window::Service::StopAndJoin() noexcept;

Graphics::DibSurface(int width, int height);
Graphics::DibSurface::dc() -> HDC;
Graphics::DibSurface::pixels() -> std::span<std::uint32_t>;
~~~

### 3. Contracts

- Window Service 的受管线程拥有 Mag host/child、Freeze、DrawpadPresentation、Drawpad、四个 PageControl HWND、Bar、Setting 和 DisplayObserver；Whiteboard 复用两个 `PptBottom*` HWND，不再创建独立左右窗口。创建结果通过 promise/future 返回，stop callback 用事件唤醒 `MsgWaitForMultipleObjectsEx`。Setting 仍是普通 app window，但不再自带绘制线程。
- style、owner、显隐、bounds、click-through、HiMsg bind/unbind 和销毁必须投递到 HWND 所属线程。`UpdateLayeredWindowIndirect`、D3D present 和明确要求 HWND 的外部 API 是受控跨线程例外。
- 基础 overlay owner 链只在创建时建立：`Mag -> Freeze -> {DrawpadPresentation, Drawpad -> PPT/Bar}`；Mag 缺失时 Freeze 为根。Presentation mode 中 overlay 保持 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`。Whiteboard mode 是显式例外：Freeze 切为唯一 `WS_EX_APPWINDOW`、可激活和任务栏锚点；Drawpad 清除 `WS_EX_NOACTIVATE` 但保留 `WS_EX_TOOLWINDOW`；其他成员仍为非任务栏辅助 UI。Bar 必须高于所有 PPT；共享底窗或其他 PPT show、`PromotePptWindow` 只把目标窗放到 Bar 正下方。置顶刷新只对链根调用一次 `HWND_TOPMOST` 或 `HWND_NOTOPMOST`，且 Whiteboard mode 强制 NOTOPMOST。Win32 会把根的 topmost band 变化传播给 owned popup；刷新后非根出现 `WS_EX_TOPMOST` 不能证明代码对它执行了独立置顶，必须审查 `SetWindowPos` 调用点。白板期间对 Freeze 调用 `ITaskbarList2::MarkFullscreenWindow`，退出和销毁前清除。
- PPT 可见性 `false -> true` 发布完成后立即请求一次根置顶刷新；成功后连续可见状态去重，失败时保留 pending 并由既有 500ms 发布节拍重试，离开放映取消 pending。PageControl 的 present 成功不代表 HWND 提交完成：`SetBounds/Show/Hide` 任一步失败都返回 RenderPipeline `Retry`。Draw3 surface 切换失败同样保留 reconciliation pending，由既有 250ms 状态节拍重试；只有窗口提交成功后才更新 drawpad ready 事实。
- Setting owner 必须为 null，style 固定为 `WS_POPUP | WS_CLIPCHILDREN`，不得包含 caption/thickframe/minimize/maximize/system-menu；ex-style 包含 `WS_EX_APPWINDOW` 且排除 topmost/layered/noactivate/toolwindow。窗口必须有箭头光标、大小图标和任务栏按钮，显示时由所属窗口线程主动 restore/show 并请求 foreground/active/focus；`WM_GETMINMAXINFO` 把最小/最大 track size 固定为配置尺寸。
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
| Whiteboard window mode 切换失败 | 回滚已修改成员的 style/visibility，不发布稳定 workspace 状态 |
| Whiteboard group 收到最小化/恢复 | 保存成员可见性；恢复时只显示此前可见成员，不激活辅助窗 |
| Setting 传入 overlay ex-style 或 owner | Service 强制归一化为普通 app window 且 owner=null |
| Bar/PPT 收到系统触摸兼容 mouse | HiMsg callback 不入队但继续 WndProc；业务 WndProc 同样返回 0，自定义 `WM_TOUCH -> Enqueue` 是唯一单指来源 |
| PPT hide 后重新 show 或交互前置 | owner 仍为 Drawpad，目标位于 Bar 正下方，且前台/焦点窗口不变化 |
| PPT 进入放映时根刷新失败 | 保留一次 refresh pending；后续状态发布继续请求根刷新，成功或离开放映后清除 |
| PageControl 的 bounds/show/hide 失败 | 当前 surface 返回 `FrameResult::Retry`，不回滚目标可见性或改用节点级 `HWND_TOPMOST` |
| Drawpad surface 显隐提交失败 | 不发布假完成的 ready 事实；即使 Draw3 runtime revision 不变也按 250ms 节拍继续收敛 |
| 未配置上述 callback 的其他 HiMsg binding | 保持库默认行为，系统触摸兼容 mouse 正常入队 |
| 队列满或 shutdown | `Enqueue` 返回 false；队列满增加 dropped count，shutdown 不再接收 |
| DIB 创建或 resize 失败 | 原 surface 保持有效，临时 GDI 资源全部释放 |

### 5. Good / Base / Bad Cases

- Good：Draw3 绘制线程只向已请求且就绪的 target present；双窗尺寸与互斥显隐通过 Window Service；根刷新整体抬升 owner 树，Bar 与目标 PageControl 只在树内用 `HWND_TOP` 保持顺序。
- Base：隐藏根也能通过 `RequestTopmostRefresh()` 越过同桌面的外部 topmost HWND；Win32 传播后的非根 topmost style 是 owner 树状态，不是节点级调用证据。
- Bad：渲染循环直接 `SetWindowPos(..., HWND_TOPMOST, ...)` 重排每个 overlay，或在窗口提交失败后返回 Idle，都会让 owner 树与目标 UI 长期不收敛。

### 6. Tests Required

- ARM64 host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64 /m:1`。
- Headless 覆盖 Surface 创建/复制/移动/resize/合成/加载保存/失败路径和 GDI handle 压力；HiMsg 覆盖过滤、clear、capacity、dropped、shutdown、并发及合成触摸字段往返。
- Message 测试需覆盖 touch signature + touch flag、真实鼠标、笔兼容 mouse、wheel/hwheel 和 XButton；Window 测试需覆盖线程 ID、owner/style、动态创建失败回滚与 stop 后无 HWND/jthread。禁止创建 HWND 的环境使用 `InkeysHeadlessTests.exe --no-window`，Window 合同仅做编译和静态检查。
- Window 测试还需覆盖持久 `SetOverlayTopmost`、`SetOverlayFullscreen`、Whiteboard activation style 和 group minimize/restore；fullscreen 不得自行改变 topmost 位，退出或 `StopAndJoin` 前必须清掉 Freeze 全屏标记。
- 允许创建隐藏 HWND 时，Window 测试需创建一个 ownerless 外部 topmost 竞争窗：先确认它位于完整 owner 树之上，再刷新根并确认每个 overlay popup 都越过竞争窗；同时断言根保持隐藏、Bar 位于目标 PPT 之上且前台/焦点不变化。禁止用“刷新后非根没有 `WS_EX_TOPMOST`”判断独立置顶，因为该位可由 Win32 owner 传播。
- RenderPipeline 测试需保留 `Retry` 会再次调度的合同；若没有稳定的 Win32 失败注入边界，PageControl/Draw3 的失败映射通过生产分支静态审查和完整集成构建验证，不得为单测扩大 module 公共 API。
- 手工 Z 序、Setting 任务栏/激活、Draw2/PPT/Freeze/Mag/DPI 回归必须在允许 GUI 的独立阶段执行，不能用静态构建冒充。白板全屏必须确认任务栏按普通全屏窗让出，且主栏/翻页栏底边都距屏幕底边 `5 DIP`。

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
// Wrong：从 Drawpad 渲染线程直接修改所属线程状态。
SetWindowLongPtrW(drawpad, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);

// Correct：主 Drawpad 样式不变；目标内容就绪后由 Window Service 互斥切换 surface。
Inkeys::Window::GetService().SetDrawpadSurfaceVisibility(
    Inkeys::Window::DrawpadSurfaceVisibility::Presentation);
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
