# 设计：设置页面滑动封装与触摸/笔惯性

## 1. 现状

- 拖动滚动实现集中在 `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp` 约 478–760 行：
  - `DragEligible(window, state, submitted)`：可滚动、非 Popup 关闭、导航/浮层屏蔽规则。
  - `CaptureDrag(owner)`：把 ActiveId 设为 `##scrolldraggingoverlay`，记录 `##drag-scroll-position`。
  - `BeginDragScrolling(state)`（帧首，`Setting.cpp:2568`）：内容型按钮越阈值时夺取为滚动；按 `io.MouseDelta.y` 推进位置，写 `##drag-scroll-frame` 待提交。
  - `ApplyDragScrolling(state)`（帧尾，`Setting.cpp:5222`）：按下时选择宿主 `CaptureDrag`；松开时 `ClearActiveID`。
  - `ScrollbarStyleScope` 构造时消费本帧待提交位置（`SetScrollY`）或标记 `rebase`；`BeginScrollChild(resetToTop)` 清理这些键。
  - `AllowContentDrag()`：登记内容型按钮。
- 当前对鼠标/触摸/笔一视同仁，无惯性；`MouseDelta` 是帧级增量，没有输入时间戳。

## 2. 模块划分

新增 `Inkeys/Inkeys/UI/Setting/Setting.Scroll.h/.cpp`：

```cpp
namespace Inkeys::UI::Setting::Scroll
{
    // ---- 纯逻辑（无 ImGui/Win32），供无窗口测试 ----
    struct InertiaParams
    {
        float decayRate = 0.95F;          // WinUI3 PositionInertiaDecayRate：每秒速度保留 (1-0.95)
        float minFlingDipPerSecond = 50.0F;
        float maxFlingDipPerSecond = 8000.0F;
        float stopVelocityDipPerSecond = 15.0F;
        float velocityWindowSeconds = 0.100F;  // 速度估计窗口
        float stillReleaseSeconds = 0.080F;    // 最后一次有效移动距抬起超过该值 → 视为停住
        float overpanDamping = 0.35F;          // 越界位移阻尼
        float maxOverpanDip = 80.0F;
        float bounceVelocityToDip = 0.02F;     // 惯性撞边界：速度 → 位移
    };
    class VelocityEstimator   // 最近窗口内样本的最小二乘一阶拟合，含停住判定与上限
    { public: void Reset(); void Add(double t, float y); float Release(double tUp, const InertiaParams&, float dipScale) const; };
    class InertiaCurve        // 解析指数衰减：y(t)=y0+v0*(1-k^t)/ln(1/k)，v(t)=v0*k^t，k=1-decayRate
    { public: void Start(float y0, float v0, double t0); bool Active() const; float Position(double t) const; float Velocity(double t) const; void Stop(); };
    struct OverpanResult { float scroll; float overpan; };
    OverpanResult ClampWithOverpan(float desired, float maxScroll, const InertiaParams&, float dipScale) noexcept;

    // ---- ImGui 宿主（原 Controls.cpp 搬入） ----
    void BeginFrame(const Design::NavigationState&);   // 原 BeginDragScrolling 主体 + 惯性推进
    void EndFrame(const Design::NavigationState&);     // 原 ApplyDragScrolling 主体 + 释放判定
    void AllowContentDrag();
    // 供 ScrollbarStyleScope / BeginScrollChild 使用的键协议小接口
    void ConsumePendingScroll(ImGuiWindow*);            // 本帧待提交位置 → SetScrollY / rebase
    void ResetWindow(ImGuiWindow*);                      // resetToTop 清理 + 停止该窗口惯性
    bool Animating();                                    // 惯性或越界回弹进行中，需要持续帧
}
```

- `Design::BeginDragScrolling/ApplyDragScrolling/AllowContentDrag` 保留为一行转发，调用点不改（R1.2）。
- `DragEligible`、`WindowStateId`、`CaptureDrag` 随搬移进入 `Setting.Scroll.cpp` 匿名命名空间；`WindowStateId` 若 Controls 仍需要则在头文件内联共享。

## 3. 设备策略

- `EndFrame` 中新按下选择宿主前检查 `io.MouseSource`：`Mouse` 直接返回，不 `CaptureDrag`。
- `BeginFrame` 中“内容型按钮越阈值夺取为滚动”同样只对非 Mouse 生效。
- 设备来源以 pointer-route 导入后的 `io.MouseSource` 为准；Win7 笔经兼容鼠标进入时 backend 也标为 Pen。

## 4. 速度与惯性

### 4.1 样本来源

- 首选 pointer-route 的 `PointerInput::ReadTrack`：每帧增量读入当前接触（代次匹配）的 `(timeSeconds, y)`，输入时间戳精度高，不受帧率影响。
- 无轨迹时（Win7 笔走兼容鼠标、或轨迹溢出）回退为每帧 `(帧时间, io.MousePos.y)`。
- 坐标为客户区物理像素；速度阈值以 DIP 定义，乘 `FontScaleDpi`（`Pixels()`）换算。

### 4.2 释放速度（R3.1）

- 取抬起前 `velocityWindowSeconds` 内样本做一阶最小二乘（与 Android `VelocityTracker`、Chromium 的短窗口拟合同类）；样本少于 3 个或时间跨度 < 1 ms 时回退为首末差分。
- 最后一次有效位移距抬起超过 `stillReleaseSeconds` → 速度 0（停住再抬起无惯性）。
- `|v| < minFling` → 不启动；`|v|` 截断到 `maxFling`。
- 方向：手指向上移动 → 内容向下滚动，即 `scrollVelocity = -fingerVelocity`。

### 4.3 衰减曲线（R3.2）

