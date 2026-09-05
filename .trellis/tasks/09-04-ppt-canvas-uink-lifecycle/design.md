# PPT 三态画布与 UInk 自动保存恢复设计

> 状态：用户已于 2026-09-04 批准实施，任务已进入 `in_progress`；实现后需按本设计复核。

## 1. 设计目标

- 保留 Desktop、Whiteboard、Presentation 三态，并让每个状态拥有独立 CPU 文档与 runtime history。
- Presentation 再按 PPT 文档身份隔离；A、B 多放映实例直接切换时，文档、页、dirty、保存目标和 ready 状态都不能串线。
- 去除退出 PPT 的模态警示，但保留 EndShow 单请求 dispatcher、切 Selection 和 PptCOM 末页翻页确认。
- 同一 PPT 的所有页完整覆盖保存到同一个 UInk 文档；页内容未变化时不产生无意义写入。
- 文件和索引统一位于 `AutoSave/presentation/`，不按 Inkeys 进程分目录；本期只自动恢复当前进程创建或成功更新过的记录。
- 优先以真实 SlideID 绑定；COM 损坏或兼容分支拿不到 ID 时，允许显式页序号退化，并把风险限制在当前进程。
- PptCOM 新读取不泄漏 `Application/Presentation/Slides/Slide/View` RCW，不因异常让 PowerPoint/WPS 进程残留后台。

## 2. 非目标

- 本期不开放跨 Inkeys 进程自动恢复。
- 本期不合并来自两个进程的页面内容，不处理插入、删除、重排后的交互式冲突，也不显示冲突决策弹框。
- 不实现 UInk append；Presentation 每次保存使用完整临时文件、自校验和原子替换。
- 不实现任意外部 UInk 的通用 Draw3 预览或有损编辑，只导入本程序当前进程索引命中的受支持子集。
- 不恢复发布版 Whiteboard 入口，不修改 Whiteboard UI。
- 不清理未编译的旧 `IdtDrawpad.cpp` 键盘路径。

## 3. 总体边界

```text
PptCOM service thread
  one late-bound COM accessor + exact temporary release
  -> immutable primitive PresentationDescriptor cache

native PptInfo thread
  GetPresentationDescriptor() + validate
  -> deterministic source identity / PresentationKey
  -> atomic SceneTarget publication

Draw3 Host / drawing thread
  shared immutable desired target + scene-stamped command -> safe-point document switch
  -> page dirty snapshot -> PresentationAutoSaveService
  -> target canvas restore/replay
  -> fixed-size identity-aware HostRuntimeSnapshot ready publication

PresentationAutoSaveService worker
  full UInk save / global index transaction / strict current-process read
  -> completion queue + control wake
```

COM 事实、Draw3 desired 和 Draw3 ready 是三份不同状态。任何 UI 或保存逻辑不得把“COM 已切到 B”误当成“B 文档已经加载并呈现”。

## 4. 身份模型

### 4.1 PresentationDescriptor

新增纯值描述符，概念字段如下：

```text
PresentationDescriptor
  schemaVersion
  provider: PowerPoint | Wps | Unknown
  status: StableSlideIds | PageIndexFallback | TransientBusy | Unavailable
  fullName
  presentationName
  applicationProcessId
  slideShowHwnd
  currentPage              // 一基，沿用 COM 事实
  totalPage
  currentSlideId?          // StableSlideIds 时必填
  slideIds[]               // StableSlideIds 时长度等于 totalPage，且唯一
  bindingRevision
```

`TransientBusy` 不生成新的 PresentationKey，也不把已有 stable binding 降级。相同 binding 已有最后一份完整描述符时继续暂存该描述符并等待有界重试；新 binding 尚无完整描述符时保持目标未 ready。

### 4.2 PresentationKey

native 将规范化 source identity 确定性映射为固定 16 字节 `PresentationKey`：

