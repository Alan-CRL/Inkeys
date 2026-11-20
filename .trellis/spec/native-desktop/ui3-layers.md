# UI3 四层结构与宿主边界

本文件记录 `Inkeys/Inkeys/UI/UI3/` 的分层、模块组成、依赖方向和宿主接入合同（2026-11-17，任务 `11-16-ui3-layered-encapsulation` 落地）。证据等级沿用 [../index.md](../index.md)；未标注的条目均为 `【直接确认】`。

## 1. Scope / Trigger

- 新增或修改自绘 UI（按钮、面板、列表、取色器、任何用 D2D 画进分层窗口或外部上下文的内容）时必须先读本文件，判断改动属于哪一层、该放在 `UI/UI3/` 还是产品目录。
- 新增一个承载 UI3 内容的窗口或表面（「宿主」）时，按 §4 的帧目标 / 帧事务 / 输入接入合同接入，不得再手写 `BeginDraw → GetDC → UpdateLayeredWindowIndirect → ReleaseDC → EndDraw`。

## 2. 四层责任与依赖方向

四层是责任划分，不是继承层级：

| 层 | 责任 | 位置 |
| --- | --- | --- |
| L1 基础绘制与动效 | 图元（Shape/Superellipse/SVG/PNG/Word）、动画值、文字格式、材质、主题状态、内容绘制器、脏区包络 | `UI/UI3/` |
| L2 基础控件 | 按钮（视觉/状态/布局/命中）、指针与手势识别、长按进度、扩展内容合同 | `UI/UI3/` |
| L3 序列与面板 | 按钮列表与排布、面板（外框/开合/候选与已呈现范围/留白阻挡）、叠层、共享光照、连续颜色选择器 | `UI/UI3/` |
| L4 宿主与呈现 | 帧目标、帧事务、提交端、坐标映射、输入接入 | `UI/UI3/` |
| 产品层 | 主栏、More、绘制属性、翻页、形状编辑、状态指示、底栏吸附等的内容组织与业务动作 | `UI/Bar/`、`UI/PageControl/`、`UI/SelectionOverlay/`、`UI/Whiteboard/` |

依赖方向只允许：产品层 → L4 → L3 → L2 → L1 → 基础设施（`Inkeys.UI.RenderPipeline`、`Inkeys.Log` 等）。

`UI/UI3/` 内的硬约束（审查时用检索核对，任何一条命中都是缺陷）：

- 不得 `import Inkeys.UI.Bar*`，不得出现 `Inkeys::UI::Bar`；
- 不得 `#include "IdtMain.h"` / `"IdtState.h"`；
- 不得读取 `barUISet`、`floating_window`、`currentBarThemeStyle`；
- 不认识任何产品消息类型、产品配置项或产品窗口角色；需要产品信息时由产品层以参数、回调或服务对象传入（例：`UIRenderingServices`、`MainContentHandler`、`SetSurfaceInputDiagnosticSink`）。

## 3. 模块组成

命名空间统一为 `Inkeys::UI::UI3`。文件名 `UI3.<Name>.cppm`（+ `UI3.<Name>.cpp`）。工程 filter 为 `Modules\Inkeys\UI\UI3`。

独立模块（不依赖 D2D 设备运行时状态，可被 `InkeysHeadlessTests` 单独导入）：

| 模块 | 内容 |
| --- | --- |
| `Inkeys.UI.UI3.Animation` | 动画值/颜色/透明度/状态、曲线、时间线、事务、推进上下文、字号修正 |
| `Inkeys.UI.UI3.Metrics` | 通用控件的间距、圆角、描边、图标与文字槽、按压/hover/禁用/光效/时长常量 |
| `Inkeys.UI.UI3.Material` | 颜色与透明度数学、材质解析、边缘光 |
| `Inkeys.UI.UI3.DirtyRegion` | 脏区跟踪、调试包络 |
| `Inkeys.UI.UI3.VisualDirty` | 视觉脏区登记表：对象/组登记、逐帧采样比较、候选提交与淘汰、墓碑、重基准、绘制纯度检查（合同见 `rendering-and-ui.md`「UI3 自动脏区登记表合同」） |
| `Inkeys.UI.UI3.Superellipse` | 超椭圆路径 |
| `Inkeys.UI.UI3.LoadingIndicator`、`Inkeys.UI.UI3.FrameRing` | 等待/失败动效状态；沿圆角外框的进度环 |
| `Inkeys.UI.UI3.Interaction` | `InteractionTask`、`InteractionMailbox`、`ButtonPointerState`、`PointerGestureRecognizer` |
| `Inkeys.UI.UI3.Progress` | 长按进度的纯状态 |
| `Inkeys.UI.UI3.ListLayout` | 列表排布算法（Main / GroupedGrid / SingleRow） |
| `Inkeys.UI.UI3.PanelStack` | 面板范围、命中几何、叠层与输入归属裁决 |
| `Inkeys.UI.UI3.WakeSignal`、`Inkeys.UI.UI3.FramePacing` | 唤醒信号；帧节拍与动画时钟 |
| `Inkeys.UI.UI3.DragSession` | 通用拖放状态机（阈值、接触 ID、点击/放下/取消），不依赖主栏与 ImGui（合同见 `settings-component-editor.md`） |
| `Inkeys.UI.UI3.PresentationProbe` | 有限场景观察协议（`Ui3Finite*`、`Ui3Svg*`） |

