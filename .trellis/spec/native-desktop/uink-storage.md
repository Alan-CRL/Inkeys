# UInk 1.1 墨迹存储与读取（2026-11-26 起）

本文是 Draw3 墨迹持久化的唯一权威合同，取代旧的 UInk 1.0 私有字段、整文件 SHA/回读、日索引、PPT 多轨索引与白板历史 JSON。来源任务：`.trellis/tasks/archive/2026-11/11-24-uink-1-1-adaptation/`（C1.4+C1.5）；规范副本见该任务 `research/uink-v11-spec.md`，下文规则编号（SAVE-10、CANVAS-8、NIB-6 等）均指它。场景行为（何时保存、撤回语义、slot）分别见 [draw3-integration.md](draw3-integration.md)、[whiteboard.md](whiteboard.md)；运行时模型见 [runtime-and-rendering.md](../native/runtime-and-rendering.md)。

## Scenario: 五层结构、写入格式与目录布局

### 1. Scope / Trigger

修改 `inkStrokeModelerTest/draw3/uink_*`、`shape_uink_*.inl`、`Draw3.PagedInkStore.*`、`Draw3.DesktopInkSnapshots.*`、`Draw3.PresentationPages.*`，或任何 UInk 字段映射、文件布局、保存时机、读取容错时必须应用本合同。UInk 库源码同时编进 `Inkeys.vcxproj`、`inkStrokeModelerTest.vcxproj`、`inkStrokeModelerTestTests.vcxproj`，增删文件三处及 `.filters` 同步；模型器测试工程只在 `inkStrokeModelerTest.sln` 中。

### 2. Signatures

| 层 | 模块 | 主要入口 |
|---|---|---|
| L0 模型 | `draw3.uink_model` | `UInkDocument/Header/HeaderExtension/Canvas/Ink/Shape/Clear`、`UInkReadLimits`、`UInkReadResult{status, summary, diagnostics, validPrefixLength, failedObjectOffset, safeAppendOffset, decodedObjectCount, geometryPointCount, modelCharge, replayedObjectCount}`、`kUInkVersion = 11`、`kUInkMaxCoordinate = 1e7` |
| L1 编解码 | `draw3.uink_codec` | `DecodeUInk`、`DecodeUInkStream`（整文件/页段两种模式）、`EncodeUInkDocument`、`EncodeUInkHeaderPrefix`、`EncodeUInkPage` |
| L2 文件 | `draw3.uink_store` | `UInkPagedFileWriter::{Create, AppendPage, Compact, Close, Directory, FileBytes, SupersededBytes, IsSealed, IsTailUnconfirmed}`、`ReadUInkPage`、`ReadUInkStoreFile`、`PublishUInkSnapshotFile`、`UInkStoreCompactResult{replacementCommitted, reopen…}` |
| L3 Draw3 适配 | `draw3.uink_draw3_v11` | `ExportDraw3PageToUInkV11`、`ImportUInkV11Page(canvas, Draw3UInkV11ImportOptions)`、`MakeDraw3UInkV11FileHeader`、`SpliceUInkV11PreviousInterval`、`Draw3UInkV11FallbackCounts` |
| L4 场景服务 | `Draw3.PagedInkStore`（白板+PPT）、`Draw3.DesktopInkSnapshots`（桌面）、`Draw3.PresentationPages`（PPT 页元数据纯函数） | 见下一节 |

