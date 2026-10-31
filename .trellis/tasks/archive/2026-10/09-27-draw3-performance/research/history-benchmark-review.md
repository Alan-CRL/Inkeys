# Draw3 集成 HistoryProbe 独立静态审查

- 范围：`Draw3.HistoryProbe.cpp/.h`、`IdtMain.cpp` 早期 CLI、`Inkeys.vcxproj/.filters` 登记，以及集成产品的 `InkDocument` / `InkHistory` API。参照 `history-resource-cost.md` 与 `history-benchmark-design.md`。本审查未运行构建、基准或 GUI；主任务另行提供 Debug/Release | ARM64 Rebuild 与采样证据。
- 判定：这可作为 **64 点、10/100/1000 笔、无 HWND 的集成生产 CPU API 块级计时基线**。本轮改动已使 `private_before` 早于输入深拷贝、摘要覆盖 item bounds 与两类 Tile 坐标，并拆分两个查询计时；静态复核未见阻断三轮窄口径采样的代码或输出格式缺陷。进程内存差值与摘要仍有下述解释边界。

## 接入与覆盖

- `HistoryProbe.cpp:22-23,236-324` 直接导入并调用集成 `ink_document`、`ink_history` 模块；产品模块与 probe 同时登记于 `Inkeys.vcxproj:1030-1035,1084`，头文件和实现也登记在 filters。并未调用 demo 版 history。`IdtMain.cpp:270-288` 对精确 CLI 参数在配置路径、单实例与 HWND 初始化前早退。probe 只构造 CPU 对象、读取时钟/进程计数并打印；没有调用 Host、绘制窗口、D3D、持久化服务或配置。静态审查无法排除整个 EXE 的既有全局初始化副作用。工程是 Windows subsystem（`Inkeys.vcxproj:347`），采样者应显式重定向 stdout/stderr 并检查退出码。
- `HistoryProbe.cpp:43-47,114-138` 构造 10、100、1000 笔各两种场景：全笔与每四笔一支橡皮，均为确定的 64 点轨迹；橡皮与前三笔重合。`RunBlock` 在 `:218-228` 建立两个不同 Page GUID、同一 Workspace GUID 和两个默认设备 Canvas，只向第一页追加。`:242-259` 检查连续 stroke/render 索引及映射；`:263-284` 检查 Undo 数量和 Redo 逆序；`:285-340` 检查新分支清除 redo、保留/可见数量、树计数及第二页未修改。它验证 CPU 历史簿记，不验证橡皮的最终像素。第二页始终为空，没有 10/100/1000 页、SlideID/EndScreen 或交替页面恢复的测试。
- `:196-208,399-407` 的非有限 **运算结果**预检使用有限 `FLT_MAX` 坐标和笔宽，确认当前 `InkStroke::IsValid()` 接受而无 visible fallback 的 `BuildStrokeTileFootprint()` 拒绝。它在计时外运行，不测试 DrawingController 的 append 事务或真实输入可达性。如果将来修复这一边界，当前预检会让基准返回 1，需要同步调整该已知行为断言。

## 计时、摘要与资源口径

- 每场景先 3 个预热块，再 11 个计量块（`:31-33,384-409`），每块重新复制相同输入并建立全新 document/history。`:236-260` 的 `append_ns`、`footprint_ns`、`history_ns` 是每笔各一次调用的累加；`:263-324` 的 Undo、Redo、分支步骤、`visible_query_ns` 与 `decompose_ns` 分别累加。复制输入、建页、资源读取、断言、摘要与打印在各调用计时之外。`append_ns` 包含产品 `InkCanvas::AppendStroke` 内部的 `IsValid()`、移动和 vector 增长，未单独测验证成本。11 个值是**整块总耗时**，可以计算块级 median/P95；不能当作单笔、末尾第 1000 笔或单次 Undo 的尾延迟。
- 所有被计时 API 的结果都参与后续检查、分支或打印（`:242-259,266-340,342-380`），完整 document/history 也被遍历生成摘要（`:147-193,337-340`）。因此没有明显的死代码消除路径。Release 启用 Whole Program Optimization（`Inkeys.vcxproj:69`），代码没有显式编译器屏障；这只是常规时钟包围的微基准，不能从静态源码保证机器指令严格落在每一段时钟之间。若优化候选出现异常大幅变化，应检查 Release 二进制与原始输出。stdout 中 `phase` 和 `block` 区分预热/计量；摘要是跨块一致性指纹，并非独立正确性 oracle。`:342-380` 的 `fprintf` 静态核对通过：12 个耗时、4 个 Tile/piece 数量、5 个状态数量，以及 private/working set/handle 与 digest 的占位符顺序、参数个数和类型对应。
- `:80-99,214,261,313` 读取 Windows 当前**整个进程**的 `PrivateUsage`、工作集和句柄数，失败时有可用标记。`private_before` 现于待转入 Canvas 的点数组深拷贝前读取，所以 `private_append - private_before` 包含该批点数组与 history 分配，也包含分支输入副本、页面及其他夹具对象。它仍不能归属于 InkCanvas/History，也不包含专门的 GPU 资源与进程外资源。CRT 堆保留、场景顺序和前面的大场景可能改变后续块的基线；句柄数平稳只说明此无 HWND 夹具没有可观察的进程级句柄增长，不能证明产品持久化/窗口路径无泄漏。

