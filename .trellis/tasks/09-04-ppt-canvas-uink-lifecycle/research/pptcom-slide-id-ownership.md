# Research: PptCOM SlideID dynamic COM ownership

> 实施后注记：本文保留规划期对 typed→dynamic 双尝试的风险分析；最终实现为 PowerPoint/WPS 共用单一 late-bound accessor，避免重复 acquisition。现行合同以 `design.md` 与 `.trellis/spec/ppt-interop/com-contract.md` 为准。

- Query: 审计 `PptCOM/PptCOM.cs` 新增 Presentation/SlideID 描述符时的 typed PowerPoint 与纯 dynamic/WPS 访问、RCW 所有权、`SafeRelease`/`FinalReleaseComObject` 边界、busy/异常路径、并发清理和测试策略；目标是不因描述符读取遗留 PowerPoint/WPS 后台进程。
- Scope: mixed
- Date: 2026-09-04

## Findings

### 1. Files found

| 文件 | 作用 |
| --- | --- |
| `.trellis/workflow.md` | 当前任务仍在 Planning/Research；研究必须持久化，不进入实现。 |
| `.trellis/spec/index.md` | 定义本仓事实/推断/待确认的证据等级。 |
| `.trellis/spec/ppt-interop/index.md` | PPT/WPS interop 边界、构建链和发布验证入口。 |
| `.trellis/spec/ppt-interop/com-contract.md` | 现有 COM ABI、managed/native 生命周期、busy/rebind 及页码发布合同。 |
| `.trellis/tasks/09-04-ppt-canvas-uink-lifecycle/prd.md` | 要求真实 SlideID 优先，损坏 COM 时显式 `PageIndexFallback`，且临时 COM 对象必须成对释放。 |
| `.trellis/tasks/09-04-ppt-canvas-uink-lifecycle/research/code-investigation.md` | 已提出在接口末尾增加纯值 Presentation 描述符及完整 SlideID 拓扑。 |
| `PptCOM/PptCOM.cs` | COM-visible 服务、PowerPoint typed 事件、WPS/dynamic 轮询、ROT 绑定、release helper 和当前页读取。 |
| `PptCOM/PptCOM.csproj` | .NET Framework 4.0、PowerPoint PIA `15.0.4420.1018`、Embed Interop Types、TlbExp/native 复制链。 |
| `Inkeys/IdtPlug-in.cpp` | native 服务快照、`GetPptState`、`PptInfo` 和 PPT 业务线程；说明新 getter 的线程与 ABI 边界。 |
| `inkStrokeModelerTest/draw3/uink_model.cppm` | `UInkCanvas.slideId` 是 `optional<int32_t>`。 |
| `inkStrokeModelerTest/draw3/uink_codec_encode.cpp` | Presentation workspace 完整编码当前拒绝缺失 `slideId` 的 Canvas。 |

### 2. Directly confirmed current patterns