- 已保存文档：规范化 FullName 是跨进程、跨 PowerPoint/WPS 的源身份，key 由该身份确定性派生并由 index 校验/持久化；provider 只作诊断元数据，不能让同一路径在两个 Office 实现下生成两份文档。
- 未保存或路径不可规范化：使用 `Inkeys PID + provider + Office PID/HWND + bindingRevision + presentationName`，只在当前进程稳定，不同 binding 不得因同名碰撞。
- 不向 Presentation.Tags 写 GUID，避免改变 PPT 文档和触发 Office 保存提示。
- 相同源身份必须复用已有 PresentationKey；不同源身份即使页数和当前页相同也必须产生不同 key。

### 4.3 SlideBindingMode

```text
StableSlideId
  Canvas.slideId = Office/WPS 真实 SlideID
  可按 SlideID 检测拓扑与重新绑定

PageIndexFallback
  使用 UInk private Workspace 类型（128+）而不是 Presentation 类型 2
  Canvas 不写 slideId，只以规范 pageIndex/pageNumber 保存
  Workspace/Canvas extra 明确写入 inkeysBindingMode=page-index
  importer 只按 ordinal 读取，严禁调用 FindBySlideID
```

UInk v10 只要求 workspaceType=2 的 Presentation Canvas 带真实 slideId；128+ private Workspace 可以诚实表达 Inkeys 的退化页集合，并让未知 reader 按通用页面保留内容。未来跨进程恢复器必须先识别 binding marker，不能把 private fallback 升级为 StableSlideId。

同一进程内，若最初处于 fallback、之后拿到完整且页数一致的 SlideID 列表，可在绘制线程安全点按现有 ordinal 一次性升级同一 DocumentSlot；若无法证明一一对应，则本次 binding 继续保持 fallback，不能局部混用两种模式。

## 5. PptCOM 页面身份读取

### 5.1 COM 接口

在 `IPptCOMServer` 末尾追加 `GetPresentationDescriptor()`，返回版本化 UTF-16 JSON/BSTR。既有方法顺序、GUID 和 `NextSlideShow(bool check)` 不变；构建重新生成并同步 `PptCOM.dll/.tlb`。

JSON 使用 .NET Framework 4.0 可用的结构化序列化器，不手工拼接路径或名称。native 使用既有 jsoncpp 解析，并设置长度、字段数量、slide 数量和字符串长度上限。接口调用、解析或 schema 不兼容时进入明确 fallback/unavailable，不读取半份描述符。

### 5.2 COM 与缓存线程边界

新 getter 不直接读取 `pptActivePresentation` 或 `pptSlideShowWindow`。PptCOM service owner 路径负责：

1. 只由现有 `PptComService` binding/monitor owner 线程在绑定建立、总页数变化或低频拓扑复核时读取全部 COM 值；Office 事件只设置 refresh-pending 或更新已能由纯值安全表达的状态，不另起枚举线程。
2. 只在所有必要字段成功后构造新的纯托管 DTO；失败则构造明确 fallback/status，不发布半个 ID 数组。
3. 将字符串、整数和 `int[]` 深拷贝到 `presentationDescriptorCache`。
4. getter 只在短锁内复制缓存引用/值，再在锁外序列化；锁内不调用 COM、不释放 RCW。
5. `FullCleanup` 与 descriptor builder 保持在同一 owner 串行路径；先清空/推进 binding cache，再解绑事件并释放长期字段。若实现阶段发现 builder 仍可能从其他线程进入，必须增加“停止新读取 -> 等待 in-flight 为零 -> cleanup”的显式 gate，不能让 cleanup 与 borrowed roots 的读取并发。

### 5.3 dynamic 获取与释放合同

`pptActivePresentation` 是 PptCOMServer 长期拥有的 borrowed field，新读取器不得释放它。每次属性/方法返回的中间 COM 对象都是 owned temporary：