产品存储根：`IdtMain.cpp` 中 `HostStartOptions::autoSaveRoot = <exe 目录>\Inkeys\Save\Ink`；取不到程序目录时为空，空根关闭持久化。旧 `Inkeys\AutoSave\` 不再被任何代码访问或清理。

### 3. Contracts

- **只写 1.1**：Header 固定 `array(7)`、`version = 11`；精确类型与位宽（TYPE-1）；不写任何私有 `extra`。读 `version` 10–19，10 按 1.1 规则尽力读（VER-5）。
- **编号**：每个 Canvas 版本 `contentId` 从 0 连续；`undoId` 从 0 单调；Inkeys 自产每笔一组（图形对象的全部部件同组），Clear 独占一组（INK-2/3、CLEAR-4）。每页最多一个 Clear。
- **Canvas 字段**：

| 场景 | 写法 |
|---|---|
| 白板 | 显式 Workspace `workspaceType = 1`；第 0 层 Canvas `background = {color.fallback, pattern(0 空白/1 点阵/2 网格)}`；平移非 0 时写 `viewport = {x, y, scale: 1}`（会话视角侧表仍优先） |
| PPT | 显式 Workspace `workspaceType = 2`、`hostId = PresentationKey` 文本；Canvas `binding`（0 SlideId / 1 PageIndex）、稳定页写 `slideId`、`pageState`（1 = Retained）、`pageKind`（1 = EndScreen，且不写 `slideId`）；缺省值不写 |
| 桌面 | 隐式 Workspace 与隐式 Device、单页 `pageIndex = 0`、`pageNumber = 1`、不含 Clear |
| Device | 三场景都是隐式 Device：Canvas 省略 `deviceGuid`，`deviceNum = 1`；显式 Workspace 时每个 Canvas 写同一 `workspaceGuid` |

- **笔尖映射**（只在 L3 读写边界换算，软件内部尺寸语义不变）：

| Draw3 | `inkType` | `nib` | 点 `width` 写入 | 读回 |
|---|---|---|---|---|
| 普通笔 | 1 | 不写（圆） | 直径 `w` | `w` |
| 荧光笔 | 2 | `{shape:1, aspect:0.125}` | 原值 `w`（渲染半高下限 0.5 px 不写进文件） | 原值 |
| 圆形橡皮 | 0 | 不写 | 直径 `s` | `s` |
| 矩形橡皮 | 0 | `{shape:1, aspect:0.6666667}` | `H = (s/2)·√(π/a)` | `s = 2H·√(a/π)` |
| 方形橡皮 | 0 | `{shape:1, aspect:1}` | `H = (s/2)·√π` | `s = 2H/√π` |

  换算用 `double`，读入时 `aspect` 与表值相差 ≤ 1e-4 视为对应形状，否则按最近形状并计 `nib` 回退；矩形笔尖不写 `cap`（NIB-3）。橡皮尺寸钳制到 `[2·FLT_MIN, 2^127]` 并计 `eraserSize`。
- **图形**：标准 Shape 部件承担可显示外观（任意 Shape 读取器可画），首部件的官方 `object` 承担可编辑身份：`id`、`revision`、`transform`、`aux`、`partCount`、`preset`（21 个官方名 + 4 个 `legacy-*`）、展平的 `construction`、`style`；后续部件 `{ref, partIndex}`。描边补 `lineCap = 0`。详见 [shapes.md](shapes.md#uink-11-图形对象层)。
- **目录布局**（`CREATE_NEW` + 不覆盖发布，冲突加 `_n`；每个进程只写自己新建的文件，不扫描、不打开其他进程的文件）：

```
<root>\desktop\YYYY-MM-DD\HHmmssfff_<请求GUID前8位>[_n].uink          每次 Clear / 退出一个快照
<root>\whiteboard\YYYY-MM-DD\HHmmssfff_<文件GUID前8位>.uink            每个 Draw3 运行实例一个，首次写入时创建
<root>\presentation\<文稿名≤40>_<key前8位>\YYYYMMDD-HHmmssfff_<文件GUID前8位>.uink   每个运行实例每份文稿一个
```

- **按页追加**（L2）：`Create` 在同目录写临时文件（Header + Header Extension + 首页完整 Canvas），写盘前在内存按页模式解码一次（须 `Complete` 且恰好一个 Canvas），Flush 后 `MoveFileExW(WRITE_THROUGH)` 不覆盖发布，再以共享读、不共享写打开；`AppendPage` 同样内存自检，单页编码 ≤ 32 MiB，一次 `WriteFile` + `FlushFileBuffers` 成功后才更新页目录（同 `pageGuid` 后者替换前者，FILE-8）。短写或 Flush 失败截断回原末尾再 Flush；截断失败即封闭写入器并标记尾部未确认。临时文件叶名不长于目标叶名。
- **页目录**：`pageGuid → {offset, length, pageIndex, writeSequence}`、被替代字节、`lastPageGuid`、`nextContentId`、`lastUndoId` 与解码上下文；写入器自维护，不扫描文件。最后三项是 C1.6“向最后一个 Canvas 追加”（SAVE-6–9）的承接点。
- **桌面一次性发布**：`PublishUInkSnapshotFile` 同目录临时文件 → 精确写入 → Flush → 关闭 → 不覆盖发布，**发布成功即提交点**；保留写盘前内存自检，无回读、无 SHA、无索引。
- 异常边界：L1–L3 入口统一捕获（含 `bad_alloc`），以“失败 + 诊断”返回。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 编码结果超过单页上限或内存自检失败 | 拒绝写入，回执失败，页保留内存 |
| 目标名已存在 | 换 `_n` 重试，绝不覆盖 |
| Append 短写/Flush 失败 | 截断回原末尾并 Flush；截断失败封闭写入器，后续写入一律失败、页不驱逐 |
| `Create` 已发布但重开失败 | 该逻辑文件锁定为失败，本进程不另建第二个文件 |
| 程序目录取不到 | 存储根为空，持久化关闭，不拼盘符根路径 |

### 5. Good / Base / Bad Cases

- Good：白板 A 页 Clear 后追加“上一区间 + Clear”，翻页时再追加完整记录，目录指向最新版本；外部读取器整读取到每页最后版本。
- Base：页没有变化时零写入；程序目录不可用时只在内存工作。
- Bad：写私有 `extra`、写盘后回读比对、用 JSON 索引记录版本、覆盖已有文件、按 `pageIndex` 去重。

### 6. Tests Required

- `inkStrokeModelerTestTests.exe --uink-file-only`（L1 + 官方 1.0 夹具 SHA-256 身份 + L3）、`--uink-store-only`、`--uink-v11-draw3-only`、`--paged-ink-store-only`、`--desktop-ink-snapshots-only`、`--presentation-pages-only`。
- 断言：成品字节根 Map 与注册表条目无 `extra`；三种橡皮与荧光笔往返误差在浮点精度内；21 + 4 个预设逐一往返（名称、参数、部件顺序/数量、编辑后重建）；临时叶名长度 ≤ 目标叶名；短写/截断/重开失败分支。
- 产品：`Inkeys.exe --draw3-whiteboard-storage-test`、`--draw3-hidden-test`、`--draw3-parked-desktop-exit-test` 等自检。

### 7. Wrong vs Correct

~~~cpp
// Wrong：内部尺寸直接写成笔尖高度，矩形橡皮在其他读取器里面积变了。
ink.points.push_back({x, y, eraserDiameter});

// Correct：只在 L3 边界按等面积换算，读回再反算。
const double h = (s / 2.0) * std::sqrt(std::numbers::pi / aspect);
ink.points.push_back({x, y, static_cast<float>(h)});
~~~

## Scenario: 读取容错、资源预算与日志

### 1. Scope / Trigger

修改解码器、页读取、L3 导入、L4 安装前检查、`Module::UInk` 日志或任何读取路径时必须应用。目标：任何输入（截断、乱码、超大数量、深嵌套、非有限数、极端几何）都不能使软件崩溃或卡死；能显示的照常显示。

### 2. Signatures

- `UInkReadLimits`：文件（整读）≤ 128 MiB、单页范围 ≤ 32 MiB、单对象 ≤ 32 MiB、深度 ≤ 32（内部硬上限 64）、字符串 ≤ 8 MiB、容器 ≤ 100 万项、模型估算 ≤ 256 MiB、几何点 ≤ 400 万、注册表 ≤ 1024 项。
- `Draw3UInkV11ImportOptions { dpiScale, footprintTileSize = 128, footprintBudget = 2^28, footprintItemBudget = 2^25, footprintItemMaxTiles = 2^18, footprintPageMaxTiles = 2^23 }`；L3 图形几何工作预算 2^26/页。
- 日志：`Inkeys::Log::Module::UInk`；行标签 `[UInkRead]`、`[UInkWrite]`、`[UInkQueue]`、`[UInkCompact]`。

### 3. Contracts

- **逐块解码，不做跨块全局复核**：首块不是有效 Header → 整文件失败；结尾截断 → 丢弃最后不完整对象（FILE-16）；中间无法解码 → 保留之前的完整对象并停止（FILE-17）；未知 Type/键/枚举 → 跳过或取缺省；字段无效的已知块 → 跳过该块并计数；坏 Canvas → 跳到下一个有效 Canvas；有回退规则的字段按规则回退（坏 `viewport` 取缺省、坏 `background` 按无背景）；同一 Map 重复已知键 → 整块无效（TYPE-7）；`shape.object` 内部重复键只丢对象层、保留可绘制 Shape（SHAPE-14 优先）。
- 单遍流式解析，点直接解到目标数组；`extra` 只做有界 wire 遍历跳过：不构造值树、不复制、不计模型费用（只有根块与真正 Header Extension 注册表条目的 `extra` 免计费；其他对象里同名未知字段照常计费）。
- **页读取的成败**：页段可解码但有跳过块 → 照常安装可显示内容，标记“读取降级”并 Warn；只有页身份或结构错误（GUID/绑定/`pageIndex`/稳定页 `slideId` 与确认记录不符、页段无 Canvas 或多于一个、范围越界、临时身份、Clear 与确认边界不符）才整页失败，此时页保持冷/未安装，**不得**用空白页替代有确认记录的页。
- **外来内容就近回退**：HDR 用 `fallback`；`opacity` 钳制；非 Inkeys 笔尖：Pen 按圆、Highlighter 保持 Draw3 1:8、Erase 取最近形状；非 Round `cap` 按 Round；Advanced Highlighter 按 Highlighter；未知 `inkType` 按 Pen；任意 `dashArray` 映射到四种线型或实线、Marker 不画；`viewport` 平移超出 ±1048576 或 `scale ≠ 1` 回退缺省视口；Media 跳过；多 Device 取第一个注册 Device；多图层按 `layerIndex` 叠一层且 `layerIndex > 0` 的 Erase 跳过；页内多个 Clear 只保留最后一个 Clear 及其之前一个区间；外来多笔同组读成逐笔撤回。每类回退计入 `Draw3UInkV11FallbackCounts` 并在日志汇总。
- **足迹预算**（L3，worker 线程，防止绘制线程安装时产生海量瓦片）：对每个笔画/图形按 `BuildStrokeTileFootprint` 的保守上界估算（逐点/逐段宽度扩张 + AA padding、DDA 跨格 + 3、整笔包围盒回退一次、填充矩形、辅助部件，撤回与合成两次枚举）。四重上限：每页访问 2^28、单项访问 2^25、单项包围盒 2^18 块、每页包围盒合计 2^23 块。超限单项跳过（不扣额度，后续较小项仍可接受），计 `footprintBudget`。Clear 前缀与当前区间共用额度。访问次数约束枚举耗时，包围盒约束驻留瓦片与索引。
- **保存侧冷读预检**：分页与桌面保存时对最终导出结果调用同一 `ImportUInkV11Page`；会被跳过、降级或预检本身失败的页，磁盘提交照常，但回执标记降级，页及 Clear 前缀保留在内存不驱逐，日志带 `stage=cold_preflight reason=…`。
- **日志**：L1–L3 只返回结构化数据；L4 worker 在每次**实际读盘**终态、在服务锁外输出一行 `[UInkRead]`（冷读、`load_previous`、PPT lane 每个实际读页、`splice_prefix`、桌面回读；空 lane 不输出），先查 `Enabled`、限制单行长度、只记文件名。判级：完整 Debug；有跳过块、L3 回退、拆撤回组或历史降级（`storedDegraded=1`）Warn；失败 Error。写入失败/降级输出 `[UInkWrite]`（含阶段、系统错误码、`sealed`、`tailUnconfirmed`、回滚阶段与错误码）；已接纳但读盘前失败输出 `[UInkQueue]`。计数用 L1 `summary.diagnosticCounts`（不受诊断明细上限影响），偏移区分 L1 文件偏移 `stop=l1_file`、L2 页范围起点 `stop_l2=range_start`、L3 `l3Offset=unknown`。格式化异常计入 `Log::RecordDropped`。
- **计时**（`[Draw3Diag][page-load]` / `[page-save]`，Debug 级）：`ioMs ⊂ readMs ⊃ decodeMs`、`importMs`、`workerMs`、`footprintMs`、`installMs`（已扣足迹，在日志输出前结束采样）、采到接手时刻才输出 `readyMs/handoffMs`；`page-save` 的 `stage` 区分 `written`（含 `success`）、`merged`、`canceled`、`file_failed`，带 `pageBytes`。

### 4. Validation & Error Matrix

| 输入 | 必需结果 |
|---|---|
| Header 无效 / 没有任何有效页 | 整文件或整页失败，Error |
| 截断尾 / 中段坏字节 | 保留前缀，Warn；页段内坏块跳过后继续 |
| 超限容器、深嵌套、超长字符串 | 按所在层停止或跳过，`LimitExceeded`，不崩溃 |
| 单点 `width = 1e7`、±1e6 长粗线、1e6 px 填充、数百个合规大填充 | 足迹预算跳过相应单项，Warn，其余照常 |
| 外来合法但 Draw3 画不了的内容 | 就近回退显示，Warn；回退不了的单项跳过，不拒绝整页 |
| 页身份或结构错误 | 整页失败，页保持冷，不显示空白替代 |
| 健康读写 | Debug 级一行，默认 Info 阈值下不刷屏 |

### 5. Good / Base / Bad Cases

- Good：一页中段有一个坏块，其余笔画照常显示，日志一行 `[WARN ][UInk] [UInkRead] … skippedBlocks=1 l1.KnownBlockSkipped=1@<offset>`。
- Base：自产页完整读取，Release 冷读一页 CPU 约 8–9 ms（S7b/S8 基准）。
- Bad：在绘制线程解码或估算足迹；对单个坏块拒绝整页；逐笔刷日志；用诊断条数冒充跳过数量。

### 6. Tests Required

- L1：截断、坏标记、深嵌套、超限、重复键、`extra` 跳过与免计费范围（两种 `type` 键顺序）、快路径与重放结果一致（`replayedObjectCount`）。
- L3：足迹四重上限各自的跳过与边界（恰好用满/超出 1）、视口回退、外来回退计数、Clear 前缀共用额度。
- L4：每次实际读盘恰好一行日志且级别正确；预检失败按降级；写失败字段完整。
- 基准：`Inkeys.exe --draw3-whiteboard-load-benchmark [--rounds N]`（无窗口，真实 `PagedInkStore` 与生产安装路径，固定种子 98/246 笔合成重页；数据见任务 `research/s7b-load-benchmark-20261126.md`）。

### 7. Wrong vs Correct

~~~cpp
// Wrong：解码成功就安装，极端宽度在绘制线程上枚举数十亿瓦片。
InstallPage(ImportUInkV11Page(canvas, {.footprintBudget = std::nullopt}));

// Correct：worker 上按默认四重上限导入，超限单项跳过并计数，绘制线程只安装结果。
const auto imported = ImportUInkV11Page(canvas, options.importOptions);
if (imported.counts.footprintBudget != 0) result.readDegraded = true;
~~~

## Scenario: 场景服务合同（白板、PPT、桌面）

### 1. Scope / Trigger

修改 L4 服务、Host 服务持有关系、DrawingController 的保存/加载/驱逐调用点、完成回执投递或退出屏障时必须应用。

### 2. Signatures

- `PagedInkStore`：`Save(PagedInkSaveRequest{identity{kind=Clear|Full, pageGuid, revision, boundaryGeneration…}, snapshot, expectedBoundary})`、`Load(kind=Load|LoadPreviousInterval|LoadLane)`、`Take`、`Drain`、`Close`、`FileState()`、`TryBeginEviction(requiresHealthyFile)` / `EndEviction()`、`RequestEvictionRetry()` / `TakeEvictionRetry()`；纯函数 `IsPageEvictable(PagedInkEvictionState, PagedInkFileState)`、`MakePagedInkPath`、`PagedInkWriteDiagnosticStage`。
- `DesktopInkSnapshots`：`Save`、`Load({identity, requestGuid})`、`Take`、`Close`；回执 `publishedPath`。
- Host 持有桌面服务与 PPT 存储注册表（每个完整 `PresentationKey` 一个懒建的 `PagedInkStore`）；白板存储随 Draw3 运行实例。

### 3. Contracts

- **页写入时机**（白板与 PPT 相同）：Clear 时追加“上一区间 + Clear”（当前区间为空），**确认落盘后**才释放上一区间内存副本（`boundaryFallback`）；翻页、离开工作区/文稿、退出时追加该页完整记录（能撤回到上一区间时“上一区间 + Clear + 当前区间”，否则只有当前区间）；页无变化不写；不写重做；任何时刻都不做“修正”。上一区间已释放时，worker 从页目录读出该页最后版本中 Clear 及之前的块（`splice_prefix`），按确认边界身份（页 GUID + 边界代次 + 页修订）核验后与当前区间拼接并重新编号；失配则写入失败、页保脏。
- **桌面**：每次 Clear（及 Redo Clear）在 CPU Clear 成功后提交被清空的当前区间快照（不带 Clear），退出写活动或停放桌面的当前区间；门控沿用 `config.Drawing.AutoSave.Enable` 与非空判定；保存不合并；跨 Clear 撤回先用内存副本，已释放时按回执登记的实际路径整读该快照；路径登记随恢复点退休释放。
- **队列**：一个 worker；加载优先但不越过同页未落盘写入（直接用内存数据）；Clear 写入先进先出不合并；同页普通写入只在同一 Clear 分段内合并。
- **完成合同**：准入时预留终态位置；成功、失败、被合并、关闭取消各结算恰好一次；投递与唤醒不分配、不抛异常；`Close` 等待在途唤醒后返回，之后不再回调；PPT 与桌面回执经 Host 泵投递到 WindowController 预留槽，保持与已排队命令的相对顺序。
- **驱逐**：白板与 PPT 共用 `IsPageEvictable`（已确认、非当前页、非加载目标、未降级、文件未失败/封闭/尾部未确认）；实际释放必须持有 `TryBeginEviction` 许可（DrawingController 的 `ScopedPagedInkEviction` 守卫），许可与“可能封闭写入器的保存阶段”及整理互斥；被屏障拒绝的驱逐在之后的回执或整理结束唤醒时重扫候选重试；驱逐时的分配失败局部捕获、保留原页并重新登记；存储关闭后拒绝许可。写失败或写入器封闭后页一律保留。
- **PPT**：一份文稿在一个运行实例里一个文件；两种绑定的 lane 共用文件、各自 `pageGuid`，冷加载按 lane 批量读取并按有效 `binding` 过滤；普通翻页不读盘；SlideID 重排按 GUID 追加新版本更新 `pageIndex`，找不到的页写 `pageState = 1` 保留，重现时改回 Active；`LoadPreviousInterval` 只读该页最后版本。首次 lane 加载失败最多自动重试 4 次（250/500/1000/2000 ms）；写入自动重试 0 次（下一个自然保存点再提交）。
- **空闲整理**：worker 在队列空、无在途业务、未关闭、写入器健康且无驱逐许可时整理；被替代字节 ≥ 8 MiB 且 ≥ 50% 触发；按 `pageGuid`+物理版本记录有独立证据的损坏段（纯资源限额不算），同页新版本确认后低于阈值也整理，仍有“最新版本”坏页时推迟并 Debug 记录；失败后等下次成功写入再查；`Compact` 原样复制页字节、保留原 `pageIndex`、刷新 Header 时间，替换已提交后用新目录，重开失败则封闭；整理不占完成槽、不清除页的降级或脏标记。
- **退出**：Host 发 `PrepareExitAutoSave`，绘制线程截取活动与停放页；三个服务排空已接受的保存，未执行的读取逐个以取消结算；退出等待无超时（风险登记在父任务“异常退出与卡死兜底”）。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| Clear 写入未确认时撤回 | 用内存 `boundaryFallback` 恢复；迟到回执按身份只核销自身 |
| 上一区间已释放、盘上边界与确认身份不符 | 拼接失败，写入失败，页保脏 |
| 写入失败 / 写入器封闭 | 回执失败，页保留内存与脏状态，Error 日志 |
| 保存时预检发现读回会降级 | 提交照常，回执降级，页不驱逐 |
| 驱逐许可被写入/整理屏障拒绝 | 登记重试，屏障解除后的回执或整理唤醒时重扫 |
| 新旧进程并存 | 各写自己新建的文件，互不读写 |

### 5. Good / Base / Bad Cases

- Good：`A → Clear → B → 离页 → 翻回 → 撤空 B → 撤回 Clear 恢复 A → 撤空 A`，冷读页按连续相同 `undoId` 装回撤回组。
- Base：PPT 未修改时切页/退出零写盘。
- Bad：在绘制线程编码或写盘；未确认就释放上一区间；整理时清除降级标记；凭快照判断可驱逐后在锁外释放而不持许可。

### 6. Tests Required

- `paged_ink_store_tests.cpp`：Clear/Full 时机与拼接、确认边界失配、队列合并与依赖、完成合同（准入、异常、关闭、恰好一个终态）、驱逐许可两侧交错与重试、整理（双阈值、坏段、两 lane、跨 Clear、重开失败、关闭）。
- `desktop_ink_snapshots_tests.cpp`：日期/并发/碰撞、写失败、按已发布路径冷读、回执未消费时快照释放、登记退休。
- 产品自检：`--draw3-whiteboard-storage-test`（100 页、退出、真实日志行）、`--draw3-ppt-completion-delivery-test`、`--draw3-ppt-current-load-retry-test`、`--draw3-parked-desktop-exit-test`、`--draw3-parked-ppt-retained-test`、`--draw3-hidden-test`；C09/C10/C11 真实夹具（`artifacts\C09|C10\Save\Ink`）。

### 7. Wrong vs Correct

~~~cpp
// Wrong：读快照判断可驱逐，随后在锁外释放；期间另一页的追加可能封闭写入器。
if (IsPageEvictable(state, store.FileState())) ReleasePage(page);

// Correct：许可与可能封闭写入器的保存阶段、整理互斥；被拒则登记重试。
ScopedPagedInkEviction permit(store, requiresHealthyFile);  // 构造时 TryBeginEviction，析构时 EndEviction
if (permit) ReleasePage(page);
~~~

## 已知限制（留给 C1.6 或后续）

- PPT 绑定槽冷读没有整槽累计预算；线上只读本进程自己写的文件。
- 按页追加的文件可超过 128 MiB，而整读入口在 128 MiB 拒绝；线上不整读分页文件，C1.6 恢复扫描需要有界的流式整读或分页枚举。
- 有“最新版本”坏页时整理推迟，整读按 FILE-17 停在坏段；线上按页读不受影响。
- 整理保留原 `pageIndex`（稀疏白板页序、PPT 两 lane 重叠），偏离 CANVAS-3/SAVE-16 紧凑页序。
- 不做 `\\?\` 扩展长度路径（与旧实现一致）。
- 按页追加时断电：新 Canvas 已写完而内容块不完整时按 FILE-8 会替代旧版本；恢复策略属 C1.6。
- 外来预设图形以重建结果显示（有意偏离 SHAPE-15）。

## Scenario: 逐笔追加写入与崩溃恢复（C1.6b，2026-11-26）

### 1. Scope / Trigger

“崩溃重启后自动恢复墨迹”（`Application.CrashRecoverInk`，默认开）开启时，桌面、白板、PPT 每次抬笔后异步追加；崩溃重启或非正常结束后的下一次启动按场景恢复。修改 `AppendContent` / `RelocatePage` / 前缀读取、`PagedInkStore` 的 Append / Rotate / 接管、会话记录、绘制线程的提交与恢复装入时适用。开关关闭时三场景保持 C1.4 行为（桌面“启用桌面画布自动保存”已去掉，桌面快照始终写）。

### 2. Signatures

- L1：`EncodeUInkContentBlocks(blocks, firstContentId, previousUndoId, previousWasClear, limits)`。
- L2：`UInkPagedFileWriter::AppendContent(pageIdentity /*content 为空*/, blocks)`、`RelocatePage(pageGuid)`；`ReadUInkStoreFilePrefix(path, prefixBytes, limits)`；页目录新增 `lastIsClear`。
- L4：`PagedInkRequestKind::Append`（`PagedInkSaveRequest::baseRevision`）、`Rotate`（`RotateFile`，仅 Desktop）；`PagedInkFileKind::Desktop`（隐式 Workspace，`root\desktop-live\YYYY-MM-DD\HHMMSSmmm_<guid8>.uink`）；`RequestOrderedCompact()`；`PagedInkStoreOptions::adoptPages` 与写前登记钩子 `registerRecoveryFile / registerRecoveryPending / clearRecoveryPending / clearRecoveryFile`；错误 `AppendBaseMismatch`。
- L3：`Draw3UInkV11PageImportResult::footprintUsed / pageTilesUsed / geometryUsed`（本次导入消耗的页级冷读预算），`kDraw3UInkV11PageGeometryBudget`。
- 绘制线程 → Host：`DrawingControllerRuntimeObserver::recoveryDurable(context, scene /*0 桌面 1 白板*/, durable)`、`recoveryInkVisible(context)`；`takeDesktopRecovery(context, current&, previous&)`。
- Host：`HostStartOptions::recoveryEnterDrawing`；`PagedInkStore::RecoveryPendingSettled()`、`TakeAdoptedNotice()`；`RotateFile(pageGuid, loadGeneration, clearGeneration)`，`clearRecoveryFile(context, clearGeneration)`。
- 会话：`Session::ClearDesktopFile(clearGeneration)`、`RegisterDesktopPreviousInterval(path, clearGeneration?)`、`PublishDesktopPreviousInterval(path, clearGeneration)`（异步，绘制线程可调）、`DesktopClearGeneration()`；记录字段 `desktopClear{generation, cleared, previous, previousGeneration}`。会话方法写盘失败时内存回到写前状态。
- 会话：`Draw3.RecoverySession.h`（`Session`、`ScanAbandonedSessions`、`ClaimSession`、`ProbeSessionLock`、`FileIdentity`），记录在 `Inkeys\Save\Ink\session\<YYYYMMDD-HHmmssfff>_<pid>_<guid8>.{lock,json,claim}`。

### 3. Contracts

- **Append 等价性**：整页导出按可见历史顺序重编 contentId（=序号）与 undoId（每个可见条目一组）。worker 只在 `confirmed[page].revision == baseRevision`、未降级时续接：单笔导出 → 编号平移到目录 `nextContentId` / `lastUndoId + 1` → 页不是最后一个 Canvas 先 `RelocatePage` → `AppendContent`；页未写过且基准为 0 时 `AppendPage`/`Create`。成功只推进确认修订，不动 Clear 前缀身份。失配、跳过图形或单笔冷读预检失败返回 `AppendBaseMismatch`，绘制线程在在途写入结束后补整页。
- **冷读预算累计**：`ConfirmedPage::coldUsage` 由整页写入的整页冷读给出（接管时同样计算），续接把单笔用量累加上去；累加后超出任一页预算（足迹访问、页瓦片、几何）或用量未知时返回 `AppendBaseMismatch`，交给整页写入判定降级。
- **绘制线程**：抬笔与图形构造成功后只做单笔快照与入队（`submitStrokeAppend`）；页还没有盘上版本且无在途写入时改为整页写入。撤回、重做、图形编辑、主题等导致失配的页在无接触的空闲时机补一次整页（每个修订只试一次）。不在输入路径上 `Drain`。
- **合并**：Full 的合并扫描遇到 Append / Rotate / Clear 即停。
- **写前登记**：整页写入（已有文件时）、搬页、多块图形追加前登记 `{fileId, start}`（记录只有 `start`，查不到 FileId 视为登记失败），写成功后清除；登记成功到确认清除之间 `RecoveryPendingSettled()` 为 false，清除失败 worker 空闲重试一次；建文件发布前登记候选路径（无身份）、建好与整理替换后登记文件身份；接管逐页走同一协议。登记失败拒绝这次写入；清除失败后之后每次写入（含单块追加）先重新登记，覆盖残留旧起点。开关关闭时不做写前登记（文件登记与清除照常）。恢复读取：文件身份与记录一致且有登记时截到登记起点，否则读全文件（单块残缺由解码器丢弃）。
- **整理**：离开白板、结束一份放映、退出时仍在其中，排一次不受阈值限制的正序整理（物理整理，保留 pageIndex）；没有被替代字节但物理顺序与页号不一致时同样整理。
- **桌面**：活动区间文件；Clear 后不建文件，首笔建文件，之后续接；Clear 时（开关关闭也做）`RotateFile` 并递增 epoch（旧区间回执只结算 pendingWrites）。Rotate 先经 `clearRecoveryFile` 持久化“无活动区间文件”与 Clear 代次再关写入器，失败回执 Failed；Rotate 不受文件失败准入限制。换文件成功回执之前（`rotateInFlight` / `rotatePending`）不写新区间；失败下次写入前或空闲时重试（每 epoch 空闲最多 3 次）。最近一次 Clear 快照完整保存后登记为上一区间（按 Clear 代次与 Rotate 配对）。跨 Clear 撤回装回清前区间后推进日志修订并立即补整页。C1.4 的 Clear/退出快照与跨 Clear 冷读不变。
- **会话**：先 `CREATE_NEW + DELETE_ON_CLOSE` 拿存活锁再发布记录；绘制线程只写原子量（场景、白板页），记录 worker 合并写出（写盘成功才推进序号，失败 500 ms 后重试；白板页 GUID 与索引成对，不一致时不写 GUID）；场景 worker 的登记同步写盘（临时文件 + Flush + `MoveFileExW(WRITE_THROUGH)`）。正常退出且退出屏障完成后写 `clean`。
- **恢复**（启动时开关开启才扫描，独立线程，DPI 按值传入）：认领最新一份被放弃的会话；较旧的逐个认领，最新会话缺桌面或白板条目而旧会话该场景未结算时借用该条目，最新记录持久化后删除旧记录（文件保留）。崩溃场景是桌面/白板且有墨迹时弹 YesNo（静默重启档的崩溃拉起除外），其他场景静默恢复；PPT 从不弹窗，读出的页只在本次启动内、识别到同一文稿时接管（存储创建时等待扫描最多 3 秒）。桌面只在当前页为空时装入；白板只在未进入且会话为空时热装入（按旧页号与崩溃时的当前页补足空白页，上限 256）并立即写盘，崩溃场景是白板时请求进入白板。最后一次保存距今 ≤5 分钟直接恢复，超过才询问。用户选“否”或无内容即结算；装入成功后等 `recoveryDurable` 报告装入的墨迹已由本进程确认写盘才结算（最多等 2 分钟；正常退出且退出屏障完成时按已落盘结算，其余退出或超时保持未结算）；读不出连续 3 次后结算。合并后的最新记录写成功才删除旧记录；最新会话 Clear 过不借用旧桌面。桌面装入时一并装入上一区间快照作为跨 Clear 撤回点（代次须与最新 Clear 一致；Clear 后尚未落笔时只装撤回点）。恢复的墨迹在当前画面可见时请求切到画笔（桌面装入了墨迹；PPT 接管后当前页加载完成且有墨迹）。

### 4. Validation & Error Matrix

| 情况 | 结果 |
|---|---|
| 盘上修订 ≠ 基准 | `AppendBaseMismatch`，补整页 |
| 回翻页落笔 | 搬页后续接，最后一个 Canvas 是该页 |
| 整页写到一半断电 | 截到登记起点，回退上一完整版本 |
| 单块写到一半 | 残缺块丢弃 |
| 整理替换后记录未更新即崩溃 | 身份不符，按完整文件读 |
| 会话记录写盘失败（登记起点 / 候选路径） | 拒绝这次写入，绘制线程按失败补写 |
| 续接后整页超冷读预算 | `AppendBaseMismatch`，补整页（按原规则降级） |
| 装入后、写盘确认前再次崩溃 | 旧记录未结算；新会话缺该场景时下次扫描借用旧条目 |
| 恢复时桌面已有内容 / 白板已进入或有编辑 | 不装入，保持未结算 |

### 5. Tests Required

- `inkStrokeModelerTestTests.exe --uink-store-only`：续接、拒绝（非最后 Canvas、编号缺口、撤回组倒退、Clear 同组）、搬页、回截与封闭、前缀读取丢弃半页。
- `--paged-ink-store-only`：`AppendStrokeContinuesPage`、`AdoptRecoveredPages`、`AppendRecoveryRegistrationFailures`。
- C1.4 隐藏自检（白板存储、PPT 保留页与 lane、桌面退出）不回归。
- 实机：三场景 × 三档 × 三种触发（调试页按钮）；任务管理器结束进程后手动启动；回翻落笔；弹窗期间落笔。

### 6. Wrong vs Correct

- Wrong：把抬笔后的整页截取放在绘制线程上逐笔提交。Correct：单笔快照入队，worker 续接；整页只在失配后的空闲时机补。
- Wrong：恢复时续写旧进程的文件。Correct：读旧写新——桌面/白板装入后由本进程写出，PPT 由存储接管重新编码进新文件。
