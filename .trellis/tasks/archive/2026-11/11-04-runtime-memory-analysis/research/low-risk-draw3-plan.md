# Research: Draw3 低风险副本优化方案

- Query: 不改变 L0/L1/L2、双device、WARP/hardware策略、稳定ID、文件格式和交互时序，能否减少历史/保存/几何中的重复分配？
- Scope: internal；仅研究，未修改产品源码，未构建或运行任何程序。
- Date: 2026-11-05
- Contract: 已阅读本任务最新 prd.md、design.md、implement.md；以“减少现有副本，不延迟大分配到正常热路径”为筛选准则。

## Findings

### 推荐先做：四处纯所有权/引用改动

| 次序 | 当前代码及确切锚点 | 最小改动 | 为何低风险 | 可量化影响 |
| --- | --- | --- | --- | --- |
| 1 | `inkStrokeModelerTest/draw3/uink_draw3_export.cpp:354` `Draw3UInkCanvasSnapshot sourceCanvas = canvases[canonicalIndex]` | 改成 `const auto& sourceCanvas = canvases[canonicalIndex]` | :355–452 整段仅检查/读取sourceCanvas；其owner canvases直到导出结束都存活。ConvertStroke/WithPageStateMarker从const输入另造输出，不修改来源 | 每次处理页免掉该页strokes/operations/extra的深复制；峰值最多减少一页原始快照载荷，累计复制字节减少全部页总载荷 |
| 2 | `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp:704` `const auto imported`；:715 `std::move(*imported.snapshot)` | imported改非const auto | const使:715实际选择拷贝构造；后续:717–738只使用snapshot和read，不再用imported。非const后std::move才转移vector/string所有权 | Desktop恢复少持有一份完整导入快照；点载荷和operations/extra一起移动，不改data |
| 3 | `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp:1587` `const auto imported`；:1596 `projected=*imported.snapshot` | imported改非const；projected以 `std::move(*imported.snapshot)` 构造 | 校验在:1590–1594先完成；之后只读写projected、completion和found，不再使用imported。移动保留canvases/active/retained全部字段 | PPT恢复少持有一份完整导入快照，含当前已有的legacy与active双投影，可能高于仅活动点大小 |
| 4 | `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:1697` `const auto output=captureCanvas(...)`；:1700 `push_back(*output)` | output改auto；`push_back(std::move(*output))` | 捕获值由当前循环独占，非空检查后不再使用。移动不改原document，也不改后面的legacy投影或返回request | 每页免掉一次临时canvas深复制；不增加绘制线程工作，反而减少原保存安全点的分配/复制 |

共享导出文件确实是生产路径：`Inkeys/Inkeys.vcxproj:1066`直接编译上述uink_draw3_export.cpp；同一TU也由`inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj:169`编译。不能因目录叫demo就把它排除。

上述四处都不改变输出结构、兼容投影或序列化顺序；不动device、GPU资源、缓存策略和稳定id。源码若使用std::move须确认既有<utility>仍可用，保持原UTF-8 BOM/CRLF及适量中文说明。

### 私有PPT请求的legacy深副本：局部去掉有依据，公共导入合同不要扩大改动

#### 当前调用关系

1. 私有`BuildPresentationSaveRequest`定义于DrawingController.cpp:1616。captureCanvas为每个活动页提取可见tail（:1672），循环在:1700填activeCanvases。
2. :1703执行`snapshot.canvases = snapshot.activeCanvases`，完整复制活动页点数据。它并不参与序列化字段定义，只是内存struct的第二个容器。
3. 生产保存入口`submitPresentationSlot`在DrawingController.cpp:14163调用builder，:14167通过observer移动request到Host/worker。
4. worker `ValidatePresentationSaveRequest`在PresentationAutoSave.cpp:1001、:1014优先读activeCanvases；只有active+retained均空的旧输入才读canvases。
5. `MergePresentationSnapshot`在PresentationAutoSave.cpp:1099复制const request以便合并canonical；:1137在active/retained上合并，:1139又建立merged.canvases投影。
6. `SavePresentation/buildDocument`在PresentationAutoSave.cpp:1259取得merged，:1279选择active后:1286交给ExportDraw3SnapshotToUInk。
7. 共享导出器的`CanonicalCanvases`在uink_draw3_export.cpp:34；只要active或retained非空，:41、:48从这两组生成规范页序，**完全不读snapshot.canvases**。仅两组均空时才:38返回legacy canvases。后续pageIndex/pageNumber和retained标记规则不依赖是否存在副本。
8. builder的另一生产消费者是`RebindStablePresentationTopology`（DrawingController.cpp:14420），只从active/retained建立known map、重映射后送MaterializePresentationSlot。后者:1767同样优先active，未使用复制的legacy投影。

