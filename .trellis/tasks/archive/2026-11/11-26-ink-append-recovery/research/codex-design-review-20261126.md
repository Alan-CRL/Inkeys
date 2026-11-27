**C1.6b 目前有四处会直接丢墨或错装墨迹的阻断点：整页提交判定、增量修订连续性、桌面跨 Clear 冷读、恢复确认时机。**（`design.md:84,98,107–115,149,191`）

## FACTS（源码与规范）

| 范围 | 已核实 |
|---|---|
| UInk | SAVE-6–9 允许向物理末 Canvas 追加完整 Ink、Erase Ink、Shape 等。编号在 Canvas 内连续，空 Canvas 从 `0/0` 开始，多部件 Shape 连续且共用 `undoId`。FILE-8、SAVE-10/12 按完整 Canvas 键整体替代，物理顺序不决定页序。（`uink-v11-spec.md:557–558,581–582,679–694,730–732,1668–1671`） |
| 写入提交 | `Create` 先刷临时文件、发布，再重开，重开失败仍可能已发布。`AppendPage` 先写、Flush，再更新目录，失败回截并再 Flush。`Compact` 替换后重算偏移，保留 Header GUID，重开失败时替换仍已生效。（`uink_store.cpp:430–455,695–708,743–793,814–899`） |
| 输入线程 | `PagedInkStore` 的编码、读写和整理在准入锁外。锁内仍有节点分配、队列扫描、确认发布与批量结算。`Take` 和驱逐许可无条件变量等待，`Drain` 会等，`Close` 会 join。正常退出在 Draw3 的 `PrepareExitAutoSave` 路径确实排空白板和 PPT。（`Draw3.PagedInkStore.cpp:393–493,1000–1009,1041–1098,1108–1165,1213–1236,1316–1356,1373–1384`，`Draw3.DrawingController.cpp:2098–2120,23048–23059,24501–24508`） |
| 场景规则 | §7.1 的桌面、白板询问规则、静默档及手动启动区别正确。PPT 延迟到同文稿识别且保留实际放映页的方向正确。（`prd.md:34–60`，`design.md:153–165`，`Draw3.DrawingController.cpp:2559–2577`） |

## INFERENCES（按风险排序）

