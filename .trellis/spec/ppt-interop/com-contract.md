# COM Contract and Lifecycle

本页以 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】` 区分源码事实、修改约束和未验证风险。

## 接口事实

`【直接确认】` `PptCOM/PptCOM.cs` 声明 `[ComVisible(true)]` 的 `IPptCOMServer`，接口类型为 `InterfaceIsIUnknown`，由 `PptCOMServer` 实现。按源码声明顺序，方法为：

1. `Initialization(int* TotalPage, int* CurrentPage, int* OffSignal)`；
2. `CheckCOM()`；
3. `PptComService()`；
4. `SlideNameIndex()`；
5. `GetPptHwnd()`；
6. `GetSlideShowAnnotationTool()`；
7. `ExitSlideShowAnnotationTool()`；
8. `NextSlideShow(bool check)`；
9. `PreviousSlideShow()`；
10. `EndSlideShow()`；
11. `ViewSlideShow()`；
12. `ActivateSildeShowWindow()`；
13. `SetConsoleOutputEnabled(bool enabled)`；
14. `GetPresentationDescriptor()`。

`【历史/兼容】` `ActivateSildeShowWindow` 的拼写已进入 COM 接口和 native 调用点；它不是新命名范例，也不能在无兼容计划时直接改名。

`【直接确认】` `PptCOM.csproj` 构建后调用 `TlbExp.exe` 生成 `PptCOM.tlb`；`Inkeys/IdtPlug-in.cpp` 通过 `#import "PptCOM.tlb"` 使用 `IPptCOMServerPtr`。因此接口变更会跨越 C#、TLB、native 调用和部署产物。

## Scenario: 纯值 Presentation descriptor 与 COM 所有权

### 1. Scope / Trigger

修改 PPT 文稿/页身份、SlideID 拓扑、PowerPoint/WPS dynamic 访问、descriptor 缓存、binding 恢复或 native 解析时必须应用本合同。目标是在不向 native 暴露 RCW 的前提下，为 Draw3 提供文稿、当前页和完整页拓扑的同一次快照。

### 2. Signatures

~~~csharp
// IPptCOMServer 的最后一个方法；既有 GUID 与前 13 个方法顺序不变。
string GetPresentationDescriptor();

PresentationDescriptorValue PresentationDescriptorReader.Read(
    object application, object presentation, object slideShowWindow,
    long bindingRevision);
~~~

JSON schema version 1 恰含 `schemaVersion/provider/status/fullName/presentationName/applicationProcessId/slideShowHwnd/currentPage/totalPage/currentSlideId/slideIds/bindingRevision` 12 个字段；只允许 string、数值、nullable int 和 `int[]` 进入缓存。

### 3. Contracts

- `GetPresentationDescriptor()` 把短锁 clone 与锁外 `JavaScriptSerializer` 都放在异常边界内；失败使用手写 Unavailable JSON 兜底。它不访问 Office、不返回 dynamic/RCW，也不得持锁调 COM。
- 只有 `PptComService` binding/monitor owner 读取 COM 图并刷新缓存。event 只置 refresh-pending；owner 在绑定、页/总数变化和低频复核时刷新。`TransientBusy` 可保留同 binding revision 的上一份 stable/fallback 纯值快照。
- PowerPoint PIA 绑定对象、WPS 和损坏 IDispatch 统一经 `InvokeMember` late-bound accessor 读取，不做会重复 acquisition 的 typed→dynamic 二次尝试。reflection 必须解包内层异常供 busy HRESULT 分类。
- application、active presentation 和 slide-show window 是长期借用字段，reader 不释放。`View`、`View.Slide`、`Slides` 及每个 `Slides.Item(i)` 是本次获取的 temporary；必须在 `finally` 按子到父每次恰好 `ReleaseComObject` 一次，不得 `foreach`、链式 COM 属性或 temporary `FinalReleaseComObject`。
- 当前页的 `SlideIndex` 和 `SlideID` 必须来自同一个 `View.Slide` acquisition。完整 topology 必须用 `for (1..Count) + Slides.Item(i)` 读取，校验当前 SlideIndex 顺序、正 SlideID、唯一性及当前 ID 对应；descriptor 中的数组顺序只表示本次 COM 的当前页顺序，不能把顺序变化当成 SlideID 身份变化。任一读取失败不得返回部分 `slideIds`。
- `FullCleanup` 必须先推进 `bindingRevision` 并发布 Unavailable 纯值缓存，再沿既有 event -> window -> presentation -> application 路径解绑/释放。WPS 所需的 final release/GC 只保留在原 cleanup 所有权边界，不下放给 descriptor reader。
- managed 在缓存/数组分配前限制单个 identity 的 UTF-8 长度不超过 32 KiB、页数不超过 10,000；native 再对 JSON 设置 1 MiB UTF-8 payload、32 KiB identity 和 10,000 页上限，并严格校验 schema/status/数值/唯一 SlideID。
- 有效 shared 页已变而 descriptor 仍是上一页，或 descriptor 为 `TransientBusy` 时，只有 descriptor bindingRevision 与 Bridge 当前 target 相同才可暂存旧画布且不发 ready；新 binding 首次 busy/unavailable 必须隔离，不能把 A 画布留给 B。

