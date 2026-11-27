# C1.6b 设计：抬笔异步追加写入与崩溃恢复（第 2 版）

依据：`prd.md`（含两轮用户确认）、`research/codex-write-chain-20261126.md`、C1.4 归档 `../archive/2026-11/11-24-uink-1-1-adaptation/design.md`（下称“C1.4 设计”）、规范副本 `research/uink-v11-spec.md`。Codex 对第 1 版的审查见 `research/codex-design-review-20261126.md`，处理记录见 §12。依赖 C1.6a 的 `Application.CrashRecoverInk`、`TeachingSafetyMode` 与 `-CrashTry`。行号以 2026-11-26 为准，实施前复核。

路径缩写：`UK\` = `inkStrokeModelerTest/draw3/`，`D3\` = `Inkeys/Inkeys/Drawing/Draw3/`，`DC` = `D3\Draw3.DrawingController.cpp`。

## 1. 目标与边界

| 做 | 不做 |
|---|---|
| 开关开启时，三场景每次抬笔后异步追加；不能追加的操作异步整页追加 | 改变开关关闭时的 C1.4 行为 |
| 回翻页上落笔时把该页搬到文件末尾；离开白板或 PPT 时正序整理 | 旧文件清理（沿用 C1.4 保留策略） |
| 会话记录：文件清单、写前登记、当前场景与页、正常结束标记 | 跨机器、跨安装目录恢复 |
| 崩溃重启或非正常结束后按场景规则恢复 | 无路径 PPT、改名或移动后的 PPT（现有 key 判定不了是同一文档） |
| 诊断格式化移出绘制线程 | 改撤回语义；放宽 C1.4 的驱逐条件 |

## 2. 总体结构

```
绘制线程（抬笔 / 撤回 / Clear / 翻页 / 切场景）
   │ 只做：从已提交的 storedStroke 取单笔快照，或有预算地截取整页 → 有界入队
   ▼
L4 场景服务（各自 worker）
   PagedInkStore（白板、每份 PPT）      DesktopInkSnapshots（桌面，新增“活动区间文件”）
   │ AppendContent / AppendPage / RelocatePage / Compact，写前登记
   ▼
L2 uink_store：UInkPagedFileWriter（新增 AppendContent、RelocatePage）＋ ScanUInkPagedFile
   ▲
