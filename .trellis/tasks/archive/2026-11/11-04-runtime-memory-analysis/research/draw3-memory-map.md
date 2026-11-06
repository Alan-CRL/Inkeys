# Research: Draw3 运行时内存结构

- Query: 当前生产 Draw3 的全尺寸绘图资源、GPU 历史缓存、CPU 文档/历史、保存快照和诊断内存在哪里分配，哪些值得优先测量？
- Scope: internal；仅只读源码调查，未编译、未运行程序、未采集运行数据。
- Date: 2026-11-04

## Findings

### 1. 生产路径与资源所有权

- `Inkeys/Inkeys.vcxproj:1093`、`:1109`、`:1113`、`:1134`、`:1148`、`:1161` 将 DrawingController、InkHistory、InkHistoryGpu、Renderer、TransparentPresentation、HistoryProbe 编译进产品。
- `Inkeys/Inkeys.vcxproj:951` 将旧 IdtDrawpad.cpp 列为 None；`:1187` 的旧 IdtFloating.cpp 同样不是本报告的生产内存所有者。
- Draw3 使用独立 D3D device；`Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp:31` 实现 hardware-first/WARP-fallback，显式关闭硬件加速则直接 WARP。不能把 UI RenderPipeline 的独立 WARP device 统计当作 Draw3 设备统计。
- Host 使用真实 Drawpad 窗口尺寸初始化 presentation（`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:2083`），下列 W/H 代表该时刻实际画布像素尺寸，不默认等于某块屏幕分辨率。
- ComPtr/视图共享一份纹理的所有权；RTV、SRV、GetBuffer 引用不应按另一张完整像素图再计数。

### 2. 全尺寸资源：启动/Resize 就有的主体

`Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:387` 的 CreateSizeDependentResources 不等待开始写字或切换到 Laser，就依次创建以下资源。

| 资源 | 格式/名义字节 | 分配及释放边界 |
| --- | --- | --- |
| 稳定 L2 | BGRA8，4WH | Renderer.cpp:405；ReleaseSizeDependentResources/整个 Renderer 释放 |
| L1 add + retain | BGRA8 + R16_FLOAT，6WH | Renderer.cpp:413；创建格式见 :337 |
| L0 add + retain | BGRA8 + R16_FLOAT，6WH | Renderer.cpp:414；创建格式见 :337 |
| trusted L2 snapshot | BGRA8，4WH | Renderer.cpp:412 启动即尝试；失败属可选降级。RendererSnapshot.cpp:39、:119 |
| laserCompositedColor | RGBA8，4WH | Renderer.cpp:415，无工具判断 |
| laserStrokeCoverage | RGBA8，4WH | Renderer.cpp:416，无工具判断 |
| 双缓冲 swapchain | BGRA8 × 2，8WH | TransparentPresentation.cpp:977、:985；backBufferTexture 只是其中 buffer 0 的引用 |
| selection ULW staging + DIB | STAGING BGRA8 + 32位 DIB，8WH | TransparentPresentation.cpp:1031：DComp 主路径也无条件初始化辅助 ULW presenter；:390、:420、:475 分配 |
| primary ULW staging + DIB | 再加8WH，仅主模式是 ULW 时 | TransparentPresentation.cpp:1016 |

完整路径均为 `Inkeys/Inkeys/Drawing/Draw3/` 下对应文件。

名义像素载荷估算（假定可选 trusted snapshot 成功、默认尚未使用 Laser 增量、不含缓存/driver overhead）：

- Renderer 自有图层 = `(4 + 6 + 6 + 4 + 4 + 4)WH = 28WH` 字节。
- DComp 主模式 + 双缓冲 + selection ULW = `44WH` 字节。
- ULW 主模式再增加 primary ULW = `52WH` 字节。

| 画布尺寸（示例） | DComp 44WH | ULW 52WH |
| --- | --- | --- |
| 1920×1080 | 87.01 MiB | 102.83 MiB |
| 3840×2160 | 348.05 MiB | 411.33 MiB |

这些是源码资源描述的载荷总和，**不是进程 Private Bytes、Working Set 或实际 GPU committed/resident 测量值**。未计 pitch/对齐、driver/设备/command buffer、DWM、CPU/GPU 中间副本及共享页。硬件、UMA 和 WARP 对这些资源的记账位置不同，不能与进程内存直接相加。STAGING 与 DIB 是两个独立存储。