### 4. Validation & Error Matrix

| 条件 | descriptor/释放行为 |
|---|---|
| application/presentation/window 任一为 null | `Unavailable`；不释放借用 root |
| `View.Slide`/SlideIndex 失败 | busy HRESULT -> `TransientBusy`；其他 -> `Unavailable`；已获取 current slide/View 均释放 |
| 当前 SlideID 属性不存在 | 保留同次 SlideIndex，返回 `PageIndexFallback` |
| Slides/Item/SlideID 普通失败、ID 重复/非法 | 若页码仍自洽则 `PageIndexFallback`，`slideIds=[]`；所有 temporary 恰好释放 |
| 任一阶段 Office busy（`0x8001010A/0x800AC472/0x80010001`） | `TransientBusy`，不返回部分 topology，不破坏同 binding 的已缓存 stable 快照 |
| cleanup 与 refresh 交错 | 候选 revision 不匹配时不发布；旧 binding 快照不得复活 |
| JSON 序列化失败 | 返回同 revision 的 `Unavailable` JSON，不向 native 抛出托管异常 |

### 5. Good / Base / Bad Cases

- Good：PowerPoint/WPS 均由 owner 读取完整页拓扑，getter 并发只复制纯值；反复扫描后 temporary acquisition/release 计数一致，Office 可正常退出。
- Base：SlideID 不可用时返回 PageIndexFallback，native 用 process-local binding 隔离，既有三个 unsafe `int*` 页码槽仍原样工作。
- Bad：在 getter 中现场遍历 Office，缓存 `dynamic Slide`，使用 `presentation.Slides[i].SlideID` 链式访问，或对借用 root 执行 FinalRelease；这些都可使 Office 进程残留或提前失效。

### 6. Tests Required

- managed fake COM ledger 覆盖 PowerPoint/WPS 值差异、重复扫描、每个异常点、busy HRESULT、缺失 SlideID、重复/非法 topology、borrowed root 零释放、temporary 恰好一次且子先父后。
- native parser/身份测试覆盖 Unicode 路径、provider-independent stable identity、含 binding revision 的 process-local identity、payload/string/page 上限、重复 ID、同 binding stale/busy 保留及新 binding busy 隔离。
- 完整 `InkeysRepo.sln Debug|x64` 构建必须重新生成 DLL/TLB，并核对 interface GUID 不变、新 getter 只在末尾追加。真实 PowerPoint/WPS 放映、busy/损坏、切文稿和进程退出必须另行设备验收。

### 7. Wrong vs Correct

~~~csharp
// Wrong：链式 temporary 无法精确释放，getter 也被 Office 阻塞。
return pptActivePresentation.Slides[index].SlideID;

// Correct：owner 显式获取 temporary，finally 从子到父释放；getter 只返回 clone。
object slides = null;
object slide = null;
try {
    slides = accessor.GetProperty(presentation, "Slides");
    slide = accessor.GetItem(slides, index);
    return GetInt32(slide, "SlideID");
} finally {
    accessor.Release(slide);
    accessor.Release(slides);
}
~~~

## `CheckCOM` 的真实作用

