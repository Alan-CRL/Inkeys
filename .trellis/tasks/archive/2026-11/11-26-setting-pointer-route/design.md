# 设计：设置窗口触摸与笔经 Pointer/HiMsg 路由导入 ImGui

## 1. 主栏框架回顾（要对齐的分层）

| 层 | 主栏实现 | 位置 |
| --- | --- | --- |
| 窗口过程入口 | `barWindowMsgCallback`：`DrainCancels`、取消类消息 `CancelWindow`、`Touch::HandlePointer(hWnd,msg,wParam)` 为 true 时 `return 0` | `UI/Bar/Bar.Interaction.cpp:3226` |
| Pointer 解码 | `HandlePointer`：只处理 `WM_POINTERDOWN/UPDATE/UP/CAPTURECHANGED`；`GetPointerInfo` → 仅 `PT_TOUCH/PT_PEN` 且接触中；`POINTER_FLAG_CANCELED` 撤销；非主按钮吞掉；同 id 旧接触、同设备新主接触撤销；转值型 `TOUCHINPUT` | `Input/UiPointerInput.cpp:1044` |
| 路由上下文 | `Touch::Context` + `Adapter{probe,deliver,cancel}`，五窗登记表 `targets` | `Input/UiTouchRouting.h`、`UiPointerInput.cpp:836` |
| 消费者 | `DeliverBarTouch`：构造 `Pointer::Sample{source=Touch/Pen, kind, screen, time, generation, device, contact}` 与翻译后的 `ExMessage`，入交互 worker | `Bar.Interaction.cpp:3145` |
| HiMsg | `QueueWindowMessageInLayoutSpace`：带 Pointer 签名的兼容鼠标 `Discard`，原生鼠标转样本入队 | `Bar.Interaction.cpp:10171` |
| Win7 | `PointerMode()==false` 时 `WM_TOUCH` → `HandleMessage` → `DispatchDecoded` | `UiPointerInput.cpp:1117` |

设置要对齐的是这套分层与规则，但消费者是 ImGui IO 而不是 UI3 交互 worker。

## 2. 关键约束：线程

- Window Service 把设置窗口放在独立的 setting owner 线程（`Window.cpp:1962` 起按 `IsSetting(role)` 分队列/线程），主栏与四个分页窗在 overlay 线程。
- 五窗登记表 `targets`、`Find`、`CancelTarget` 等无锁，只在 overlay 线程使用；`TargetCount=5` 的槽位语义（0=Bar，1–4=Page）也写进了 spec（`ui-pointer-session.md` “Miss/Blocked/外部/设置/画布不接纳”）。
- 结论：**不把设置登记进五窗表**。设置拥有一个单窗口、单接触的本地路由，复用同一份解码与规则。设置与五窗之间天然不存在跨窗接触（设置是普通可激活窗口，不是覆盖层），无需跨窗转交。

## 3. 方案

### 3.1 抽出共享 Pointer 解码（行为不变的重构）

把 `Touch::HandlePointer` 中“查询 + 分类”部分抽成纯函数（放在 `UiPointerInput.cpp`，声明于 `UiTouchRouting.h`）：

```cpp
enum class PointerDecode { NotHandled, Swallow, Cancel, Contact };
struct DecodedPointer { TOUCHINPUT input{}; Sample sample{}; UINT32 id = 0; UINT32 flags = 0; LONGLONG performanceCount = 0; };
PointerDecode DecodePointer(UINT message, WPARAM wParam, DecodedPointer& out) noexcept;
```