生命周期补充：

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:430` 释放尺寸相关纹理；`:470` 才释放固定 buffer、shader、设备引用等。已检查主体中没有空闲一段时间即释放这些全尺寸资源的逻辑。
- Resize 保留旧 L2、L1 add/retain、laserCompositedColor，再创建新尺寸资源并复制交集（Renderer.cpp:515）。这带来短时旧、新载荷重叠，不能只测稳定状态。
- trusted snapshot 是独立像素快照；RendererSnapshot.cpp:170 执行 CopyResource。其存在是视觉回退合同，不能直接删除来换内存。

### 3. 按需资源、固定上传 buffer 与粒子

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cppm:235` 的 kMaxBufferCapacity=200000。
- Renderer.cpp:655 一次创建 `200000×16=3,200,000 B` 普通墨迹上传 buffer；`:670` 一次创建 `200000×24=4,800,000 B` 荧光笔上传 buffer。二者名义合计约7.63 MiB，启动即存在，具体 DYNAMIC/DEFAULT 由设备兼容路径决定。
- Renderer.cpp:678 已有独立256 B合成 buffer，避免合成矩形 DISCARD 整个3.2 MB buffer；这项优化已经存在，不应当作新发现。
- Renderer.cpp:727 启动即 Initialize 粒子系统。`Inkeys/Inkeys/Drawing/Draw3/Draw3.LaserParticles.cppm:19` 容量2048、`:82` 断言粒子80字节；主粒子 buffer 163840 B（0.15625 MiB），不含 shader/常量/视图。它远小于全屏纹理，优先级较低。
- laserLiveCoverage 是按选中 Laser 才开启的额外 RGBA8全尺寸层（Renderer.cppm:377）；启用后 Resize 会重建它（Renderer.cpp:417）。不能把它算成所有启动都有的额外4WH。
- laserBakeScratchColor 在事务烘干 BeginLaserBake 时按需创建（Renderer.cpp:293），ClearAllLaserCoverage 归还它（:290）。不是永久多一张必有纹理。

### 4. GPU 历史缓存：预算不是启动实际占用

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistory.cppm:18`：Undo tile 为128² BGRA8，即64 KiB；每纹理页256 slice，即16 MiB。默认 UndoCachePolicy 是64 MiB、最多20个热撤销条目（:28）。20个是 GPU 热前像条目限制，不是语义撤销只能20笔。
- 同文件:22：composition tile 为256² ×(BGRA8 + R16F)=384 KiB；每页64 slice=24 MiB；默认预算192 MiB（:34）。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistoryGpu.cpp:1101` Initialize 只建常量 buffer和CPU planner，不预分配整套64+192 MiB。
- Undo 在 EnsureUndoSlots 不够时才按最多256 slice扩张（:326）；composition 在 free slots为空时才按最多64 slice扩张（:514）。因此第一份少量缓存就可能触发16/24 MiB级的名义纹理载荷，而不是只分配当前有效tile的精确字节数。
- RemoveHotEntry(:347)与RemoveResident(:835)只把slice归还池；普通逻辑淘汰不缩纹理页。SetPolicy 的重排只在allocated超过新capacity时执行（:361、:850）。
- 并非永不归还：DiscardHotPreimages(:1349)和DiscardCompositionCache(:1553)明确clear纹理页；文档槽切换（DrawingController.cpp:14119）及Clear（:14918）调用它们。整个GPU cache只有一套（DrawingController.cpp:10926），不是每个白板/PPT页都预留256 MiB。
- EnsureScratch 按需创建3个单slice operator页（InkHistoryGpu.cpp:499），名义 `3×256²×6=1.125 MiB`，独立于普通composition slice预算；已创建后随cache Release/Reset释放，不随上述仅DiscardCompositionCache自动清理。
- 已有 `Residency()` 接口（InkHistoryGpu.cpp:1540）报告undo/composition页与slice数、hotEntries/residentNodes，可用于把“有效缓存”与“池实际分配”对照。它不等同OS/driver resident bytes。