## 可操作发现

1. **P2 — private bytes 差值是夹具进程增量，不是文档对象体积。** `HistoryProbe.cpp:213-217` 将 `before` 移至 `prepared` 与 `branchStroke` 深拷贝之前；`:238-260` 的 move append 转移点数组后，`private_append - private_before` 现可包含 N 笔 64 点数组与 history/tile 分配。这修复了先前遗漏点数组的口径问题。差值同时包含未追加的 `branchStroke`、Page/Canvas/临时容器及 allocator 的保留/提交行为；`private_branch - private_append` 仍不包含分支笔的点数组初次分配。可比较同场景三轮的**进程级水位与增量**，不可称为精确的文档字节或长期泄漏速率。
2. **P2 — 摘要覆盖范围已扩展，但它仍是确定性指纹。** `HistoryProbe.cpp:169-192` 现混入 item `pixelBounds`、`contentGeneration` 和完整 undo/composition Tile 坐标，先前“同数量不同 undo Tile 仍同摘要”的缺口已消除。摘要尚未混入 `compositionBarrier`、`previousVisibleIndex`、style 颜色/透明度和 tree 内部 generation；`:248-252` 只检查非空，`:337-340` 仅把首个预热块作为同实现后续块的期望值。优化前后比较 digest、数量和断言有价值；需要证明关键边界与 Undo 精确语义时仍需独立 oracle，不能把相同摘要单独当作完整正确性证明。
3. **P2 — 分离的 Tile 查询仍不代表冷回放或页切换。** `:315-324` 分别计时固定 2048px 视区的 `VisibleCompositionTiles`，以及用首个可见 Tile 作参数的全范围 `DecomposeRange(0, itemCount, tile)`。所有 `AppendStroke(..., true)`（`:255,308`）均无 barrier；生产树在全范围且无 barrier 时可以直接产生 CachedNode piece（`Draw3.InkHistory.cpp:855-880`），即使传入 Tile 也是如此。真实 GPU 路径按 Tile 取子范围（`Draw3.InkHistoryGpu.cpp:1429,1528`），还包含缓存命中/失效、绘制和页面恢复。这两个新字段可分别报告 CPU 查询成本，不能据此断言长文档冷回放或交替 PPT 页面成本。
4. **P2 — 当前采样不覆盖资源研究中的若干增长源。** 每块仅做一次 Undo→新笔分支（`:285-312`），没有研究方案所列的 1000 次分支累积；也没有 tap、1024 点慢笔、10/100/1000 页、planner 容量/eviction、快照捕获与保存队列。`retained_items` 与 `stored_strokes` 能说明这一次分支仍保留历史，不支持长期泄漏速率、保存队列背压或 CPU/GPU cache 上限结论。相关项需单独使用生产调用路径测量。

## 采样解释边界

可按同一 Release | ARM64 构建、串行的三轮进程采样保存每块原始行，检查每轮退出码、stderr、`memory_available`/`handles_available`、场景计数与 digest，并计算块级 median/P95 和轮间波动。`visible_query_ns`、`decompose_ns` 与内存三时点须按上述窄口径标注。此 no-HWND probe 不产生 Down→成功 Present、GPU residency、光学延迟、Win7/FL11.0/WARP、DComp/ULW 或 durable UInk 证据；这些仍需各自的目标环境与生产路径验证。
