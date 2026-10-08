# PPT 画布生命周期与 UInk 接入调查

> 实施后注记：本文记录规划期现状与候选形状；最终实现采用每 Presentation 一组文档级 mutation/queued/committed revision，而非下文候选的逐页 revision。现行合同以 `design.md` 与 `.trellis/spec/native-desktop/draw3-integration.md` 为准。

调查日期：2026-09-04

## 1. 结论摘要

1. 生产路径中的 PPT 退出确认来自 `Inkeys/IdtPlug-in.cpp` 的 `CheckEndShowClass`，主栏/分页控件的 EndShow 业务命令会在非选择模式下调用它。旧键盘钩子实现位于未编译的 `Inkeys/IdtDrawpad.cpp`；当前产品编译的是 `IdtDrawpadFacade.cpp`，其中键盘 hook 已为空实现。因此运行时去除警示只需清理 `IdtPlug-in` 的确认链，不需要为达成行为目标修改历史 Draw2 文件。
2. `Bridge::Workspace` 已经是 Desktop、Whiteboard、Presentation 三态，但 `DrawingController` 只拥有两套文档槽：Desktop 与 Presentation 共用主 `document_`，Whiteboard 使用 `secondaryDocument`。当前 `PptTouched` 只是防止混合内容被误存为 Desktop 的临时门控，并没有解决串画布。
3. PPT 状态目前只向 Draw3 发布 `workspace + pageIndex`。Host runtime 也只发布 `workspace + currentPageIndex`；两个不同 PPT 恰好位于同一页时，现有 ready 条件无法区分 A 与 B。
4. UInk v10 已实现完整读取、强类型模型、事务性覆盖保存和 Draw3 单向导出，但导出器固定生成 `workspaceType=0`，没有 `hostId` 或 `slideId`；也没有 `UInkDocument -> Draw3` 反向投影。PPT 保存/恢复必须补齐这些边界，并为 SlideID 不可得的页码退化写入明确 marker，不能把当前 Desktop 自动保存直接改名复用。
5. 正确的最小目标不是“为 Presentation 再加一个 bool”，而是建立 `SceneTarget` 的事实/请求/ready 三阶段：Presentation 目标必须原子携带演示文稿身份与页身份，绘制线程必须在旧文档安全点保存，再切到目标文档，UI 只能在身份和页码均 ready 后发布。

## 2. PPT 退出确认与键盘路径

### 2.1 实际生产路径

- `Inkeys/IdtPlug-in.cpp:706-715`：UI3 EndShow 命令在非选择模式下调用 `CheckEndShow.Check()`；取消时不结束放映，确认时先切选择模式再调用 `EndPptShow()`。
- `Inkeys/IdtPlug-in.cpp:805-838`：`CheckEndShowClass::Check()` 构造并显示“结束放映将清空画布”的 OK/Cancel 模态弹框，并通过 `isChecking` 避免重复进入。
- `Inkeys/IdtPlug-in.h:53-64`：声明 `CheckEndShowClass` 和全局实例。
- `Inkeys/IdtPlug-in.cpp:31,45-46,59-60`：MessageBox module、I18n 头及宏处理仅被该确认实现使用；删除确认类后可一并做最小清理。

推荐行为：EndShow dispatcher 仍保持单请求去重和 PPT 业务线程所有权；只移除确认分支。非选择模式仍先切到 Selection，再执行 `EndPptShow()`，以保持工具/窗口状态收敛，不把“去弹框”扩大成退出状态重构。

### 2.2 历史键盘钩子不是产品路径

- `Inkeys/Inkeys.vcxproj:886,896`：`IdtDrawpad.cpp` 登记为 `None`，`IdtDrawpadFacade.cpp` 才是 `ClCompile`。
- `Inkeys/IdtDrawpadFacade.cpp:43-51`：`DrawpadHookCallback` 直接传递，`DrawpadInstallHook()` 明确不安装旧低级键盘 hook。
- 未编译的 `Inkeys/IdtDrawpad.cpp:89-175,230-326` 仍保留 `CheckEndShow`、末页 `CurrentPage == -1`、长按翻页和 Esc 确认控制流，但它对当前发布二进制没有运行影响。

