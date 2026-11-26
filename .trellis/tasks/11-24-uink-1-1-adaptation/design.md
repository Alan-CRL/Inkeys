# 设计：UInk 1.1 适配与冷页加载提速（C1.4 + C1.5 联合）

- 需求见 `prd.md`；规范副本 `research/uink-v11-spec.md`（下文“规范 XXX-n”指其中的规则编号）。
- 现状依据：2026-11-25 Codex 只读调查（`%TEMP%\inkeys-codex\c14\investigate-report.md`，结论已并入本文）、`research/uink-1-1-draw3-requirements.md`、C1.5 `research/*.md`。行号以调查时为准，实施前复核。
- 路径缩写：`UK\` = `inkStrokeModelerTest/draw3/`（产品与模型器工程共享编译），`D3\` = `Inkeys/Inkeys/Drawing/Draw3/`，`DC` = `D3\Draw3.DrawingController.cpp`。

## 1. 目标与边界

| 做 | 不做 |
|---|---|
| 读写只用 1.1 官方字段；三种橡皮、荧光笔、图形、Clear、页面元数据全部官方化 | 写 1.0；兼容导出（烘焙橡皮、删 Clear）；识别旧 Inkeys 私有字段 |
| 白板与 PPT 改为同一种单文件按页追加结构；桌面改为“Clear 时写当前区间”的快照 | 跨进程续写或恢复（留给 C1.6 与以后的历史画布） |
| 去掉 SHA、回读、重复历史 JSON、索引与多轨；读取只做必要检查 | 翻页后的 GPU 冷重建（留给 C2） |
| 损坏输入不崩溃、不卡死；跳过项写 warning | 扩展渲染器去支持完整级（FollowDirection、任意 Marker 等只做就近回退） |
| 为 C1.6 的抬笔追加与截断恢复留好接口 | 实现抬笔追加与恢复本身 |

## 2. 分层

```
L4 场景服务   PagedInkStore（白板、PPT 共用）   DesktopInkSnapshots（桌面）
                 │ 页快照 ↔ 页块                    │ 区间快照 ↔ 整文件