| 对象 | 获取 | 释放位置 | 禁止行为 |
| --- | --- | --- | --- |
| `Slides` | borrowed Presentation 的 `Slides` 属性 | 最外层 `finally` 调用一次 `SafeRelease` | 缓存 RCW、`FinalRelease`、遗漏异常路径 |
| 单个 `Slide` | `Slides.Item(i)` | 每次循环自己的 `finally` | `foreach` 隐式枚举器、链式属性后失去引用 |
| `View` | borrowed SlideShowWindow 的 `View` 属性 | 当前页读取 `finally` | 与 bound `pptSlideShowWindow` 一起释放 |
| 当前 `Slide` | `View.Slide` | `View` 之前释放 | 存进 descriptor cache |
| Count/SlideID/字符串 | 值类型或深拷贝字符串 | 无 COM release | 把 boxed COM 对象留在 DTO |

具体约束：

- 使用显式 `for (1..Count)`，不使用 COM `foreach`，避免隐藏 `_NewEnum` RCW。
- 每次 dynamic/reflection 属性访问先赋给具名局部变量，禁止 `presentation.Slides.Item(i).SlideID` 链式调用。
- 当前页的 SlideIndex 与 SlideID 必须来自同一次 `SlideShowWindow.View.Slide` 获取；View/Slide 不可得时不能用旧共享页码拼 fallback。完整 topology 再从 Presentation.Slides 取得并校验当前 ID 所在位置。
- temporary 只调用 `SafeRelease/ReleaseComObject` 一次；`FinalReleaseComObject` 继续只用于既有 `FullCleanup(final=true)` 的长期 owner 字段。
- 每一次 object-valued COM 属性/方法返回都是独立 acquisition；即使 COM identity 与长期 field 相同，只要未转移为 owner，也必须释放一次。反之，`as`/cast 或简单赋值只是 borrowed alias，不能多释放。
- 释放顺序为 `Slide -> Slides`、`current Slide -> View`；异常、busy、转换失败和重复 ID 都走同一 finally。
- cache 中只允许托管值；不得保存 `dynamic`、RCW、`object` COM proxy 或枚举器。
- PowerPoint PIA 绑定对象、WPS 与损坏接口统一经过 `InvokeMember` late-bound accessor；不做第二次 typed→dynamic 重试。accessor 必须解包 reflection 的 `TargetInvocationException.InnerException`，让 busy HRESULT 保持可分类，并让每次 temporary acquisition 只有一条释放路径。
- COM busy 使用既有 HRESULT 分类；新 getter 不 sleep、不调用现有会改写页码的 `HandleBusyException`。busy 不等于“不支持 SlideID”，不得立即把 stable binding 抖动成 page-index fallback。

### 5.4 可测试性

将新 identity acquisition 限定在独立内部 helper，并注入 late-bound accessor 与 temporary release callback。增加无窗口 managed test harness，用 PowerPoint-like、WPS-like 和逐故障点 fake 验证：

- 成功时每个 temporary 恰好释放一次，borrowed presentation/window 从不释放。
- 任一页 Item、SlideID、current View/Slide、Count 抛异常时仍完整释放，且不返回部分 slideIds。
- busy 保留 stable cache；确定不支持才发布 fallback。
- cache 和 JSON 在原 fake/RCW 生命周期结束后仍只含纯值。

fake 只能验证所有权控制流，不能替代真实 Office/WPS RCW 验收；真实进程残留检查保留为后续用户授权的可见/Office 集成门禁。

## 6. 原子 SceneTarget 与 ready

### 6.1 Bridge 契约

保留 `Workspace` 三态，新增：

```text
SceneTarget
  workspace
  presentationKey?        // Presentation 必填
  bindingMode
  sourceIdentity
  bindingToken/revision
  pageIndex
  slideId?
  targetRevision
  immutableTopology       // 共享纯值 slideIds，不借用 COM
```

PPT 使用单一 `PublishPresentationTarget(descriptor)` 原子发布 key、页和 topology；不再用 `PublishWorkspace(Presentation)` 与 `PublishProductPage()` 两次调用拼接。Desktop/Whiteboard 继续使用 workspace 事务，工具/颜色 publication 不得覆盖 SceneTarget。