`【直接确认】` `PptCOMServer.CheckCOM()` 当前返回常量字符串 `20260627a`。`IdtPlug-in.cpp::CheckPptCom` 会调用并保存返回值，但本轮静态搜索没有找到把它与期望版本比较、拒绝服务或选择兼容分支的代码。

因此：

- `【直接确认】` 当前存在“读取组件标识”的机制；
- `【待确认】` 何时更新常量、是否应强制匹配、如何兼容旧 DLL/TLB，尚无可追溯政策；
- 不能把“接口变更必须更新 CheckCOM”写成当前程序已执行的安全门。若维护者希望它成为版本门，需要单独设计 native 比较与失败处理。

## unsafe 状态共享 ABI

`【直接确认】` `IdtPlug-in.cpp::CheckPptCom` 把以下地址传给 managed `Initialization`：

- `reinterpret_cast<long*>(&PptInfoState.TotalPage)`；
- `reinterpret_cast<long*>(&PptInfoState.CurrentPage)`；
- `GetOffSignalInteropPointer()` 返回的独立 `LONG` 槽地址。

`PptCOMServer.Initialization` 把它们保存为 C# `int*` 字段；`PptComService` 及其事件/轮询路径之后持续写页数/页码并读取退出信号。Windows ABI 下这里依赖 native `long`/`LONG` 与 C# `int` 均为 32 位，以及这些全局对象地址在服务期内稳定。

`【直接确认】` 退出信号使用地址稳定的独立 `LONG` 槽：native 的唯一写入口 `SetOffSignal` 先以 `InterlockedExchange` 发布该槽，再以 release store 更新 C++ `offSignal`；managed 侧用 .NET Framework 4.0 的 `Thread.VolatileRead(ref *offSignal)` 读取。不得把 `IdtAtomic<int>` 包装对象强转为 ABI 指针，也不得把 managed 读取退回普通解引用。

`【直接确认】` `PptInfoStateStruct` 的 `TotalPage/CurrentPage` 仍是普通 `int`。`pptComSlotSm` 及 `Get/Set/ResetPptComSnapshot` 保护的是 `IPptCOMServerPtr` 服务槽/快照；它们不同时保护 managed 指针所写的两个页码整数。

`【待确认；风险观察，不是本轮修复范围】` 需要维护者确认跨 managed/native 线程读写 `TotalPage/CurrentPage` 所依赖的同步和可见性契约。静态扫描不足以把服务指针 mutex 描述成页码字段的同步保证。

涉及该 ABI 的 `【合理推断】` 检查项：

- 不传局部变量、可移动容器元素或宽度不同的整数地址；
- 退出槽只通过 `SetOffSignal` 写入，并在 managed 侧用 `Thread.VolatileRead` 读取；
- 服务释放/退出后不再由 managed 侧解引用；
- 改变字段类型或位置时同时检查 C# unsafe 声明、native cast 和所有读写线程；
- 若补同步，必须设计跨 CLR/native 边界都成立的协议，不能只在一侧加锁后宣称完成。

## native 服务生命周期

`【直接确认】` 主要流程可追溯到：

1. `IdtMain.cpp::wWinMain` 调用 `CoInitializeEx`，准备/激活 PptCOM manifest context 并 `LoadLibrary` DLL；
2. `IdtPlug-in.cpp::CheckPptCom` 创建服务，调用 `CheckCOM` 与 `Initialization`，再写入受保护的服务槽；
3. `GetPptState` 取得快照并运行 `PptComService`；
4. `PPTLinkageMain` 通过 `Inkeys.UI.Ppt` 租用 `Inkeys.UI.PageControl` 的四个渲染客户端，并启动 `GetPptState`、`PptInfo` 与 PPT 业务队列；
5. UI3 回调只向业务队列提交请求，业务线程再通过 `NextPptSlides`、`PreviousPptSlides` 等取得服务快照并执行 COM 或模态确认；
6. 退出路径通过 `offSignal`、服务 reset、COM/activation-context/module 清理收束。

native 调用处可见 `_com_error` 处理。`【合理推断】` 新调用应每次通过既有快照入口取得服务并处理空值/COM 失败，而不是缓存未经同步的 raw interface pointer。