RecoverySession（新，Host 持有）：会话记录 worker ＋ 独立的弹窗线程
```

## 3. 开关

### 3.1 去掉“启用桌面画布自动保存”（用户确认，2026-11-26 第三轮）

- 删除设置项“启用桌面画布自动保存”（`Setting.cpp:3898–3913` 的 `ToggleRow`、i18n `SettingsUI.Configuration.CanvasSave.Enable/EnableE`）及配置字段 `Drawing.AutoSave.Enable`（`Other.Config.cppm:545–547`）。桌面画布**始终保存**；以后会提供“按天数自动清理”选项。
- “绘制 → 保存”子页保留，内容直接留空（与“显示器”页一样），不删子页、不加说明。设置侧栏结构不变。
- `WindowController::AutoSaveEnabled()`（`Draw3.WindowControl.cpp:1775`）及调用点（`DC:23093,23708`）改为恒真或删除门控；`Bar.Presentation.Test.cpp:629` 同步。

于是桌面只看 `CrashRecoverInk`：

| CrashRecoverInk | 桌面行为 |
|---|---|
| 关 | C1.4 行为（按 AutoSave 开启的路径）：Clear 与退出时写快照 |
| 开 | 活动区间文件；Clear 时文件保留，作为历史与跨 Clear 撤回的冷读来源（取代快照） |

白板与 PPT 本来就不受 AutoSave 门控，只看 `CrashRecoverInk`。

### 3.2 运行中切换

- 每个场景操作入口读一次原子量，不缓存。
- **开 → 关**：停止追加；会话记录写 `recoverInk=false`（扫描时视同正常结束）；桌面回到 C1.4 行为。
- **关 → 开**：
  - 桌面当前区间**非空**时，立即 `Create` 活动区间文件写入当前区间；为空则等 Clear 后首笔（与“不建空文件”一致）。
  - 白板、PPT 的脏页按 C1.4 整页写入一次（复用离页保存入口），之后逐笔追加。
  - 历史上被放弃的会话不在此时处理，等下次启动扫描。
- **启动时开关关闭**：不扫描、不修改任何旧会话记录。

## 4. 写入路径

### 4.1 绘制线程交接（不得延迟下一次落笔）

| 事件（`DC` 位置实施前列全） | 交出的数据 | 请求 |
|---|---|---|
| 普通抬笔成功（`CommitRuntimeStoredStrokeCpu(...NormalUp...)` 成功后，`DC:26854–26870`） | 从已提交的 `storedStroke` 生成 `Draw3UInkStrokeSnapshot`（不再扫描 `active`） | `Append` |
| 多笔构造图形安装成功（`Draw3.ShapeRuntimeActions.inl:459–467,490–495`） | 全部部件快照，同一撤回组，首部件带 `object` | `Append`（一个请求） |
| 撤回、重做、已有图形移动/编辑/删除、Clear 撤回/重做、背景与白板视口变化 | 整页快照（§4.2） | `Full` |
| Clear | 沿用 C1.4：“上一区间 + Clear” | `Clear` |
| 翻页、切场景 | 当前页身份 | 会话事件（§6） |

**每个请求携带修订身份**：基准修订（改动前页修订号）、目标修订（改动后）、槽代次、边界代次、lane。worker 和回执据此判定“磁盘是否包含此前所有改动”（§4.4）。

**有界交接**：
- 准入沿用 `PagedInkStore::Submit`。锁内只做节点入队，不做合并扫描：`Append` 不参与 `MergeCandidate`；`Full` 的合并扫描在遇到同页 `Append`、`Clear` 或整理屏障时停止（锁内为 O(1) 摊还）。
- 每个文件设队列上限（初值 256 个未执行请求）。超限时这一次不入队，把该页标记为“需整页补写”，等下一个自然时机（下一次抬笔、离页、空闲）提交一次 `Full`。
- 会话事件同样非等待：写入 `RecoverySession` 的“最新状态”槽（原子交换），由其 worker 合并写出。
- 任何普通输入、离页、恢复入口**都不调用 `Drain`**。退出流程的排空沿用现状（C1.6a 已由 15 s 监督兜底）。

### 4.2 整页快照的预算

撤回、重做等需要整页快照，现状 Clear 和离页也在绘制线程上复制整页。新增的触发点更频繁，因此：

- 实施前先测 953 笔真实重页 `CaptureCanvasTail` 的 Release / Debug 耗时。
- 截取放在“无接触且输入队列为空”的时机，并做成**可让出**的：每复制一批笔画检查一次输入队列，有新输入就保存进度、让出；输入结束后若页修订未变则继续，变了就重新开始。同页多次撤回只截取最后一次。
- 截取完成时校验页修订仍等于目标修订，不等则丢弃，等下一次。

### 4.3 L2 新接口

| 接口 | 行为 |
|---|---|
| `AppendContent(blocks)` | 只追加到文件中最后一个 Canvas（SAVE-7）。`contentId` 接 `directory.nextContentId`；新撤回组 `undoId = lastUndoId + 1`，图形部件连续且共用一组（SAVE-8、SHAPE-11）。一次 `WriteFile` + `FlushFileBuffers`；失败时截断回写入前末尾，截断失败则封闭写入器（与 `AppendPage` 同一协议）。成功后更新页段长度、尾编号、文件长度与确认修订。追加后超过单页上限（32 MiB）时拒绝，由 L4 转为 `Full`（仍超限则该页降级、写 Error） |
| `RelocatePage(pageGuid)` | 把这一页**已确认、完整、编号从 0 开始**的最新页段按字节原样追加到文件末尾，使其成为最后一个 Canvas（FILE-8、SAVE-7/10/12）；旧页段计入被替代字节。读写使用各自独立的文件位置（现有 `CopyRange` 共用同一句柄的位置，不能直接复用）。拒绝临时身份页和损坏范围 |
| `ScanUInkPagedFile(path, limits)` | 恢复用的**有界流式扫描**：不整读，逐个对象前进。输出每页的全部完整页段（不只最后一个，§5 回退需要上一版本）、最后一个 Canvas 的键与编号、尾部是否截断、安全追加位置（SAVE-22）、中间损坏位置（SAVE-24）。上限：对象数、单页字节 32 MiB、单页点数（沿用 C1.4 导入预算）。只读 |

### 4.4 PPT 与白板

worker 每次出队都**重新确认**文件中的最后一个 Canvas，因为其他整页写入和整理都可能改变它。

| 情况 | 做法 |
|---|---|
| `Append`，基准修订 ≠ 该页已确认修订（前面有请求失败或未准入） | 不追加；该页保持脏，安排一次 `Full` |
| `Append`，目标页是最后一个 Canvas | `AppendContent` |
| `Append`，目标页在文件中但不是最后一个（回翻后首次落笔） | 先 `RelocatePage`，再 `AppendContent`。这就是“把当前页放到最后”：只搬这一页，代价只和这一页有关；旧页段留给离开时的整理 |
| `Append`，目标页还没写过（新页） | `AppendPage`：新 `pageGuid` 的 Canvas，内容就是这一笔（SAVE-13） |
| `Full` | C1.4 整页 `AppendPage`；同页同分段的合并遇到 `Append`、`Clear` 或整理屏障即停 |
| `Clear` 失败后的 `Append` | 基准修订不匹配，按第一行处理，不会接到上一区间后面 |

- **Clear 前缀身份**：`Append` 不改变页的 Clear 前缀身份（`boundary`、`clearContentId`、“上一区间在盘上”、降级状态），只推进确认修订；只有真正重写前缀的 `Clear` 或 `Full` 才推进前缀身份。这样 C1.4 的跨 Clear 拼接和边界校验继续成立。
- **回执与驱逐**：每个准入只结算一次。确认修订推进后，驱逐仍走 C1.4 的同一谓词（已确认、无 pending、文件健康、未降级、非当前页、非加载目标）。
- **离开白板 / 结束 PPT 放映 / 退出时仍在其中**：先补发需要的最终 `Full`，再排一个 `CompactOrdered`。现有 `Compact` 已按 `(pageIndex, writeSequence)` 正序复制最新页（`uink_store.cpp:824–858`），这里只是不受空闲阈值限制。整理是物理整理：保留各页原 `pageIndex`（稀疏白板与两个 PPT lane 可能重叠或有空洞，C1.4 设计 §5.2 裁定 11），不追求规范意义上的紧凑页序，不改写顺序绑定的页码。
- **翻到新页**：不整理。

### 4.5 桌面活动区间文件

| 时机 | 做法 |
|---|---|
| Clear 之后 | 不建文件 |
| Clear 之后第一笔抬起 | 写前登记候选路径（§5.1），`Create` 新文件（单页、隐式 Workspace），首页内容就是这一笔 |
| 之后每次抬笔 | `AppendContent` |
| 撤回、重做、图形编辑 | `Full`：`AppendPage` 同一页新版本 |
| Clear | 封存当前活动区间文件（关闭写入器），登记为“已封存区间”（路径、页 GUID、最后确认的页段范围、Clear 代次、历史修订）；文件保留（§3.1） |
| Undo Clear | 恢复旧区间时，活动区间指针改回该区间的文件，之后继续追加 |
| Redo Clear | 封存该区间，清空活动区间指针 |
| 停放（进入 PPT / 白板） | 活动区间指针随 `desktopSlot` 一起保存 |
| 退出 | 区间已在文件里，不再写退出快照 |

**跨 Clear 冷读不能原样复用现有 `LoadSnapshot`**：它要求整读后 `canvasCount == 1`，且文件 Header GUID 等于保存请求 ID（`Draw3.DesktopInkSnapshots.cpp:413–466`），多版本文件会被拒，整读还受 128 MiB 上限。改为：对“已封存区间”按登记的确认范围调用 `ReadUInkPage`，再复用 `ImportUInkV11Page`；保留页 GUID、Clear 代次和历史修订校验；最终修订未确认时保留热副本，不释放内存。

## 5. 写前登记与截断判定

### 5.1 问题

只有最后一次写入可能残缺。残缺可能恰好停在对象边界上：例如整页 `Canvas + A + B` 只写到 `A`，文件结构看起来完整，扫描器察觉不到。多部件图形的单次写入也可能只留下完整的首部件。仅凭文件结构无法判断。

### 5.2 协议

会话记录对每个文件保存一份**写前登记**：

```json
"pending": { "fileId": "<NTFS FileId>", "kind": "page|create|relocate|shapeGroup",
             "start": 123456, "end": 234567 }