- WinUI3 `InteractionTracker.PositionInertiaDecayRate` 默认 0.95：速度每秒衰减到 5%，`v(t)=v0·0.05^t`，总滑行距离 `v0/ln 20 ≈ 0.334·v0`（v0=3000 DIP/s 约滑 1000 DIP）。
- 采用解析式按真实时间计算（QPC），帧间隔波动不影响轨迹；`|v(t)| < stopVelocity` 结束。
- 是否加入“惯性中同向再甩速度叠加”（WinUI/Chromium 都有）：研究步骤实测后决定，默认不加；若加，规则为“惯性中按下后 300 ms 内同向释放，新速度 + 当前残余速度，仍受上限”。

### 4.4 推进与写回

- `BeginFrame`：若某宿主窗口惯性活动，计算 `desired = curve.Position(now)`，经 `ClampWithOverpan` 后写入现有 `##drag-scroll-position` / `##drag-scroll-frame` 协议，由 `ConsumePendingScroll` 在该宿主 Begin 前 `SetScrollY`——复用现有“单宿主、Begin 前投递”的写回路径，不后移光标。
- 惯性宿主身份以 `ImGuiWindow::ID` 记录；宿主本帧未提交、`ResetWindow`、页面切换（导航状态变化）、设置会话失效时停止。

### 4.5 停止条件（R3.3）

| 事件 | 处理 |
| --- | --- |
| 惯性中任何来源按下 | 停止；若当时速度 > stopVelocity，`CaptureDrag` 到滚动覆盖层，这次按下不激活控件（WinUI “点按停住”） |
| 滚轮/键盘导致 `ScrollTarget` 被他人设置 | 停止并 rebase |
| 滚动条 ActiveId | 停止 |
| pointer-route 接触 Cancel | 不启动惯性，清越界 |

### 4.6 帧调度（R3.4）

- 设置渲染为按需模式；`Scroll::Animating()` 为真时每帧末 `RenderPipeline::Request(Client::Settings)`，结束后停止请求。按现有 `FrameResult` 机制接入，不新增计时器线程。

## 5. 边界与窗口位移（R4）

### 5.1 系统 API

文件资源管理器的越界效果来自 uxtheme 的 panning feedback：`BeginPanningFeedback(hwnd)`、`UpdatePanningFeedback(hwnd, totalOverpanX, totalOverpanY, fInInertia)`、`EndPanningFeedback(hwnd, fAnimateBack)`（Win7 起提供，`uxtheme.dll`，动态获取）。窗口整体平移，`EndPanningFeedback(TRUE)` 由系统动画回位。

### 5.2 数据流

- 渲染线程（ImGui 帧）计算越界量：
  - 拖动中：`overpan = clamp(excess × overpanDamping, ±maxOverpanDip)`，`excess` 为手指位移超出边界的部分（超出后 ImGui 滚动硬停）。
  - 惯性撞边界：`overpan = clamp(|vHit| × bounceVelocityToDip, maxOverpanDip)`，一次 Update(fInInertia=TRUE) 后立即 End(TRUE)，惯性结束。
  - 松手/取消：End(TRUE)。
- 线程归属：panning feedback 会移动 HWND，必须在设置窗口线程调用。渲染线程把“最新越界量 + 阶段（Begin/Update/End）”写入原子邮箱，`PostMessage` 私有消息唤醒窗口线程；`ImGuiWndProc` 处理该消息时读取最新值调用 API（只保留最新值，合并多次 Update）。

### 5.3 副作用隔离（R4.4，需研究确认）

- 研究步骤先实测 `UpdatePanningFeedback` 的实现方式（是否经 `SetWindowPos` 产生 `WM_WINDOWPOSCHANGED/WM_MOVE`、是否进入 move-size 状态）。
- 若产生窗口移动消息：在反馈活动期间置标记，设置的窗口位置保存、`InteractiveWindowOperation`、外置缩放边框（`settingResizeBorder`）跟随逻辑忽略这些移动；反馈期间外置缩放边框隐藏或保持原位，End 后按真实位置恢复。
- 若 API 不可用或研究发现不可接受副作用：退化为仅硬停（R4.5），不另造窗口移动。

## 6. 研究步骤（实施前）

写入本任务 `research/`：
1. WinUI3 `InteractionTracker` / `ScrollViewer` 惯性衰减默认值与速度阈值、Chromium/Android fling 参数（最小/最大速度、估计窗口），给出常量取值依据。
2. `Begin/Update/EndPanningFeedback` 在本机 Win11 与 Win7 的行为：线程要求、是否发窗口移动消息、最大化窗口表现、回弹动画时长。
3. 现有 `setting_design_tests` 中拖动滚动/导航相关用例，确认搬移后需同步的断言（见 memory：改侧栏结构会打破导航用例，此处不改结构，仅确认）。

## 7. 风险

| 风险 | 缓解 |
| --- | --- |
| 搬移改变现有拖动滚动细节（rebase、单宿主投递） | 纯搬移提交单独审查；先搬移再加功能 |
| 惯性写回与原生滚轮/`SetScrollY`（导航滚入选中项、贴底意图）冲突 | 他人设置 `ScrollTarget` 即停止惯性并 rebase；贴底意图在用户滚动时本就清除 |
| panning feedback 触发窗口位置保存或缩放边框错位 | 研究确认 + 活动期间屏蔽；不可控则退化为硬停 |
| 惯性中点按吞掉点击被误认为“点不动” | 只在速度高于 stopVelocity 时吞；低速直接停止并正常点击 |

## 8. 回滚

- 提交拆分：①搬移封装（行为不变）；②鼠标屏蔽 + 惯性 + 测试；③边界窗口位移。③可单独回滚为仅硬停；②回滚恢复无惯性但保留封装。