`【待确认；风险观察】` PPT 相关线程中可见 detached thread，主退出也有状态等待。本轮未运行快速退出/Office 忙碌场景，不能把其生命周期称为已验证安全或已确认缺陷。

## managed 绑定、WPS 与恢复

`【直接确认】` `PptCOMServer` 用 `dynamic` 保存 application、active presentation、slide-show window。`PptCOM.cs` 包含：

- PowerPoint 与 WPS 的 ROT/COM/进程绑定分支；
- 事件与轮询更新；
- `IsBusyComException`、`HandleBusyException` 等 Office-busy 分类，重试窗口约 10 秒；
- `FullCleanup`、`SafeRelease` 及循环对象释放；
- WPS 路径所需的额外 FinalRelease/GC 处理和相应源码注释。

`【历史/兼容】` 这些分支证明代码考虑过多个 Office/WPS 版本，不证明每个组合仍在发布测试范围。

`【待确认】` 需要维护者提供实际支持的 PowerPoint/WPS 版本、Office 位数/安装方式、Windows/架构组合，以及事件和轮询各自的正式回退条件。

`【合理推断】` 修改绑定/恢复时按源码现有分类分别验证：应用暂忙、放映结束、文档切换、COM 对象失效、应用退出和重新绑定；事件绑定/解绑及 slide-show window → presentation → application 的清理依赖不能只改一半。

## HWND 与窗口控制

`【直接确认】` `PptCOM.cs::GetPptHwnd` 会在 slide-show window 与 Win32 查找路径之间选择，并使用窗口/进程信息；native `IdtPlug-in.cpp` 用返回句柄定位、激活和联动 PPT 控件。

`【合理推断】` 修改窗口发现时应覆盖 PowerPoint/WPS 的类名与进程、多个演示文稿/放映窗口、不可见或已销毁窗口、句柄复用和前台窗口不等于实际放映窗口的情况。是否支持所有 Office 位数由测试矩阵决定，不能仅凭 HWND API 外推。

## 页码、缓冲状态与 `PptImg`

`【直接确认】` 当前数据不是一步直达：

1. managed `PptComService` 通过 unsafe 指针写 `PptInfoState.TotalPage/CurrentPage`；
2. `IdtPlug-in.cpp::PptInfo` 最多每 `50ms` 复核一次 COM 事实；有效页码以零基绝对页请求发布给运行中的 Draw3 Host。发布入口对相同页面幂等，Host 未运行时返回失败，Host 重启清空 bridge 后下一轮会重新发布，不得把未被运行实例接受的请求记作已发送；
3. Draw3 在绘制线程完成页面切换，并由 document/command observer 发布真实 `currentPageIndex/pageCount`。任一值变化都会推进 `runtimeRevision` 并唤醒 `WaitForProductRuntimeRevision`；
4. `PptInfo` 被 native revision 唤醒或达到 `50ms` 上限后读取一致 runtime snapshot，只有 Draw3 已到达 COM 对应绝对页时才推进 `PptInfoStateBuffer`；
5. `PptInfo` 当前只把 ready buffer 传给 `Inkeys.UI.Ppt::PublishPageState`；结束页已在 buffer 中归一为 `-1/-1`，所以 PageControl 无法观察 managed 保留的 `-1/有效总页数`。

`【实施合同】` 修复后的第 5 步必须由 `Inkeys.UI.Ppt::ResolvePageStateForPublication` 把 COM 事实与 ready buffer 解析为 UI 三态：有效页使用 ready buffer，结束页投影为 `-1/有效总页数`，未放映/已结束为 `-1/-1`；`PublishPageState` 再发布给 `Inkeys.UI.PageControl` 四窗。Page 数字使用共享即时文字事务；主栏 A2 EndShow 不参与页码计算。PageControl 的结束页 Next 与 A2 必须共享 `Bar::RequestEndShow` 单请求 dispatcher，只向 PPT 业务线程投递一次请求，再沿用确认与 COM 服务调用流程；不得把结束页动作继续投递为会被 native 丢弃的普通 Next。

## Scenario: COM 事实到 Draw3-ready 页码发布

### 1. Scope / Trigger