```

- **整页类写入**（`Create`、`AppendPage`、`RelocatePage`）和**多块图形追加**：worker 先把 `{fileId, start, end}` 写进会话记录（临时文件 → `FlushFileBuffers` → 原子替换），完成后才写数据。数据写完并 Flush 后不必再更新记录：下一次写前登记会覆盖它。
- **单块追加**（普通一笔、橡皮）不登记。残缺的单块必然是不完整对象，扫描器能察觉，丢掉即可。
- `Create` 在发布前就登记候选路径与 `end`。发布成功但重开失败时，文件仍可被恢复找到。
- **会话记录写失败**：暂停该文件后续依赖登记的写入（页保持脏），写 Error；下次写入时重试登记。

### 5.3 文件身份与整理

`Compact` 用新文件替换旧文件，所有偏移都会变化。会话记录用 NTFS FileId（`GetFileInformationByHandle`）标识物理文件：

- 整理替换成功后，worker 在处理该文件的下一个请求前，先写入新的 FileId，并清空 `pending`。
- 恢复时，如果文件实际的 FileId 与记录不符（整理替换后、记录更新前崩溃），说明这是一个完整写好的整理结果，没有未完成的整页写入，只按单块规则处理尾部。

### 5.4 恢复判定（按页，在去重之前）

对每个文件，用 `ScanUInkPagedFile` 的结果：

| 情况 | 处理 |
|---|---|
| FileId 匹配，`pending` 存在，实际有效长度 < `pending.end` | 从 `pending.start` 起的页段（或图形组）未写完：整段丢弃。整页类 → 该页回退到上一个完整页段；新页首个 Canvas → 该页不恢复；图形组 → 整组丢弃，保留之前的内容 |
| FileId 匹配，有效长度 ≥ `pending.end` | 该次写入完整；之后的单块残缺照常丢弃 |
| FileId 不匹配 | 视为整理后的完整文件，只处理尾部单块残缺 |
| 中间损坏（SAVE-24） | 只取损坏点之前的完整页段，写 Warn |

## 6. 会话记录 `RecoverySession`

### 6.1 文件与存活锁

```
Inkeys\Save\Ink\session\<YYYYMMDD-HHmmssfff>_<pid>_<guid8>.lock   存活锁
Inkeys\Save\Ink\session\<同名>.json                                 会话记录
```

- **先拿锁再发布记录**：启动时用 `CREATE_NEW`、`FILE_FLAG_DELETE_ON_CLOSE`、共享模式只给 `FILE_SHARE_DELETE`、句柄不可继承，创建 `.lock` 并在进程生命周期内持有。创建失败则本次不写会话记录、不追加（写 Warn），等同开关关闭。
- **存活探测**（扫描方）：对 `.lock` 以明确的访问与共享参数尝试打开：
  - 文件不存在 → 原进程已结束；
  - `ERROR_SHARING_VIOLATION` → 原进程存活，跳过；
  - 权限或 I/O 等其他错误 → 不确定，本次暂缓（不恢复、不结算），下次启动再看。
- **恢复认领**（防止允许多实例时两个新进程同时恢复同一旧会话）：恢复方先以 `CREATE_NEW` 创建 `<旧会话名>.claim` 并持有（同样 DELETE_ON_CLOSE），创建失败说明已被别的进程认领，跳过。认领随进程释放，未完成的恢复下次还能被认领。

### 6.2 记录内容与写入

```json
{ "version": 1, "pid": 1234, "startTime": "...", "recoverInk": true, "clean": false,
  "workspace": "desktop|whiteboard|presentation",
  "desktop":    { "path": "...", "fileId": "...", "pending": null },
  "whiteboard": { "path": "...", "fileId": "...", "pending": null,
                  "currentPageGuid": "...", "currentPageIndex": 3 },
  "presentations": [ { "key": "...", "path": "...", "fileId": "...", "pending": null } ],
  "settled": { "desktop": false, "whiteboard": false, "presentations": [] } }