分区（`Inkeys.UI.UI3:<Name>`，只作为整体使用的重型运行时）：

| 分区 | 层 | 内容 |
| --- | --- | --- |
| `:Primitive`、`:Format`、`:Theme`、`:Envelope` | L1 | 图元、DWrite 格式缓存、主题（产品绑定主题状态来源 `SetThemeStyleSource`，UI3 查询时实时读取；状态变化在查询路径上经边沿闩锁通知订阅者）、图元可视包络 |
| `:Renderer` | L1 | 内容绘制器 `UIRendering`：设备相关缓存与内容绘制；不拥有目标、不 Begin/End |
| `:Button` | L2 | `ButtonClass` 与标准按钮的配置、推进、命中、绘制入口；固定按钮入口与扩展内容合同 |
| `:VisualDirtySampler` | L2 | 标准图元、按钮、固定按钮的登记表采样器与可见性门槛（与绘制门槛一致） |
| `:List` | L3 | `ButtonListOf` 容器 |
| `:Panel` | L3 | `Panel`：外框、开合、候选/已呈现范围、留白阻挡 |
| `:Lighting` | L3 | 光照状态机、共享光照发布与订阅、指针通知 |
| `:ColorPicker` | L3 | 连续颜色选择器（主栏与形状样式栏共用） |
| `:Host` | L4 | `FrameTarget`、`FrameTransaction`、提交端、`FrameCoordinateMapping` |
| `:HostInput` | L4 | 共享输入邮箱、表面注册表、输入 owner 的所有权与泵、离屏 worker |

根模块 `Inkeys.UI.UI3`（`UI3.cppm`）`export import` 全部分区与全部独立模块，是产品层的唯一常规入口。

### 产品层的引用约定

- 产品层翻译单元在全局作用域写一行 `using namespace Inkeys::UI::UI3;`，之后不加限定地使用 UI3 名字。不得在 `namespace Inkeys::UI::Bar` 里再写 using 指令或 using 声明把 UI3 名字引进 Bar（迁移期垫片，已全部删除）。
- 需要限定名时写 `Inkeys::UI::UI3::X`（或 `UI3::X`），不得写 `Inkeys::UI::Bar::X` 去指一个 UI3 符号。
- 进入 UI3 的符号不带 `Bar` 前缀（`UiShapeClass`、`UIRendering`、`ButtonVisualParts`、`DirtyRegionTracker`）；留在产品目录的类型保留 `Bar` 前缀（`BarButtonClass`、`BarUISetClass`、`BarPageSurface`）。`BarButtonClass` 继承 `UI3::ButtonClass`，只追加产品元数据。
- 产品层为标准按钮入口补主栏缺省值时，写同名重载并转调 `Inkeys::UI::UI3::` 的入口（例：`AdvanceButtonVisual(BarButtonClass&, …)` 把 `preset == Divider` 作为 `separator` 传入）。UI3 的通用入口不读取产品 `preset`。

## 4. 宿主边界合同（L4）

### 4.1 Signatures