- 三个字段 `pptApplication`、`pptActivePresentation`、`pptSlideShowWindow` 是一次 binding 生命周期中的长期根对象（`PptCOM/PptCOM.cs:81-83`）。新描述符只可借用它们，不可释放或 `FinalRelease`。
- `SafeRelease` 每次只调用一次 `Marshal.ReleaseComObject`，`SafeFinalRelease` 会把整个 RCW 计数归零（`PptCOM/PptCOM.cs:162-186`）。`FullCleanup(false)` 对三个根按 Window -> Presentation -> Application 单次释放；`FullCleanup(true)` 对三个根 FinalRelease，随后均置空并触发两轮 GC（`PptCOM/PptCOM.cs:220-254`）。
- `GetCurrentSlideIndex` 已展示正确的逐层形式：先取得 `View`，再取得 `Slide`，最后在 `finally` 中按 Slide -> View 释放（`PptCOM/PptCOM.cs:1641-1659`）。`GetTotalSlideIndex` 对临时 `Slides` 集合也在 `finally` 中释放（`:1661-1673`）。新 SlideID 读取应沿用这个形状，而不是链式访问。
- PowerPoint typed 分支由 `pptApplication as Microsoft.Office.Interop.PowerPoint.Application` 决定；cast 失败就进入无事件的 `forcePolling`，这正是 WPS/损坏 PIA 的现有兼容边界（`PptCOM/PptCOM.cs:1122-1158`）。
- `TryGetDynamicProperty` 通过 `InvokeMember` 做 late binding，但吞掉全部异常并丢失 busy HRESULT（`PptCOM/PptCOM.cs:285-299`）。它适合当前非关键标量兼容读取，不足以直接承担带状态分类的描述符构建。
- busy 分类已有 `0x8001010A`、`0x800AC472`、`0x80010001`，但 `HandleBusyException` 会 sleep 200ms，并在十秒后改写共享页码为 `-1/-1`（`PptCOM/PptCOM.cs:258-391`）。描述符 getter 不应调用它。
- typed PIA 静态检查确认：`Slide.SlideID`/`SlideIndex` 为 `Int32`，`Slides.Count` 为 `Int32`，`Slides.Item(object)` 与 `FindBySlideID(int)` 返回 `Slide`。这与 UInk 的 `optional<int32_t>` 对齐（`inkStrokeModelerTest/draw3/uink_model.cppm:310-327`）。
- 当前 native 端至少有三个并行调用来源：`GetPptState` 长时间运行 `PptComService`，`PptInfo` 读取标题/HWND，PPT 业务线程执行翻页/退出；两个监测线程在 `Inkeys/IdtPlug-in.cpp:650-651` detached。新描述符若由 `PptInfo` 每 50ms 直接遍历 Office 对象，会新增跨线程、长调用和 cleanup 竞态。

### 3. RCW ownership rule

释放责任必须按“COM 指针如何进入 CLR”记录，不能按 C# 变量数或 COM identity 推断：

1. 简单赋值、参数传递、`as`/显式 interface cast 只创建同一 RCW 的 managed alias；这是 borrowed alias，不产生一次可单独释放的 acquisition。
2. COM 属性、COM 方法、ROT 或枚举器新返回的 object-valued 结果是一笔 owned acquisition，即使它映射到与长期字段相同的 RCW，也必须由本次取得者恰好 `ReleaseComObject` 一次，或明确转移给长期 owner。
3. `AreComObjectsEqual` 只比较 identity。它内部 `GetIUnknownForObject` 取得的两个 raw `IUnknown*` 已在 `finally` 用 `Marshal.Release` 配平（`PptCOM/PptCOM.cs:395-413`）；比较结果不会取消第 2 条的释放责任。
4. `FinalReleaseComObject` 等价于重复 release 到 RCW 计数为零，会让同一 RCW 的所有 alias 失效。它不能用于单次属性返回值、event 参数、typed/dynamic alias 或描述符失败恢复。

现有代码有两个不能复制到新实现的风险形状：

- `TryGetActivePresentationFullNameForCompare` 新取 `Application.ActivePresentation` 后，若 identity 与长期字段相同就跳过 release（`PptCOM/PptCOM.cs:438-456`）。
- 放映窗口比较/监测也在 identity 相同时跳过新取对象的 release（`PptCOM/PptCOM.cs:464-491,1236-1279`）。

这只能证明当前兼容代码的行为，不能作为新描述符的 ownership 模板。一次新属性调用即是一笔 acquisition；若不把它转移到字段，就应 release 一次。反向地，typed cast 到同一对象只是 alias，绝不能因为多了一个变量而 release 一次。

### 4. Required ownership classification for the new descriptor