#### 已检索的消费者边界

- builder其余直接调用均为同一DrawingController.cpp内自检：:6128、:6206、:9530、:9700、:9708、:9729、:9836、:9955、:9979。现有断言读取active/retained，未发现要求该私有builder必须同时填canvases的断言。
- 主产品其他直接 `.snapshot.canvases` 用处分别属于Desktop保存验证（AutoSave.cpp:1051）和Whiteboard单页快照（DrawingController.cpp:7223、:10801），不是该builder返回的PPT request。
- FailedCleanupFixture的PPT receipt也优先active（Draw3.FailedCleanupFixture.cpp:362、:969、:1015）；其:300对三个容器做容量安全检查，不要求三者均有数据。
- `Draw3UInkExportSnapshot`声明（uink_draw3_export.cppm:87–90）明确canvases保留给Desktop/旧调用者，Presentation另有active/retained。公共struct和旧输入路径应保留。

#### 结论与建议边界

**可以作为本轮的可选局部优化：**仅去掉DrawingController.cpp:1703这一次私有builder中的legacy投影复制，并把旧注释改为“Presentation消费者读取active/retained，避免请求持有重复活动页”。其他公共producer、importer和Desktop/Whiteboard路径保持不变。这样request排队期间不再持有重复的全部活动页数据，且worker的legacy merged投影/导出兼容仍按原合同产生。

该变化的安全依据是所有已查明生产消费者都采用active优先，builder私有且调用链可穷尽；没有把alias指向可变document或跨线程借用容器，也没有减少历史信息。它不像删除公共字段或改变UInk格式那样扩大兼容边界。仍需对旧/新内存表示导出字节相等作一个针对性验证，才能称修改完成。

**不建议本轮顺手扩大：**

- 不删除`Draw3UInkExportSnapshot::canvases`字段，公共导入器在uink_draw3_import.cpp:499、:527仍显式填legacy镜像；现有外部/测试消费者可能按该合同读取。
- 不同时去掉PresentationAutoSave.cpp:1139、:1620、:1655及共享importer的全部镜像。局部builder优化已经能减少排队与capture峰值，全面统一表示需要另做公共API/旧测试审查。
- 不把MergePresentationSnapshot(:1099)中const request直接move掉；SavePresentation后续仍以原request的file/workspace identity、clear边界和target生成索引。不能为了消除一份复制破坏重试和调用者所有权。
- 不把retainedSlides(:1707)直接move出运行文稿：它仍属于活动/parked文档，后续拓扑恢复需要。
- 不把Immutable共享指针侵入现有快照public struct，也不改变CPU history索引、压缩discarded redo项或重建tile拓扑。这些不是当前低风险最小改动。

### 几何与历史：本轮不选高风险“节省”

- CPU不可达redo分支确有长期保留，但回收会涉及strokeIndex、RenderItemId、树、before/after raster tokens以及Clear continuation；不能归入单纯容器清理。
- 几何临时vector若只是添加reserve可以减少扩容，却也可能增大容量高水位；不能未经真实分配证据就作为内存降低结论。先做已明确深复制的四处即可。
- Laser/可信快照/辅助ULW延迟建立仍可能影响首选工具、首次显示或回退，本研究不把这些带入自动实施范围。