基于最小修改要求，建议本任务不改 `IdtDrawpad.cpp`。若以后要清理历史源码，应另作纯清理提交，避免让本功能的行为 diff 混入未编译 Draw2 重写。

### 2.3 明确保留的 PptCOM 保护

- `PptCOM/PptCOM.cs:1692-1745` 的 `NextSlideShow(bool check)` 会先确认当前页是否仍可读取；在结束页且 `check != true` 时拒绝继续 Next。
- native `NextPptSlides(int check)` 仍按 `check == -1` 传入该标志。

该保护属于 COM 翻页容错，不是退出弹框；按用户要求保持不动。

## 3. 三态与当前文档所有权

### 3.1 已有三态协议

- `Draw3.Bridge.h:28-34`：`Workspace { Desktop, Whiteboard, Presentation }` 已存在。
- `Draw3.Bridge.cpp:27-43`：workspace 发布会清理非 Presentation 的页请求，并用 `presentationVisitEpoch` 防止 latest-state 合并遗漏 PPT 访问。
- `IdtPlug-in.cpp:569-579`：PptInfo 先根据 COM 总页数发布 Presentation/Desktop，再单独发布零基绝对页。
- `IdtState.cpp:36-40`：Whiteboard 退出时只按 `PptInfoState.TotalPage` 选择 Desktop 或 Presentation。

### 3.2 实际只有两套文档

- `Draw3.DrawingController.cpp:1466-1484`：Run 启动只创建一个主 `InkCanvasCollection`。
- `Draw3.DrawingController.cpp:3837-3852`：只为 Whiteboard 延迟创建 `secondaryDocument`、独立页索引和 runtime history。
- `Draw3.DrawingController.cpp:4277-4299`：只有跨越 Whiteboard 边界才 swap 两套文档；Desktop 与 Presentation 之间只改枚举，不切文档。
- `Draw3.DrawingController.cpp:4264-4268`：`ObservePresentationVisit` 只把 Desktop 自动保存资格永久标成 `PptTouched`，用于避免误存，不提供文档隔离。

因此当前行为是：

```text
Desktop ─┐
         ├─ 主 document_ + 同一组 pageRuntimeStates
PPT ─────┘

Whiteboard ─ secondaryDocument + secondaryPageRuntimeStates
```

目标行为应是：

```text
Desktop      ─ singleton DocumentSlot
Whiteboard   ─ singleton DocumentSlot（发布期开关关闭时不可达，但所有权保留）
Presentation ─ map<PresentationKey, PresentationDocumentSlot>
```

任何实际 DocumentSlot 切换都必须执行现有 Whiteboard 边界已经做的 GPU 热前像、composition cache、trusted L2、viewport 计划、Laser/光标瞬态清理和目标页完整重放；不能只交换 CPU `InkCanvasCollection`。

## 4. PPT 身份与多放映切换

### 4.1 当前可用信息不足

- `PptCOM/PptCOM.cs:1412-1428` 的 `SlideNameIndex()` 只返回 `Presentation.FullName` 与 Application Caption 两行。
- `IdtPlug-in.cpp:247-261,500-507` 在一次放映初始化时取这两行作为标题/软件显示信息。
- `PptCOM/PptCOM.cs:1037-1068` 会在更高优先级活动实例出现时重绑；`:1190-1203` 检测同一应用内活动 Presentation 改变后结束当前 service。
- `FullCleanup(true)` 会把共享页码重置为 `-1/-1`，但 native 以最多 50ms 间隔抽样，不能依赖一定观察到中间 Desktop；A→B 可能在一次 native 复核中直接表现为另一个有效 Presentation。

当前架构并不是为每个放映创建一套 native PptCOM：一个 `PptCOMServer` 在任一时刻只绑定一个最佳活动 Presentation，多实例/多放映通过 service 结束与重新选择来表现。因此 Draw3 应把每次绑定观察为带身份的目标切换，而不能按“COM 服务对象是否相同”划分画布。

### 4.2 必需的 Presentation 描述符