```cpp
// :Host
enum class FrameTargetKind { GdiCompatibleBitmap, PlainBitmap, ExternalContext };
class FrameTarget {
    FrameTarget(UIRendering& renderer, FrameTargetKind kind);
    void BindRenderer(UIRendering*);
    void SetExternalDirtyClipEnabled(bool);             // 仅 ExternalContext
    HRESULT Ensure(const DeviceEpoch&, UINT32 w, UINT32 h);          // 位图形态
    HRESULT AttachExternalContext(ID2D1DeviceContext*, unsigned long long generation, UINT32 w, UINT32 h);
    void Discard();
    void NotifyExternalEndDraw(HRESULT);                // 仅 ExternalContext
};
class FrameTransaction {
    FrameTransaction(FrameTarget&, const D2D1_RECT_F& dirtyRect);   // 构造即开始：裁剪先于清空
    void FinishDrawCommands();                                       // 退出裁剪；之后做提交准备
    FrameTransactionResult Commit(const FramePresentSink&, const FrameCommitStepObserver& = {});
    FrameTransactionResult EndWithoutPresent();                      // 延后/放弃本帧
};
struct FrameTransactionResult {
    bool commitRequested; HRESULT getDc; bool sourceDcMissing;
    bool presentAttempted, presentSucceeded; DWORD presentError;
    bool releaseDcAttempted; HRESULT releaseDc; HRESULT endDraw;
    bool PixelsCommitted() const;   // 四步全部成功
};
struct FrameTransactionLogState { void Report(const char* role, const FrameTransactionResult&) noexcept; };
struct FrameCoordinateMapping { POINT screenOrigin; double layoutScale; POINT targetOrigin; int lightingOutset; };

// :HostInput
bool RegisterInteractionSurface(HWND, uint64_t generation, SurfaceInputHandler, SurfaceInputTick,
    SurfaceInputCancel, void* context, bool nativeWindow = true, uint64_t contentInstance = 0);
void UnregisterInteractionSurface(HWND, uint64_t contentInstance = 0);
bool SubmitSurfaceInput(const SurfaceInputEvent&);
bool RegisterMainContentHandler(const MainContentHandler&);
bool SubmitMainContentInput(std::shared_ptr<void> payload, MainContentCoalesce, MainContentAdmit);
bool BeginInteractionWorker() noexcept;  void EndInteractionWorker() noexcept;
steady_clock::time_point PumpInteractionWorker(void* mainContext, steady_clock::time_point now);
void WaitInteractionInput(steady_clock::time_point deadline, uint64_t observed);
```

### 4.2 Contracts

- **绘制器与目标分离**：`UIRendering` 只持有设备相关缓存并执行内容绘制。目标位图、设备上下文、Begin/End、脏区裁剪归 `FrameTarget`；绘制器经 `NotifyFrameTargetReady/Discarded`、`NotifyFrameDirtyClipPushed/Popped`、`NotifyFrameBegunWithoutClip`、`NotifyFrameEnded` 得知设备与帧状态。帧目标重建、丢弃、换上下文都先通告「已丢弃」（绘制器清设备缓存并触发 `resetDeviceCaches` 服务），再通告「就绪」。
- **三种形态**：`GdiCompatibleBitmap`（分层窗口，提交端拿到 `GetDC(COPY)` 的源 DC）；`PlainBitmap`（事务先 EndDraw，提交端拿到已结算的目标位图，用于读回类提交端）；`ExternalContext`（上下文与目标归宿主，事务不 BeginDraw/Clear/EndDraw，宿主 EndDraw 后必须调 `NotifyExternalEndDraw`）。
- **事务顺序固定**：Begin → 推入脏区裁剪 → Clear → 绘制 → `FinishDrawCommands`（退出全部裁剪/图层）→ 宿主的提交准备 → GetDC → 提交端 → `ReleaseDC`（空修改矩形）→ EndDraw。每帧恰好一次 Begin/End；析构时未结束的事务自动 `EndWithoutPresent`。
- **异常收尾**：提交端或分步观察回调抛出异常时，`Commit` 仍先归还 DC、再 EndDraw（各一次），异常照常传播；下一帧可以正常开始。
- **成功判定归宿主**：事务只如实返回四步结果。只有 `PixelsCommitted()` 且宿主自己的后续步骤（窗口几何交接、回执核对等）成功，宿主才可以消费脏区、发布命中与已呈现范围。失败时保留旧的成功快照并保留全脏重试。
- **视口裁剪**：内容绘制不保证自己限制在视口内（柔和阴影会画到视口之外）。位图形态由帧目标的脏区裁剪保证；`ExternalContext` 若 `SetExternalDirtyClipEnabled(false)`，宿主必须自己裁到视口。
- **内容绘制入口不认识宿主**：一段内容（例：主栏的 `DrawMainBarContent`）只依赖传入的绘制器、设备上下文、`FrameCoordinateMapping` 和本帧状态，不读窗口句柄、不读某个特定帧目标的成员、不依赖 GDI 兼容标志。
- **输入**：一个有界邮箱、一个输入 owner。表面事件与宿主主内容的消息共用同一 FIFO，保序、每批最多 32 条、溢出即取消全部已准入的捕获。表面注册键是「HWND + 内容实例」（今天全部调用方的内容实例为 0）；同键的新注册使旧注册失效。主内容消息在 UI3 里是不透明载荷（`shared_ptr<void>`），合并与准入由宿主回调在邮箱锁内判定，只在相邻两条都是主内容消息时才询问合并。生产输入循环留在宿主线程，所有权、等待与泵由 `:HostInput` 提供。
- **面板输入归属**：面板登记进 `PanelStack`；点落在谁的已呈现范围内、谁在前，由叠层统一裁决。面板的已呈现范围与成员命中快照在同一把锁内一起提交和读取。