latest-state 可以合并重复目标，但每次 PresentationKey、bindingMode、SlideID、页或 topology 改变都推进 targetRevision。`presentationVisitEpoch/PptTouched` 在文档独立化完成后删除；Desktop 自动保存直接依赖自己的 singleton slot，不再需要污染门控。

### 6.2 HostRuntimeSnapshot

Bridge 的 desired state 以 `shared_ptr<const PresentationTarget>` 共享完整拓扑，发布后不可修改；命令同时固定发布时的 workspace/target。Host 先按 FIFO 重放命令场景并执行命令，再收敛到 latest state，避免 `A Clear -> Desktop/B` 时 Clear 落入后来的画布。

高频 runtime 快照不复制 source/name/完整 `slideIds`，只在 mutex 内发布固定大小的 `PresentationReadyIdentity?`，并与页数/runtime revision 组成同一快照：

```text
workspace
presentationReady { key, bindingMode, pageIndex, slideId, bindingRevision, targetRevision }
currentPageIndex/pageCount/runtimeRevision
```

`PptInfoStateBuffer` 只有在同一 runtime snapshot 同时满足以下条件时推进：

- running；
- workspace 为 Presentation；
- presentationKey 与 COM observed key 相同；
- bindingMode 相同；
- Stable 模式下 boundSlideId 等于 currentSlideId；fallback 模式下 pageIndex 相同；
- ready targetRevision 精确等于本次请求返回的 revision。

这可以防止 A 第 3 页被误认为 B 第 3 页已经 ready。

## 7. DocumentSlot 模型

### 7.1 所有权

DrawingController 绘制线程独占所有可变文档/runtime：

```text
desktopSlot: DocumentSlot
whiteboardSlot: DocumentSlot
presentationSlots: map<PresentationKey, PresentationDocumentSlot>
activeSlotKey
pendingSceneTarget
```

`DocumentSlot` 拥有：

- `InkCanvasCollection`；
- 每页 `CanvasPageRuntimeState`；
- 当前页；
- GPU-independent page identities。

`PresentationDocumentSlot` 额外拥有：

- PresentationKey、bindingMode、source identity；
- stable fileGuid；workspaceGuid 保存在 `InkCanvasCollection`；
- ordered real SlideID 或 fallback page-index bindings；
- 文档级 `mutationRevision/queuedRevision/committedRevision`；
- `persistenceInitialized/loadPending`，其余 warm/pending/failed 状态由 slot 内容与三 revision 表达。

### 7.2 切换安全点

SceneTarget 只在活动 contact 已收尾的绘制线程命令边界应用。任何实际 slot/page 切换统一执行：

- 丢弃 hot preimages 与 composition GPU cache；
- 推进 raster pipeline generation；
- invalidate trusted L2；
- 清 viewport 计划、Laser、粒子、cursor 和 transient operator layers；
- 清 backbuffer 后从目标 CPU document/runtime 完整重放；
- 成功 present 后再发布 ready identity。

Whiteboard 当前入口关闭不改变其 singleton 定义；重新启用时继续通过同一 slot 机制工作。

### 7.3 多目标与异步完成

每个 load/save 请求携带完整 PresentationTarget；保存另带文档 mutation revision。worker completion 到达后：

- 保存完成可以更新对应 inactive slot 的 committed revision，即使它已不是当前目标。
- 加载完成只可进入 key/source/topology 仍兼容的 active 或 parked slot；同文稿页码/targetRevision 已前进时可用 latest target materialize 全文档，不兼容结果直接丢弃，绝不覆盖当前画面。
- A→B→A 时，B 的迟到结果绝不能激活到最终 A 上。
- 保存未完成或失败的 inactive slot 保留 warm 文档；成功且 clean 的 inactive slot 才可淘汰，以保证真正存在 index→read→import 的冷恢复路径。

## 8. UInk 映射

### 8.1 导出扩展

以向后兼容默认值扩展 `Draw3UInkExportSnapshot`：

- workspaceType，Desktop 默认 0，Presentation 为 2；
- hostId；
- currentPageIndex；
- workspace extra；
- 每个 Canvas 的 slideId 与 canvas extra。