当 native 读取 managed 写入的页码、向 Draw3 发布绝对页请求，或把完成状态交给分页 UI 时，必须使用本节。该链路跨越 COM、Draw3 和 UI；修改 native 签名或等待语义时，即使 COM 接口不变，也必须同步更新本节与无窗口测试。

### 2. Signatures

~~~cpp
bool Inkeys::Drawing::Draw3::PublishProductPage(std::uint32_t page) noexcept;
HostRuntimeSnapshot Inkeys::Drawing::Draw3::ProductRuntimeSnapshot() noexcept;
bool Inkeys::Drawing::Draw3::WaitForProductRuntimeRevision(
    std::uint64_t revision, std::uint32_t timeoutMilliseconds) noexcept;
struct Inkeys::UI::Ppt::PageStatePublication {
    int currentPage = -1;
    int totalPage = -1;
};
constexpr Inkeys::UI::Ppt::PageStatePublication
Inkeys::UI::Ppt::ResolvePageStateForPublication(
    int readyCurrentPage, int readyTotalPage,
    int observedCurrentPage, int observedTotalPage) noexcept;
void Inkeys::UI::Ppt::PublishPageState(
    int currentPage, int totalPage) noexcept;
Inkeys::UI::PageControl::PptDirectionAction
Inkeys::UI::PageControl::ResolvePptDirectionAction(
    bool next, int currentPage, int totalPage) noexcept;
void Inkeys::UI::Bar::RequestEndShow();
~~~

`PublishProductPage` 的 `page` 是零基绝对页；COM 的 `CurrentPage` 是一基页，调用前必须减一。该 native `bool` 不是 COM ABI：它只表示运行中的 Host 是否已接受该目标（相同 bridge 目标也算幂等成功）。

### 3. Contracts

- 请求前置：`CurrentPage > 0 && TotalPage > 0`；请求值为 `CurrentPage - 1`。
- `PublishProductPage == false`：Host 正在停止或尚未运行；调用方不得缓存为已发送，下一次不超过 `50ms` 的复核必须重试。
- `PublishProductPage == true`：目标已写入运行中 Host 的 bridge，或 bridge 已持有完全相同的绝对页；这不等价于 Draw3 已完成页面切换。
- ready 条件：同一个 runtime snapshot 同时满足 `running` 且 `currentPageIndex == CurrentPage - 1`，此时才把 COM 的当前页/总页数写入 `PptInfoStateBuffer`。
- `PptInfoStateBuffer` 只表达 Draw3-ready 的有效页；结束放映页没有 Draw3 页，因此 buffer 必须保持/重置为 `-1/-1`，不得为了图标切换写入 `-1/有效总页数`。
- UI 解析规则：`observedTotalPage <= 0` 或 `observedCurrentPage == 0` 返回 `-1/-1`；`observedTotalPage > 0 && observedCurrentPage < 0` 返回 `-1/observedTotalPage`；两者均为正数时返回 ready buffer。
- 发布去重比较解析结果，不只比较 buffer。结束页恢复出有效 COM 页后，即使 Draw3 尚未 ready、解析结果暂为 `-1/-1`，也必须先触发箭头恢复；ready 后再即时更新页码。
- 结束页动作规则：PageControl 必须用同一已发布二元组解析 Next 动作；`-1/有效总页数` 返回 `EndShow` 并通过 `PptCallbacks::endShow -> Bar::RequestEndShow` 复用 A2 dispatcher，未知 `-1/-1` 不得误路由。结束页 Down 不建立 repeat；普通 Next hold 转入结束页时至多在下一次合法重复投递一次 EndShow 后停止。
- observer 只有在 `currentPageIndex` 或 `pageCount` 真实变化时推进 `runtimeRevision`；推进和停止通知必须与等待使用的 mutex 建立条件变量握手，避免丢失唤醒。
- 等待上限为 `50ms`，用于复核没有 native wait handle 的 COM 事实；revision 变化或 Host 停止应提前唤醒。
- 放映结束时 `PptInfoStateBuffer` 重置为 `-1/-1`，并清空 `PptImg.IsSave`、`PptImg.IsSaved` 与 `PptImg.Image`；不得把旧演示文稿的 ready 状态或页级墨迹带入下一次放映。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `observedTotalPage <= 0` | 不发布 Draw3 页请求；UI 为 `-1/-1`，放映结束流程清空 ready buffer 与 `PptImg` |
| `observedTotalPage > 0 && observedCurrentPage < 0` | 不发布 Draw3 页请求；ready buffer 保持 `-1/-1`，UI 发布 `-1/observedTotalPage`；Next 单次路由到共享 EndShow dispatcher |
| `observedTotalPage > 0 && observedCurrentPage == 0` | 视为非法/未知状态；不发布 Draw3 页请求，UI 为 `-1/-1`，不得误判结束页 |
| 非 busy `GetCurrentSlideIndex` 失败但总页数仍有效 | 当前 ABI 与结束页共用 `-1/有效总页数`；按既有 sentinel 投影，并纳入真实 Office/WPS 恢复验收，不得宣称已严格区分错误态 |
| Host 未运行或正在停止 | `PublishProductPage` 返回 `false`，本轮不推进 ready buffer，最多 `50ms` 后重试 |
| Host 已接受但 runtime 页仍不匹配 | 等待 revision 或 `50ms` 上限；UI 继续显示上一个 ready buffer |
| runtime 页匹配 COM 目标 | 同轮推进 `PptInfoStateBuffer`，再调用 `PublishPageState` |
| 仅页数变化 | observer 也推进 revision 并唤醒 waiter |
| Host 停止 | stop 通知唤醒 waiter；调用方重新进入未运行重试路径 |
| 结束页恢复为有效 COM 页 | 解析结果先离开 `-1/有效总页数` 并恢复箭头；页码等 Draw3-ready 后再更新 |
| `SlideShowEnd` 后总页数失效 | 发布不可见/`-1/-1`，并清空 `PptImg` 三个缓存字段 |

