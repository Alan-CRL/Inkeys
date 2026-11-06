# Research: 候选1导出、白板JSON与释放验证入口

- Query: 保存/导出中的 active、legacy、规范页、JSON 与编码副本，在操作完成后是否继续持有大载荷？有哪些既有入口可证明释放？
- Scope: internal；仅源码研究，不修改产品或测试，不编译、不运行应用；Desktop/PPT worker 的队列与完成记录由另一专题审计。
- Date: 2026-11-05
- Boundary: 用户本轮要求调查候选1释放，候选6不修改；保留 L0/L1/L2、UI WARP 与画布独立 hardware-first device、已有保存格式与撤销能力。

## Findings

### 结论和证据边界

1. 本专题未发现导出器、JSON 编码器或白板 worker 在普通空闲等待时，把上一轮完整快照/编码缓冲保存在静态对象中的路径。白板的 request 在每次循环体内，下一次等待前析构；导出规范页、导出自校验编码 bytes、MessagePack 工作缓冲均为局部变量。
2. 有意长期保留的文档、当前白板页、失败白板暖页、Clear 撤销安全副本、PPT retainedSlides 不能算作保存泄漏。它们是内容/恢复合同的一部分。非当前白板页已有 durable 后驱逐机制。
3. 上述是所有权/析构路径的静态结论，不能替代运行时活分配证据。现有测试主要证明文件和内容完整性，未直接统计所有临时分配的存活/释放。
4. `clear()` 不等于容量归零。部分区间投影确实保留外层 vector 容量，但嵌套点数组会随元素销毁而释放；而再次 copy 空 vector 时，旧 capacity 不随之复制，必须沿具体 move/copy 链判断长期影响。
5. `WhiteboardStore::Drain()` 是工作完成门，**不是严格的 request 析构完成门**：busy=false、notify/wake 均早于本轮 request 离开作用域。Drain 返回后极短时间内仍可观察到已完成 request；不能据此误判长期泄漏。

### 主要文件