```

- 写入：worker 合并最新状态，临时文件写完后 `FlushFileBuffers`，再 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`。
- 触发：文件创建、写前登记（§5.2，worker 等它写完才写数据）、整理后更新 FileId、切场景、白板翻页（含空页的索引）、开关切换、恢复结算、正常结束。PPT 当前页只作诊断记录，不驱动恢复时跳页。
- **正常结束**：退出流程中三个存储服务排空并关闭之后，写 `clean: true`，再释放存活锁。被 15 s 监督强退的退出写不到这一步，下次启动按非正常结束处理（C1.6a design §3.8）。

## 7. 启动扫描与结算

在启动门禁完成、Draw3 Host 就绪之后：

1. **先登记待恢复 PPT，再放行 PPT**：扫描在 Host 创建任何 PPT 存储之前完成“哪些 key 有待恢复数据”的登记（只读会话记录，很快）。在此之前识别到的 PPT，首次 `LoadLane` 等待登记完成（有界，超时按无恢复处理）。
2. 列出 `session\*.json`，跳过 `clean`、`recoverInk: false`、存活锁仍被持有、已被认领、全部场景已结算的记录。
3. 只恢复**最新的一个**被放弃会话。较旧的被放弃会话全部直接结算（含其中的 PPT 条目）。
4. 认领选中的会话，对各文件执行 `ScanUInkPagedFile` 与 §5.4 判定。
5. 判断桌面、白板“有无墨迹”，按 §8.1 决定弹窗或静默。
6. **按场景结算**，在以下时机把对应场景写为 `settled`：
   - 新进程已把该场景的数据写进自己的文件并确认落盘，**且** Draw3 回报装入成功；或
   - 用户对该场景明确选“否”。
   
   在此之前崩溃，下次启动仍能恢复。
7. **PPT 待恢复条目只在下一次启动有效**（用户确认，2026-11-26 第三轮）：只有处理该会话的这个进程会在识别到同一文稿时 `Adopt`。本进程结束（正常或异常）时，未被认领的 PPT 条目一律结算丢弃，不转入以后的启动；旧 `.uink` 文件仍留在磁盘上。

## 8. 恢复

### 8.1 规则（用户确认，prd“按场景的恢复规则”）

| 场景 | 是崩溃场景且有墨迹 | 不是崩溃场景但有墨迹 |
|---|---|---|
| 桌面 | 弹窗询问；选“是”恢复并显示 | 静默恢复 |
| 白板 | 弹窗询问；选“是”恢复并进入白板，停在记录的页 | 静默恢复数据，界面留在桌面 |
| PPT | 不弹窗；登记待恢复，再次识别到同一文稿时按页码恢复，不跳页 | 同左 |

- **跳过弹窗**：带 `-CrashTry` 且 `TeachingSafetyMode == 1` 时不弹窗，按“是”处理。非正常结束后手动启动时，不论哪一档都弹窗。
- **弹窗**：`Inkeys::UI::MessageBox` `Buttons::YesNo`，无 owner、置顶，在**独立的弹窗线程**上显示，不占用会话记录 worker（worker 还要服务写前登记）。正文给出场景和最后保存时间（取该场景文件的最后写入时间，日期不是今天时带日期），文案进 i18n。进程退出时取消弹窗，场景保持未结算。
- **选“否”**：只结算该场景；其他场景照常按上表处理。

### 8.2 “读旧写新”

恢复不续写旧文件（C1.4：每个进程只写自己的文件）。新进程把旧文件各页的最后完整版本复制到自己的新文件，视为**同一批页面的另存**：

- 适用于本产品的拓扑：白板和 PPT 是显式 Workspace + 隐式 Device，桌面是隐式 Workspace + 隐式 Device。新文件沿用旧文件的 Workspace GUID 与 `hostId`，保留 Header Extension 中解析页所需的上下文与注册顺序；隐式项继续省略 GUID（HEXT-2–5、HEXT-9/10）。
- Header 的文件 GUID 新生成，Header 统计和当前页按实际内容重新计算（HDR-3/4）。
- 页段按字节原样复制，用完整 Canvas 键校验；拒绝临时身份和损坏范围；读写使用独立的文件位置。
- 保留各页原 `pageIndex`、`slideId`、`binding`、`pageState`、`pageKind`；与 §4.4 的物理整理一样，不追求紧凑页序。

### 8.3 各场景装入

| 场景 | 装入方式 |
|---|---|
| 桌面 | 新进程创建自己的活动区间文件（复制旧区间页段），确认落盘后，Draw3 新增“装入桌面区间”入口：用 `ImportUInkV11Page` 把区间装成当前可见历史，逐笔可撤回。**不复用** `PrepareClearUndoFromSnapshot`（它会替换当前页并建立 Clear 历史）。若用户在弹窗期间已经落笔，恢复内容按“在上方”合并，然后提交一次 `Full` 写出合并后的完整版本，确认后才结算 |
| 白板 | 只在新进程的白板会话仍为空（尚未进入或未编辑）时装入；已有新编辑则不覆盖，白板场景保持未结算（下次启动还能恢复），写 Warn。装入步骤：① 白板 `PagedInkStore` 以“恢复描述”创建：worker 先复制页段，初始化 writer 目录与各页 `ConfirmedPage`（含 Clear 前缀身份）；② Draw3 新增“装入白板会话”入口：重建 `InkCanvasCollection` 及对应的 runtime、主题、页状态，保留旧页 GUID 与页号空洞（缺页填空壳，数量设上限），各页处于冷状态（与被驱逐页相同），不能按 `pageIndex` 压缩列表，否则违反冷读的索引检查（`Draw3.InkDocument.cpp:263–302`）；③ 崩溃场景选“是”时，经 `RequestWhiteboardActive`（`IdtState.cpp:1753–1769`）走现有准入与 UI 交接；进入分支里显式加载冷的目标页（不能依赖同页 `SetPage`，它会提前返回），加载成功后才发布 ready |
| PPT | `Draw3.Host.cpp:118–142` 的懒建 `Get` 接收恢复描述：存储 worker 先 `Adopt`（复制页段，沿用旧 Workspace GUID 而不是新生成），初始化目录、确认边界、两个 lane 的元数据与页序 lane 的可信旧页数，完成后才放行首次 `LoadLane`（否则 `LoadLane` 看到空的 `confirmed` 会直接确认空槽，`Draw3.PagedInkStore.cpp:590–614`）。之后完全等同“本进程之前放映过这份文稿”：按 `slideId` / 页码匹配，SlideID 找不到的页进保留页，不跳页 |