L3 Draw3 适配  uink_draw3_export / uink_draw3_import / shape_uink_*（快照 ↔ UInkDocument / UInkCanvas）
L2 文件       uink_store（新）：创建、按页追加、页目录、按范围读一页、整读、整理
L1 编解码     uink_codec：1.1 块的编码与逐块容错解码
L0 模型       uink_model：1.1 字段
```

- L0–L3 在 `UK\`，产品与模型器测试共用，**不依赖** `Inkeys::Log`；读取结果带结构化诊断（§8）。
- L4 在 `D3\`，负责线程、触发时机、日志输出。
- 最终删除：`UK\uink_page_file.*`、`uink_file` 中的 SHA revision、自读校验、`UInkOwnedVersion`、追加计划（`AnalyzeUInkAppend`/`ExecuteUInkAppend` 及 `bytesSha256`）、`UInkEditingSession` 等只为旧保存流程服务的部分。**分阶段删除**：S1–S3 新增接口、旧接口保留给尚未迁移的调用方；每个场景迁移完（S4–S6）再删它独占的旧接口，S6 结束时删光。改签名后全仓搜索调用点（`inkStrokeModelerTestTests`、`InkeysHeadlessTests`、`IdtMain.cpp` 自检、`Draw3.Host.cpp`、`Draw3.HiddenWindowTest.cpp`、`Draw3.FailedCleanupFixture.cpp`、`contact_input_tests.cpp` 等）。

## 3. L0/L1：1.1 字段与编解码

### 3.1 写入（编码）

- Header `version = 11`，固定宽度 `array(7)` 不变（规范 HDR-1）。
- 精确类型与位宽（TYPE-1）；私有字段一律不写（Inkeys 目前没有需要私有的数据）。
- 每个 Canvas 版本的 `contentId` 从 0 连续；`undoId` 从 0 只增不减，Clear 独占一组（INK-2/3、CLEAR-4）。
- Canvas 字段映射：

| 数据 | 1.1 写法 |
|---|---|
| 白板 | Workspace `workspaceType = 1`（无私有标记）；第 0 层 Canvas `background = { color: {fallback}, pattern }`（0 Blank / 1 Dots / 2 Grid） |
| PPT | Workspace `workspaceType = 2`、`hostId` = 现有 `PresentationKey`（UUID 文本）；Canvas `slideId`、`binding`（0 SlideId / 1 PageIndex）、`pageState`（0 Active / 1 Retained）、`pageKind`（0 Normal / 1 EndScreen）；缺省值不写 |
| 桌面 | 隐式 Workspace 单例（屏幕批注，HEXT-3），不写 Header Extension |
| Device | 三个场景都用隐式 Device 单例：Canvas 省略 `deviceGuid`，Header `deviceNum = 1`（HEXT-4/6） |
| 白板与 PPT 的 Workspace 引用 | 显式 Workspace 时每个 Canvas 都写同一个 `workspaceGuid`（HEXT-5） |
| 视口 | Draw3 `scale` 固定为 1，但白板可平移：平移量非 0 时写 `viewport = {x, y, scale: 1}`，为 0 时不写。读回时会话内的视角侧表（`whiteboardViewports`）仍然优先，侧表没有才用文件里的值 |

- Ink 映射（`nib` 与 `cap`，规范“笔尖与端帽”）：

| Draw3 笔 | `inkType` | `nib` | 点 `width` 写入值 | 读回内部值 |
|---|---|---|---|---|
| 普通笔 | 1 Pen | 不写（圆） | 直径 `w` | `w` |
| 荧光笔 | 2 Highlighter | `{shape:1, aspect:0.125}`（Fixed 缺省不写） | 原值 `w`（Draw3 渲染时半高下限 0.5px 只是绘制细节，不写进文件，保证原值往返） | `H` |
| 圆形橡皮 | 0 Erase | 不写 | 直径 `s` | `s` |
| 矩形橡皮 | 0 Erase | `{shape:1, aspect:0.6666667}` | `H = (s/2)·√(π/a)` | `s = 2H·√(a/π)` |
| 方形橡皮 | 0 Erase | `{shape:1, aspect:1}` | `H = (s/2)·√π` | `s = 2H/√π` |

  换算只在 L3 的写出/读入边界做；软件内部与 UInk 逻辑都不改。矩形笔尖不写 `cap`（NIB-3）。读入时 `aspect` 与上表相差 ≤1e-4 视为对应橡皮形状；其他 `aspect` 的 Erase 按最接近的形状并写 warning。
- Shape：标准层与现状一致（箭头 Line + Polyline、闭合虚线 8 段、立体按可见性拆段，算法已与 PRESET-9/14/21–23 对齐）。描边补写 `lineCap = 0`（点线依赖圆帽，PRESET-7），`lineJoin` 缺省 Round 不写。`object` 见 §6。

### 3.2 读取（解码）

逐块解码，按规范容错，**不做跨块的全局一致性复核**：

| 情况 | 处理 |
|---|---|
| 首块不是有效 Header（`array(7)`、`type=0`、`version` 10–19、UUID） | 整个文件失败（Error） |
| `version = 10` | 按 1.1 规则尽力读（VER-5），Info 记一次 |
| 结尾截断 | 丢弃最后一个不完整对象，保留之前（FILE-16） |
| 中间无法解码 | 保留失败位置之前的完整对象，停止（FILE-17）；Warn |
| 未知 Type ID、未知键、未知枚举值 | 跳过或按缺省值（TYPE-8/9、NIB-7、CANVAS-12） |
| 字段无效的已知块（缺必填、类型错、非有限数、点损坏） | 跳过该块，计数（INK-19、SHAPE-19/22、CLEAR-12） |
| 坏 Canvas | 跳过它的作用域直到下一个有效 Canvas，计数 |
| Header 统计与实际不符 | 不检查（HDR-5、SAVE-21） |
| 编号空洞、同组多笔 | 接受（TYPE-9、INK-3） |

两个有意例外（S1 审查后确定）：`shape.object` 内部重复已知键只丢弃对象层、保留可绘制的 Shape（SHAPE-14 优先于 TYPE-7 的整块无效）；非第 0 层上被 CANVAS-8 忽略的页面元数据不再检查其内部的 TYPE-7。

作用域区分：可恢复的字段错误只跳过该块；有专门回退规则的字段按规则回退而不是跳过整块（例如 `viewport` 缺字段或非法时整个 `viewport` 取缺省值，CANVAS-22；坏 `background` 按无背景）；同一 Map 里重复已知键使整个块无效（TYPE-7）；坏 Canvas 跳过到下一个有效 Canvas；对象边界无法确定时按 FILE-17 停止。

性能要求：
- 每个顶层对象只解析成模型一次，点直接解到目标数组；不复制 `extra`（只跳过，Inkeys 不读任何私有键）。
- 现有“先 `ScanValue` 有界预检、再 `msgpack::unpack`”（`UK\uink_codec.cpp:197,817,2443`）**S1 先保留**：预检限制了容器、深度与 MsgPack 临时区，是防崩溃的一部分。只有在替换方案能证明同等的上限保护（用同一组恶意语料测试）时才可去掉。
- S1 实测解码耗时，目标 Release 下解码 250 笔的一页 ≤ 5 ms（实测基线后可修正）；达不到再单独提出读取器改造。
- Ink 位移用 `double` 累加（INK-4），累加结果再检查能否缩窄为 `float` 且为有限数。
- **S1b 实施结果（2026-11-25）**：解码改为单遍流式解析（去掉 `ScanValue` 预扫与 `msgpack::unpack` 中间树），行为与 S1 版本逐项一致（冻结的参考实现 + 2 万余例差分测试，S6 随旧路径一起删除）。Release 250 笔×120 点一页约 1.4 ms（S1 版本 14.6 ms）。有意差异两处：递归深度另有内部硬上限 64；调用方把字符串/文件上限放宽到 4 GiB 以上时，EXT32 长度按 64 位计算判为超限（msgpack-c 会窄化）。

### 3.3 资源上限（防崩溃）

所有检查在分配或派生计算之前完成，每项都要有测试：

| 预算 | 规则 |
|---|---|
| 文件与范围 | 整读：文件长度 ≤ 128 MiB（桌面快照与手动导入）；按页读：范围必须落在文件内且 ≤ 单页上限 32 MiB。按页追加的文件本身不受 128 MiB 限制（只按页读，靠整理控制大小） |
| 单个对象 | 字节数 ≤ 32 MiB；嵌套深度 ≤ 32；字符串 ≤ 8 MiB；容器 ≤ 100 万项（沿用 `UK\uink_model.cppm:426`） |
| 分配 | 数组按“剩余字节能容纳的最大元素数”与声明数量取小值 `reserve`；整份读取的模型估算 ≤ 256 MiB、总点 ≤ 400 万 |
| 注册表 | 父链遍历步数 ≤ 注册表条目数（条目数 ≤ 1024），出现循环按 FILE-22 断开 |
| 几何派生 | 初值（S1/S3 实测后可调，调整须记录理由）：单个图形顶点 ≤ 10 万；弧细分每段 ≤ 4096 段；每页累计瓦片足迹访问 ≤ 400 万块次；坐标绝对值 > 1e7 的单项视为无效。超限的单项跳过并计数 |

- **S8 调整（2026-11-26，Claude）**：瓦片足迹预算此前未实现，S8 补上（L3 导入时按 `BuildStrokeTileFootprint` 的保守上界估算，worker 线程）。初值 400 万实测会跳过正常自产内容（一笔 13 万点即超），最终为四重上限（S8 三轮复核收敛）：**每页 2^28 次访问、单项 2^25 次访问、单项包围盒 2^18 块、每页包围盒合计 2^23 块**（128 px 瓦片）。访问次数约束枚举耗时，包围盒约束驻留瓦片与索引（填充的访问数≈驻留数，细笔则反复访问同一批瓦片）。原写法为“每页 2^28 次访问 + 单项 2^25 次访问”：L1 每页至多 400 万点、正常笔宽每段约 50 次，仅凭点数不会触发；单项上限约束安装时临时瓦片向量峰值（足迹枚举逐次追加、最后去重，约 1.3 亿字节以内）；超宽点、粗长线、1e6 像素量级填充等病态项被跳过并计 `l3.footprintBudget`。保存侧对导出结果做同一导入预检，会降级的页保留驻留副本。S7b 合成重页预算占用远低于上限。

超限时停止或跳过（按所在层级），保留已读内容，写 warning。三种结果都要测：能定位坏块时跳过后继续；对象边界无法定位时保留前缀；没有任何有效页时失败。

异常边界：L1–L3 入口统一 `try/catch(...)`（含 `bad_alloc`），以“失败 + 诊断”返回。L4 的失败出口见 §5.5。

## 4. L2：`uink_store`（新）

### 4.1 写入器 `UInkPagedFileWriter`

| 接口 | 行为 |
|---|---|
| `Create(path, header, extension, firstPage)` | 按完整保存的顺序（SAVE-1）：在同目录写临时文件（Header + Header Extension + 第一页的完整 Canvas，满足 SAVE-19），检查编码与写入结果，Flush，`MoveFileExW` **不覆盖**地发布为目标名（目标已存在则换名重试），再以共享读、不共享写的方式打开用于追加 |
| `AppendPage(canvas)` | 编码这一页的完整 Canvas 与内容块；**编码结果超过单页读取上限（32 MiB）时拒绝写入并报告失败**（否则会出现“写得进去、读不回来”的页被驱逐）。一次 `WriteFile` 写到末尾，`FlushFileBuffers`；成功后才更新页目录（SAVE-10/12/13）。短写或 Flush 失败：截断回写入前的末尾（`SetFilePointerEx` + `SetEndOfFile`）并报告失败；截断也失败则**封闭写入器**（之后的写入一律失败、不再追加），避免后续版本接在半截对象后面无法解析。`Create` 的首页同样受此上限约束 |
| `Compact()` | L4 在空闲时按被替代字节的比例与总量调用（阈值 S7 实测后定，初值 50% 且 8 MiB）。写同目录临时文件（紧凑、有序，SAVE-2/3/16），**写的同时算出新页目录**，Flush。**提交点是 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 成功**：之前任一步失败 → 删临时文件，原文件、原句柄、原目录照常使用；之后（关闭旧句柄、重新打开）失败 → 新文件已生效，用已算好的新目录，重新打开失败则封闭写入器（页留在内存、保持脏状态）。`Header.guid` 与各 `pageGuid` 不变（VER-7） |
| `Close()` | 关闭句柄；不整理、不修正 |
| （C1.6 预留）`AppendContent(blocks)` | 只允许追加到最后一个 Canvas；接续 `contentId`、`undoId`（SAVE-6–8）。本任务只在目录里维护“最后一个 Canvas、下一个 contentId、最后 undoId”，不实现该接口 |

- 页目录 `UInkPageDirectory`：`pageGuid → {offset, length}`（以最后写入为准，FILE-8）、被替代字节数、最后一个 Canvas 的键与编号，以及解析页所需的上下文（版本、注册表）。写入器自己维护，**不需要扫描文件**。
- **S2 审查后补充（2026-11-25）**：`Create` 入口解析绝对路径；`Create`/`AppendPage` 写盘前在内存中按页模式解码一次编码结果（默认上限、须 `Complete` 且恰好一个 Canvas），保证“写得进去就读得回来”（不是旧的磁盘回读）；截断回退后再刷盘，失败即封闭并标记“尾部未确认”（目录是最后确认的快照，L4 不得据此驱逐）；`Compact` 原样复制页字节、保留原 `pageIndex` 不重新编号（PPT 按顺序绑定时 `pageIndex` 即幻灯片序号），`pageIndex` 连续性由 L4 负责。
- 一个文件只由创建它的进程写；进程不打开其他进程的文件，所以不需要命名 mutex 和 revision 校验。

### 4.2 读取

| 接口 | 用途 |
|---|---|
| `ReadPage(path, directoryContext, range)` | 按目录读一段字节，用目录里的版本与注册表上下文解码成一个 `UInkCanvas`（白板冷页、PPT 跨 Clear 读上一区间）。实现要求（S1b 审查）：页范围必须作为流式读取器的边界（回看与恢复扫描都不得越出页范围借用下一页字节）；单页解码复用对象循环，由目录注入版本、注册表与文件基址偏移，**不**执行整文档的连续页号、Workspace 当前页等全局校验，也不产生续写判定 |
| `ReadUInkFile(path)` | 整读整解（桌面跨 Clear 撤回、以后的手动导入）。按完整 Canvas 唯一键去重、取最后版本（FILE-8），按 `pageIndex` 排序，相同值按最后出现位置（SAVE-14） |

崩溃后扫描文件重建目录、确定安全追加位置（SAVE-22–24）属于 C1.6，本任务不实现。

## 5. L4：场景服务

### 5.1 目录布局

```
<exe>\Inkeys\Save\Ink\
  desktop\YYYY-MM-DD\HHmmssfff_<请求GUID前8位>[_n].uink
  whiteboard\YYYY-MM-DD\HHmmssfff_<文件GUID前8位>.uink            每个进程一个，首次写入时创建
  presentation\<文稿名>_<key前8位>\YYYYMMDD-HHmmssfff_<文件GUID前8位>.uink   每个进程每份文稿一个
