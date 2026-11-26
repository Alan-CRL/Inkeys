# PPT 与桌面画布的墨迹读取、翻页与保存链路（2026-11-24 只读调查）

行号以调查时为准。`DC` = `Draw3.DrawingController.cpp`，`PAS` = `Draw3.PresentationAutoSave.cpp`。标【码】为已在代码中核实，【推】为推断。

## PPT

- **何时读盘**【码】：只在一个放映 lane 第一次进入（或被淘汰的 lane 重新进入）时读盘；普通翻页不读盘。跨 Clear 撤回时另有一次 `PreviousInterval` 加载。
- **读什么**【码】：后台线程 `LoadPresentation` 用 `ReadUInkFile` 读**整份多页文件**，revision 含整文件 SHA-256，再与 JSON 索引里的 `sourceRevision` 比对；随后在后台导入全部页并逐页投影区间。`PreviousInterval` 也读整份、导入全部页后只取一页。
- **没有白板那份重复的历史 JSON**【码】：PPT 只存可见笔画尾段与区间序号。
- **安装**【码】：绘制线程 `MaterializePresentationSlot`（DC:2173）一次性物化**整份演示所有页**，逐笔追加、复制点列、`BuildStrokeTileFootprint`、追加历史；之后 `InstallLoadedActivePresentationSlot` 交换，再 `restoreSwitchedDocument` 全量 GPU 重建。
- **翻页**【码】：走 `SetPresentationTarget`，不走 `SetPage`。所有页常驻内存，只改当前页序号。但**同一 lane 内翻页也会丢弃热前像与合成缓存**（`restoreAfterDocumentSlotSwitch`，DC:21510），每次翻页都冷重建可见瓦片。
- **翻页时保存**【码】：每次翻页都调用 `capturePresentationAutoSave`；只要有任意一页改过，就在绘制线程用 `BuildPresentationSaveRequest` **深拷贝全部页的全部可见笔画点**。后台 `SavePresentation`：重读整份文件并做 SHA → 导入、合并、导出、编码 → 再算 SHA → 写临时文件 → 自读一次（再解码一次并 SHA）→ 写 JSON 索引。保存与加载在同一个后台队列里串行【推：会推迟后续加载】。
- **现有计时**：`TracePptTiming`（`INKEYS_PPT_TIMING=1` 开启）已有 `document_switched`、`canvas_presented` 打点，但没有分段。

## 桌面画布

- 翻页走与白板相同的 Next/Previous/SetPage 处理器的非白板分支：不读盘、不驱逐，**保留合成缓存**，只做 `resetGpuForPageSwitch` + `restorePageContent`。
- 只有跨 Clear 撤回恢复时读盘（`Draw3.AutoSave.cpp` 的 `ReadUInkFile` + `ImportDraw3UInkDocument`，安装在 `restoreSnapshot`）。

## 三处的共享与重复（统一封装的候选）

- 已共享：UInk 编解码与导出、区间投影、`BuildStrokeTileFootprint`、`restorePageContent` / `resetGpuForPageSwitch`、命令队列回投。
- 重复：
  - 笔画物化三处几乎相同：`MaterializePresentationSlot`、`materializeCanvasPage`、白板 `RestoreWhiteboardHistoryUnchecked`。
  - 三套后台服务（PresentationAutoSave、DesktopAutoSave、WhiteboardStore）各自实现队列、完成回调与排空。
  - 读取接口不一致：白板按页读（`ReadUInkPageFile`），PPT 与桌面整文件读（`ReadUInkFile`）。
  - 翻页时的 GPU 缓存策略三种：白板丢弃（翻到冷页本来就要重建）、PPT 同 lane 也丢弃、桌面保留。
  - 保存门控不同：白板按页记修订，PPT 按文档记修订并做全量快照。

## 计时插入点（如需补 PPT 计时）

起点在 `SetPresentationTarget` 处理处（DC:23065）并包住 `capturePresentationAutoSave`；`activePresentationLoadPending` 时记请求；后台 `PresentationPersistenceCompletion` 增加 queue/read/import 字段；完成到达（DC:22777）记 readyMs；物化（DC:22953）与安装（DC:22979）分别计时；`restoreSwitchedDocument()`（DC:23001）前后记 GPU；沿用现有 `pageLoadTrace` 在首次呈现后输出。