### 5. CPU 文档、历史与各工作区

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkDocument.cppm:73` 的 StoredInkPoint 是三个float；名义点数据12 B/点，未计vector capacity、stroke对象和分配器开销。
- Collection→Page→Device Canvas→vector<InkStroke>→vector<StoredInkPoint> 分层持有（同文件:89、:107、:130、:147）。运行历史另外保存每笔undo/composition tile列表、树/稀疏索引和redo栈（InkHistory.cppm:80、:124、:173）。这些都是CPU侧结构，不是GPU纹理。
- DrawingController.cpp:519 每页还有beforeStates/afterStates以及Clear恢复分支；:771 的DrawingDocumentSlot拥有document、所有pageRuntimeStates、retainedSlides等。:10469–10513 为桌面、白板、Presentation多lane状态建立各自slot，通过swap切换，不是每次切换复制整套文档。
- 白板已有冷页：非当前页仅在保存成功、revision一致、非正在加载时被替换为空page并清空runtime（DrawingController.cpp:570、:10880）。不可笼统说所有白板页始终热驻。
- PPT同一个lane的整份文稿页/CPU历史会保留；已离开的lane仅满足持久化等条件时才淘汰（DrawingController.cpp:789、:1060）。redo或clearRedoContinuation未清时禁止淘汰，属于正确性合同。
- DesktopClearRecovery 可保留内存快照（DrawingController.cpp:734），持久化成功后可转为locator；Clear后的Redo continuation会复制page+runtime（:542），不得把这些当作无用途泄漏。

**值得单列的长期增长模式：**

- 新笔先追加Canvas stroke，再DiscardRedoBranch（DrawingController.cpp:2637）。InkHistory.cpp:990的DiscardRedoBranch仅clear redoItems_；AppendStroke从items_.size()继续追加（:902），没有压缩掉旧隐藏item；InkCanvas也只有追加和整Canvas ClearStrokes（InkDocument.cpp:119、:127）。
- 因此“多次Undo后另起新笔”会留下已无法redo的旧stroke点、history item和tile拓扑，至少直到清画布/释放页面或整个slot。它是**已确认保留行为**，但增长幅度和优化收益尚未测量；不宜直接称泄漏。
- 真要回收必须保留稳定id、raster token、clear continuation和持久化语义，单纯erase vector会破坏关联。

### 6. 保存快照会叠加CPU峰值

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:1689` 遍历全部PPT页捕获点数据；`:1700` push到activeCanvases；`:1703` `snapshot.canvases = snapshot.activeCanvases` 深复制legacy投影。同一个请求的活动页点数据因此至少有两份快照，且原document仍在；retainedCanvases还会另行复制(:1707)。这是可明确定位的峰值候选，不能把删除兼容投影当成已批准方案。
- 白板保存还会把完整历史转成Json::Value并序列化到extra字符串（DrawingController.cpp:10783），与UInk可见tail快照同时存活；存储worker Save会合并同页请求，并限制最多两个待处理save（Draw3.WhiteboardStore.cpp:143）。
- Desktop保存worker已在终态移走并释放大snapshot（Draw3.AutoSave.cpp:1185–1204），不是所有已保存图长期常驻。SubmitLocked(:1214)只作幂等去重，未见按snapshot bytes硬限流；已有queuedSnapshotBytes诊断可观测。
- Presentation SubmitSave按同lane/file/generation合并普通tail，Clear边界保持FIFO（Draw3.PresentationAutoSave.cpp:1893–1919）；不能把所有翻页事件都乘以整文稿快照大小推算堆积。

### 7. 输入/诊断不是默认全部吃满

- ContactInput.cpp:29定义4096 slot硬上限；默认根据系统触点数计算，queue至少256（:110、:328），不能把最大容量当当前机器实际分配。
- Host.h:275 默认enableRuntimeMetrics=false。Host.cpp:625、:635在关闭时跳过RuntimeMetricsSession创建；开启后使用32 MiB共同预算（RuntimeMetrics.cpp:41、:420），并按effective sample数reserve多个vector。预算/保留容量/真实驻留应分别报告。
- DiagnosticOutput.h:13 定义1024×4096字符记录池，名义文本载荷4 MiB。DiagnosticOutput.cpp:319只有有效stdout/stderr sink或debugger时才new State；不能直接计为所有正常启动的固定4 MiB。分析器/调试器附着本身会改变此条件。

### 8. 已有无窗口测量接缝