| 对象/值 | 取得方式 | 分类 | 描述符结束时动作 |
| --- | --- | --- | --- |
| `pptApplication` field | binding 保存 | binding-owned root | 描述符 borrowed；0 次 release |
| `pptActivePresentation` field | binding 保存 | binding-owned root | 描述符 borrowed；0 次 release |
| `pptSlideShowWindow` field | binding/event 保存 | binding-owned root | 描述符 borrowed；0 次 release |
| typed `Application`/`Presentation`/`SlideShowWindow` alias | 对上述 field 做 `as`/cast | borrowed alias | 0 次 release |
| event `WnObj` | Office 调用 callback 的参数 | borrowed callback object | 只在 callback 内读；0 次 release；不得缓存到描述符 |
| `window.View` | COM object-valued property | owned temporary | `finally` 中 1 次 `SafeRelease` |
| `view.Slide` | COM object-valued property | owned temporary | `finally` 中 1 次 `SafeRelease`，先于 View |
| `presentation.Slides` | COM object-valued property | owned temporary | `finally` 中 1 次 `SafeRelease` |
| `slides.Item(i)` | COM object-valued indexer/method | owned temporary per iteration | 每轮自己的 `finally` 中 1 次 `SafeRelease`，再进入下一轮 |
| `Application.ActivePresentation`（若不得不重取） | COM object-valued property | owned temporary | 即使 identity 等于 field 也 release 1 次；更推荐不重取 |
| `Presentation.SlideShowWindow`（若不得不重取） | COM object-valued property | owned temporary | 若未转移到 field，identity 相同也 release 1 次 |
| `SlideID`、`SlideIndex`、`Count`、`HWND` | 标量属性 | managed scalar | 0 次 COM release |
| `FullName`、`Name`、Caption | BSTR -> managed string | managed value | 0 次 COM release |
| JSON/BSTR 描述符及 SlideID 数组 | managed-only immutable value | managed value | 不持有 RCW；普通 GC |
| `GetIUnknownForObject` 结果 | raw pointer | owned raw AddRef | 同一 `finally` 中 `Marshal.Release`，不能用 `ReleaseComObject` |
| `_NewEnum`/`GetEnumerator` | COM collection enumeration | hidden owned temporary | 新实现禁止产生 |

若 late-bound 标量属性意外返回 `Marshal.IsComObject(value) == true`，它仍属于 owned temporary：读取应拒绝这个类型，并在 `finally` 单次 `SafeRelease`，不可因为“期望是数字”而泄漏。

### 5. Phase-by-phase release matrix

#### Phase A: select borrowed roots

| 步骤 | 动作 | release |
| --- | --- | --- |
| A1 | 在既有 PptCOM service/binding owner 路径读取 binding generation 和三个 field 引用 | fields/aliases 均 0 |
| A2 | typed cast 可用则进入 typed attempt；cast 不可用进入 pure dynamic attempt | cast alias 0 |
| A3 | 任何 rebind/cleanup 开始前先发布 `Unavailable`/新 generation，禁止 native 继续消费旧 descriptor | 纯 managed snapshot，无 COM release |

#### Phase B: read authoritative current slide

当前页必须来自 `SlideShowWindow.View.Slide`，不能用共享页码再回查 `Slides.Item(page)`；前者同时适用于 custom show，并且结束帧会明确失败而不是复用上一页。

| acquire 顺序 | 对象 | success/failure release 顺序 |
| --- | --- | --- |
| B1 | borrowed `SlideShowWindow` root | 不释放 |
| B2 | owned `SlideShowView view = window.View` | B3 若存在先释放 B3，再释放 view |
| B3 | owned `Slide slide = view.Slide` | 先读同一个 slide 的 `SlideIndex`，再读 `SlideID`；最后先释放 slide |
| B4 | managed `pageIndex/slideId` | 不释放 |

`SlideIndex` 成功而 `SlideID` 明确不受支持/损坏时，才可形成 `PageIndexFallback`。若 View/Slide/SlideIndex 本身不可得，则没有“同一次观察的页码”，必须返回 Busy/Unavailable，不得拿旧共享页码拼成 fallback。

#### Phase C: read full SlideID topology when the descriptor requires it

| acquire 顺序 | 对象 | release |
| --- | --- | --- |
| C1 | borrowed Presentation root/typed alias | 0 |
| C2 | owned `Slides` collection | 扫描完成或任意异常后，在外层 `finally` 释放 1 次 |
| C3 | managed `Count` | 0 |
| C4 | `for (int i = 1; i <= count; ++i)` 取得 owned `Slides.Item(i)` | 每轮 `finally` 释放当前 Slide 1 次 |
| C5 | 每个 item 的 managed `SlideIndex/SlideID` | 0 |
| C6 | 可选重读 Count/当前 slide 做一致性复核 | 只释放复核中新增的 object-valued temporary |