### 4.3 Validation & Error Matrix

| 条件 | 结果 |
| --- | --- |
| `Ensure` 用于 `ExternalContext` / `AttachExternalContext` 用于位图形态 | `E_UNEXPECTED` |
| `Ensure` 宽或高为 0 | `E_INVALIDARG` |
| 新目标资源任一步创建失败 | 返回该 HRESULT，旧目标保持可用；每设备代次每个 API 记一次 Error |
| `Commit` 时事务未激活（没有上下文或已结束） | 返回缺省结果：`commitRequested=true`，`PixelsCommitted()==false` |
| `GetDC` 成功但没有给出 DC | `sourceDcMissing=true`，`getDc=E_POINTER`，不调用提交端，仍 EndDraw |
| 提交端为空（`ExternalContext` 宿主自行呈现） | `presentAttempted=false`、`presentSucceeded=true` |
| `EndDraw` 返回 `D2DERR_RECREATE_TARGET` | 宿主丢弃本地帧目标（`Discard`）；共享设备丢失另经 RenderPipeline 上报，两者分开处理 |
| 表面事件的注册代次与当前注册不符、或没有活动注册 | 丢弃该事件；诊断开启时经宿主登记的诊断出口输出 |
| 输入邮箱溢出 | 清空在途输入、建立取消屏障、先取消主内容捕获再取消表面捕获；记一次 Warn |
| 已有主内容处理器或缺少必需回调时 `RegisterMainContentHandler` | 返回 `false` |

### 4.4 Good / Base / Bad Cases

- Good：新宿主持有自己的 `FrameTarget` 与 `UIRendering`，每帧 `Ensure` → 构造 `FrameTransaction` → 调内容绘制入口 → `Commit(提交端)` → `FrameTransactionLogState::Report(角色, 结果)` → 仅在 `PixelsCommitted()` 且自身后续步骤成功后发布命中。
- Base：延后或放弃本帧时调 `EndWithoutPresent()`；EndDraw 成功时它不算失败，日志闩锁也不把它当成恢复；EndDraw 失败仍按失败记录。
- Bad：在事务之外对同一上下文 `BeginDraw`；提交失败后仍推进已呈现范围；把绘制器从一个帧目标直接借给另一个帧目标使用而不经 `BindRenderer`。

### 4.5 Tests Required

| 入口 | 断言点 |
| --- | --- |
| `Inkeys.exe --ui3-host-offscreen-test`（`Bar.Host.Test.cpp`，不创建窗口） | 同一帧主栏内容画进普通位图目标、外部上下文（两种裁剪方式），与 GDI 兼容路径逐字节相同；视口外全透明；连续多帧不漂移；丢弃目标后绘制器清缓存并能重建；`transaction-unwind.*`：四种抛出点之后下一帧四步全部成功；`log.*`：UI3 生命周期记录落盘 |
| `Inkeys.exe --ui3-golden-dump <目录>` | 35 个场景的 BGRA 与基线逐字节相同（纯迁移类改动必须保持） |
| `Inkeys.exe --ui3-input-worker-test` | 生产邮箱、派发、续接、截止时间、取消 |
| `InkeysHeadlessTests.exe --no-window` | `Interaction`、`Progress`、`ListLayout`、`PanelStack`、`FramePacing` 等独立模块的单元测试 |

所有 `Inkeys.exe --…-test` 入口都在产品的日志启动点之前返回。需要核对日志落盘的入口自己开测试专用日志会话：`--ui3-host-offscreen-test` 把 `Inkeys::Log::Start` 的根目录指向隔离目录 `Build/ui3-host/runtime-config/`，结束前 `FlushAndStop` 并断言 `[UI3]` 的 `frame target recreated / attached / discarded` 各至少出现一次（`log.session`、`log.ui3-frame-target-lifecycle`）。日志入口非阻塞、可能丢弃，断言只要求「至少一次」，不核对条数。其余入口不启动日志，其日志调用为空操作。