- `NotHandled`：非接触 Pointer、悬停、`PT_MOUSE`、查询失败的 Down（调用方交 `DefWindowProc`）。
- `Swallow`：非主按钮/笔桶键 Down（吞掉，不进业务）。
- `Cancel`：`POINTER_FLAG_CANCELED`、`WM_POINTERCAPTURECHANGED`、Move/Up 查询失败（调用方撤销该 id）。`CAPTURECHANGED` 仍按现规则返回“未消费”。
- `Contact`：Down/Move/Up 的值型结果。
- 五窗专属逻辑（按 id 撤销 `targets`、同设备新主接触撤销、`DispatchDecoded`）留在 `HandlePointer`，改为调用 `DecodePointer`。主栏/分页行为、日志字段不变；`ui_pointer_tests` 现有用例即回归保证（`Testing::SetPointerQuery` 替身对两条路径同样有效）。

### 3.2 设置本地接触路由

新增 `Inkeys/Inkeys/UI/Setting/Setting.PointerInput.h/.cpp`（模块化风格按现有 `Setting.*.h + .cpp` 惯例）：

```cpp
namespace Inkeys::UI::Setting::PointerInput
{
    // 纯状态机：只裁决，不碰 ImGui/Win32，便于无窗口测试。
    class ContactArbiter
    {
    public:
        enum class Verdict { Ignore, Begin, Update, End, Cancel };
        Verdict Accept(const Input::UiPointer::Sample& sample, bool primary) noexcept; // Down/Move/Up
        bool CancelActive() noexcept;                     // 返回是否有活动接触需要收尾
        bool Active() const noexcept;
        const Input::UiPointer::Sample& Latest() const noexcept;
    };
    // 窗口过程入口（设置窗口线程）：返回 true 表示已消费，调用方 return 0。
    bool HandleWindowMessage(HWND, UINT, WPARAM, LPARAM) noexcept;
    // HiMsg 回调：过滤派生兼容鼠标。
    Inkeys::Message::Reply FilterChannelMessage(HWND, UINT, WPARAM, LPARAM) noexcept;
    // 取消当前接触（隐藏/销毁/会话失效等）。
    void CancelContact(HWND) noexcept;
    // R6 样本轨迹，渲染线程读取。
    struct TrackSample { Input::UiPointer::Kind kind; Input::UiPointer::Source source; float x, y; double timeSeconds; std::uint64_t generation; };
    std::size_t ReadTrack(std::uint64_t& cursor, std::span<TrackSample> out, bool& overflowed) noexcept;
}
```

裁决规则（`ContactArbiter`）：
- 无活动接触时，主接触 Down → `Begin`（代次 +1）；非主接触或已有活动接触时的 Down → `Ignore`（吞掉）。
- Move/Up 只接受同 `device + contact + generation`；其他 → `Ignore`。
- 同一 pointerId 再 Down、同设备新主接触 → 先 `Cancel` 旧接触再按新 Down 处理（对齐主栏 R2.4）。
- `CancelActive()` 幂等。
- 起始容差（实机反馈后补充，2026-11-26）：半径 `ContactSlopPixels = 20` 物理像素，不随 DPI 缩放，与主栏主按钮点击/拖动分界（`ShouldAllowBarBottomDockClick` 的 20 px）一致（用户确认；先试过 8 DIP）；主栏量的是路径累计长度，这里量的是相对按下点的直线距离，避免原地抖动累加成“拖动”；越过前的 Move 不进入 ImGui，越过前的 Up 按按下点抬起；越过后如实跟随。原因：组件卡片长按要求按住位移小于 `io.MouseDragThreshold`（6 物理像素），原兼容鼠标路径由系统吸收抖动，改为直接导入 Pointer 原始坐标后轻微抖动即转为页面滚动。容差内 ImGui 看到的位移为 0、越过即跳到 ≥ 容差，仍保持“静止容差 = 拖动阈值”无死区的合同。

### 3.3 窗口过程接线（`ImGuiWndProc`）

在首个 `switch` 之前（与主栏顺序一致）：