撤回粒度沿用 C1.4 冷读规则。

## 9. 失败与降级

- 写入失败：沿用 C1.4（页保持脏，写 Error；写入器封闭后该文件不再追加）。会话记录标记该文件“尾部未确认”。
- 恢复读取失败或超出预算：该场景不恢复，写 Warn，保持未结算（下次还能再试）；连续 3 次启动都失败则结算并写 Error，避免永远弹窗。
- 恢复过程中再次崩溃：未结算的场景下次仍可恢复；认领锁随进程释放。C1.6a 的“5 分钟内再崩溃不再拉起”只限制自动拉起，不影响下次手动启动时恢复。

## 10. 线程约束

- **绘制线程**：单笔快照（来自 `storedStroke`）、有预算可让出的整页截取、有界入队、写诊断数值记录、写会话事件槽。不读盘、不编码、不等 worker、不 `Drain`、不持阻塞锁、不拼字符串、不扫描 `active`。
- **场景 worker**：读盘、编码、写盘、整理；写前登记时等待会话记录 worker。
- **会话记录 worker**：会话记录读写、启动扫描、页段复制。
- **弹窗线程**：只显示弹窗、回报选择。
- **诊断**：`[Draw3Diag][lift-path]`、`[page-save]`、`[page-load]` 改为绘制线程只写定长数值记录到无锁环形缓冲（满则丢弃并计数），由日志 worker 格式化。

## 11. 测试

- **L2 单元测试**：`AppendContent` 编号、图形同组、超限拒绝、失败回截与封闭；`RelocatePage` 读回一致、被替代字节、读写位置独立；`ScanUInkPagedFile` 在每个合法对象边界与对象中间截断、页头截断、中间乱码、超大声明；§5.4 每一行。
- **L4 / 无窗口测试**：
  - 三场景连续抬笔后文件内容与运行时一致；回翻落笔后最后一个 Canvas 是该页；离开后正序整理；
  - 基准修订失配转 `Full`；Clear 失败后的 `Append` 不接到上一区间；队列满后补写；
  - 桌面已封存区间按范围冷读；Undo Clear / Redo Clear 后活动区间指针正确；停放与恢复；
  - 开关关闭时与 C1.4 字节级行为一致；§3.1 去掉 AutoSave 后的两种行为；
  - 会话记录：存活锁三种探测结果、认领冲突、`clean`、按场景结算、PPT 条目在本进程结束时丢弃；
  - 恢复：桌面装入（含弹窗期间已落笔）、白板装入（含已有新编辑时不覆盖）、PPT `Adopt` 先于 `LoadLane`。
- **注入测试**：每个对象边界断写、Flush 后会话记录写前、`Create` 发布后重开失败、`Compact` 替换后记录更新前、整理后重开失败。
- **落笔延迟**：953 笔重页上“撤回后立即落笔”、连续“抬笔 → 落笔”的交接耗时分布（Release），与 C1.4 基线对比；慢盘与队列满时不劣化；确认不触发 C1.6a 的 15 s 看门狗。
- **实机**（配合 C1.6c）：三场景 × 三档 × 三种触发；任务管理器结束进程后手动启动；弹窗期间落笔与进入白板；PPT 先于扫描被识别；两个实例同时启动。

## 12. Codex 第 1 版审查处理记录（2026-11-26）

| # | 意见 | 处理 |
|---|---|---|
| 1 阻断 | 半页可能停在对象边界上冒充完整页；`Create` 发布后可能无登记 | §5 改为写前登记（含图形组与 `Create` 候选路径） |
| 2 阻断 | `Compact` 让提交偏移失效 | §5.3 用 NTFS FileId 标识物理文件；整理后先更新记录 |
| 3 阻断 | 确认修订必须证明磁盘包含此前所有改动 | §4.1 请求带基准/目标修订；§4.4 失配转 `Full`、Clear 前缀身份不随 `Append` 推进、合并遇屏障停止 |
| 4 阻断 | 桌面冷读原样复用会失败 | §4.5 改为“已封存区间 + 按范围 `ReadUInkPage`”，补齐 Undo/Redo Clear 与停放 |
| 5 阻断 | 提前结算会吞掉恢复；弹窗占用 worker | §7 按场景、在落盘且装入成功后结算；PPT 条目按用户决定只在下一次启动有效；§8.1 弹窗独立线程 |
| 6 应改 | 原样搬移的适用条件 | §4.3、§8.2 写明拓扑、校验、独立文件位置与物理整理限制 |
| 7 应改 | 输入延迟缺实现约束 | §4.1 有界交接、不扫描 `active`；§4.2 可让出的整页截取；§10 禁止 `Drain` |
| 8 应改 | 存活探测与恢复认领是两件事 | §6.1 先拿锁再发布、三种探测结果、独立认领锁 |
| 9 阻断 | 白板冷安装缺两个入口 | §8.3 重建集合并保留页号空洞、初始化目录与确认页、经 `RequestWhiteboardActive` 进入并显式加载目标页 |
| 10 阻断 | PPT `Adopt` 必须先于首次 `LoadLane` | §7 第 1 步先登记；§8.3 懒建时传入恢复描述、沿用旧 Workspace GUID |
| 11 应改 | 开关组合与启动竞争 | §3 按用户决定去掉 AutoSave 开关，并写明运行中切换；§7 第 1 步；§8.3 弹窗期间书写的合并与白板不覆盖 |