## 必要验证

1. 用同一InkeysRepo.sln Debug|ARM64完成产品编译；共享导出TU已在其中编译，不单独错误构建Inkeys.vcxproj。
2. 已有无窗口生产探针可覆盖PPT capture/materialize/retained与fallback：`--draw3-loaded-retained-install-test`、`--draw3-parked-ppt-retained-test`、`--draw3-parked-desktop-exit-test`。这些参数的main入口已有早退；执行前仍确认对应路径不创建HWND。完整UInk回归若要跑，沿用inkStrokeModelerTest.sln及其真实测试入口，不假造命令。
3. 若仅做四处ownership/reference变更：已有UInk export/import、Desktop Clear load、PPT Current/PreviousInterval load的正确性专项应保持身份、页序、点/operations、intervalOrdinal、EndScreen与retained数据一致；不必为std::move写镜像式单元测试。
4. 若去掉私有builder的legacy副本：在现有PPT capture自检中把新snapshot同一个临时legacy-filled副本分别导出/编码，比较完整UInk字节和capabilities；至少覆盖StableSlideId+retained+EndScreen及PageIndexFallback。这个验证检查真实序列化兼容，优于只断言canvases.empty()。
5. 代码审查确认移动来源无后续使用，sourceCanvas引用owner未变更；静态确认没有把重分配/解码/同步工作放到输入、绘制、Undo或切页路径。没有动态流畅度结论。
6. Git/格式检查由主会话或检查代理执行；本研究代理未运行任何Git命令。

## 量化方案

- `Draw3UInkPoint`是3个float（uink_draw3_export.cppm:34），每份逻辑点载荷为12N B；容器capacity、stroke/operation对象、extra/string与allocator overhead另计，不与Private Bytes混加。
- 引用sourceCanvas：一次export的累计copy载荷减少全部canvas快照数据；瞬时峰值仅能保证少一页副本，不能声称少整份文稿。
- AutoSave/PPT Load真正move：在导入结果→本地投影期间少一份完整snapshot；PPT当前snapshot本身含active+legacy，逻辑重复份数须按实际数据统计。
- builder output move：capture每页少一份临时page副本；builder去legacy后：每个排队request少一份全部active页快照；worker自己仍可能建立merged legacy，所以不承诺整个保存过程峰值等比例下降。
- 优先用固定页数/笔数/点数的离线合成快照，采集进程PrivateUsage/WorkingSet峰值与CPU heap存活分配栈；对before/after相同构建和同一输入分别起独立进程。重复取稳态/峰值并保存点数和输出hash；不修剪Working Set或强制GC。
- 如只取得源码payload计数或allocation计数，明确标为估算/局部归因；不能包装成整个产品常驻内存的实测节省。

## Files Found

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：私有PPT保存快照、topology remap、capture/restore自检。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp`：Desktop导入与投影、loadedSnapshot发布。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp`：PPT请求验证、canonical合并、持久化与载入。
- `inkStrokeModelerTest/draw3/uink_draw3_export.cpp/.cppm`：产品共享snapshot struct、规范页投影和导出。
- `inkStrokeModelerTest/draw3/uink_draw3_import.cpp`：仍保留的公共legacy镜像producer。
- `inkStrokeModelerTestTests/uink_tests.cpp`：既有UInk编码、导入/导出与文件回归集合。

## Related Specs

- `.trellis/spec/native-desktop/draw3-integration.md`：文稿/页、Clear区间、slot与线程所有权合同。
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`：异步值快照与资源边界。
- 本任务`prd.md` R3–R6、`design.md`“优化决策边界”。

## Caveats / Not Found

- 本报告只读当前源码，未构建、未执行probe、未打开Office/用户文档；所有内存量化仍待主会话测量。
- 输出仅研究文档；未改产品代码或测试文件。源码行号基于研究时工作区，其他代理后续改动会使行号移动。
- 无明显低风险的CPU历史回收项；最可行的是纯引用/真正move与private capture请求的重复legacy载荷。
