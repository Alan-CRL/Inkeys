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

## 当前 native 页码与会话合同

当前生产路径、接口签名、验证矩阵和错误行为统一见 [native-session-ui3.md](native-session-ui3.md)。早期 `PublishProductPage` / `ResolvePageStateForPublication` 页码 helper 保留兼容和单元测试，不再描述新版会话 envelope 的完整生产路径。

- 旧 `IPptCOMServer` 的 14 个方法及 v1 descriptor 均保留；新 `IPptCOMSessionState` 使用独立 IID，不在旧接口中改顺序。
- native 对新版放映状态有界16ms采样、未放映100ms；旧 DLL 兼容页状态50ms。managed owner 已可被事件唤醒，500ms仍用于慢维护，不能把它写成固定端到端延迟。
- Draw3 ready 必须匹配文稿、binding/target/session revision、SlideID/index，且在成功 Present 后发布；有效页不能提前显示。UI成功提交同一目标再开放新输入。
- 新接口以明确 EndScreen 表达结束页，读取失败为 Unknown；旧 DLL 的 -1/有效总数歧义仅留在兼容路径。隐藏控件或覆盖白板都不是结束会话。
- 仅主栏 EndShow 点击使用 Inkeys MessageBox 确认；PageControl 图标/滚轮/长按直接退出，原生 Esc/Office 退出不拦截。
- `PptImg` 是遗留 DibSurface 缓存；生产墨迹归 Draw3 稳定文稿/SlideID slot 及 UInk 保存事务所有。

## 接口或构建产物变更清单

以下仅在任务确实改变 COM 接口/部署时适用：

1. 保持现有 interface GUID 与方法顺序，除非明确进行破坏性版本升级并设计兼容。
2. 使用 TLB 可稳定表达且 C#/native 宽度一致的参数；unsafe 指针变化需单独审查生命周期与同步。
3. 由 `PptCOM.csproj` 重新生成 TLB，不手工修改生成包装或复制产物。
4. 明确 `CheckCOM` 是信息标识还是强制版本门，再决定常量和 native 行为；不要假装当前已有比较策略。
5. 按 `AGENTS.md` 使用完整 `InkeysRepo.sln` 验证 DLL/TLB 生成、复制和 native `#import`。
6. PowerPoint、WPS、busy、结束/重开、切文档和 Inkeys 退出是建议验证范围；正式支持矩阵仍由维护者确认。