## 13. 实施中的调整（2026-11-26）

| 项 | 调整 | 理由 |
|---|---|---|
| 恢复读取 | 不做字节级页段搬运与有界流式扫描。恢复按写前登记决定读取长度（FileId 匹配且有写前登记时截到 `pending.start`，否则读全文件；§13.2 起只登记起点），用 `ReadUInkStoreFilePrefix` 整流解码并按页去重，再由新进程的写入器重新编码写出 | 截断位置天然覆盖“整页回退上一版本 / 图形组整组丢弃 / 新页首个 Canvas 不恢复”；重新编码即 §8.2 允许的回退方案，避免跨文件原样复制的合规风险。代价：受现有 128 MiB 整读上限约束，超限时该场景不恢复并写 Warn |
| L2 续接自检 | `AppendContent` 用“同页身份的空 Canvas + 新块”在内存中解码一次（解码器会重排续接编号，只记 Warning） | 保持 C1.4“写得进去就读得回来” |
| L1 | 新增 `EncodeUInkContentBlocks`；页目录新增 `lastIsClear` | SAVE-8 的 Clear 前后撤回组校验需要上一块是否为 Clear |
| 会话记录 | `Draw3.RecoverySession.h/.cpp`（普通头，不 include `<Windows.h>`）；绘制线程只写原子量，记录 worker 合并写出；场景 worker 的登记同步写盘 | 绘制线程不阻塞 |
| 诊断异步化（S2） | 不改。`DiagnosticOutput` 已是定长缓冲、`Log::TryWrite` 单次 try-lock、拿不到即丢弃，无 I/O 与分配；逐笔追加路径不新增日志 | 现状已满足“不延迟下一次落笔” |

### 13.1 L4 接入方案（2026-11-26，依据写入链调查）

事实：整页导出（`ExportDraw3PageToUInkV11`）每次按可见历史顺序重编 contentId（=序号）与 undoId（每个可见条目一组，Clear 两侧换组）。页修订由 `markPresentationMutation` 在落笔、撤回、重做、图形编辑等处递增；白板 `WhiteboardPageState`、PPT `PresentationPageState` 都有 `revision / confirmedRevision / pendingWrites / saveRequestedWhileWriting`。

- **Append 请求**：`PagedInkRequestKind::Append`，携带基准修订（改动前）与目标修订、单笔快照（`Draw3UInkCanvasSnapshot`，只含这一笔，页元数据同整页）。worker 只在 `confirmed[page].revision == 基准修订` 且未降级时执行：导出单笔页 → 编号平移到目录 `nextContentId` / `lastUndoId + 1`（盘上为空时从 0）→ 页不是最后一个 Canvas 时先写前登记再 `RelocatePage` → 多块（图形）先写前登记 → `AppendContent`；页未写过且基准为 0 时 `AppendPage`/`Create`。成功只推进 `confirmed.revision`，不改 Clear 边界身份。失配或失败返回失败，绘制线程按原机制补一次整页保存（`saveRequestedWhileWriting` 延后到在途写入结束）。
- **合并**：`MergeCandidate` 向前扫描遇到 Append 即停止；Append 不参与合并。
- **回执**：绘制线程新增 Append 回执处理，只把 `confirmedRevision` 推进到目标修订，不动 `confirmedBoundary` / `previousIntervalOnDisk`。
- **桌面**：保留 C1.4 的 Clear/退出快照（冷读路径不变），另加“活动区间日志文件”：Clear 后首笔 `Create`，之后 `AppendContent`；撤回、重做、图形编辑使桌面修订失配，由绘制线程在无接触的空闲时机截取整页补 `AppendPage`；Clear 时封存日志。恢复只读最新会话的桌面日志，桌面为空时装入。
- **离开白板 / 结束放映**：在途写入完成后排一次不受阈值限制的整理（`CompactOrdered`）。

### 13.2 恢复实现中的简化（2026-11-26）

| 项 | 实现 | 与原设计的差别 |
|---|---|---|
| 写前登记 | 只登记起点 `{fileId, start}`，写成功后清除；恢复时有登记即截到起点。登记失败拒绝写入；清除失败后之后每次写入先重新登记 | 不需要 `end`；代价是“数据已写完、清除登记前崩溃”会回退到上一版本 |
| 建文件 | 发布前先登记候选路径（无文件身份，恢复时读全文件），建好后再登记身份 | 与原设计一致（§14 修正） |
| 桌面装入 | 只在当前页为空时装入；已有新写内容不合并，保持未结算 | 原设计“在上方合并后整页写出” |
| 白板装入 | 白板未进入且会话为空时热装入（补足空白页，上限 256 页），页标脏后由空闲或离页整页写出 | 原设计冷装入（恢复描述 + 冷页） |
| PPT 接管 | `adoptPages` 在 worker 启动时重新编码写进新文件，确认修订 1，有 Clear 时边界 {1,1}；读出即结算 | 与原设计一致，结算提前到读出时（本就只在本次启动有效） |
| 结算 | 用户选“否”或无内容即结算；装入成功后等本进程确认写盘（§14）才结算 | 与原设计一致 |

## 14. Codex C1.6b 实现审查处理记录（2026-11-27）

审查原文：`research/codex-impl-review-20261126.md`（7 个阻断项，均已逐条对照代码确认属实）。