### 5. Good/Base/Bad Cases

- Good：COM 从 `12/12` 进入结束页后变为 `-1/12`，ready buffer 仍为 `-1/-1`，UI 发布 `-1/12`；Next 复用 A2 dispatcher 完成确认/退出且不重复；返回有效页时先恢复箭头/NextPage，Draw3 到达目标后再发布有效页码。
- Base：COM 页不变，`PublishProductPage` 对同一绝对页幂等成功；runtime 也未变化时最多等待 `50ms` 后复核，不重启动画或提前改 UI。
- Bad：只在 PageControl 测试中手工构造 `-1/12`，而生产 `PptInfo` 继续只发布被归一化的 `-1/-1` buffer；测试会通过，但 EndShow 分支在真实放映中永远不可达。
- Bad：只验证 `barEndShow` 资源目标，却让 `InvokeDirection` 固定调用 `nextPage`；native 在 `CurrentPage == -1` 时丢弃请求，图标正确但无法结束放映。

### 6. Tests Required

- `draw3_bridge_tests`：断言页索引变化与页数变化各自只推进一次 revision，稳定值不推进；waiter 在 publish 和 stop 时均被唤醒，并以重复并发交接覆盖通知边界。
- 产品调用点静态检查：两处 Draw3 document/command observer 都调用同一个 `HostRuntimeRevisionSignal::PublishPageChange`。
- `ppt_ui_tests`：直接调用生产 `ResolvePageStateForPublication`，断言有效 ready 页、结束页、未知态、非法零页、恢复未 ready 和恢复 ready 的精确二元组。
- `IdtPlug-in.cpp` 静态检查：没有用于 Draw3 完成等待的固定 `500ms` sleep；页请求失败不会推进 buffer；ready 比较使用同一轮 COM 快照；发布前调用生产解析器而不是直接发布 buffer。
- PageControl/动画测试：数值走 Immediate，旧关键帧被取消且不能在下一帧回写；Arrow/Add 与 Arrow/EndShow 仍走 Animated，并覆盖 `barMore -> barEndShow -> barMore`、有效页 Next、结束页 EndShow、恢复 Next，以及 EndShow 不可重复。
- 完整 `Debug|ARM64` Solution 构建及 ARM64 `--no-window` 测试通过；真实 PowerPoint/WPS 快速跳页与结束重开留给设备验收。
- 真实设备额外覆盖短暂页码读取失败后恢复；若产品要求错误态绝不显示 EndShow，必须另行设计显式 managed/native 状态，不能在 native 根据延迟或重复次数猜测。