Desktop 现有字节/行为除 Header 时间等既有动态字段外保持不变。Presentation snapshot 遍历该 PPT 的全部页面，而不是只导出当前页；每张页 Canvas 使用稳定 pageGuid，空页也保留，以便 SlideID/ordinal 拓扑完整。

### 8.2 严格反向投影

新增共享 `uink_draw3_import` 纯值模块：

```text
UInkDocument + ExpectedPresentationBinding
  -> Draw3UInkImportResult
  -> Draw3UInkImportSnapshot | structured diagnostics
```

只接受：

- Header/fileGuid、唯一 Presentation Workspace、hostId 与 index entry 全匹配；
- layer 0、受支持 viewport、当前 Draw3 可表达的 Pen/Highlighter/Eraser/Line/Rectangle；
- Stable 模式真实 SlideID，或带明确 marker 的 private PageIndexFallback Workspace；
- 无 Media、AdvancedHighlighter、未知/private 顶层语义和无法安全投影的 Shape/HDR。

reader 仍完整保留合法 UInk；“strict current-process importer 拒绝”不得被记录为文件损坏。导入失败不修改现有 slot，随后创建隔离空 Presentation slot。

materialize 在绘制线程完成：按 snapshot 构造 InkCanvasCollection，为每个 Stored Stroke 计算 footprint、追加 CanvasRuntimeHistory，并建立 before/after raster token；redo 按 UInk 合同为空。GPU 资源不进入 import snapshot。

### 8.3 拓扑校验

- Stable 模式要求 index/UInk/当前 descriptor 的有序 SlideID topology 完全一致；页序重排、插入或删除均属于后续冲突解决范围，本期隔离且不自动猜测。
- PageIndexFallback 仅接受当前 session 的 entry。稳定路径文稿以相同 PresentationKey/source identity 和相同页数跨放映 binding 按 ordinal 恢复；process-local 身份仍要求 exact binding revision/token。缺页、越界或页数不同均拒绝整份导入。
- fallback Canvas 不含 slideId，永不参与 Stable 模式匹配。

## 9. dirty 与捕获

### 9.1 revision

`currentPageHasContent/contentRevision` 继续服务 UI 显隐，不承担持久化 dirty。每个 Presentation slot 使用一组文档级 `mutationRevision/queuedRevision/committedRevision`；以下任一成功变化推进 mutation：

- Stored Pen/Highlighter/Eraser/Shape 进入文档；
- Undo、Redo；
- Clear，包括清成空；
- viewport 改变；
- fallback→stable 的安全身份升级。

Laser、prediction、L0/L1 暂态、GPU cache、纯 Present 和选择模式不推进。保存始终捕获全部页，因此无需逐页 captured revision 或独立 structure revision：任一页 mutation 都使整份文档 dirty；每个离页/离场景边界检查同一 revision，既不会漏掉脏页，也不会在无变化时写盘。SlideID 拓扑不兼容直接隔离，安全 fallback→stable 升级作为 mutation。

### 9.2 触发

- 同一 PPT 切页：文档 mutationRevision 超过 queuedRevision 时，捕获整份 Presentation snapshot 后立即切页；磁盘写入异步。
- Presentation A→Desktop/Whiteboard/B：在旧 slot 安全点做同样捕获，再切目标。
- EndShow：COM 目标离开 Presentation 后走上述场景离开触发，不依赖弹框。
- Inkeys 正常退出：scene-stamped 最终 barrier 位于所有已接受命令之后，捕获 Desktop、active Presentation 及全部 parked dirty slot，再关闭 producer 并 drain 两类服务。
- 无变化切页/退出不重复写；首次从未发生持久化修改的全新空文档不创建文件。
- 保存门控只比较 mutation/queued/committed revision，不读取 `currentPageHasContent`。已恢复或保存的文档被清空后即使立即退出，也提交空 Canvas 全量覆盖，防止旧内容复活。

### 9.3 latest-wins 队列