禁止 `foreach (Slide slide in slides)`、LINQ、`Cast<Slide>()` 或任何隐式枚举，因为 `_NewEnum`/`GetEnumerator` 的 RCW 不可在正常语法中显式配平。WPS 官方 Slides 文档明确该集合映射到 `Kingsoft.Office.Interop.Wppapi.Slides.GetEnumerator`，因此 WPS 分支同样必须使用一基 `Item(i)`。

完整拓扑采用 all-or-nothing：Count 前后不一致、任一 Item/SlideID 失败、ID 不能转成 int32、ID 重复、当前 `SlideID` 不在列表对应位置，均丢弃整个 managed candidate；绝不发布半份 ID 列表。若 Office 允许同 count 重排与扫描同时发生，可在刷新点做最多两次、两次 fingerprint 完全一致才发布；每次扫描各自完整释放，不能保留上一轮 Slide RCW。

#### Phase D: typed failure -> pure dynamic fallback

typed 与 dynamic 必须是两个拥有独立局部变量和 `finally` 的 attempt：

1. typed cast 不适用：不产生 acquisition，直接 dynamic。
2. typed 在 View/Slide/Slides/Item 任一阶段失败：先退出 typed attempt，逆序释放所有已取得 temporary。
3. busy HRESULT：返回 `Busy`，本次不再用 dynamic 重打同一组 Office 调用。
4. interface/type/member 兼容失败：typed temporaries 全部释放后，再进入 pure dynamic。
5. pure dynamic 使用 `InvokeMember` 逐层取得 `View`、`Slide`、`Slides`、`Item`；每个 object-valued 返回仍按 B/C 矩阵释放。

dynamic helper 应保留原始异常或 `InnerException`，让现有 `TryGetBusyComErrorCode` 能识别 reflection 包装的 `COMException`。不能直接复用会吞异常的 `TryGetDynamicProperty` 来决定 `Busy`、`PageIndexFallback` 与 `Unavailable`。

#### Phase E: publish or cleanup

| 结果 | 行为 | release 边界 |
| --- | --- | --- |
| `SlideIdReady` | 验证 binding generation 未变后，原子发布只含 string/int/array 的 immutable snapshot | 发布前所有 temporary 已释放 |
| `PageIndexFallback` | 显式 mode + 同次读取的一基 page index + ephemeral binding identity；不得把 page index 填进 `slideId` | 发布前所有 temporary 已释放 |
| `Busy` | 快速返回并让现有 monitor 下一轮重试；不 sleep、不改 `pptTotalPage/pptCurrentPage`、不 cleanup | 已取得 temporary 正常释放；roots 不动 |
| `Unavailable/Disconnected` | 使 descriptor 失效，交给既有 binding state machine 判断 rebind/cleanup | reader 不 `FinalRelease` roots |
| rebind/final exit | 先使 cached descriptor 不可消费，再走既有 `UnbindEvents -> Window -> Presentation -> Application` | 只有 binding owner 调用既有 cleanup |

新 reader 的任意 catch 路径都不得调用 `SafeFinalRelease`、`FullCleanup`、GC 或 Office `Quit`。`SafeRelease` 自身若失败，可以限频记录 acquisition stage/object kind；不得升级为 `FinalRelease` 重试，因为这可能使仍由其他路径使用的共享 RCW 失效。

### 6. Recommended pure-dynamic access shape

最小可靠形状不是链式 `pptSlideShowWindow.View.Slide.SlideID`，而是：

```text
borrowed window
  -> acquire View into local
  -> acquire Slide into local
  -> read SlideIndex scalar
  -> read SlideID scalar
  -> finally release Slide, then View

borrowed presentation
  -> acquire Slides into local
  -> read Count scalar
  -> for i = 1..Count
       acquire Item(i) into local
       read SlideIndex + SlideID scalars
       finally release Item(i)
  -> finally release Slides
```

建议内部 accessor 返回四态 attempt result：`Success`、`NotApplicable`、`Busy`、`Failed`。typed `NotApplicable/Failed` 才进入 dynamic；typed/dynamic `Busy` 都停止本轮；dynamic `Failed` 在同次 SlideIndex 已成功时可成为 `PageIndexFallback`，否则为 `Unavailable`。所有状态都是纯 managed value，不携带原异常中的 COM object。