建议在 `IPptCOMServer` 接口末尾追加一个显式只读方法，返回版本化 JSON/BSTR 描述符；不能改变既有方法顺序。描述符至少包含：

- PowerPoint/WPS 类型与应用进程身份；
- `Presentation.FullName`、Name 和放映 HWND；
- 当前一基页序号；
- 按当前顺序排列的全部 `SlideID`；
- 描述符 schema/revision，失败时返回明确的 unavailable，而不是部分拼接旧值。

native 将描述符规范化为 `PresentationKey`：已保存文稿优先用规范化完整路径查询共享 presentation index，从而为跨进程的同一文档保留同一 key/file 形状；未保存文稿用应用进程身份、COM 文稿身份/名称和本进程注册表生成的 GUID 作为退化键。任何模式都不写回或修改 PPT/WPS 文稿。

PowerPoint 官方文档明确 `SlideID` 在插入或重排幻灯片后保持稳定，`FindBySlideID` 比 `SlideIndex` 更适合重新绑定；WPS 官方对象模型也提供 `Slide.SlideID` 与 `Slides.FindBySlideID`/`FindBySlideID2`。因此 UInk Canvas 必须使用真实 SlideID，不能用页序号伪造。

证据：

- https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slide.slideid
- https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slides.findbyslideid
- https://open.wps.cn/documents/app-integration-dev/docs-center/online-preview-edit/client/PPT/Slide
- https://open.wps.cn/documents/app-integration-dev/wps365/client/wpsoffice/jsapi/wpp/Slides/obj

用户已确认：若某个兼容分支因 COM 损坏只能 pure dynamic 且拿不到 SlideID，Presentation 仍按页序号退化保存。退化记录必须显式标记，只允许当前进程中相同 binding、相同页序号恢复；不能被未来跨进程 reader 当作稳定 SlideID。若后续取得完整 ID 且 ordinal 能一一对应，可安全升级，否则本次 binding 继续 fallback。

### 4.3 PptCOM 所有权审计结论

现有 helper 已展示可沿用的基本模式：

- `PptCOM.cs:1641-1659` 的 `GetCurrentSlideIndex` 把 `View`、`Slide` 保存为具名局部并在 finally 中按 `Slide -> View` 释放。
- `PptCOM.cs:1661-1673` 的 `GetTotalSlideIndex` 对临时 `Slides` 使用 finally。
- `PptCOM.cs:162-187` 区分 `SafeRelease` 与 `SafeFinalRelease`；后者只用于 `FullCleanup(final=true)` 的长期字段。

新增 SlideID 枚举必须借用长期 `pptActivePresentation`，只拥有属性返回的临时 `Slides` 和逐页 `Slide`。使用 `for + Item(i)`，不能使用 COM `foreach` 或链式 `presentation.Slides.Item(i).SlideID`，否则枚举器或中间 RCW 无法精确释放。COM 读取过程不得持有 descriptor cache lock；成功/失败都先释放全部 temporary，再发布只含字符串、整数和数组的纯值 cache。

## 5. ready 状态必须包含身份

当前 `PptInfo` 的 ready 条件只检查：

```text
runtime.running && runtime.currentPageIndex == observedCurrentPage - 1
```

这在 A 与 B 都位于第 N 页时会产生假 ready。目标合同必须比较同一份 runtime snapshot 中的：

```text
runtime.workspace == Presentation
runtime.presentationKey == observed.presentationKey
runtime.boundSlideId == observed.currentSlideId
runtime.presentationPageIndex == observed.currentPage - 1
runtime.documentReadyRevision >= requestedTargetRevision
```

Bridge 应提供原子的 `PublishPresentationTarget(descriptor, page)`，而不是继续用“先 PublishWorkspace、再 PublishProductPage”组合出一个可能被并发观察的半状态。Host/DrawingController 完成文档选择或恢复、目标页重放并提交可接管帧后，才发布上述 ready 身份；UI 页码仍沿用 `PptInfoStateBuffer` 的二阶段含义。

## 6. UInk v10 能力与缺口

### 6.1 已可直接复用