```

- 根目录由 `IdtMain.cpp:4026` 改为 `...\Inkeys\Save\Ink`；`Inkeys\AutoSave\` 不再被任何代码访问。
- 文稿名去掉非法字符、截到 40 个字符；拿不到路径的文稿（进程内身份）同样按 key 分文件夹。
- 名字冲突时加 `_n`；`CREATE_NEW` 保证不覆盖任何已有文件。
- 删除：桌面 `index.json/.bak` 与索引 mutex、PPT 各轨 `index.json/.bak`、轨道与会话目录、跨进程冲突检测、白板 `current` 与版本文件。

### 5.2 白板与 PPT：`PagedInkStore`（共用）

白板与 PPT 的文件结构完全相同（Header + 一个 Workspace + 每页一个 Canvas），差别只在 Workspace 类型、`hostId` 与 Canvas 元数据，所以用同一个服务实现，替代 `D3\Draw3.WhiteboardStore.*` 与 `D3\Draw3.PresentationAutoSave.*` 的存取部分：

- 一个后台线程；队列中加载优先，但**不得越过它所依赖的未落盘写入**（同一页还有排队中的写入时，加载直接用内存数据或等该写入完成）。Clear 触发的写入按先进先出、**不与其他写入合并**；同一页的普通写入只保留最新一份。
- 每个逻辑文件（白板会话、一份 PPT 文稿）一个 `UInkPagedFileWriter` 与页目录。

页写入（`prd.md`“白板与 PPT 的页写入”）：

| 时机 | 写入内容 | 之后 |
|---|---|---|
| Clear | 这一页“上一区间 + Clear”（当前区间为空） | 写入**已落盘**后才可释放上一区间的内存副本；跨 Clear 撤回经页目录读回这一页、取 Clear 之前的部分 |
| 翻页、离开工作区或文稿、退出 | 这一页的完整记录：能撤回到上一区间时“上一区间 + Clear + 当前区间”，否则只有当前区间 | 白板：写入已落盘且页修订号未变、页既不是当前页也不是加载目标时才驱逐（沿用现有条件）；PPT：沿用现有槽驱逐条件 |
| 页没有变化 | 不写 | — |

- **写入状态**：每个写请求带页 GUID、页修订号、槽代次与区间序号；状态分“已排队 → 已落盘（Flush 成功且目录已更新）”。驱逐、释放上一区间副本、迟到结果的接收都只认“已落盘”且各项都匹配的回执。写失败时页保留在内存、保持脏状态，写 Error 日志；写入器被封闭后，该逻辑文件之后的写入都失败，页一律不驱逐。
- **上一区间已释放时的整页写入**：快照里只标记“上一区间在盘上”，由后台线程从页目录读出这一页最后版本中 Clear 及其之前的块，与当前区间拼成新版本后写入（重新编号）。
- 绘制线程只做快照截取（复制可见记录与上一区间的点、样式、图形对象），编码与写盘在后台线程；去掉现在绘制线程上的 JSON 序列化（实测 36–52 ms）。
- 删除白板私有历史 JSON（`inkeysWhiteboardHistory`）、journal 及其整页可逆校验（`DC:845,937,1044`）。

**白板冷读后的撤回状态**（热页，即没被驱逐的页，保留现有完整历史，不受影响）：

1. 冷页丢弃重做、Clear 重做分支与图形修改前版本（11-24 已定）。
2. 当前区间按连续相同的 `undoId` 装回撤回组，可以逐组撤空（不设导入根）。
3. 有上一区间时，撤空当前区间后再撤回就是“撤回 Clear”：装回上一区间（同样按 `undoId` 分组、可逐组撤空），并在内存中建立这次撤回 Clear 的重做分支（与现在热页的行为一致）。
4. 冷读页上再次 Clear、或撤回后离页，按上面的页写入规则写出当时的撤回范围。
- 验收场景：`A → Clear → B → 离页 → 翻回 → 撤空 B → 撤回 Clear 恢复 A → 撤空 A`；二次 Clear 后只保留最近一个区间；撤回到 A 后离页再翻回。

**S4 开工核对后的裁定（2026-11-25，Claude；依据 `%TEMP%\inkeys-codex\c14\s4-precheck-report.md`）**：
1. 撤回粒度：Draw3 运行时一项撤回对应一个绘制项。Inkeys 自产文件每笔独占一组（图形对象本身是一项），外来文件的多笔同组读入时**拆成逐笔撤回**并计回退（就近回退，偏离 INK-3 的“同组共撤回”），不改运行时撤回机制。
2. 热页保留完整运行时历史（含 `clearUndoHistory` 中的图形编辑版本）直到真正驱逐；可由磁盘重建的 `boundaryFallback` 在对应写入确认落盘后释放。
3. 盘上边界：每页记录“已确认落盘的边界身份”（页 GUID + 边界代次 + 页修订号）；上一区间已释放后的整页拼接只认匹配的确认版本，失配时写入失败、页保脏。拼接在 L2 字节层按 Clear 切分并按组重新编号（不经旧 `ProjectDraw3UInkCanvasInterval`/`MergeDraw3UInkCanvasTail`，它们会拆散同组）。
4. 导出跳过了单个图形（S3 冷读预算）时：文件照常提交，该页标记“降级”，不驱逐、写 Warn。
5. `Create` 已发布但重开失败：该逻辑文件锁定为失败状态，本进程内不另建第二个文件，页保持在内存。
6. 页登记：页有内容或背景变化才写；`pageIndex` 为真实页序，允许空洞；Header `currentPageIndex` 写 0（快照，不更新）。
7. 文件生命周期随 Draw3 运行实例（与旧 `WhiteboardStore` 一致），正常即每进程一个。
8. 队列：同页普通写入只在同一 Clear 分段内合并；Clear 写入先进先出不合并；加载遇到同页未落盘写入时直接用内存数据，不越过依赖写入。
9. 完成回执：接收时预留终态位置，唤醒不分配；成功、失败、被合并、关闭取消各结算一次；两处驱逐入口共用同一资格判断（已确认、非当前、非加载目标、文件未封闭且尾部已确认、页未降级）。
10. 实施切分：S4a 新服务与安装路径并行（产品仍走旧路径）→ S4b 切换白板全部调用点与自检 → S4c 删除旧白板路径（`WhiteboardStore`、历史 JSON、`uink_page_file` 及专属测试）。

**PPT**（行为沿用现状，只换存储）：

- 一份文稿在一个进程里一个文件、一个 Workspace。运行时槽仍按 `(PresentationKey, bindingMode)` 隔离：两种绑定方式的页各有自己的 `pageGuid`，Canvas 的 `binding` 字段标明归属；某个槽冷加载时只取 `binding` 匹配的页。
- 普通翻页复用常驻页；槽的驱逐条件不变（已落盘、无 redo、无 Clear 重做分支、无图形编辑历史）。
- SlideID 重排：受影响的页按 GUID 追加新版本以更新 `pageIndex`（SAVE-14）；SlideID 找不到的页写 `pageState = 1`（保留页），重新出现时按 GUID 改回 Active；结束页 `pageKind = 1`、不写 `slideId`，单独处理；页码回退模式按页序配对、不写 `slideId`（CANVAS-13）。
- 页数变化：新增的幻灯片在第一次写入时作为新 Canvas（新 `pageGuid`）追加；删除的幻灯片按上一条变为保留页，不删除。
- `PreviousInterval` 改为读这一页的最后版本，不再整读整份文稿。
- 停放槽的脏页在退出时照常写出；写失败时的重试次数与现有行为一致（S5 简报中按代码核实后写明），最终失败写 Error 日志。

**S5 开工核对后的裁定（2026-11-26，Claude；依据 `%TEMP%\inkeys-codex\c14\s5-precheck-report.md`）**：
1. PPT 页常驻：普通翻页不读盘；只在首次进入某个 lane（`(PresentationKey, bindingMode)`）或该 lane 被驱逐后重进时，**按 lane 批量加载**该 lane 的全部已登记页（含保留页、结束页，按有效 `binding` 过滤），绘制线程成组安装；首次进入从未写过的 lane 直接确认空槽。跨 Clear 撤回是独立的单页 `LoadPreviousInterval`。
2. 每份文稿（完整 `PresentationKey`）一个 `PagedInkStore`，首次需要时懒建，各自一个后台线程；文件中一个显式 Workspace（`workspaceType=2`、`hostId` = key 文本），两个 lane 共用文件、各自的页 GUID。生命周期随 Draw3 运行实例（同白板裁定 7）；无路径文稿只在现有 `process:` key 稳定期间续写。存储根 S5 仍用现有注入根，S6 统一切换。**（S5b 前修订）** 为沿用现有“预留终态槽”保序投递（裁定 6），PPT 存储注册表改由旧 `PresentationAutoSave` 的持有者（Host）持有，生命周期与之相同；回执经 Host 现有泵投递到 WindowController 预留槽。
3. 页级持久化状态（确认修订号、确认边界代次、降级、待写、上一区间在盘）随页 GUID 移动；热重排、冷重投影、删除进保留页、保留页重现、结束页移动都推进受影响页的修订以写出新的 `pageIndex`/`pageNumber`/`pageState`/`pageKind`；保留页沿用原 GUID。未登记且无历史的空白占位不写、不挡驱逐。
4. Clear 与白板相同：Clear 成功后提交“上一区间 + Clear”，确认后释放 `boundaryFallback`；`intervalOrdinal` 取 0/1 语义，另有单调边界代次；撤回 Clear 依次用 `clearUndoHistory`、`boundaryFallback`、`LoadPreviousInterval`。
5. 驱逐：PPT 槽整体驱逐的四处入口共用一个谓词 = 现有历史保护（无 redo、无 Clear redo、无 `clearUndoHistory`、无图形编辑）+ 每个有持久化义务的页都通过 `IsPageEvictable`（实时 `FileState`）。
6. 完成回执沿用 PPT 现有“预留终态槽”保序机制（回执与已排队命令的相对顺序不变），`--draw3-ppt-completion-delivery-test` 迁移后须通过。
7. **按页读取的容错（同时修正 S4a 服务）**：页段可解码但有被跳过的块（L1 Partial、`containsInvalidCompleteBlocks` 等）时照常安装可显示的内容并写 Warn、标记该页“读取降级”；只有页身份或结构错误（GUID/绑定不符、页段无 Canvas、范围越界）才整页失败，此时页保持冷/未安装，**不得**用空白页替代有确认记录的页。白板同样适用。
8. 重试：写入自动重试 0 次（失败后在下一个自然保存点再提交）；首次 lane 加载失败最多自动重试 4 次（250/500/1000/2000 ms），与现状一致。
9. 删除：S5c 删除 PAS（多轨、会话轨、索引、跨进程检测、owned version）、私有 Workspace 类型 128 与私有绑定/页状态/结束页字段的写入与 PPT 专用导入、`MergeDraw3UInkCanvasTail` 及只被 PPT 使用的测试；L1 的 `InkeysPageKind` 私有回退因冻结参考解码器仍在用，留到 S6。
10. 实施切分：S5a 服务与页级元数据准备（产品仍走 PAS）→ S5b 切换产品 PPT 调用点与自检迁移（含 `HiddenWindowTest`、`FailedCleanupFixture` 的 PPT 部分）→ S5c 删除旧 PPT 路径。
11. （S5a 审查后补充）所有按页读取按确认边界核验 Clear 是否存在，失配按结构错误；页段出现临时身份一律失败；`Compact` 是物理整理，保留各页原 `pageIndex`（两个 lane 可能重叠），不追求 CANVAS-3 的紧凑页序；文件中间损坏后整文件读取会在坏处停止（FILE-17），按页读取不受影响——S7 整理策略须包含“损坏段所在页已有新版本后触发整理”。S5b 调用方须跨驱逐保留页序 lane 的可信旧页数，冷投影前检查页数变化。

### 5.3 桌面：`DesktopInkSnapshots`

- Clear 时把被清空的当前区间写成一个快照文件（只有这一个区间，不带 Clear）；退出时写当前区间。保持现有触发条件（`config.Drawing.AutoSave.Enable` 与非空判定）。
- 一次完整写入：同目录临时文件 → Flush → 不覆盖地发布（SAVE-1）；不回读、不比对 GUID、不写索引。
- 跨 Clear 撤回：先用内存副本，内存已释放时读该快照文件（路径保存在服务内存中），与现状一致。

**S6 开工核对后的裁定（2026-11-26，Claude；依据 `%TEMP%\inkeys-codex\c14\s6-precheck-report.md`）**：
1. Clear 与 Redo Clear：Clear 前用 `CaptureCanvasTail` 截取一次，CPU Clear 成功后才提交保存（不再在 Clear 前提交、不再复制两次）；Redo Clear 沿用现有触发资格。活动桌面、停放桌面（PPT/白板活动时取 `desktopSlot`）、故障封笔沿用现有资格与开关门控。
2. 文件：单页、隐式 Workspace 与隐式 Device、`pageIndex=0`、`pageNumber=1`、不含 Clear；文件名 `desktop\YYYY-MM-DD\HHmmssfff_<请求GUID前8位>[_n].uink`。
3. 发布：L2 新增一次性发布入口（同目录 `CREATE_NEW` 临时文件 → 精确写入 → Flush → 关闭 → 不覆盖发布，冲突换 `_n`），**发布成功即提交点**；保留写盘前内存解码自检（同 `ValidateStorePage`），去掉磁盘回读、SHA、Header GUID 比对、日索引与命名 mutex。
4. 新服务 `DesktopInkSnapshots`（Host 持有），保存不合并；冷读按保存回执登记的实际路径 `ReadUInkStoreFile` 整读 + 单页 `ImportUInkV11Page`；热历史（图形编辑）与降级副本照旧保留；单项损坏按 1.1 容错安装可用内容并 Warn。
5. 迟到结果：保留 Clear 代次与每次加载的请求身份、页 GUID、加载时历史修订检查；迟到终态只核销自身，只有匹配当前恢复点的才变更状态。
6. 完成合同同 S4/S5：准入时预留终态、无分配投递（沿用 PPT 的 Host 预留槽模式保序）、读写共同异常边界；正常退出排空已接收的保存，未执行读取逐个以取消结算，ACK 后只核销不安装。
7. C09/C11 夹具：生产者与独立读回子进程改为通过夹具协议传递确定的发布路径（不再扫描日索引），收据去掉索引 SHA、序号、storage session 与显式 Workspace GUID，改验隐式拓扑，保留独立文件字节与内容验真。
8. `HasValidUInkPresentationBinding` 保留但只按官方 `binding`/`pageKind`/`slideId` 判定（S6b 删 `InkeysPageKind`）。归档探针文件中对旧代码的引用作为历史保留，验收搜索单列。
9. 切分：S6a 桌面切换（含 C09/C11 夹具、停放桌面自检、HiddenWindow 橡皮往返迁移；旧模块暂留供模型器测试编译）→ S6b 删除旧路径、私有字段、冻结参考解码器与差分测试、codec 跳过 `extra` → S6c 存储根切到 `Inkeys\Save\Ink`（含 C09/C10 夹具与监督器清理路径）。

### 5.4 并发与重启

- 每个进程只写自己新建、名字带 GUID 的文件（发布时不覆盖已有文件）；不扫描、不读取其他文件（本任务没有跨进程恢复）。新旧进程并存时互不影响。

### 5.5 退出与失败出口

- 退出沿用现有流程：Host 发 `PrepareExitAutoSave`，绘制线程截取活动页与停放页（含停放的桌面），三个服务排空队列后关闭文件；允许阻塞几秒。配置开关只门控桌面，与现状一致。
- 每个请求在**任何**失败下都必须产生完成回执：准入失败（服务已关闭、请求无效）立即返回失败；执行中的异常在后台线程任务边界捕获；完成队列在接收请求时预留位置，投递与唤醒不分配内存、不抛异常；关闭时把未执行的请求逐个以失败回执结束。失败回执同样清除 pending、唤醒等待者，数据留在内存。
- 现有的退出等待没有超时（`Draw3.Host.cpp:2850-2894`），本任务不改。根目录在 exe 同目录的本地磁盘上，阻塞 I/O 的风险记录在父任务的“异常退出与卡死兜底”待办里统一处理。
- 自检覆盖：活动页与停放页的退出保存、图形设备故障下的退出、后台任务抛异常、写失败。

## 6. 图形对象层映射

`extra.inkeysShape`（v7）→ 首部件的官方 `object`：

| v7 | 1.1 |
|---|---|
| `id`、`revision`、`transform`、`aux`、`partCount` | 同名字段 |
| `construction.tool.preset` / `legacyKind` | `preset`：注册表名（`line`、`arrow`、`rectangle`、`rounded-rectangle`、`circle`、`ellipse`、`triangle`、`parallelogram`、`rhombus`、`trapezoid`、`regular-polygon`、`polyline`、`polygon`、`arc`、`sector`、`cuboid`、`cube`、`cylinder`、`cone`、`prism`、`pyramid`、`legacy-solid-line`、`legacy-dashed-line`、`legacy-outline-rectangle`、`legacy-filled-rectangle`） |
| `construction.{start,end,parameters}`、`tool.{creationMode,sides,rectangleCornerRadius,constraint,constraintVertex,rightSolid}` | 展平到 `construction`：`start`、`end`、`vertices`、`mode`、`sides`、`cornerRadius`、`constraint`、`constraintVertex`、`sweep`、`height`、`baseSize`、`radius`、`baseRotation`、`mirrored`、`offset`、`right`；点写成 `{x, y}` |
| `style`（stroke/fill 开关、strokeRgb/fillRgb、strokeOpacity/fillOpacity、width、linePattern、dashOffset） | `object.style`：`stroke`、`fill`、`strokeColor`/`fillColor`（Color Map，只写 `fallback`）、`strokeOpacity`、`fillOpacity`、`width`（局部单位，部件描边宽 = `width × √|det T|`）、`linePattern`、`dashOffset`（Canvas 单位） |
| 根里的 `geometry`、`parts`，`kind`、`legacyKind` 冗余项 | 删除（由构造派生） |
| 后续部件的 6 键引用 | `{ ref, partIndex }` |

各预设的必填 `construction` 键按规范附录“平面预设 / 立体预设”表；21 个官方名与 4 个 `legacy-*` 各写一个往返测试，断言名称、参数、部件顺序、部件数与编辑后的重建结果。

读取：
- `preset` 认识、`construction` 有效、重建出的部件数与 `partCount` 一致 → 重建为 Draw3 可编辑图形。**不再**逐值比对几何（去掉 4ε 复核与精确键数检查）。Inkeys 自己写的部件就是同一算法的结果，差别只在浮点误差（PRESET-1）。外来文件的预设对象若与文件部件有细微差异，以重建结果显示：这是有意偏离 SHAPE-15“以文件部件为准”，换取不必在 Draw3 里新增“显示部件、编辑时才重建”的状态。
- 对象层完整但没有 `preset`、预设不认识、`construction` 无效或部件数不符 → 读成 Draw3 已有的 `Kind::Compound` 对象：部件原样保留、整体选择/移动/删除、一个撤回项（与现有无构造对象的往返行为一致，`DC:14147`、`shape_uink_tests.inl:38`），写 warning（SHAPE-13/15、PRESET-4/5）。
- 对象层不完整（部件缺失、数量不符、顺序不连续、`ref` 找不到、`id` 重复）→ 相关部件作为独立的普通图形显示，写 warning（SHAPE-14），不拒绝整页。
- 旧版预设（`legacy-*`，规范说以部件为准）：实线、虚线直接按部件；旧矩形的圆角取部件的 `cornerRadiusX`，**不按当前屏幕 DPI 重算**。S3 核实：Draw3 旧矩形只能按 `min(4×显示比例, 半短边)` 动态计算圆角，无法承载显式圆角。Claude 裁定：文件圆角与当前缩放下的计算值一致时恢复为旧矩形（同机自产无损），否则读成 `Kind::Compound` 按文件部件精确显示。

## 7. 外来内容的就近回退

| 内容 | Draw3 处理 |
|---|---|
| 扩展颜色（HDR） | 用 `fallback`（COLOR-2） |
| `opacity` 越界 | 钳制（INK-18、SHAPE-23） |
| 非 Inkeys 取值的 `nib`（其他 aspect、FollowDirection） | Pen：按圆形笔尖、`width` 当直径；Highlighter：Draw3 荧光笔只有固定 1:8 矩形笔尖，保持该近似显示（S3 审查后确认）；Erase：按最接近的形状；写 warning |
| `cap` 非 Round（圆形笔尖） | 按 Round；warning |
| Advanced Highlighter | 按 Highlighter、用块级样式 |
| 未知 `inkType`（非 0） | 按 Pen（INK-13） |
| Shape 任意 `dashArray`、Marker、`lineJoin` | 能映射到 Draw3 四种线型的映射，否则按实线；Marker 不画端头；warning |
| Square（`shapeType 3`） | 作为等边矩形 |
| `viewport` | `scale = 1` 时照常应用平移；`scale ≠ 1` 按缺省 `{0,0,1}` 显示并 warning（Draw3 尚不支持缩放，C2 之后再兑现） |
| Media | 跳过；warning |
| 多 Device | 每页取第一个注册 Device 上的 Canvas；该 Device 没有这一页时取第一个有这一页的 Device（FILE-9） |
| 多图层、嵌套 Workspace | 各层按 `layerIndex` 从小到大叠成一层；子 Workspace 当独立页序（规范“读取回退”）。S3 审查后：`layerIndex > 0` 图层中的 Erase 笔画跳过并计回退（Draw3 单层无法表达图层作用域，宁可多显示也不误擦下层） |
| `renderOnlyWhenLatest` | 忽略标记，全部显示 |
| 页内多个 Clear | 只保留最后一个 Clear 及它之前的一个区间（与 Inkeys 每页最多一个 Clear 一致），更早的丢弃 |

## 8. 诊断与日志

- L1–L3 返回 `UInkReadReport`：结果（Complete / Partial / Failed）、按原因分类的跳过计数（原因枚举 + 首个出现的字节偏移 + 页序号）、截断与中间停止的位置、版本提示、耗时分段。
- L4 在每次读取后输出（新增日志模块 `Module::UInk`，`Log.cpp` 模块名表同步加一项）：
  - 成功：`Debug` 一行汇总（文件、页、笔数、字节、各段耗时）。
  - 有跳过或回退：`Warn` 一行汇总（按原因计数与首个偏移），不逐笔输出。
  - 失败：`Error` 一行（原因、偏移、文件）。
  - 写入失败：`Error`；页保留在内存不驱逐（沿用现状）。
- C1.5 的 `[Draw3Diag][page-load]` / `[page-save]` 计时保留到验收对比完成，字段随新链路调整（去掉 `parseMs`、`journalMs` 等），收尾时与用户确认是否长期保留。

**S7 开工核对后的裁定（2026-11-26，Claude；核对报告 `%TEMP%\inkeys-codex\c14\s7-precheck-report.md`）**

1. 切分：S7a 统一日志 → S7b 计时与无窗口基准 → S7c 空闲整理 → S7d 足迹移到后台（是否做由 S7b 的 Release 数据决定）。
2. 日志：`Module::UInk` 加在 `Log.h` 枚举 `Count` 前并同步名称表；L1–L3 只返回结构化数据，L4 worker 在每次**实际读盘**的终态输出一行（PPT lane 每个实际读的页一行，空 lane 不输出；Full 保存为拼接 Clear 前缀的读盘也算），写入终态（Create/Append/Publish）失败或降级也由 worker 输出；格式化与路径转码在 worker、先查 `Enabled`、限制单行长度。DC/Host 只保留各自独有的准入、身份/安装（`stage=install`）、快照和退出屏障消息，删掉与 worker 重复的同类消息。判级：完整读 Debug；有跳过、L3 就近回退或拆撤回组 Warn（不改变现有“降级页不驱逐”的语义）；失败 Error。偏移区分 L1 文件偏移、L2 页范围起点、L3 偏移未知。`--draw3-whiteboard-storage-test` 的真实日志断言随之迁到 `[WARN ][UInk]`。
3. 计时：L2 `ReadRange` 增加 `ioMs`；沿用 L1 `decodeMicroseconds`、L4 `importMs`；保留 `readMs` 并注明它包含 `ioMs`+`decodeMs`+收尾；`installMs` 在输出 Warn 之前结束采样；保存行补 `pageBytes` 并区分写入终态与合并/取消。C1.5 两个标签保留到 S8 对照验收后再与用户定去留。
4. 基准：原 98/246 笔页几何已无法取得，新增早退开关 `--draw3-whiteboard-load-benchmark`（复用真实 `PagedInkStore` 冷读与 `SettleWhiteboardPageLoad`），用同规模合成页建立新基线，Debug 与 Release 各测一轮，标明与 C1.5 旧数据只能作量级参照；真机端到端对照在 S8 用 `[page-load]` 日志。
5. 整理：`PagedInkStore` worker 在队列空、无在途业务、未关闭、写入器健康时做低优先级整理，初值“被替代字节 ≥ 8 MiB 且 ≥ 50%”；按 `pageGuid`+物理版本记录损坏段，同页新版本确认后旧坏段即使低于阈值也安排整理；若文件中还有已知“仍是最新版本”的坏页，允许推迟并记录原因；失败后需要新的成功写入或退避才再试；关闭时不启动新整理，进行中的整理纳入排空。L2 `Compact` 结果显式区分“替换已提交”与“重新打开是否成功”，L4 据此在锁内发布写入器失败/封闭/尾部未确认状态；整理不占 Host 完成槽，不清除页的降级或脏标记。

## 9. 冷页加载的 CPU 与 IO 预算

| 段 | 现状（Debug，98/246 笔页） | 改后 |
|---|---|---|
| 离页截取 + 序列化（绘制线程） | 36–52 ms | 只截取快照，序列化移到后台 |
| 后台整文件重写 | 120–150 ms | 一次 `WriteFile` 追加一页 |
| 读盘 + 整文件 SHA + 解码 | 41–61 ms | 只读这一页的字节、不哈希 |
| 历史 JSON 解析（绘制线程） | 76–97 ms | 删除 |
| journal 校验 | 2.6–3.4 ms | 删除 |
| 足迹 | 15–39 ms | 实测后决定是否移到后台线程（只依赖笔画几何，可在后台算好随快照交给绘制线程） |
| GPU 冷重建提交 | 121–184 ms | 不在本任务（C2） |

**S7b 实测（2026-11-26，合成 98/246 笔重页，详见 `research/s7b-load-benchmark-20261126.md`）**：Release 冷读一页 CPU 合计 p50 约 8.3 ms（读盘 0.15–0.26、解码约 1.1、导入约 0.2–0.3 ms 在后台；足迹 5–6 ms、安装约 1 ms 在绘制线程）；Debug 约 40 ms。**足迹本任务不移到后台**（Claude 裁定，用户可否决）：绘制线程上合计 6–7 ms 在一帧之内，搬迁需新增准备结果路径并抬高 PPT 批量 lane 的等待内存，收益有限；C2 解决 GPU 冷重建后再按同一基准评估。

## 10. 测试策略

- 模型器测试（`inkStrokeModelerTestTests`）：
  - 编解码往返：每种笔、橡皮换算往返、荧光笔、Clear、页面元数据、图形对象（21 个预设 + 旧版 4 种）。
  - 写出字节夹具：Header 的 `str8` GUID 与固定宽度、`type:uint16`、编号 `uint32`、枚举 `int32`、几何 `float32`；多部件共用一个 `undoId`；点线带 Round 端帽；Clear 前后独占撤回组；每个 Canvas 的 `workspaceGuid`。
  - 容错语料：截断在每种块中间、中间乱码、超大数量声明、深层嵌套、非有限数、重复键、未知块与键、坏对象层；断言不崩溃、保留前缀、诊断计数正确。
  - 按页追加：多次替代同一页、目录正确、`ReadPage` 与整读一致、整理前后内容一致、短写后截断回原末尾、截断失败后写入器封闭。
  - version 10 样例按 1.1 尽力读。
  - 迁移：删除依赖 SHA、自读、私有字段、owned version、页文件旧语义的断言；保留行为目标（并发保护改为“只写自己的文件”）。
- 产品自检（`IdtMain` 的 Draw3 自检入口与 `DC` 内自检）：白板翻页冷读、Clear 写入、§5.2 的冷读撤回验收场景、PPT 同进程再进入续写、两种绑定方式、保留页与结束页、`PreviousInterval`、桌面 Clear 快照与跨 Clear 撤回、写入失败时不驱逐、§5.5 的退出与失败出口。
- 每步做一次变异验证（故意改坏后确认用例失败）。
- 实机：用户用重页面翻页，对比 C1.5 基线；损坏文件人工投放验证不崩溃。

## 11. 风险与待 C1.6 解决的问题

- **按页追加时断电**：新版本 Canvas 已写完但内容块只写了一部分时，按 FILE-8 它会替代旧版本，丢掉这一页的部分内容。一次 `WriteFile` + Flush 缩小了窗口但不能消除。恢复策略（例如崩溃恢复时比对或保留前一完整版本）留给 C1.6 设计。
- **Header 统计滞后**：按页追加期间 `pageNum`、`currentPageIndex` 不更新（SAVE-20、HEXT-10）。Inkeys 读取不依赖它们。
- **测试迁移量大**：`uink_tests.cpp`、`uink_page_file_tests.cpp`、`presentation_autosave_tests.cpp`、`desktop_autosave_tests.cpp`、`shape_uink_tests.inl` 有大量针对旧机制的断言，需按行为目标重写，防止“删用例凑通过”。
- **S8 全量审查后记为已知限制（2026-11-26，Claude 裁定，留给 C1.6/后续）**：
  - PPT 绑定槽冷读没有整槽累计预算（多张单独合规的重页一次物化）；白板/PPT/桌面线上只读本进程自己写的文件，累计量受真实书写量约束。以后若支持导入外来文件或 C1.6 批量恢复，需要按累计字节、点数、安装工作量分批。
  - 按页追加的文件不受 128 MiB 限制，而整读入口在 128 MiB 拒绝整个文件；线上不整读分页文件。C1.6 恢复扫描需要有界的流式整读或分页枚举入口。
  - 中段坏页的整文件可读性：有“仍是最新版本”的坏页时整理推迟，整读按 FILE-17 会在坏段停止；线上只按页读不受影响。C1.6 恢复需区分“页目录可读”与“整文件已恢复”，或对已知中段损坏改用 SAVE-18/24 的完整替换。
  - 整理保留原 `pageIndex`（稀疏白板页序、PPT 两绑定槽重叠），偏离 CANVAS-3/SAVE-16 的紧凑页序；兼顾两者需要新的导出映射或规范裁定。
  - 不做 `\?\` 扩展长度路径（与旧实现一致）；临时文件叶名保证不长于目标文件名。