PowerPoint/WPS 分支都必须用 `Convert.ToInt32(value, CultureInfo.InvariantCulture)` 或等价 checked conversion 对齐 UInk int32，并把 mode 放在独立字段中；不得使用 `-1` 等 sentinel 冒充 SlideID。SlideID 只在所属 Presentation 内唯一，downstream key 必须继续是 `(PresentationKey, SlideID)`。

### 7. Concurrency and cleanup boundary

推荐的关键隔离是“COM graph producer / value getter”：

```text
PptComService binding owner
  -> serialized COM read (typed, then dynamic)
  -> release every temporary
  -> validate binding generation
  -> atomically publish immutable descriptor value

native GetPresentationDescriptor
  -> read cached immutable value only
  -> no Office COM call, no RCW returned across ABI
```

理由：`PptInfo` 当前最多每 50ms 复核状态（`Inkeys/IdtPlug-in.cpp:490-611`），同时 `GetPptState` 正在执行 `PptComService`，业务线程也会调用同一 PptCOMServer。让 public getter 直接遍历 `Slides` 会与 rebind 的 `FullCleanup`、Office event 和用户翻页并发。缓存纯值后，native getter 既不获得 RCW，也不会因 Office busy 阻塞 UI/ready 状态线程。

实现阶段至少需要：

- 单调 `bindingGeneration`；rebind、Presentation 改变、结束放映和 cleanup 先失效旧 generation。
- descriptor builder 只在现有 managed COM owner/monitor 路径执行；不要新增 `Task.Run`、ThreadPool 或另一个 COM 轮询线程。
- event callback 参数仅用于当次读取或置 refresh-pending；不把 event RCW 放入 descriptor。
- publish 使用 immutable object/string 的原子交换或短锁；锁内不得调用 Office COM，也不得在持锁时 `FullCleanup`。
- builder 完成后再次核对 generation；A 的迟到结果不能在已经绑定 B 后发布。
- full topology 不必按 native 50ms getter 频率重扫；至少在新 binding、页切换、保存/退出前 refresh，并为 topology 改动设计单调 revision。具体节流属于后续 design。

现有 `FullCleanup(true)` 与其他线程方法之间没有本文件可确认的 managed in-flight gate；这是既有风险，不能在本研究中宣称已安全。新描述符通过 cached-value getter 可以做到不扩大该竞态。如果未来要调整 FinalRelease 边界，必须先让 Office 调用单线程化或增加“停止接收 -> 等待 in-flight 为零 -> unbind -> final release”的协议。

### 8. Busy/error classification

| 条件 | 描述符结果 | 禁止行为 |
| --- | --- | --- |
| `RPC_E_SERVERCALL_RETRYLATER` / `0x8001010A` | `Busy`，下一监测 tick 重试 | 不 fallback、不 sleep in getter、不清根对象 |
| `RPC_E_CALL_REJECTED` / `0x80010001` | `Busy` | 同上 |
| 既有 `0x800AC472` | 按现有策略视为 `Busy` | 不借描述符读取改变全局页码 |
| typed cast/interface/member 不适用 | 释放 typed temporary 后 dynamic attempt | 不把 cast alias release |
| dynamic `SlideID` member 缺失/非 busy 失败，但同一 Slide 的 SlideIndex 成功 | `PageIndexFallback` | 不伪造 slideId，不复用上一页 SlideID |
| View/Slide/SlideIndex 不可得或放映结束帧 | `Unavailable`/ending | 不用旧 `pptCurrentPage` 拼 fallback |
| COM disconnected/server closed | `Unavailable`，由 binding monitor 处理 | reader 不 `FullCleanup`/FinalRelease |
| full list 中途变化/异常 | 丢弃全 candidate，限频诊断，稍后重试或 fallback | 不发布部分 topology |
| release helper 抛异常 | 记录 release stage，继续让 owner 收束 | 不用 `FinalRelease` 作为补救 |

### 9. Executable test strategy