| 文件 | 用途 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | PPT 请求捕获、白板 JSON 构造、warm/cold 页转换、完成消费与文档物化 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardStore.cpp/.cppm` | 白板有界异步保存队列、页级加载、完成队列与 current/backup 目录 |
| `inkStrokeModelerTest/draw3/uink_draw3_export.cpp/.cppm` | 值快照类型、规范页副本、区间投影/合并、UInkDocument 转换与编码自检 |
| `inkStrokeModelerTest/draw3/uink_draw3_import.cpp` | UInk 转换回 Desktop/PPT 值快照、active/legacy 镜像 |
| `inkStrokeModelerTest/draw3/uink_draw3_capture.cpp` | 独立捕获结果，返回值拥有点数组 |
| `inkStrokeModelerTest/draw3/uink_codec_encode.cpp` | 局部 MessagePack buffer 与返回 bytes |
| `inkStrokeModelerTest/draw3/uink_page_file.cpp/.cppm` | 流式目录与页级读写；目录刻意剥离墨迹/extra |
| `inkStrokeModelerTestTests/contact_input_tests.cpp` | 无窗口专项入口、已有 new/delete 测试替换 |
| `inkStrokeModelerTestTests/uink_tests.cpp` | 共享 capture/export/import/codec/文件事务回归 |
| `inkStrokeModelerTestTests/uink_page_file_tests.cpp` | 元数据不持有笔迹/历史、页级读写与缓冲检查 |

### PPT 私有 producer 与 legacy 镜像

- `Draw3.DrawingController.cpp:1616` 的 `BuildPresentationSaveRequest` 是作用域内捕获，不是静态缓存。
- 每个可见笔的点在 `:1672–1685` 复制进独立 outputStroke，随后 move 到页；页在 `:1700` move 到 activeCanvases。
- `:1703` 的 `snapshot.canvases = snapshot.activeCanvases` 仍深复制一份活动页，`retainedSlides` 在 `:1705–1710` 复制进请求；快照在 `:1715` move 进返回 request。
- `:1729` 正常返回由调用方接管；所有 `return nullopt` 和 `:1731–1734` catch 均展开局部对象，不把半份快照存到其他全局对象。
- 生产 `submitPresentationSlot` 在 `:14321–14326` 创建 request 并 move 给 observer；入队是否继续持有由 Host/worker 专题负责，不在此据 producer 局部析构推导整条链已释放。
- `RebindStablePresentationTopology` 的 known map、captured、remapped 在 `:14578–14630` 有多个整页副本；函数成功/失败退出会析构这些局部变量，结果保留物化后的实际文档/retainedSlides。属于拓扑操作瞬时峰值，未发现静态副本缓存。

### 共享 export / import / 编码链

| 对象 | 创建和转移 | 释放边界与容量注意 |
| --- | --- | --- |
| `CanonicalCanvases` 返回 vector | `uink_draw3_export.cpp:34–56`；legacy 输入复制 canvases；新输入复制 active/retained 并改规范页码 | `ExportDraw3SnapshotToUInk` 的局部 `canvases`（`:351`），函数 return 或异常展开时销毁。未返回或静态保存此 vector |
| 转换后的 UInkDocument | `uink_draw3_export.cpp:327` 起局部 document；每笔点在 `ConvertStroke` 中复制到 UInkInk.points | 成功在 `:478` move 给 result.document，调用方仍拥有这份合法结果；失败早退前局部文档销毁。不能把返回值存在算作内部泄漏 |
| 导出自校验 bytes | `uink_draw3_export.cpp:468` 局部 `validated=EncodeUInkDocument(document)` | 导出返回/异常时销毁；不会挂在 Draw3UInkExportResult 中。它放大导出峰值，但不成为普通空闲缓存 |
| MessagePack sbuffer | `uink_codec_encode.cpp:1290` 每次局部 UInkEncoder，其成员 buffer_ 位于 `:1277` | Finish 在 `:166–168` 再复制成结果 bytes；encoder 在调用返回时销毁，结果 bytes由调用方接管。两份字节同时存在仅在返回构造期间 |
| Desktop import map / snapshot | `uink_draw3_import.cpp:223–275` 局部 ordered，依序 move 页到 snapshot，snapshot move 给结果 | map 空壳及局部对象离开函数释放；结果快照由调用方拥有 |
| PPT import map / active / legacy | `uink_draw3_import.cpp:328–339` 局部 map；stable 模式 `:469` copy active，`:497` move retained；`:499/:527` copy legacy；`:530` move 结果 | 函数内 map 退出释放；结果内 active/legacy 两份在结果接纳者释放前仍在。并非 import 自身静态缓存 |

`UInkEncoder` 的 bad_alloc 和其他异常在 `uink_codec_encode.cpp:1294–1304` 转成错误结果；局部 encoder 会展开。`ExportDraw3SnapshotToUInk` 的 catch（`:481` 起）不返回半成品 document。这里只确认正常异常展开，不声称处理内存破坏或强杀时仍会执行析构。

### 区间投影的 clear 与容量

- `uink_draw3_export.cpp:224–226`：`projected = canonical` 后 clear strokes 和 operations。被 clear 的 stroke/variant 元素析构，嵌套 points、clear extra 随之释放；两组外层 vector capacity 保留在 projected 中。
- 如正常区间有可见点，随后 `:243–248` 把选定 operation 的 stroke 复制进 projected.strokes。结果依然拥有当前区间应保留的点。operations.size()==0 不能说明 operations.capacity()==0。
- `Draw3.PresentationAutoSave.cpp:1613–1623` 的 PreviousInterval 分支把 const interval 复制进 projected.canvases/activeCanvases，并 clear retained。此处复制零 size 的 operations 不会复制原先满历史大小对应的 capacity；不能根据 helper 本地 capacity 直接报告 completion 长期保留满历史。
- `Draw3.DrawingController.cpp:1771–1807` 把 importedActive/retained 页 copy 入 bySlideId；`:1830` move 到局部 active；随后物化真实 InkCanvas 的 points（`:1907–1914`）。未接纳的局部投影退出释放。
- retained 页面在 `:1935–1939` 从 bySlideId move 后 clear operations，再 move 入 slot.retainedSlides。这确实可能保留该阶段曾非空 operations 的**外层 envelope** capacity；但 bySlideId 来源已经经过上游 Current-load 区间投影时，operations 往往 size==0，先前 capacity 不会被 copy 传下。要判断真实可达保留大小，应记录具体载入页的 operations.size/capacity，不能只凭 clear 下结论。
- `MergeDraw3UInkCanvasTail` 从 requested copy 后 `operations.clear()`，再构建完整 canonical log；返回 merged 的完整历史是文件合并所需数据。Canonical 长期 owner 的保留边界由 worker 专题核对。

### 白板 JSON、Store worker 与完成回执

1. `WhiteboardHistoryJson`（`Draw3.DrawingController.cpp:613–647`）创建局部 Json::Value 树，包含完整 strokes、Undo/Redo、Clear boundary/continuation。它返回值对象，没有 static/cache。
2. `saveWhiteboardPage`（`:10871–10907`）先捕获可见 tail，再构造本地 history，`Json::writeString`（`:10890`）生成额外字符串并放入 canvas.extra。`:10901` move canvas，`:10904` move request 给 Store。history JSON 树离开 lambda 返回路径析构，排队的 extra 字符串继续归 request 持有。异常/入队失败也展开本地对象，不保存半份快照。
3. `WhiteboardStore::Save`（`Draw3.WhiteboardStore.cpp:143–156`）同页 pending 以 move 赋值替换，旧 pending 内容销毁；待保存队列上限2，满队列拒绝并让入参析构。另加一个正在执行的 request，因此正常保存最多有限在途数据，而非每页永久一份快照。
4. worker 的 `for (;;)` 内声明 request（`:70–72`），在 mutex 下 move 出 saves.front 并 pop；exported 和 saved 是 try 内局部（`:97–113`）。保存成功/失败都只发布轻量 completion，save completion.document 默认未接纳。
5. worker 在 `:121–126` 发布结果、busy=false、notify、wake 后结束循环体。request 在此时析构，**下次 wait 不带上一轮 request**。`wake` 的生产接缝只发唤醒通知，若自定义回调故意阻塞则可延后这个析构边界，应按测试环境说明。
6. `WhiteboardStore::Drain`（`:169–172`）等待 busy=false 和 queues.empty，发生在 request 析构前。验证应等到下一轮真实 wait/专用测试边界或观察随后稳定值，不以单次 Drain 返回时的释放计数做硬断言。
7. load completion 会在 `:91–92` move UInkDocument 到 completed 队列，正常在 `Take`（`:163–167`）转移给 controller。`consumeWhiteboardCompletions`（`Draw3.DrawingController.cpp:10913–10988`）的 result、解析 JSON、reader、error 都为本次 lambda 内局部；拒绝 stale/非法结果时也会在后续 move 赋值或 lambda 结束时释放，不跨普通主循环长期保留。
8. `Take` 返回 false 时不会重置调用者已有 completion。产品的 completion 在消费 lambda 内，返回后析构；测试若将最后一份 load completion 放在外层长期变量里，应先 reset/离开作用域，否则测到的是测试自身持有。

### 白板 current/backup 目录与合法暖页

- `UInkPageFileDirectory`（`uink_page_file.cppm:10–18`）长期存储 path、revision、metadata、pages、标量 peakBufferBytes。
- `CanvasMetadata`（`uink_page_file.cpp:63–67`）只抽取页身份/页码/工作区/设备，不提取 content 或 extra。
- Scan 的 ObjectReader、raw/sparse、msgpack object 都限定在内部 block（`:115–131`），逐页自校验前就释放扫描临时物；逐页 ReadPage 的整页 decode 结果在每次忽略返回值后销毁（`:133`）。
- ReadPage 的 layout/bytes 是局部（`:99–109`），只有单页 document 作为返回值转移。Rewrite 单页 replacement/encoded（`:205–206`）、layout、64KiB chunk均为局部，copy 未修改页是分块进行。
- 白板 Store 的 directory/backup move 更新（`Draw3.WhiteboardStore.cpp:107–108`）不长期持有历史 JSON/墨迹副本。
- 产品完成消费只有 saved==revision 的非当前页才调用 EvictWhiteboardPage（`Draw3.DrawingController.cpp:10976–10983`）；Evict 在 `:570–579` 用新空页替换旧页并 reset runtime。当前页、失败页/新修改页继续 warm 属于规定行为。
- Desktop Clear 安全副本在 durable save completion后已有 `desktopClearRecovery->canvas.reset()`（`Draw3.DrawingController.cpp:15241–15246`）；不应为内存目标提前删除失败恢复责任。PPT boundaryFallback、Clear redo continuation、retainedSlides 也属于需保留的内容/撤销合同。

### 已有验证入口与覆盖缺口

| 入口 | 具体覆盖 | 对释放能作出的结论 |
| --- | --- | --- |
| 产品 `--draw3-whiteboard-storage-test`，`IdtMain.cpp:1987–1988` | `RunWhiteboardStorageProductionTest`（DrawingController.cpp:7251–7483）：100页、真实Store、I/O失败后重试、index失败、cold read/Undo/Redo | `:7430–7432` 检查非当前页实际 CPU内容/历史为空；并验证当前/失败/新修改页不得误删。**含离屏WARP/GPU cache探针**，不是纯CPU测试。尚未统计临时snapshot/JSON堆存活 |
| 测试 `--uink-page-file-only`，`contact_input_tests.cpp:3468–3469` | `uink_page_file_tests.cpp:45–46` 直接检查 directory.metadata 每页 content为空、无extra；buffer估计小于文档 | 证明目录不囤积页内容，未证明CRT/OS commit回到初值 |
| 测试 `--uink-file-only`，`:3470–3471` | UInk文件/codec/capture/export/import以及页文件事务 | 回归格式/失败安全；可扩展作用域内活分配观测 |
| 测试 `--uink-presentation-only`，`:3465–3466` | active/retained/end-screen/legacy 导入导出等价 | 证明兼容语义，未直接证明owner释放 |
| 产品 `--draw3-parked-ppt-retained-test`、`--draw3-loaded-retained-install-test`、`--draw3-pending-topology-load-test`，IdtMain.cpp:1993–2000 | retained保存、加载安装、拓扑变化真实controller helper | 可验证合法长期数据与瞬时重映射分离；不要因retained仍在而报泄漏 |

### 建议主会话的最小实测路线

1. 先运行现有共享测试专项和白板storage专项（独立进程、明确产物身份）。这能排除现有功能失败，不能单靠 exit0 声称内存回落。
2. 对 Desktop/PPT worker 由对应专题选最小真实API lifecycle probe；白板可复用 Store，真实保存/加载/失败后重试，不模拟替代serializer。
   共享测试工程已直接编译 WhiteboardStore.cpp/.cppm（vcxproj:128–129），不需新增工程项。CPU-only probe 可 `import Inkeys.Drawing.Draw3.whiteboard_store`，构造 `WhiteboardStore(root.wstring(), wake)`，使用 `Save(request)`、`Drain()`、`Take(result)`、`Load(page,id,generation)`。白板请求快照参考 DrawingController.cpp:7306–7323；单页UInkDocument工厂参考 uink_page_file_tests.cpp:19–29。无需执行现有生产storage测试中的GPU准备即可直接检验同一个Store/serializer。
3. 若需新增诊断，优先在既有测试入口与Store测试接缝内做 **testing-only** 观测。建议记录：待保存/在途/完成队列数量，snapshot三组canvas.capacity、每页strokes/operations.capacity、各stroke.points.capacity、extra字符串capacity，encoded.bytes.capacity，load completion.document内容点数/extra字节。把当前文档/合法warm页另列。
4. 若使用精确析构门，白板需要可确定的“本轮request已经析构且worker准备wait”测试事件，或者把诊断RAII放在循环体结束并配合下一轮等待确认；若使用大分配live跟踪，可在Drain后短时有界轮询直到live归零并要求worker仍活着，吸收这个析构时点竞态。不要改生产Drain业务语义只为通过断言。正常wake回调也须经过返回。
5. 成功保存、写失败/重试、同页覆盖、队列满拒绝、加载完成未Take/Take后/调用者释放后、worker停止/析构分别测。**未Take的load completion有意仍拥有document**，不能期望为0；save completion应没有大payload。
6. 同一进程至少连续3轮等量大快照。热身后若每轮完成时的活payload仍累积，调查owner；若活payload已0但Private/WS高水位稳定，报告allocator/系统驻留差异，不直接认定泄漏。不得强制GC、EmptyWorkingSet或HeapCompact来伪造回落。
7. 当前 `contact_input_tests.cpp:3386–3423` 已替换全局new/delete，只有累计分配次数，没有存活字节/释放计数。新增计数若走该接缝必须避免递归分配的map、并覆盖实际分配族及多线程；单看累计alloc次数不能证明泄漏。共享测试工程虽然名为Debug，实际配置显式NDEBUG、MultiThreaded（vcxproj:93–100），不能直接依赖Debug CRT hooks。可扩展现有new/delete做默认关闭的限定大分配live跟踪，清楚记录阈值/分配族/不覆盖的malloc等范围；线程/日志初始化差异需预热，单次Private Bytes变化不能替代对象存活证据。

## Related Specs

- `.trellis/spec/native-desktop/errors-logging-and-resources.md:53–55`：异步快照按值拥有、const move不转移、资源指标不可重复相加。
- `.trellis/spec/native-desktop/whiteboard.md:19–24`：页级流式存储、完整撤销extra、非当前durable页卸载、失败暖页保留。
- `.trellis/spec/native-desktop/canvas-freeze-boundaries.md:55–86`：PPT真实worker与严格加载失败合同；`:374`禁止超时析构/越过保存所有权。
- `.trellis/spec/native-desktop/build-and-compatibility.md` 与根 AGENTS.md：实际产品构建沿用 InkeysRepo.sln、Debug ARM64；共享Tests使用既有solution，不新增构建系统。

## External References

本轮没有远端查询或引入第三方用法，结论来自当前本地源码。文件内指向标准容器容量/析构行为的判断仅用于解释代码所有权，不作为OS立即去提交内存的证明。

## Caveats / Not Found

- 未编译、未运行任何测试/产品；没有新增动态采样、heap stack或活分配计数，报告只给静态路径及验证建议。
- 未发现 shared export/import/codec 内 static/thread_local 大载荷缓存；这不覆盖所有调用方或 allocator 内部缓存。
- Desktop/PPT canonical长期状态、队列取消/重试/终态快照释放以独立worker专题为准，本报告不重复审计。
- 白板完成队列可在controller未消费期间持有load文档；产品正常消费会释放，永久不消费/线程被阻塞不能从静态源码排除。
- 强杀进程或未处理异常不承诺执行C++局部析构，进程内存由系统回收；这与正常空闲释放问题不同。
- 候选6、L0/L1/L2、设备分工与现有GPU缓存策略均未修改。