Presentation 与 Desktop 的历史保存语义不同：Desktop 每次请求不可合并，Presentation 是同一逻辑文件的全量最新状态。因此 Presentation worker 按 key 管理：

- pending 尚未开始时，新 revision 替换旧 pending snapshot；
- writing 期间出现新 revision，只保留一个最新 follow-up；
- completion 按 mutation revision 回写，旧 completion 不能把更新文档标 clean；失败 completion 回退 queued 到 committed，保留 dirty 供下一安全点重试；
- 同一文件写入严格串行，revision 单调；
- CloseAndDrain 保证每个 key 的最新 accepted revision 到达 Committed 或 Failed。

## 10. 统一 Presentation 存储与索引

### 10.1 布局

```text
<AutoSave>/presentation/
  index.json
  index.json.bak
  files/
    <fileGuid>.uink
```

不按日期或进程建目录。PresentationKey 负责文档身份，fileGuid 负责 UInk 逻辑文件身份；文件名不直接暴露或编码用户路径。

### 10.2 index schema

版本 1 entry 严格且恰好包含以下 12 个字段（不包含 `createdAt/updatedAt/topologyFingerprint`）：

```json
{
  "presentationKey": "...",
  "sourceIdentity": "path:... | process:...",
  "sessionId": "...",
  "fileGuid": "...",
  "workspaceGuid": "...",
  "relativePath": "files/<fileGuid>.uink",
  "bindingMode": "slide-id | page-index",
  "processLocal": false,
  "bindingRevision": 1,
  "mutationRevision": 1,
  "slideIds": [101, 202],
  "sourceRevision": { "volumeSerial": 0, "fileIndex": 0,
    "length": 0, "lastWriteTime": 0, "sha256": "..." }
}
```

index validator 严格检查 schema/字段数、唯一 presentationKey/source identity/fileGuid/path、`relativePath == files/<fileGuid>.uink`、binding 拓扑和 UInk Header/source revision。所有 entry 必须带 writer session；`process-local` 和 `page-index` 记录未来跨进程 reader 默认拒绝。

### 10.3 当前阶段的 claim 规则

服务生成一个进程级 sessionId，但它只用于 claim/恢复资格，不进入目录层级：

- entry 的 `sessionId == ProcessSessionId()` 即视为当前进程 claim，允许覆盖与自动恢复；实现不维护第二份 `currentProcessEntries` 集合。
- stable source identity 已存在、但 entry 来自其他进程且本进程尚未加载/解决：不自动读取、不静默覆盖，返回 `CrossProcessConflictDeferred` 并保留 warm dirty slot。
- page-index/process-local entry 永不被其他 session 认领；稳定路径 fallback 在同 session/key/source 且页数相同时允许跨新放映 binding 恢复，process-local fallback 仍要求 exact binding。
- `ProcessSessionId()` 是进程静态值；同一进程内不同 service/Host generation 复用，`Host::Start` 不重建。

此保守规则意味着跨进程恢复功能完成前，遇到旧进程记录的同一文档时不会更新 canonical UInk；这是避免无提示覆盖旧内容的刻意限制，而不是保存成功。

### 10.4 文件/index 事务

- 由规范化 presentation root 派生跨进程 named mutex。
- worker 在互斥区内重读并严格验证 index，解析/认领 entry，再执行对应 UInk create-new 或 SaveExistingLogicalFile。
- 首次创建：UInk durable+self-validate 后原子发布 index；index 失败保留孤儿和可重试状态。
- 覆盖：携带 expected UInkSourceRevision；`SourceChanged` 不更新 index、不标 clean。
- 文件仍保持同一路径和 fileGuid；成功覆盖后原子更新 mutation/source revision 与显式 binding/slideIds。
- 主/备 index 都损坏时停止写入并记录；不扫描后猜测重建、不删除未知文件。
- 文件已提交但 index 失败的 pending entry 只在同一规范化 autosave root 内保留；同根 Host generation 可经 source revision/strict import 恢复并收敛，切换 root 必须清除。实际 I/O 使用保留大小写的绝对路径，大小写折叠 key 只用于 root 比较与 named mutex。
- 当前任务不弹保存错误或冲突窗口。