| # | 意见 | 处理 |
|---|---|---|
| 1 阻断 | 写前登记 `end` 默认 0，`FileFromJson` 要求 `end >= start`，回读丢登记 | 记录只写读 `start`（旧记录里的 `end` 忽略），去掉 `PendingWrite::end` |
| 2 阻断 | 登记、清除失败不阻止写入；建文件发布后才登记；接管最后才登记 | `NoteRecoveryPending` / `NoteRecoveryFile` 返回结果，失败拒绝写入（`*.recovery_register` 阶段）；清除失败置 `recoveryPendingStale`，之后单块追加也先登记；`CreatePagedFile` 发布前登记候选路径；接管逐页走同一协议。开关关闭时 Host 钩子不做写前登记（写入开销同 C1.4），清除照常 |
| 3 阻断 | PPT 正常回执分发漏了 `Append`，`pendingWrites` 悬挂 | `PresentationPersistenceCompleted` 分支把 `Append` 与 `Clear` / `Full` 同样交给 `CompletePresentationPageSave`，复用后续区间读取与驱逐 |
| 4 阻断 | 桌面 Clear 忽略 Rotate 准入，会话仍指向清前文件；跨 Clear 撤回不推进日志修订 | Rotate worker 先经新钩子 `clearRecoveryFile` 持久化“无活动区间文件”（`Session::ClearDesktopFile`），失败回执 Failed；Rotate 不受文件失败准入限制（顺带让失败的桌面文件恢复写入）；绘制线程未准入或失败置 `rotatePending`，下次写入前重试。开关关闭时也轮换。跨 Clear 撤回的热、冷两条装回路径推进日志修订并立即补整页；空闲补写不再要求已有确认修订（覆盖 Redo Clear 恢复非空后继、开关中途开启） |
| 5 阻断 | 装入后立即结算；停放白板不走空闲补写；较旧会话直接删除 | 新增 `recoveryDurable` 回报：桌面在同 epoch 确认修订追上装入修订（或 Clear 换了 epoch）后、白板在所有页不脏且无在途写入后才报 true，文件失败报 false；白板装入后立即 `saveWhiteboard()`。恢复线程收到装入结果后再等落盘结果（最多 2 分钟，退出即放弃，保持未结算）。较旧会话先认领；最新会话缺桌面或白板条目而旧会话该场景未结算时借用，最新记录持久化后才删旧记录 |
| 6 阻断 | 扫描线程读 `presentationStoreDpiScale` 数据竞争 | 启动扫描时按同一窗口、同一算法计算 DPI，按值传给 `RunRecoveryScan` |
| 7 阻断 | 逐笔续接可累计超出页级冷读预算 | L3 导入结果新增 `footprintUsed` / `pageTilesUsed` / `geometryUsed`；`ConfirmedPage::coldUsage` 由整页写入的整页冷读给出、续接累加、接管时计算；累加后超页预算或用量未知返回 `AppendBaseMismatch` 补整页。单项开销与顺序无关，累加判定与整页冷读等价 |
| 应改 | 会话写失败仍推进序号；页 GUID 与索引可能错配 | worker 写盘成功才推进序号，失败等满 500 ms 重试；GUID 与索引成对存放，不一致时不写 GUID |
| 应改 | 强制整理无被替代字节时直接返回 | 强制时检查物理顺序与页号是否一致，不一致也整理 |
| 应改 | 白板装入：并行结构错位、当前页被夹、跳过页无记录 | 补页失败回退 runtime；页数覆盖崩溃时的当前页；跳过页写 Error 日志 |
| 应改 | 白板逐笔回执日志标成 Full | 标为 Append |
| — | 确认目录有页、当前文件无页时单笔会写成新页 | 改为 `AppendBaseMismatch`（防御） |

**未处理（记录为剩余风险）**：抬笔路径的单笔点复制与准入锁、队列无上限、空闲整页截取不可让出、桌面 Full 失败的重试频率（需落笔延迟实测后再定）；PPT 懒建存储在绘制线程上最多等扫描 3 秒、迟到的扫描结果不再装入、接管部分失败不报告；`LimitExceeded` 的部分文档按成功结算；`clean` 标记不反映保存失败；桌面装入用当前视口覆盖文件视口；白板装入物化失败的页不回报整体失败；退出时若装入尚未确认落盘，旧记录保持未结算，下次启动可能再次提示。

## 15. Codex 复审与第二轮实测处理记录（2026-11-27）

复审原文：`research/codex-impl-rereview-20261127.md`（#1/#3/#6/#7 已修；5 个阻断项，均核实属实）。

| # | 问题 | 处理 |
|---|---|---|
| R1 阻断 | 登记后写失败并回截，旧起点留在记录里，之后尾页单块免登记被截；FileId 查询失败仍写登记 | `recoveryPendingStale`（原子）在登记成功即置位，确认清除、登记文件或 Rotate 清条目后复位；清除失败后 worker 空闲重试一次（锁外）。Host 查不到 FileId 时登记返回失败 |
| R2 阻断 | Rotate 一入队就写新区间；失败后迟到的重试可清掉新文件；Clear 后不落笔无重试 | `DesktopJournalState::rotateInFlight`：成功回执前不写新区间，回执到达后由 `saveRequested` 补写；失败置 `rotatePending`，下次写入前或空闲时重试（每 epoch 空闲最多 3 次），同轮不连环重提 |
| R3 阻断 | 落盘判定过宽 | 桌面：同 epoch 确认且 `RecoveryPendingSettled()`；Clear 只在换文件成功回执后才算完成。白板：所有页不脏、无在途写入、未降级且 `RecoveryPendingSettled()` |
| R4 阻断 | 合并记录写失败仍删旧记录；Clear 后的空条目触发借用 | 合并记录写成功才删旧记录；记录新增 `desktopClear{generation, cleared, previous, previousGeneration}`，最新会话 Clear 过就不借用旧桌面 |
| R5 阻断 | 开关关闭时会话写失败影响普通保存，整页后同步清登记 | 开关关闭时文件登记失败不拒写；`Session::ClearPending` 无登记且身份未变时不写盘 |
| 应改 | 补页抛异常留下多余 runtime；物理顺序检查在锁内分配 | 异常路径同样回退；`PhysicallyOrdered` 改 noexcept，分配失败按有序处理 |

