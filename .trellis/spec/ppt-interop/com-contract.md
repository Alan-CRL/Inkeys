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
- 只有既有 `PptComService` owner 读取 COM 图、执行业务命令、重绑及释放；公开getter只读纯值。event只推进轻量代次/唤醒，不以借用事件参数覆盖长期root。稳定复核与busy恢复重试分开；短暂失败可以保留同场旧画面，但页不确定时暂停写入，不能把旧stable当新页Valid。
- 常规文稿/页/拓扑属性、集合和命令必须由生产 C# `dynamic` 访问；字符串 accessor 只是测试/分派边界，不能以 `InvokeMember` 充当 dynamic。只有逐成员有证据说明 dynamic 无法实现的能力才允许包装例外；busy 不触发另一访问方式重复执行有副作用命令。
- **HWND 标量例外**：用户确认历史测试中 dynamic 无法取得 HWND，PIA 元数据亦标为 FRestricted；沿用借用的类型接口标量读取，不要求普通 dynamic 前置重试。包装不可用时仅用有文稿/COM绑定进程与放映窗口关联证据的 Win32 后备；标题唯一或前台窗口不足以证明身份。借用窗口 RCW 不额外释放；未知窗口不阻断已确认COM场次的对象定向基础控制。
- application、active presentation 和 slide-show window 由 service owner 长期持有，reader/类型标量包装只借用，reader 不释放。`View`、`View.Slide`、`Slides` 及每个 `Slides.Item(i)` 是本次获取的 temporary；必须在 `finally` 按子到父每次恰好 `ReleaseComObject` 一次，不得 `foreach`、链式 COM 属性或 temporary `FinalReleaseComObject`。
- 禁止通过 `xx.yy.zz` 跨过中间 COM 对象；必须分别取得具名 `a = xx.yy`、`b = a.zz`，并在 `finally` 中先释放 b 再释放 a。不能因访问最终为标量就省略中间对象的显式释放。
- 每次ROT/COM对象返回都是独立acquisition，即使同identity也须单次释放或明确转移；managed赋值/as/cast只是alias。去重列表不得再次释放已释放/转移的同一笔取得；转移到root后清空临时槽。Next/Previous/Exit的View及总览Navigation也必须具名/finally释放，不能保留链式temporary。
- 事件例外使用内部raw IDispatch sink和显式IConnectionPoint/cookie，不让通用CLR事件包装将入站VARIANT物化为RCW。Invoke只派发DISPID/纯值hint，长期root由owner自行取得；Find/Advise失败自动轮询。成功的CP取得只转移一次，Unadvise成功或明确服务器断连后才Release该取得。
- 失败解绑的CP/cookie/sink停用并转入静态retirement registry，由当前或后续service owner锁外重试；native重建server不能丢掉这笔所有权。首次解绑失败后，最终停止再作最多3轮显式重试，仍失败返回-1并记录未完成/保留可重试lease，不能伪称已释放；外部COM已阻塞的调用不能用本地超时撤销。
- 文稿 `FullName/Name` 不能随稳定拓扑永久缓存：相同 COM identity 可被另存或首次保存；fast 复用前必须由 owner dynamic 重新确认身份。跨放映 session 首次重新完整验证，身份 busy/失败不能把旧路径发布为可信新身份；不以单独增加 binding revision 绕过正常事件绑定生命周期。
- 当前页的 `SlideIndex` 和 `SlideID` 必须来自同一个 `View.Slide` acquisition。完整 topology 必须用 `for (1..Count) + Slides.Item(i)` 读取，校验当前 SlideIndex 顺序、正 SlideID、唯一性及当前 ID 对应；descriptor 中的数组顺序只表示本次 COM 的当前页顺序，不能把顺序变化当成 SlideID 身份变化。任一读取失败不得返回部分 `slideIds`。
- `FullCleanup` 由 owner 在旧 binding 请求失效且在途使用结束后执行：先推进 `bindingRevision`/纯值状态，再逐项解绑事件并按 window -> presentation -> application 释放持有。显式释放、root 字段清空及页码重置后，保留原有 `GC.Collect → GC.WaitForPendingFinalizers → GC.Collect` 兼容收尾；每笔取得仍须显式配平，不以 GC、循环Release或共享RCW的FinalRelease替代。取消某一事件失败不能阻断其他订阅清理。
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
- `PptCOM/PptCOM.csproj Release|AnyCPU` 必须重新生成 DLL/TLB；构建不自动复制，接口变更时显式同步同次产物到 Inkeys 后，再按 AGENTS 验证完整 Solution 的当前原生架构 Debug。核对 interface GUID 不变、新 getter 只在末尾追加。真实 PowerPoint/WPS 放映、busy/损坏、切文稿和进程退出必须另行设备验收。

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