- `uink_model.cppm`：完整强类型 UInk v10 文档、Workspace、Canvas、`slideId`、有序内容和来源 revision。
- `uink_file.cppm`：`ReadUInkFile`、`CreateUInkEditingSession`、`SaveUInkFile`，支持 create-new 与 `SaveExistingLogicalFile` 事务性覆盖。
- `uink_draw3_export.cppm`：不借用 controller 的不可变 Draw3 snapshot 与单向转换。
- `Draw3.AutoSave`：owned 串行 worker、先 UInk 后索引、原子 JSON、自校验、故障注入和退出 drain 的实现经验。

### 6.2 PPT 接入必须新增或扩展

- `uink_draw3_export.cpp:193-204` 目前固定 `workspaceType=0`，没有 `hostId/currentPageIndex`；Canvas snapshot 也没有 `slideId`。需以带默认值的字段扩展，保持 Desktop 输出不变，Presentation 输出 `workspaceType=2` 和真实 SlideID。
- 现有 Desktop capture 只导出当前页，并为每次历史快照创建新 fileGuid；PPT capture 必须遍历整份 Presentation 文档、复用稳定 fileGuid/workspaceGuid/pageGuid，并为每张幻灯片保存一个 Canvas。
- 当前没有 UInk→Draw3 importer。建议新增严格的 app-owned 子集投影：只接受本进程索引命中的、完整读取成功且符合本程序导出能力的 Presentation 文件；转换为纯值 import snapshot，再由绘制线程构造 `InkCanvasCollection` 和 `CanvasRuntimeHistory`。外部 UInk 的通用降级/预览继续不在本任务范围。
- 恢复后 redo 不保留，符合 UInk 规范；已恢复的可见 Stroke 应重建为可 Undo 的 runtime items，GPU cache 从空状态完整重放。

## 7. dirty 与保存语义

`currentContentRevision_` 目前只在当前页“空/非空”布尔值变化时递增，不能判断追加第二笔、Undo 后仍非空、Redo、擦除、Clear 或 viewport 变化。PPT 自动保存需要独立的持久化 revision：

- 每个 Presentation 页保存 `serializedRevision` 与 `lastCommittedRevision`；
- Stroke/Shape/Eraser 成功提交、Undo、Redo、Clear、viewport 变化以及 SlideID 拓扑重建均推进对应 revision；Laser、预测点、GPU cache 和纯呈现不推进；
- 离开当前页时，只有该页或文档结构 revision 尚未提交才捕获整份 Presentation snapshot；
- 离开 Presentation 或 Inkeys 正常退出时，若任一页仍 dirty，则捕获最终 snapshot；
- 同一个 Presentation 的待处理完整覆盖允许 latest-wins 合并，但已经开始的写入与后续写入必须按 revision 单调提交，旧完成结果不能覆盖新版本。

“首次从未有内容”不创建文件。如果某 PPT 已经保存过，随后用户把全部内容清空，则仍必须覆盖写入空 Canvas 集合（或显式撤销索引），否则再次进入会恢复已删除墨迹；这是防止陈旧内容复活的必要例外。

## 8. 已确认的统一 Presentation 存储边界

用户确认不按进程拆目录，采用共享 Presentation 命名空间：

```text
<AutoSave>/presentation/
  index.json
  index.json.bak
  files/<fileGuid>.uink
```

- 同一稳定源文档在 index 中绑定同一 PresentationKey/fileGuid/path；进程 sessionId 只记录 claim 与本期恢复资格，不进入目录结构。
- 当前任务只自动恢复本进程创建或成功更新过的 entry；跨进程读取、内容/拓扑冲突合并与用户提示延期。
- 遇到 foreign entry 或 SourceRevision 已被其他 writer 修改时，本期必须拒绝静默覆盖、保留 warm dirty slot并记录 `CrossProcessConflictDeferred`。
- stable SlideID entry 为未来跨进程恢复保留 topology fingerprint；page-index/process-local entry 明确限制为原进程。
- inactive 且已成功持久化的 Presentation slot 可以淘汰；保存失败或仍 dirty 的 slot 必须保留在内存。立即重入且保存尚未完成时优先使用 warm slot，不能读旧文件覆盖新内容。

## 9. 极端切换矩阵