1. **blocker｜§5 对象边界上的半页会冒充完整页。** `Canvas + A + B` 只写到 `A` 也能正常结束。若最后一笔是多块 Shape，单次 `WriteFile` 同样可能留下完整首部件。反过来，页已 Flush、JSON 尚未更新时，新提交可能被漏认，`Create` 还可能留下无路径登记的好文件。**改文**：整页与搬页写前持久登记文件身份、起止偏移和校验，恢复在去重前核验，覆盖恰好落在对象边界、无效尾和中段损坏。首次发布前登记候选路径。多块 Shape 明确整组回退，普通单块保留完整前缀。（`design.md:107–115,137`，`uink_store.cpp:444–455,695–708,750–793`，FILE-8/16–19、SAVE-22–24、SHAPE-11/14）
2. **blocker｜§5 `Compact` 会让提交偏移失效。** 整理后末 Canvas 偏移可能变小，随后合法增量撕裂会落进规则未定义的 `<` 分支。**改文**：偏移绑定物理文件代次，替换前登记新旧代次与各自提交范围，恢复按实际文件身份选记录。空闲和强制整理都走该协议，处理 `replacementCommitted` 后重开失败。JSON 临时文件明确 Flush，记录失败时暂停依赖它的后续写入。（`design.md:86,109–115,137`，`uink_store.cpp:846–899`）
3. **blocker｜§4.3 确认修订必须证明磁盘包含此前所有改动。** A 准入失败、B 成功后确认 B，会把只含 B 的页当成干净页。`MergeCandidate` 也可能选中同页 Append，或跨整理屏障重排。Clear 失败后，Append 可能接到上一区间。Full 若拿着旧的 `previousIntervalOnDisk`，还会被精确边界检查拒绝。**改文**：Append 携带基准和目标修订、槽代次、边界代次、lane；基准失配时保持脏并安排 Full。Append 保留既有 Clear 前缀身份、`clearContentId` 和降级状态，真实重写前缀才推进其身份。Full 合并遇 Append、Clear、整理屏障即停止。每个准入只结算一次，按现有 pending、健康与降级谓词驱逐。离场整理排在回执补发的最终 Full 之后。（`Draw3.PagedInkStore.cpp:410–448,799–837,982–1013`，`Draw3.DrawingController.cpp:795–857,1034–1048,1890–1920,2498–2536`）
4. **blocker｜§4.4 桌面冷读原样复用会失败。** `LoadSnapshot` 检查去重前 `canvasCount == 1`，并要求 Header GUID 等于 `saveRequestId`。多版本文件即使只剩一个逻辑页也会被拒，整读另受 128 MiB 上限。**改文**：在 `DesktopInkSnapshots` 增加已封存区间登记，按确认范围调用 `ReadUInkPage`，复用 `ImportUInkV11Page`。保留页 GUID、Clear 代次和历史修订校验，最终修订未确认时保留热副本。Undo Clear 恢复旧区间时同步改活动区间指针，Redo Clear 封存该区间并清空指针，停放时随 `desktopSlot` 保存。启动恢复另走安装入口，跨 Clear 的 `PrepareClearUndoFromSnapshot` 会替换当前页并建立 Clear 历史。（`Draw3.DesktopInkSnapshots.cpp:413–466,675–695`，`uink_store.cpp:931–955,981–1008`，`Draw3.DrawingController.cpp:924–972,23088–23100,23700–23709,23738–23754,24398–24426`）
5. **blocker｜§6–8 提前 `handled` 会吞掉恢复。** 用户选“是”后、安装前崩溃，或 PPT 尚未识别便退出，旧数据会失去恢复入口。`恢复中` 永久跳过也有同样结果。弹窗还占用了存储 worker 等待写提交点的同一线程。**改文**：提示与会话写入分开，提示可在退出时取消。按场景在新文件提交并收到 Draw3 安装确认后结算，用户明确拒绝时结算对应场景。PPT 待恢复引用跨启动保留到 Adopt 成功。恢复占用随进程释放，保留未完成条目与其来源。五分钟抑制只限制再次拉起。（`design.md:137–149,183,189–197`，`Draw3.Host.cpp:3236–3284`，`MessageBox.cpp:277–298`，`Helper.CrashHandler.cpp:463–470`）
6. **should-fix｜§4.2/7.2 原样搬移有明确适用条件。** 同文件搬已确认、完整且从零编号的页段符合 SAVE-7/10/12。现有 `CopyRange` 只定位源一次，直接传同一 HANDLE 会共用文件位置。跨文件复制可作为同一批页面的另存，保留显式 Workspace/Device、父引用和注册顺序，隐式项继续省略 GUID；同一逻辑文件则须保留 Header GUID。**改文**：限定本产品拓扑、用完整 Canvas 键验证，读写分离文件位置，拒绝临时身份和损坏范围。另存重算 Header 统计和活动页。稀疏白板、双 PPT lane 的 `pageIndex` 不能一边原样保留一边宣称符合紧凑文件规则；保留绑定语义并明确 C1.4 的物理整理限制，严格整理另需映射，尤其禁止改写顺序绑定页码。（`uink_store.cpp:270–287,729–733,842–855`，C1.4 `design.md:329–333`，SAVE-2/16/18、HEXT-2–5/9–10、HDR-3/4、VER-7、CANVAS-1–5/13–14）
7. **should-fix｜§4.1/9 输入延迟承诺缺实现约束。** 单笔复制仍随点数增长；Up 的诊断还扫描 `active`；无接触 150 ms 后启动整页复制，下一次 Down 仍可在复制期间到达。**改文**：从 `storedStroke` 取单笔，使用有界交接，队满按第 3 条补写；会话事件也走非等待交接。诊断仅采定长值，格式化及扫描移出热路径。Full 采用有预算、可让出且带修订校验的截取；任何普通输入、离场、恢复入口都不调用 `Drain`。现有退出等待单列。（`Draw3.DrawingController.cpp:1766–1796,26854–26885,26985–27007`，`Draw3.PagedInkStore.cpp:453–493,1326–1356`，`prd.md:9–10,25,65`）
8. **should-fix｜§6.1 存活探测和恢复占用是两件事。** `FILE_FLAG_DELETE_ON_CLOSE` 的删除取决于所有句柄关闭，探测还受访问及共享模式影响。“打不开”不足以区分在用与权限错误。现有提前交接是普通重启，更新与崩溃走父退出合同。**改文**：先用 `CREATE_NEW` 获得非继承独占锁，再发布 JSON；创建失败停用本次恢复记录。用明确访问与共享参数探测，遇权限或 I/O 错误暂缓。多实例对同一旧会话另做独占认领，锁释放后复查待定记录。（`design.md:126,142–149`，`IdtSingleInstance.h:145–189`，`ShutdownSupervisor.cpp:615–664,669–693`，[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)）
9. **blocker｜§7.3 白板冷安装缺两个入口。** 按 `pageIndex` 排列后直接压缩列表会违反冷读的索引检查；进入白板只交换槽并重绘，设当前页后发同页 `SetPage` 会提前返回。**改文**：重建 `InkCanvasCollection` 和对应的 runtime、主题、页状态，保留旧 GUID 与页号空洞，缺页填空壳并限额；同时初始化 `PagedInkStore` 的 writer 目录和 `ConfirmedPage`。通过 `RequestWhiteboardActive` 走现有准入与 UI 交接，在进入分支显式加载冷的目标页，成功后才发布 ready。（`Draw3.InkDocument.cpp:263–302`，`Draw3.DrawingController.cpp:1923–1943,22863–22889,24318–24352,24734–24793`，`IdtState.cpp:1753–1769`）
10. **blocker｜§7.3 PPT Adopt 必须先于首次 lane 判空。** `LoadLane` 遍历 `confirmed`，空结果会确认空槽；`Host` 懒建还会生成新 Workspace GUID。**改文**：在 `Draw3.Host.cpp` 的 `Get` 传入恢复描述，在存储 worker 先 Adopt 并初始化目录、Workspace、确认边界及两个 lane 的元数据，完成后才放行首次 `LoadLane`。保存页序 lane 的可信旧页数。沿用路径 key 和 lane，SlideID 缺失进入保留页。路径改名或同路径换文稿不能由现有 key 判定同一文档，写明适用范围。（`Draw3.Host.cpp:118–142`，`Draw3.PagedInkStore.cpp:354–365,590–614,734–755`，`Draw3.DrawingController.cpp:1062–1072,24122–24138`，`Draw3.Presentation.cpp:135–172`，`Draw3.PresentationPages.cpp:80–111`）
11. **should-fix｜§3/6/7 统一开关与启动竞争。** §3 重新开启时立即建桌面文件，并要求关闭启动时处理旧记录，与首笔条件和关闭沿用 C1.4 冲突。提示期间若允许书写，预复制的旧桌面文件会漏掉新笔，直接装入白板也可能覆盖新页。**改文**：写明 `CrashRecoverInk` 与 `AutoSave.Enable` 真值表，关闭走原门控，开启后的正常 Clear 只退休区间，首笔抬起才建；关闭启动不批量改旧记录。启动门禁后先登记待恢复 PPT，防止首次 lane 抢跑；安装按场景及文档代次校验。桌面按约定 Z 序合并后异步写完整版本再确认。白板有新编辑时保留旧恢复待办，禁止覆盖。记录白板当前位置时包含空页索引，PPT 页记录只作诊断，不驱动跳页。（`prd.md:7–11,43–60`，`design.md:35–41,130–137,179–183`，`IdtMain.cpp:4292–4298`，`Draw3.DrawingController.cpp:23088–23100,23700–23709,24122–24138`）

## UNKNOWN / 需运行验证

| 优先验证 | 注入点 |
|---|---|
| 提交协议 | 每个合法对象边界断写、Flush 后 JSON 前、`Create` 已发布但重开失败、`Compact` 替换后重开失败。验证完整旧版、新版和多块 Shape 的选择。（`uink_store.cpp:695–708,750–793,860–899`，`design.md:201–205`） |
| 输入与恢复竞态 | 953 笔重页 Undo 后立即 Down、慢盘、队满、弹窗期间落笔和进白板、PPT 先于扫描命中、安装一半再次崩溃。分别检查输入尾延迟和 C1.6a 的 15 秒看门狗。（`design.md:57–62,181–183,209–212`，C1.6a `design.md:82–101`） |
| Windows 交接 | 两实例同时认领、锁创建或探测失败、强杀、普通提前重启、更新重启，实测锁可见性、错误码及旧进程释放后的复查。（`design.md:126,138–149`，`ShutdownSupervisor.cpp:615–693`） |

本轮只读核对，未修改文件、运行编译或执行运行时测试。