## 11. 恢复流程

1. SceneTarget 指向 Presentation A。
2. 若 A 有 warm slot，直接使用；dirty/pending slot 永远优先于磁盘。
3. 若无 warm slot，查询共享 index，但只接受 `sessionId == ProcessSessionId()` 的 entry，或同根 service 保存的待发布 index entry。
4. worker 读取 UInk、校验 source revision/fileGuid/hostId/binding marker，并运行 strict importer。
5. 绘制线程只在 completion targetRevision 仍最新时 materialize、重放和 present。
6. stable 模式按 current SlideID 定位；fallback 模式只按当前进程相同 pageIndex 定位。
7. 读取/index/import 失败时保留源文件，创建新的隔离空 slot；不显示 A/B/Desktop 的旧墨迹。

加载期间 Drawpad 墨迹 surface 保持隐藏/透明并抑制输入，不能让旧文档短暂覆盖新 PPT。PageControl 的有效页码继续等待 identity-aware ready；COM 结束页的 EndShow 特例不受影响。

## 12. 极端状态转换

| 转换 | 必需顺序 |
| --- | --- |
| Desktop→A | 保留 Desktop slot → resolve A warm/index/new → A present → ready A |
| A 页 1→2 | contact 收尾 → dirty 时 capture A 全文档 → 切 A 页 2 → present → ready 页 2 |
| A→Desktop | capture A → 切 Desktop slot → present Desktop；A pending/failed 时保留 warm |
| A→B | capture A → invalidate ready A → resolve B → present B → ready B |
| A→B→A | targetRevision 单调；B completion 过期不得激活；A warm 优先 |
| A 保存中立即重入 | 不读旧文件，使用 A warm slot；completion 只更新 matching revision |
| A clean 淘汰后重入 | current-process index → read/import A → present |
| stable ID 暂时 busy | 保留同 binding 最后 stable descriptor，不降级、不换 key |
| 初始即拿不到 ID | 创建 PageIndexFallback slot；稳定路径同 session/key/source 且页数相同时按页码恢复，process-local 必须 exact binding |
| ID 后续恢复 | 能证明 ordinal 一一对应时原地升级，否则本次继续 fallback |
| 外进程 entry/source revision | 拒绝覆盖，保留 dirty，记录 deferred conflict |

## 13. PPT 退出警示移除

- 删除 `CheckEndShowClass` 声明/定义/实例及其唯一 MessageBox/I18n 依赖。
- PPT UI business EndShow 始终在非选择模式先 `ChangeStateModeToSelection()`，随后 `EndPptShow()` 并完成 dispatcher。
- 不改 PageControl/A2 的 EndShow 去重和线程投递。
- 不改 `PptCOM.NextSlideShow(bool check)`。
- 不改未编译 `IdtDrawpad.cpp`；产品 facade 已无键盘 hook。

## 14. 错误矩阵

| 条件 | 行为 |
| --- | --- |
| descriptor transient busy | 保留同 binding 稳定值并重试；不切 fallback |
| 文档 binding 首次 unavailable | 保持目标未 ready，并使用隔离空 slot；不复用上一 PPT key |
| 文档身份可用但 SlideID 不可得 | 使用 PageIndexFallback；稳定路径只允许当前 session 的同 key/source、相同页数按 ordinal 恢复；process-local 仍限 exact binding |
| SlideID 部分读取失败/重复/数量不符 | 丢弃整个 ID 列表，PageIndexFallback；所有 temporary 已释放 |
| PresentationKey 相同但 stable topology 不同 | 进入隔离空 slot 并记录 topology conflict，不自动附着或覆盖 |
| 无变化切页 | 不捕获、不写盘 |
| 保存 pending 时又修改 | 保留最新 follow-up snapshot |
| Save SourceChanged/foreign entry | 不覆盖、不推进 index、不标 clean；保留 warm dirty |
| UInk 成功而首次 index 失败 | 保留孤儿，当前 entry 不具备恢复资格，可重试 index |
| index 主损坏、备份有效 | 使用备份；主备都坏则隔离写入 |
| current-process UInk 损坏/strict import 拒绝 | 保留文件，创建空 slot并记录 |
| app 退出有 pending | drain 最新 accepted revision；明确失败后允许退出并记录 |