| 事件 | 离开侧动作 | 进入侧动作 | ready 条件 |
| --- | --- | --- | --- |
| Desktop→PPT A | Desktop 原文档保持原位 | 选择/创建/加载 A slot，再定位 SlideID | A key + A SlideID + target revision |
| PPT A 页 1→页 2 | 页 1 dirty 时捕获 A 全文档；写盘异步 | 同一 A slot 切页并重放页 2 | A key + 页 2 SlideID |
| PPT A→Desktop | A dirty 时捕获，失败则保留 A slot | 恢复 Desktop singleton | Desktop slot 首帧 ready |
| PPT A→PPT B | 捕获 A；不得先把 A 页号冒充 B ready | 命中 B warm slot、当前 session index 或新建 B | B key + B SlideID |
| PPT A→B→A（B 尚在加载） | 每个目标发布单调 token | 旧 B completion 可缓存但不得激活；最终只应用 A token | latest target token |
| PPT A 退出后立即重入且 A 保存中 | 保留 A warm dirty/queued slot | 使用 warm slot，不读取旧磁盘版本 | A 当前内存 revision |
| PPT A 保存成功后重入 | clean inactive slot可淘汰 | index→ReadUInkFile→严格 import→重放 | key/fileGuid/SlideID 全匹配 |
| binding 初始缺少 SlideID | 不串用上次 key | 使用 PageIndexFallback slot，按页码保存/同进程恢复 | key + fallback pageIndex |
| stable binding 短暂 COM busy | 保留同 binding 最后 stable descriptor | 不降级、不换 key | stable descriptor 恢复 |
| foreign index/source revision | 保留 warm dirty slot | 拒绝跨进程读取和覆盖，记录 deferred conflict | 本任务不进入 ready-from-disk |
| 文件/索引损坏 | 保留旧文件与诊断，不删除 | 激活新的空 A slot，不尝试猜页绑定 | 空 slot 完整首帧 |

## 10. 对现有代码的预期影响面

- 退出确认：`Inkeys/IdtPlug-in.cpp/.h`；历史 `IdtDrawpad.cpp` 默认不动。
- COM 描述符：`PptCOM/PptCOM.cs`、TLB/manifest 生成链、`IdtPlug-in.cpp/.h`。
- 原子目标与 ready：`Draw3.Bridge.*`、`Draw3.Product.*`、`Draw3.Host.*`、`Draw3.WindowControl.*`、`IdtPlug-in.cpp`、`IdtState.cpp`。
- 多文档与 dirty/capture/materialize：`Draw3.DrawingController.*`、必要时 `Draw3.InkDocument.*`/`Draw3.InkHistory.*` 的最小公开构造辅助。
- UInk 元数据和严格反向子集：`uink_draw3_export.*`、新增 `uink_draw3_import.*` 及两个工程登记。
- Presentation 存储/index/worker：新增职责单一模块，复用 `uink_file`；不改变 Desktop 文件布局和现有 `desktop/index.json` schema。
- 测试：UInk round-trip/import、Presentation autosave/index、Bridge identity/ready、multi-PPT state machine、PPT UI EndShow 静态/逻辑回归。

## 11. 已确认决策

- Presentation 文件/index 使用跨进程共享目录，不按 session 分层。
- 未来支持同一 PPT 跨进程恢复，但先完成页面内容、插入/删除/重排、身份缺失与用户提示的冲突设计；本任务不提前开放读取。
- SlideID 不可得时按页序号退化保存，只允许当前进程、相同 binding 和相同页码恢复。
- 新 SlideID 读取必须以 pure dynamic 可用和 COM temporary 精确释放为硬门禁，不能以 Office/WPS 后台残留换取功能。

## 12. 2026-09-05 需求增量

- 稳定 COM 的 SlideID 不只用于当前顺序校验：页面重排、插入和删除都必须按 SlideID 恢复；删除页的 Canvas 保留在同一 UInk/index，当前放映只建立 active projection，重新出现的 SlideID 再取回历史 Canvas。
- `PageIndexFallback` 仍只按 ordinal 工作，并以相同页数作为恢复条件；不能从无 SlideID 的数据推断重排、插入或删除关系。