`【直接确认】` `PptCOMServer.CheckCOM()` 当前返回常量字符串 `20261024a`。`IdtPlug-in.cpp::CheckPptCom` 会调用并保存返回值，但本轮静态搜索没有找到把它与期望版本比较、拒绝服务或选择兼容分支的代码。

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
- `TryGetBusyComErrorCode` 区分Office暂忙；descriptor独立约100ms重试，不受稳定3秒复核阻挡，慢维护约500ms；COM调用本身阻塞不在调度上界内；
- `FullCleanup`、`SafeRelease` 及循环对象释放；
- WPS 与 PowerPoint 均按实际 acquisition/事件连接配平；FullCleanup 显式清理后保留原三步强制 GC 收尾，不对共享 RCW 使用 FinalRelease 补漏。
- COM线程诊断不得调用会写入当前线程IErrorInfo的Marshal.GetHRForException。使用ExternalException.ErrorCode读取已知错误，普通managed异常明确未知；否则原生调用只返回HRESULT时，CLR可能重抛前序日志保存的无关异常而错误分类busy。测试人为设置ErrorInfo后须finally恢复并配平原始指针。

`【历史/兼容】` 这些分支证明代码考虑过多个 Office/WPS 版本，不证明每个组合仍在发布测试范围。

`【待确认】` 需要维护者提供实际支持的 PowerPoint/WPS 版本、Office 位数/安装方式、Windows/架构组合，以及事件和轮询各自的正式回退条件。

`【合理推断】` 修改绑定/恢复时按源码现有分类分别验证：应用暂忙、放映结束、文档切换、COM 对象失效、应用退出和重新绑定；事件绑定/解绑及 slide-show window → presentation → application 的清理依赖不能只改一半。

## HWND 与窗口控制

`【直接确认】` owner通过统一ResolveSlideShowHwnd解析窗口，先用已确认必要的类型标量例外，再用有COM进程/文稿关联的Win32候选；GetPptHwnd只取纯值缓存。native将基本控制与窗口/墨迹能力分开，无可信窗口时不Focus、不自动接管Office笔。

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
5. DLL/TLB 在 PptCOM 自身输出目录生成，工程间无依赖和自动复制。实现或接口变更均须手动同步新 DLL 到 `Inkeys/PptCOM.dll`；TLB 有变化时同步同次产物。拷贝保留旧时间戳时须触发资源重编译，并用 `.github/scripts/release-artifacts.ps1::Assert-EmbeddedPptCom` 核对 EXE 内嵌哈希，再按 `AGENTS.md` 用完整 `InkeysRepo.sln` 验证；发布 CI 的 artifact 交接合同见 [build-and-compatibility.md](../native-desktop/build-and-compatibility.md)。
6. PowerPoint、WPS、busy、结束/重开、切文档和 Inkeys 退出是建议验证范围；正式支持矩阵仍由维护者确认。


## Scenario: COM 生命周期控制台诊断

### 1. Scope / Trigger

用户设备出现 Office 关闭后残留、对象释放无法配平或事件解绑失败时，使用内部 PptComDiagnostics。入口跟随既有 ConsoleOutput.PptCOM / SetConsoleOutputEnabled，修改开关后重启。无需额外环境变量，不能修改 COM ABI。

### 2. Signatures

```csharp
PptComDiagnostics.Acquired(object value, string source, object parent = null, long call = 0, long elapsedTicks = -1);
PptComDiagnostics.Transfer(object value, string destination);
int PptComDiagnostics.Release(object value, string source);
PptComDiagnostics.Summary(string stage);
```

getter/Item、ROT 与 FindConnectionPoint 记录已有对象返回，Release 返回原 Marshal.ReleaseComObject 的计数。调用前后日志将未返回的 COM 调用定位到具体 member；不得为日志重做 getter/命令。

### 3. Contracts