## 15. 兼容与迁移

- 不改变 UInk version 10 wire version；稳定模式使用已注册 Presentation workspace 与真实 slideId，退化模式使用规范允许的 private workspace 和 extra marker。
- Desktop `desktop/YYYY-MM-DD/index.json` 与不可变历史文件完全不迁移。
- 删除 `PptTouched` 只能在 Desktop/Presentation 真实独立且三态测试通过后进行。
- COM 新方法只追加在接口末尾；重新生成 TLB/DLL，不手工编辑产物。
- PowerPoint/WPS 正式版本矩阵不扩大；无法读取 SlideID 的既有兼容路径走 fallback。
- 旧程序不会读取新的 `presentation/index.json`，无需反向迁移。

## 16. 测试策略

### 16.1 纯逻辑/无窗口

- SceneTarget 原子性、identity-aware ready、same-page A/B 隔离和 stale completion。
- Desktop/Whiteboard/多个 Presentation DocumentSlot 往返及 GPU-independent runtime 重建。
- 每类持久化 mutation 的 dirty revision、无变化不写、清空覆盖和 latest-wins。
- stable SlideID 精确拓扑映射、topology conflict 隔离、PageIndexFallback 和 fallback→stable 安全升级；插入/删除/重排后的自动冲突解决按用户范围延期。
- UInk Presentation exporter/importer round-trip、hostId/slideId/extra、unsupported rejection 和 redo 清空。
- global index schema、same-key overwrite、current-process claim、foreign writer conflict、source revision、主备恢复、孤儿和故障注入。
- A→B→A、save/load 乱序、立即重入、失败后 warm slot 保留和退出 drain。
- native descriptor JSON parser 的限额、invalid schema、重复 SlideID、Unicode path 和 fallback。

### 16.2 Managed ownership harness

- 单一 late-bound accessor 对 PowerPoint-like/WPS-like 返回值与损坏 IDispatch 的处理。
- Count/Item/SlideID/View/Slide 每个故障点的 exact release 表。
- borrowed/owned 分类、无 foreach、无 chained RCW、cache 纯值。
- busy cache 稳定和 cleanup 后旧 binding 不再可见。

### 16.3 构建与静态门禁

- x64 MSBuild 构建完整 `InkeysRepo.sln Debug|x64`，覆盖 PptCOM→TLB→native `#import`。
- x64 MSBuild 构建完整 `inkStrokeModelerTest.sln Debug|x64`。
- 运行全部相关 x64 无窗口测试和 managed ownership harness。
- `git diff --check`、UTF-8/BOM/CRLF、COM 方法顺序、生成产物与静态引用审计。
- `rg` 证明生产无 `CheckEndShow`、旧 `IdtDrawpad.cpp` 仍为 None、`NextSlideShow(check)` 未改。

真实 PowerPoint/WPS 的重复绑定、退出、进程残留和任务管理器检查需要可见 Office，按仓库规则不在自动实现回合启动；记录为后续用户明确授权的人工验收，不以 headless 测试冒充。

## 17. 回滚形状

- Presentation 自动保存可由现有 `saveSetting.enable` 整体关闭；关闭时不捕获、不创建目录，但已接受请求继续收敛。
- storage/index 模块与 Desktop 服务独立，撤下 Presentation 接线不影响现有 Desktop 历史。
- strict importer 或 identity descriptor 失败只降级为空/fallback slot，不回退到共享 Desktop 文档。
- COM 接口接线若构建或兼容失败，可暂时保留 page-index fallback，但不得发布虚假 StableSlideId。
- 任何 index/file 失败均保留旧可读文件；不以自动清理作为回滚。