1. 取消类消息：`WM_CANCELMODE`、`WM_SHOWWINDOW(FALSE)`、`WM_DPICHANGED`、`WM_NCDESTROY/WM_DESTROY` → `PointerInput::CancelContact(hWnd)`（不吞消息，原逻辑照常执行）。
2. `PointerMode()` 时：`WM_POINTERDOWN/UPDATE/UP/CAPTURECHANGED` → `HandleWindowMessage`，true 则 `return 0`。
3. `!PointerMode()` 时：`WM_TOUCH` → `HandleWindowMessage`（`GetTouchInputInfo` 单次读取并 `CloseTouchInputHandle`，只处理 `TOUCHEVENTF_PRIMARY` 的那一条，其他吞掉），`return 0`。
4. `PointerMode()` 时收到 `WM_TOUCH`：派生副本，关闭句柄后 `return 0`。

`HandleWindowMessage` 对 `Contact` 结果：`ScreenToClient` 得到客户区坐标 → `ContactArbiter` 裁决 → 在 `lock_guard(settingImguiMutex)` 下写 ImGui：

| 裁决 | ImGui 事件 |
| --- | --- |
| Begin | `AddMouseSourceEvent(src)`、`AddMousePosEvent(x,y)`、`AddMouseButtonEvent(0,true)` |
| Update | `AddMouseSourceEvent(src)`、`AddMousePosEvent(x,y)` |
| End | `AddMouseSourceEvent(src)`、`AddMousePosEvent(x,y)`、`AddMouseButtonEvent(0,false)`；触摸再 `AddMousePosEvent(-FLT_MAX,-FLT_MAX)` |
| Cancel | `AddMouseSourceEvent(src)`、`AddMousePosEvent(-FLT_MAX,-FLT_MAX)`、`AddMouseButtonEvent(0,false)`（移出后松开，ImGui 不会判为点击） |

之后 `RenderPipeline::Request(Client::Settings)` 唤醒一帧（与现有 ImGui 输入后请求帧的方式一致）。仅在 `Setting::IsVisible()` 且 ImGui context 存在时写 IO；否则只裁决不写入。

> 实施修订：设置窗口可见期间每帧结束都返回 `FrameResult::Continue` 持续出帧，写 IO 后无需额外 `Request`。

#### backend 光标补位覆盖（实施中发现）

`ImGui_ImplWin32_NewFrame` 在窗口有焦点、`MouseTrackedArea == 0` 时会用 `GetCursorPos` 补一个鼠标位置事件；触摸被消费后系统光标不跟手指移动，这个补位会把触摸位置覆盖成旧光标位置。处理：
- PointerInput 维护跨线程覆盖状态：接触活动时为接触位置；触摸抬起/取消后“停放”在 `(-FLT_MAX, -FLT_MAX)`；笔结束后清除（悬停由兼容鼠标提供）。
- 渲染线程在 `ImGui_ImplWin32_NewFrame()` 之后、`ImGui::NewFrame()` 之前调用 `ApplySettingPointerOverride()` 追加覆盖位置；`AddMousePosEvent` 对重复值去重，追加无副作用。
- 窗口线程先写 IO 事件、再发布覆盖状态，渲染线程不会在按下/抬起事件入队前读到新的停放状态。
- HiMsg 放行任意鼠标消息（真实鼠标或笔悬停）即结束停放。

ImGui 的 `ConfigInputTrickleEventQueue`（默认开）保证同帧内 Down/Up 不丢点击。

### 3.4 HiMsg 绑定

- `IdtMain.cpp` 设置 `WindowSpec`：`bindMessages=true`；`messageOptions.filter=Inkeys::Message::Filter::None`（不入队，HiMsg 只作窗口子类过滤层）；`messageCallback=PointerInput::FilterChannelMessage`。
- `FilterChannelMessage`：
  - `PointerMode()`：`IsPointerGeneratedMouseMessage(msg, GetMessageExtraInfo())` 为真时，若为笔签名的 `WM_MOUSEMOVE` 且无按钮（`wParam` 无 `MK_LBUTTON/MK_RBUTTON/MK_MBUTTON`）且当前无活动接触 → `Discard`（继续原窗口过程，即放行给 ImGui 作为 Pen 悬停）；其余 → `Handled(0)`（不进 ImGui）。
  - `!PointerMode()`：只对 `IsTouchGeneratedMouseMessage` → `Handled(0)`；笔签名放行（Win7 笔的唯一路径）。
  - 其它消息 → `Discard`（不入队、继续原窗口过程）。