### 4.6 Wrong vs Correct

#### Wrong

```cpp
// 宿主自己管理 Begin/End 与 GDI 互操作，失败分支各写一遍
context->BeginDraw();
context->PushAxisAlignedClip(dirty, D2D1_ANTIALIAS_MODE_ALIASED);
context->Clear(&transparent);
DrawContent(renderer, context);
context->PopAxisAlignedClip();
HDC dc = nullptr; interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc);
const BOOL ok = UpdateLayeredWindowIndirect(hwnd, &info);   // 这里抛出或提前 return 就漏 ReleaseDC/EndDraw
interop->ReleaseDC(nullptr);
context->EndDraw();
PublishHitTargets();                                         // 没有核对四步结果
```

#### Correct

```cpp
if (FAILED(target.Ensure(epoch, width, height))) return Retry();
FrameTransaction transaction(target, dirty);
DrawContent(renderer, transaction.DeviceContext(), mapping);
transaction.FinishDrawCommands();
// 提交准备（租约、几何核对……）放在这里；被否决时 transaction.EndWithoutPresent()
const auto result = transaction.Commit([&](const FramePresentSource& source) -> FramePresentOutcome
{
    info.hdcSrc = source.sourceDc;
    const BOOL ok = UpdateLayeredWindowIndirect(hwnd, &info);
    return { ok != FALSE, ok ? ERROR_SUCCESS : GetLastError() };
});
presentLog.Report("my-host", result);
if (result.PixelsCommitted()) PublishHitTargets();
else RetainDamageForRetry();
```

## 5. 有意保留在产品层的专有绘制

以下内容没有标准按钮对应物，通过固定按钮的扩展内容合同（`FixedButtonDrawOptions::underlay/content`）接入，不搬进 UI3：线宽斜向胶囊预览、粗细连续预览、滑块与精调拨盘；翻页主题色与网格/点阵预览；橡皮示例与自动粗细分隔线/箭头；状态指示器及其超椭圆进度环、主 Logo；多步形状提示的步骤圆环与文字；Geometry 滚动条。

带扩展回调但没有声明内容范围（`contentExtent`）的按钮不做整体剔除，按保守方式绘制。

## 6. UI3 日志规则

- `UI/UI3/` 内一律 `Inkeys::Log::Module::UI3`；在全局模块片段以相对路径包含 `Log/Log.h`。
- UI3 的代码可能运行在渲染线程、输入 owner、窗口过程：**只用 `TryFormat` / `TryWrite`**，不用可能等锁的 `Info/Warn/Error`。
- 不做逐帧日志，只在边沿或经闩锁后记录：

| 事件 | 级别 | 频率 |
| --- | --- | --- |
| 帧目标重建 / 附着外部上下文 / 丢弃（形态、像素尺寸、设备代次） | Info | 每次发生 |
| 设备相关资源创建失败（API 名、HRESULT） | Error | 每设备代次每类资源一次 |
| 精确遮罩失败回退 | Warn | 每设备代次一次 |
| 帧事务失败（四步各自的结果） | Error | 首次失败记录，持续失败不重复 |
| 帧事务从失败恢复（连续失败次数） | Info | 每次恢复一次 |
| 输入表面注册/注销、主内容处理器注册/注销、光照订阅者登记/注销 | Info | 每次发生 |
| 输入邮箱溢出 | Warn | 每次发生 |
| 输入回调或离屏输入 owner 抛出异常 | Error | 每次发生 |
| 主题状态发布、面板开合、颜色选择器打开/提交/取消 | Debug | 值或状态变化时 |
| 长按进度资源失败后停止尝试 | Warn | 每次手势一次 |
| SVG / PNG 图元生成或上传位图失败（阶段、尺寸、内容长度、HRESULT） | Error | 每个图元每设备代次一次 |
| SVG / PNG 资源提取失败（资源类型与名称）、SVG 尺寸解析失败、PNG 解码失败 | Error | 资源提取每资源每进程一次；其余在设置内容时每次发生 |

宿主绑定/解绑帧目标的日志由产品层用自己的 Module 记录（`role=…`）。日志不包含用户文档内容。`RenderPipeline` 的 `[UI3Diag]` 聚合机制见 [ui3-render-diagnostics.md](ui3-render-diagnostics.md)。