仓库当前没有 PptCOM C# 测试项目；`InkeysHeadlessTests` 是 C++ 测试，无法直接证明 managed RCW release。实现前应先建立一个很小的可注入 seam，例如 descriptor reader 接收 object-access/release adapter，生产 adapter 用 `InvokeMember + Marshal.ReleaseComObject`，测试 adapter 用 acquire/release ledger。不要用只能验证返回 JSON、无法观察释放的 managed dynamic fake 代替 ownership 测试。

#### 9.1 Headless ownership/failure-injection tests

每个 case 都断言：owned acquisitions 与 single releases 一一对应、逆序关系正确、borrowed root release 次数为 0、`FinalRelease` 次数为 0、candidate 只在完整验证后发布。

| case | 注入 | 必须断言 |
| --- | --- | --- |
| typed PowerPoint success | typed View/Slide/Slides/Item graph | 完整 descriptor；Slide -> View、Item -> Slides 释放 |
| WPS/pure dynamic success | typed cast `NotApplicable`，dynamic 数字属性 | descriptor 完整；没有 typed root release |
| damaged typed -> dynamic success | typed 在取得 View 或 Slide 后抛兼容异常 | typed 已取得对象先释放；dynamic 再独立取得/释放 |
| busy at every stage | 分别在 View、Slide、SlideID、Slides、Count、Item N 抛三个已知 HRESULT | 结果 Busy；此前 temporary 全释放；无 sleep/cleanup/global-state write |
| SlideID missing | SlideIndex 成功，SlideID 抛 member-not-found | 明确 PageIndexFallback；slide/view 释放；无伪 ID |
| index unavailable | View/Slide/SlideIndex 失败 | Unavailable，不复用 cached page/ID |
| topology mid-loop failure | 第 N 个 Item 或 SlideID 抛异常 | 第 N 个 item 与 collection 均释放；partial list 不发布 |
| same RCW identity repeatedly returned | 不同 acquisition token 映射到同一 fake identity | 每笔 acquisition 各 release 一次；不能因 identity 相同跳过 |
| managed alias/cast | 多个变量引用同一 borrowed root | release 仍为 0 |
| binding A -> B race | A build 暂停，generation 切 B 后恢复 | A 全部 temporary 释放；A candidate 被丢弃，不能覆盖 B |
| cleanup/ending | 每个 acquisition stage 后触发失效 | 无 use-after-release；getter 只返回 value status |
| repeat scan | 同一图循环 10,000 次 | ledger 最终 acquire == release，owned live count 为 0 |

静态 guard 还应拒绝新代码中出现：COM `Slides` 的 `foreach`/LINQ、`View.Slide` 链式表达式、descriptor reader 内 `SafeFinalRelease`/`FullCleanup`/`GC.Collect`、以及把 RCW 放进 cached snapshot。

#### 9.2 ABI/build checks

- 新方法只能追加在 `IPptCOMServer` 末尾；当前末项是 `SetConsoleOutputEnabled`（`PptCOM/PptCOM.cs:72-73`）。
- 生成 TLB 后由 native `#import` 使用；按项目要求构建完整 `InkeysRepo.sln` 的 Debug|x64，而不是单独构建 native 项目。
- native parser 覆盖 schema version、Ready/Fallback/Busy/Unavailable、缺字段、非法 int32、重复 SlideID 和旧 DLL/TLB 调用失败。

#### 9.3 Manual real Office/WPS matrix

这些验证不能由无窗口单元测试替代，也不应由本次 research 自动启动 GUI：

1. typed Microsoft PowerPoint：普通页、custom show、最后一帧、结束后重入；验证当前 SlideID 与 PIA/VBA 观察一致。
2. WPS pure dynamic：不加载 Kingsoft PIA，通过 IDispatch 读取 `View -> Slide -> SlideID` 和 `Slides.Item(i)`；分别验证属性可用与缺失版本。
3. 强制 typed adapter 失败但 dynamic adapter 成功，模拟“COM/PIA 损坏但 IDispatch 可用”。
4. 同一进程 A -> B -> A、两个放映窗口相同页码、页面插入/删除/重排期间切换；A 的迟到 descriptor 不得成为 B ready。
5. 连续翻页/重排并重复刷新数千次，观察 Inkeys 停止后 RCW/handle/内存不持续增长。
6. 用户正常结束放映并关闭文稿/Office 后，在只剩本测试实例的前提下确认 `POWERPNT.EXE`/WPS `wpp.exe` 能在有界时间退出；不得由测试调用 Kill 掩盖泄漏。
7. Office 正忙、文件关闭中、应用退出中逐阶段触发；确认 getter 快速返回且不会使 Office 卡死或后台残留。