- HiMsg 子类过程在 `ImGuiWndProc` 之前执行；`ImGuiWndProc` 中原有的 `ImGui_ImplWin32_WndProcHandlerEx` 路径不变，backend 依旧按 extra info 把放行的笔悬停标成 Pen。
- 需确认 Window Service 的 HiMsg 绑定在设置窗口重建、`UnbindMessages`、销毁路径上与现有 `ClosePolicy::Forward` 一致（设置的关闭已由 `WM_SYSCOMMAND SC_CLOSE` 拦截到 `Hide()`）。

### 3.5 Win7 注册

- 在 `ImGuiWndProc` 的 `WM_CREATE`（touch-feedback 已在此调用 `ConfigureNativeTouchFeedback`）中：`!PointerMode()` 时 `RegisterTouchWindow(hwnd, 0)`。PointerMode 下不注册，避免引入派生 `WM_TOUCH`。

### 3.6 样本轨迹（R6）

- 窗口线程每次 Begin/Update/End/Cancel 追加 `TrackSample` 到固定容量环（64），由一把小 `std::mutex` 保护（只做拷贝，临界区极短）；时间优先 `POINTER_INFO::PerformanceCount / QPF`，为 0 时用 `dwTime / 1000.0`；Win7 WM_TOUCH 用 `TOUCHINPUT::dwTime`。
- 渲染线程以游标增量读取；游标落后超过容量时 `overflowed=true` 并从最旧可用处继续。
- 本子任务只提供接口和测试，消费者在 scroll-inertia。

### 3.7 诊断

- 可选：沿用 `UiDiag::Log`，组件名 `"setting"`，记录 begin/ignore/cancel 原因；跟随现有 ui_input 诊断 gate，关闭时零额外开销。

## 4. 取舍

- **为何不把五窗表改成六窗**：需要把无锁全局表改为跨线程安全，并改写已批准的“五窗独立”合同，风险与改动面远大于单窗口本地路由；设置也不需要跨窗转交。
- **为何仍要 HiMsg**：与主栏同层次职责（窗口子类层过滤派生鼠标），也满足用户“通过 HiMsg 处理”的要求；ImGui backend 只看见原生鼠标与笔悬停。
- **单接触**：ImGui 只有一个鼠标指针，多指不做手势。

## 5. 风险

| 风险 | 缓解 |
| --- | --- |
| HiMsg 子类改变设置窗口消息顺序（NCHITTEST、DWM 自绘框架） | 回调对非鼠标消息一律 `Discard` 透传；实机检查标题栏、最大化、缩放 |
| 笔悬停兼容鼠标与接触消息交错，导致 ImGui 位置跳变 | 活动接触期间丢弃所有派生鼠标；接触结束后才放行悬停 |
| `ScreenToClient` 与 ImGui backend 的坐标空间不一致（DPI） | 与 backend 一致使用物理像素客户区坐标；实机多 DPI 检查 |
| 设置渲染线程持 `settingImguiMutex` 绘制整帧时，窗口线程写 IO 等锁 | 与现有鼠标路径同样等待，不新增锁；Pointer 消息频率与鼠标同级 |
| 抽取 `DecodePointer` 改变主栏行为 | 纯搬移 + 现有 `ui_pointer_tests` 回归；对比日志字段 |

## 6. 回滚

- 提交拆分：①`DecodePointer` 抽取（可独立回滚）；②设置本地路由 + HiMsg 接线 + Win7 注册；③测试。回滚②即恢复设置旧路径。