### 7. Wrong vs Correct

~~~cpp
// Wrong：只发布 ready buffer；结束页已被归一成 -1/-1，UI 无法识别。
PublishPageState(PptInfoStateBuffer.CurrentPage,
    PptInfoStateBuffer.TotalPage);

// Correct：有效页仍等待 Draw3；结束页仅在 UI 发布边界投影。
if (observedCurrentPage > 0 && observedTotalPage > 0) {
    const bool accepted = PublishProductPage(observedCurrentPage - 1);
    const auto runtime = ProductRuntimeSnapshot();
    if (accepted && runtime.running
        && runtime.currentPageIndex ==
            static_cast<std::size_t>(observedCurrentPage - 1)) {
        PptInfoStateBuffer.CurrentPage = observedCurrentPage;
        PptInfoStateBuffer.TotalPage = observedTotalPage;
    } else {
        (void)WaitForProductRuntimeRevision(runtime.runtimeRevision, 50);
    }
}
const auto publication = ResolvePageStateForPublication(
    PptInfoStateBuffer.CurrentPage, PptInfoStateBuffer.TotalPage,
    observedCurrentPage, observedTotalPage);
PublishPageState(publication.currentPage, publication.totalPage);

// Wrong：结束页图标已切换，业务仍投递普通 Next 并被 native 丢弃。
callback = pptCallbacks.nextPage;

// Correct：同一 UI 二元组把结束页 Next 解析到共享 EndShow dispatcher。
callback = ResolvePptDirectionAction(next, currentPage, totalPage)
    == PptDirectionAction::EndShow
    ? pptCallbacks.endShow : pptCallbacks.nextPage;
~~~

`【直接确认】` managed 当前只写既有三个 `int*` 槽。`【历史局部合同】` 本节原始的 COM→Draw3-ready 页码修复不得新增 COM 方法、native wait handle、GUID 或改变当时的方法顺序；后续 PPT UInk 任务已依上文独立合同在既有 GUID 末尾追加 `GetPresentationDescriptor()`，不得把该历史句子误读为回退 getter 的要求。`50ms` 继续作为 native 对无事件 COM 事实的有界复核上限，不能用固定长睡眠替代，也不能绕过 identity-aware Draw3-ready buffer 追求有效页数字即时显示。结束页投影是无对应 Draw3 页面的 UI 语义例外，不改变 buffer 所有权。

`【直接确认】` UI3 渲染回调与 COM/模态业务之间以队列隔离。新增 PPT UI 命令时，渲染线程只能复制不可变请求数据并入队；不得在共享 UI3 调度线程内直接调用 Office COM、`PptComWriteSetting()` 或结束放映确认，否则任一阻塞都会饿死 Bar 与其余 PPT 窗口。

`PptImgStruct` 包含 `IsSave`、`IsSaved` 和 `map<int, IMAGE> Image`；它是 native 页级墨迹缓存，不是从 Office 获取的幻灯片位图。

`【合理推断】` 页码修改应端到端验证第一页/末页、快速跳页、非相邻跳转、结束后重进、切换文档、有无页墨迹以及撤销历史；只验证 COM 页码或 UI 数字都不够。

## 接口或构建产物变更清单

以下仅在任务确实改变 COM 接口/部署时适用：

1. 保持现有 interface GUID 与方法顺序，除非明确进行破坏性版本升级并设计兼容。
2. 使用 TLB 可稳定表达且 C#/native 宽度一致的参数；unsafe 指针变化需单独审查生命周期与同步。
3. 由 `PptCOM.csproj` 重新生成 TLB，不手工修改生成包装或复制产物。
4. 明确 `CheckCOM` 是信息标识还是强制版本门，再决定常量和 native 行为；不要假装当前已有比较策略。
5. 按 `AGENTS.md` 使用完整 `InkeysRepo.sln` 验证 DLL/TLB 生成、复制和 native `#import`。
6. PowerPoint、WPS、busy、结束/重开、切文档和 Inkeys 退出是建议验证范围；正式支持矩阵仍由维护者确认。