### 10. External references

- Microsoft, `Slide.SlideID`: SlideID 是 Slide 的只读 Long，并且插入或重排后不变；`FindBySlideID` 比 index 可靠。https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slide.slideid
- Microsoft, `Slides.FindBySlideID`: ID 在 slide 创建时分配，返回所属 Slides 集合中的 Slide。https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slides.findbyslideid
- Microsoft, `SlideShowView.Slide`: 返回当前放映画面对应的 Slide；嵌入式 presentation 是需单独识别的边界。https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slideshowview.slide
- WPS 开放平台, `幻灯片(Slide)`: 文档列出 `ActivePresentation.SlideShowWindow.View.Slide.SlideID`，返回 Number。https://open.wps.cn/documents/app-integration-dev/docs-center/online-preview-edit/client/PPT/Slide
- WPS 开放平台, `Slides 对象`: 文档列出一基 `Item`/`FindBySlideID`，并明确 Visual Studio 映射包含 `GetEnumerator`；本设计因此避免隐式枚举器。https://open.wps.cn/documents/app-integration-dev/wps365/client/wpsoffice/jsapi/wpp/Slides/obj
- Microsoft, `Marshal.ReleaseComObject`: 每次 COM interface pointer 映射到 RCW 会增加 RCW 计数；错误 release 可能使其他调用方的 RCW 失效。https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.releasecomobject
- Microsoft, `Marshal.FinalReleaseComObject`: 等价于 release 到 0；此后所有使用该 RCW 的代码会得到 `InvalidComObjectException`。https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.finalreleasecomobject
- Microsoft Support, `Office application does not exit after automation`: 未释放的 RCW 引用会让 Office 不退出，建议把每个 object-valued 中间结果放入变量并显式释放。https://support.microsoft.com/en-us/servicing/visual-studio/troubleshooting/office-application-does-not-exit-after-automation-from-visual-studio-net-client

## Related specs

- `.trellis/spec/ppt-interop/index.md`
- `.trellis/spec/ppt-interop/com-contract.md`
- `.trellis/spec/guides/cross-layer-thinking-guide.md`
- `.trellis/tasks/09-01-uink-file-persistence/research/uink-v10-spec.md`

## Caveats / Not Found

- 本轮只做静态调查，没有启动 PowerPoint/WPS，没有验证任何具体 Office/WPS 版本、位数或安装方式；WPS 开放平台文档证明 API 形状，不等价于所有 desktop WPS COM 版本均已实测支持。
- 当前源码没有 PptCOM managed test seam/test project，也没有可静态证明 `ReleaseComObject` 实际次数的测试；需要实现阶段增加可注入 release ledger，真实进程退出仍需人工矩阵。
- `SlideShowView.Slide` 官方说明存在嵌入式 presentation 情况；若当前 Slide 的 parent 不属于绑定 Presentation，完整 topology 校验必须拒绝误绑定。本文没有为嵌入式放映制定恢复产品策略。
- 现有 `FullCleanup(true)` 与其他 native 调用线程之间未找到明确的 managed in-flight gate；本文只要求新 getter 不再直接访问 Office COM，从而不扩大竞态，未宣称既有清理已无风险。
- UInk writer 当前要求 Presentation Canvas 有真实 `slideId`（`inkStrokeModelerTest/draw3/uink_codec_encode.cpp:1162-1175`）。`PageIndexFallback` 必须保持显式 mode，不能把页码伪装成 SlideID；fallback 如何在统一 Presentation `.uink` 中编码/仅供本进程恢复，需要后续 design 明确，不能在 PptCOM 层自行绕过规范。
- PowerPoint 的 SlideID 只在所属 Presentation 内有意义；不同文稿可能出现相同数值，不能脱离 `PresentationKey` 做全局索引。