第二轮实测问题（用户 2026-11-27）：

| # | 问题 | 原因 / 处理 |
|---|---|---|
| T1 | “墨迹绘制错误”“模拟界面卡死”退出后不重启 | 两次都发生在崩溃拉起后 5 分钟内，命中防循环规则；该路径原先直接返回、连报告都不写。现照常写报告与 Critical 日志；“提示”档改为“是否重启”（建窗截止超时按否），其他档直接退出（`Helper.CrashHandler.cpp`，i18n `Dialogs.Crash.RepeatBody`） |
| T2 | 询问规则 | `RecoveryNeedsPrompt`：恢复文件最后写入距今 ≤5 分钟直接恢复，超过或取不到时间才询问 |
| T3 | 恢复后可见时切到绘制 | 新 observer `recoveryInkVisible` → `HostStartOptions::recoveryEnterDrawing` → 主循环在“选择/穿透”时 `ChangeStateModeToPen()`。桌面：装入了墨迹即触发；PPT：存储 `TakeAdoptedNotice()` 且当前页加载完成并有墨迹时触发。白板沿用进入白板 |
| T4 | 桌面恢复后撤回不到上一区间 | Host 记录本次运行最近一次 Clear 快照的请求 ID，保存成功后登记 `RegisterDesktopPreviousInterval(path, clearGeneration)`；Rotate 把同一 Clear 代次经 `clearRecoveryFile` 写进记录（两者无先后保证，按代次配对）。恢复时代次一致才读 `desktop\` 快照，作为 `DesktopClearRecovery` 的 CPU 副本装入（撤回到底走热路径）；Clear 后尚未落笔时只装撤回点、直接报告已落盘。装入落盘后新会话接手该快照登记 |
| — | 退出前装入尚未确认落盘会再次提示 | 正常退出时绘制线程在 `captureExitAutoSave` 末尾排空桌面活动区间写入，按各场景实际回执调用 `settleRecoveryDurable()`，仍未确认的报告 false；Host `Stop` 入口置 `recoveryClosing`，之后不再弹窗或交接装入 |

### 15.1 Codex 第 3 轮复审处理（2026-11-27）

复审原文：`research/codex-impl-rereview2-20261127.md`。

| # | 问题 | 处理 |
|---|---|---|
| 阻断 | `ClearPending` 先清内存再写盘，写失败后重试被“无登记”短路误报成功 | `RegisterPending` / `ClearPending` / `RegisterDesktopPreviousInterval` 写盘失败时把内存恢复到写前；worker 空闲重试清掉后经 `options.wake` 唤醒调用方重查结算 |
| 阻断 | 只有“已清空”标记的记录被扫描当作已结算删除，随后借用更旧桌面；只剩上一区间的旧记录不借用却被删 | `ScanAbandonedSessions` 的可恢复判定加入 `desktopCleared`；借用条件与扫描统一（`desktop` 或有效上一区间） |
| 阻断 | 当前区间读失败时用上一区间顶替并结算 | 区分无条目 / 空页 / 读失败；读失败计入 3 次重试预算，不装入 |
| 阻断 | 上一区间交接忽略登记失败，且可能绑到用户新 Clear 的代次 | 交接前取 `DesktopClearGeneration()`，落盘后代次未变才以该代次登记；登记失败不结算 |
| 阻断 | 退出兜底用白板结果替桌面确认 | 去掉 `recoveryExitDurable`，改为退出时按实际回执逐场景报告（见上表） |
| 阻断（部分） | YesNo 不能关闭；截止线程空回调也抢重启意图 | 有“否”按钮即可退出，不改 MessageBox；截止线程先判回调再抢意图 |
| 应改 | Rotate 失败后切到白板/PPT 不再重试 | 记下被清页的 GUID 作队列身份，空闲重试不再限于桌面场景 |
| 应改 | 只装撤回点时 `canUndo` 未发布 | `installPrevious` 后 `publishHistoryAvailability()` |
| 应改 | Clear 快照回执在绘制线程同步写 JSON，关开关也写 | 改为 `Session::PublishDesktopPreviousInterval`（只记请求，记录 worker 写盘），开关关闭不登记 |
| 应改 | 主循环无锁读模式再切换 | 新增 `ChangeStateModeToPenIfIdle()`，判断与切换在 `stateModeTransitionMutex` 内 |
| 应改 | 退出屏障期间仍可弹窗或装入 | `Stop` 入口置 `recoveryClosing`：Take* 拒绝、扫描不再弹窗或交接 |

**重新评估后不改（剩余风险）**：PPT 懒建存储在绘制线程最多等扫描 3 秒——扫描先读 PPT 再处理弹窗，等待只出现在启动时已在放映且 PPT 文件读取慢的情况；扫描后接管需要重载已加载的 lane，代价大于收益。`LimitExceeded` 的部分文档：仍装入并结算（不结算会在下次启动重复提示或因画面非空永远无法结算），文件保留在磁盘上。上一区间只保留一级（与运行中一致）；跨区间撤回装回后再崩溃，可能再次提供同一撤回点。双重崩溃时最新会话已有部分白板条目会挡住借用旧会话。白板页降级（冷读超预算）时恢复不结算，2 分钟后保持未结算。