- `Inkeys/IdtMain.cpp:1960` 的 `--draw3-history-benchmark` 在主GUI初始化前早退，runDraw3SelfTest也明确不创建交互控制台（:1689）。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HistoryProbe.cpp:498` 声明 `hwnd=0 gpu=0 present=0`；ReadProcessResources(:80)取PrivateUsage、WorkingSet、handle数；输出(:343)包含stored/retained/visible/redo、tile数以及append/branch阶段内存，适合CPU历史基线和分支保留测量。
- 相同入口区域的 `--draw3-geometry-benchmark` 是纯几何计时；`--draw3-renderer-upload-test` 和 RunRendererUploadValidation 是WARP离屏GPU正确性验证（Renderer.cppm:193），不能代替真实DComp/ULW进程内存测量。
- `--draw3-hidden-test` 等“隐藏窗口”仍创建HWND，不属于no-window许可，不能默认为本轮可运行。
- 本报告没有执行上述命令；已编译EXE/PDB与当前分支的一致性须先验证。

## 候选优化优先级与最小测量

| 顺序 | 候选 | 已确认依据 | 下一步测量与正确性边界 |
| --- | --- | --- | --- |
| P1 | Laser两张主体coverage按需/生命周期释放 | 每次CreateSizeDependentResources无条件8WH；4K名义63.28 MiB | 冷启动→首选Laser→首笔→尾消失→切回普通笔；保持多contact、烘干事务和零残影，不把已按需live/scratch重复计算 |
| P1 | DComp下辅助selection ULW缓冲延迟建立/按呈现需求回收 | 初始化即8WH，4K名义63.28 MiB | 空桌面→进入选择语义→退出→多次切工作区；保持前置辅助窗可用性、输入/所有权及失败回退 |
| P1 | trusted L2快照按有效内容/需求创建和回收 | 启动即尝试4WH，4K名义31.64 MiB | 空白页和真实需要回退的导航/重绘故障场景；必须保留可信回退合同 |
| P1/P2 | 缓存纹理页粒度、空闲池收缩、压力自适应预算 | 首次页16/24 MiB，普通淘汰不归还页 | 对照Residency的allocated slices/used条目、Private Bytes及GPU进程预算；同时测Undo/切页/重建延迟，不能仅降低预算看节省 |
| P2 | 丢弃redo后不可达CPU分支回收/结构去重 | Canvas/history只追加，DiscardRedoBranch只clear栈 | 固定可见笔数，反复“写长笔→Undo→另起新笔”100/1000次；比较stored/retained/visible和CPU堆存活栈；保留稳定索引与Clear语义 |
| P2 | PPT保存快照与legacy投影重复点载荷、白板Json中间态 | 同请求深复制两份active canvases，原文档仍在 | 大文稿翻页/Clear/保存、慢磁盘队列前后峰值；先量份数/容量，再设计共享不可变数据或流式编码，兼容与FIFO不改变 |

粒子buffer只有约0.156 MiB，默认关闭metrics也不是普遍常驻32 MiB；不建议将它们作为“大幅降低内存”的首个方向。固定上传buffer的7.63 MiB可后续评估按实际几何需求增长，优先级低于每张全屏纹理。

## Files Found

- Draw3.Renderer.cpp/.cppm：全尺寸图层、固定上传buffer、粒子初始化、Resize及释放。
- Draw3.RendererSnapshot.cpp：可信L2快照分配与CopyResource。
- Draw3.TransparentPresentation.cpp/.cppm：双缓冲、DComp/ULW与辅助ULW staging/DIB。
- Draw3.InkHistory.cpp/.cppm、Draw3.InkHistoryGpu.cpp/.cppm：CPU拓扑、GPU策略/纹理页/Residency。
- Draw3.InkDocument.cpp/.cppm、Draw3.DrawingController.cpp：页/Canvas/Stroke与工作区、历史、持久化快照。
- Draw3.AutoSave.cpp、Draw3.PresentationAutoSave.cpp、Draw3.WhiteboardStore.cpp：worker快照排队、合并、终态释放。
- Draw3.HistoryProbe.cpp、Draw3.PerformanceProbe.cpp、IdtMain.cpp：已有基准与早退命令。
- Draw3.Host.cpp/.h、Draw3.RuntimeMetrics.cpp、Draw3.ContactInput.cpp、Draw3.DiagnosticOutput.cpp/.h：指标、输入、日志分配门控。

## External References

- 本报告无外部API能力承诺；全部结论来自当前本地源码。工具支持范围由主任务的工具盘点报告单独核实。

## Related Specs

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native-desktop/draw3-integration.md`
- `.trellis/spec/native-desktop/errors-logging-and-resources.md`
- `.trellis/spec/native-desktop/whiteboard.md`
- `.trellis/spec/shaders/cpu-gpu-contracts.md`

## Caveats / Not Found

- 未获GUI/Computer Use授权，未启动主程序、VS、WPA、隐藏窗口或任何测试；无本机运行基线、没有构建通过结论。
- 未调查UI/字体/设置/托管互操作的内存，需与另一份研究合并，不能把Draw3资源估算当整个Inkeys进程。
- 纹理预算不含driver bookkeeping；资源描述载荷不等同物理页/系统提交量；CPU vector reserve也不自动等同全部页已驻留。
- 报告只提出候选，未批准减少功能、撤销深度、页内容或兼容性；所有收益排序仍应由实际存活分配/峰值对照确认。