- `[PptComLife] v=1` 行含 seq/pid/tid/qpc/event；启用行含模块 MVID，方便核对实际加载版本。
- obj/parent/target 是当前进程内的弱托管对象标识，不是 IUnknown 地址或提供方文稿身份。不调用 GetIUnknownForObject 做日志身份查询。
- acquire/transfer/release.begin/release.end 对应每次取得、转移与一次实际释放；transfer 不增加取得计数。remaining 是 RCW 剩余计数，不是原生 AddRef 总数，非零不授权再次 Release。
- summary 含 acquired/released/balance/outstanding/untracked_release/release_failures/dropped/output_failures；outstanding 行给出每个仍有已跟踪取得的对象及最后来源/转移信息。重绑时被选中的新 bestApp 等仍可能合法存活，不能将正 balance 一概当泄漏。
- ConditionalWeakTable 仅以弱键关联 RCW；独立待清理字典最多 4096 项且只保存纯值 Entry，不保存 RCW/委托/COM 属性。掉项、未知释放、失败输出均需显式呈现，不能宣称完整配平。
- 诊断关闭时 getter/Item/命令直接走原分派；Release 仍只调用一次原 API。诊断不额外读取 Office 属性、不增加 AddRef/Release、不输出文稿名/路径，输出失败不改变业务异常/线程 IErrorInfo。
- owner.maintenance/service.stop/service.finally/cleanup.begin/gc.begin/gc.end/service.end 只记录当前纯值快照和已有 root。状态含 generation；回滚候选后不再输出 close_binding/closed_binding，也不再使用关闭延后/最终退休策略。
- COM 事件诊断在回调中只入最多 64 条的纯值队列，由 owner 每轮最多输出 64 条，连续生产不能饿死 owner；office.event 的 event_qpc/event_tid 是通知现场，行头 qpc/tid 是实际输出现场。event_dropped 非零表示事件日志不完整，不能使控制台写入阻塞 Office 回调。
- getter/Item/命令/Release/Unadvise 的 elapsed_ticks 从 begin 输出返回后开始，到实际调用返回或抛出后立即锁存，再进行输出；frequency 为 Stopwatch.Frequency。不能拿两个输出 qpc 之差当纯 COM 执行时间。

### 4. Validation & Error Matrix

| 条件 | 诊断结果 |
| --- | --- |
| 同 RCW 重复返回 | 每次 acquire 各记一笔，分别 Release；不按 identity 去重取得 |
| getter/命令失败 | call.fail 给出原 HRESULT 与异常类型；不改 IErrorInfo、不重复调用 |
| Unadvise busy | 输出原 cookie、HRESULT 与待清理数量；仍按原策略重试并保留 lease |
| 输出流关闭/抛异常 | 吞掉诊断输出异常，output_failures 增加；COM 操作照原语义执行 |
| 日志不完整或 dropped/untracked_release 非零 | 明确视为不完整，不能据 summary 宣称全部底层对象已释放 |

### 5. Good / Base / Bad Cases

Good：控制台开启后，保留获取→转移→释放与关闭后的日志，结合 service-end 汇总定位 root/CP。
Base：控制台关闭时无新日志，原有联动行为与每次取得/释放次数保持。
Bad：日志为读取对象名称而触发 COM、使用强引用字典保活 RCW，或对 remaining 非零循环 Release。

### 6. Tests Required

真实 IDispatch 夹具覆盖重复同 identity 取得/释放、原 RCW 计数、命令恰次执行、事件 busy 与成功重试、disabled 静默、输出抛异常、弱键不保活真实 RCW、慢输出不计入调用耗时；已有线程 IErrorInfo marker 保持不变。保留既有 owner/断连/末文稿 Count0 回归，以及回调不执行控制台 I/O 的诊断测试；已撤回候选新增的关闭延后、2031直接退休和0x800706BA分类，不再将对应测试列为当前实现合同。真实 Office 退出由用户设备结果另行确认。

### 7. Wrong vs Correct

```csharp
// Wrong：为诊断额外获得 COM 指针；日志本身改变引用。
IntPtr identity = Marshal.GetIUnknownForObject(value);
// Correct：只记录已经返回的对象，不再向 Office 获取对象或属性。
PptComDiagnostics.Acquired(value, "get:View");
```

## 调查结论与后续架构边界

2026-11-14用户实机已确认：旧PowerPoint清理中的Unadvise或CP Release可同步等待数十秒，旧进程尚在时重启Inkeys却能立即联动新文稿。当前先回退候选行为，仅保留FullCleanup三行GC与诊断；同步服务链仍存在该阻塞，不能标为已修复。证据和用户决策见 `.trellis/tasks/archive/2026-11/11-14-pptcom-release-regression/research/rollback-to-gc-and-diagnostics.md`。

用户确定后续采用多线程动态绑定，使旧绑定清理与新绑定建立可并行推进，当前尚未实施。届时必须覆盖：旧清理被闸门阻塞时新绑定仍可Active、旧回调/命令/页码不得污染新session、每笔COM取得只有一个清理所有者、失败lease不丢失及线程/待清理任务有界；不能仅以最终计数配平代替进度验收。
