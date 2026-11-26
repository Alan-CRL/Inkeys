# Inkeys3 UInk 1.1 与冷页加载调查

【码】核对的是 `refactoring/draw` 当前工作树，包含未提交的 C1.5 计时。调查全程只读，未修改文件，未运行构建或测试。下文 `DC`、`AS`、`PAS`、`WB` 分别指 [DrawingController](/D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp)、[AutoSave](/D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp)、[PresentationAutoSave](/D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp)、[WhiteboardStore](/D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardStore.cpp)。`spec` 指 [UInk 1.1 单文件规范](/C:/Users/alan-/AppData/Local/Temp/inkeys-uink11/spec.md)。

## A. 架构与线程

| 层 | 入口、线程与调用者 |
|---|---|
| 模型 | 【码】`uink_model.cppm:89,345,365,426,474` 定义 Header、Canvas、Document、限制、结果及诊断。无自有线程。产品、模型器工程和模型器测试编译共享实现。`Inkeys.vcxproj:1115`、`inkStrokeModelerTest.vcxproj:274`、`inkStrokeModelerTestTests.vcxproj:155`。 |
| 编解码 | 【码】同步 `DecodeUInk`、`EncodeUInkDocument`、`EncodeUInkAppendObjects`。产品由文件层在存储 worker 内调用，测试可直接调用。`uink_codec.cpp:2413`、`uink_codec_encode.cpp:1293,1315`、`uink_tests.cpp:964`。 |
| 整文件 I/O 与追加 | 【码】桌面、PPT worker 用 `ReadUInkFile`、`SaveUInkFile`，PPT 另用 `SaveUInkFileWithOwnedVersion`。`AnalyzeUInkAppend`、`ExecuteUInkAppend` 是同步接口，调用点在模型器测试。`uink_file.cpp:1032,1392,1444,1688`、`AS:704,774,1145`、`PAS:623,1572,1801`、`uink_tests.cpp:3226`。 |
| 按页文件 | 【码】`ScanUInkPageFile`、`ReadUInkPageFile`、`RewriteUInkPageFile` 管页偏移和物理版本。白板单 worker 调用，绘制线程接收结果。`uink_page_file.cpp:111,163,172`、`WB:73,104,115`、`DC:18531`、`uink_page_file_tests.cpp:32`。 |
| Draw3 转换 | 【码】产品绘制线程采集桌面可见项、PPT 全页快照、白板页尾与历史。worker 做 `ExportDraw3SnapshotToUInk`，桌面/PPT worker 分别用 `ImportDraw3UInkDocument` / `ImportApplicationOwnedPresentation`，绘制线程再物化。白板从历史 JSON 安装。通用 `CaptureDraw3UInkExportSnapshot` 的调用在模型器测试。`DC:1951,2043,18413,18480,18571,2186,22520`、`uink_draw3_export.cpp:334`、`AS:704`、`PAS:1587`、`uink_draw3_capture.cpp:57`、`uink_tests.cpp:3735`。 |
| Shape 元数据 | 【码】`StandardShapeParts` / `AppendParametricShape` 生成标准部件，`Encode/DecodeInkeysShapeMetadata` 处理 v7，`ImportCanvasContent` 组装。随 worker 导入导出执行，白板历史也在绘制线程调用同一元数据编解码。`shape_uink_standard.inl:77,116`、`shape_uink_metadata.inl:238,268`、`uink_draw3_import.cpp:141`、`DC:825,868`。 |

## B. 字段差距

| 块 | 当前编码与 1.1 改动 |
|---|---|
| Header | 【码】官方 `array(7)`，固定宽度写入、GUID `str8`，读端硬判 `version == 10`。库只记录统计不符，桌面导入要求 `pageNum > 0`，PPT 要求与实际页数相等。`uink_model.cppm:16`、`uink_codec_encode.cpp:410`、`uink_codec.cpp:826,2307`、`uink_draw3_import.cpp:317,398`。 【推】写 `11`，保留首块检查，按实际流计算页数，追加保持 Header 与 GUID。按产品决策可安全拒绝 `10`，这与 `VER-5` 的尽力读取要求有差距。`spec:848,862,716,2570`。 |
| Header Extension / Workspace / Device | 【码】注册表、`hostId/currentPageIndex`、父 GUID、Display/Window 几何和可选 `hardware` 已有官方编码。桌面、白板、PPT 日常快照用隐式 Device。白板 `1 + extra.inkeysWorkspace`，PPT SlideID 为 `2`，页序绑定为私有 `128`，Workspace 和 Canvas 都带 `inkeysBindingMode`。`uink_codec_encode.cpp:505,551,639`、`DC:2062,18509`、`uink_draw3_import.cpp:266,419`、`uink_draw3_import.cppm:14`。 【推】白板只写 `1`，PPT 两种绑定都写 `2`，绑定移入 Canvas。`128+` 仍是规范私有范围。保留现有 Device 字段以及隐式单例，未解析的引用按临时根处理。`spec:917,925,937,1015,1068`。 |
| Canvas | 【码】`pageGuid/pageIndex/pageNumber/layerIndex/layerNumber/slideId/viewport` 已是官方字段。导入导出限定 `scale=1`，白板冷读优先恢复会话侧表。`background` 无官方存储，白板写历史 JSON 的 `rgb/pattern`，另有 `extra.inkeysPageState`、`inkeysPageKind`、`inkeysBindingMode`。`uink_codec_encode.cpp:713`、`uink_draw3_export.cpp:62,377,427`、`uink_draw3_import.cpp:448`、`DC:18493,18589`。 【推】第 0 层写 `background={color,pattern}` 和官方三枚举。缺省为 Active、Normal、SlideId，页序绑定和结束页可省 `slideId`。缺失 SlideID 的绑定目标保留为未绑定。按四元组取末版，按 `pageIndex` 排序，同值以末版位置决胜，`pageNumber` 用于显示。安装时兑现合法的 `viewport`。`spec:1123,1149,1157,1187,1197,694`。 |
| Ink 与编号 | 【码】`type=3`、`contentId/undoId/inkType/color/opacity/texture/points` 与可选最新组已编码。点首绝对后相对，当前以 `float` 累加。Draw3 导入额外要求编号等同数组下标、撤回组无跳号，普通同组笔迹也会拒绝。`uink_codec_encode.cpp:769`、`uink_codec.cpp:1447,2112`、`uink_draw3_import.cpp:141,238`。 【推】写时每个 Canvas 版本从零连续分配 `contentId`，`undoId` 从零非递减，同组共撤回。读时保持流序，允许编号空洞，以 `double` 累加位移。`spec:1342,1348,2281,692`。 |
| 笔尖与端帽 | 【码】非圆橡皮写 Ink 顶层私有 `inkeysEraserShape`，点宽原样存等面积圆直径。荧光笔只写 `inkType=2` 和点宽。`nib/cap/lineCap/lineJoin` 均未编解码。`uink_codec_encode.cpp:769,798,959`、`uink_codec.cpp:1341,1390,1616`、`uink_draw3_export.cpp:166`。 【推】以官方 `nib` 取代私钥。荧光笔 Rectangle/Fixed `a=1/8`，矩形和方形橡皮 `a=2/3、1`，仅 I/O 边界做 `H=(s/2)√(π/a)` 与 `s=2H√(a/π)`。圆形沿用直径、默认 Round。矩形忽略 `cap`，Shape 处理 `lineCap/lineJoin`，缺省均 Round。`spec:2155,2163,2181,2205,2216`。 |
| Shape 几何、描边、填充 | 【码】`type=5`、基本几何、`stroke/fill`、Marker、`dashArray/dashOffset` 已有官方编码。产品导出 Rectangle 走 `2`，通用 codec 支持 Square `3`，Draw3 导入为等边 Rectangle。箭头写 Line＋Polyline，Draw3 标准部件导入拒绝 Marker、非预置 dash 和扩展色。现有 dash 已按最终像素写，如 `[4w,6w]`。`uink_codec_encode.cpp:929,959,987`、`shape_uink_standard.inl:3,38,73`、`shape_uink_import.inl:71,146`、`shape_core.cpp:92`。 【推】保留预设箭头两部件，普通开放线支持 OpenArrow。Square 用于需要保持等比的对象，等边尺寸本身不足以辨认编辑约束。保留像素单位和点线零长度段，按规范处理任意有效 dash、Marker、Solid fill 与局部回退。`spec:1558,1596,1631,2396`。 |
| Shape `object` | 【码】v7 首部件 `extra.inkeysShape` 存 `id/revision/kind/creationMode/legacyKind/geometry/transform/style/aux/parts/construction` 与 `partIndex/partCount`。后续用六键私有引用。`shape_uink_metadata.inl:109,193,238`、`shape_uink_standard.inl:145`、`shape_uink_import.inl:32`。 【推】`id/revision/transform/aux/partCount` 直迁。`construction.tool.preset` → `object.preset`，`legacyKind` → `legacy-*`，如 `RoundedRectangle→rounded-rectangle`、`RegularPolygon→regular-polygon`、`SolidLine→legacy-solid-line`。其余按注册表小写连字符。将 `tool/parameters` 展平为预设字段，如 `creationMode→mode`、`rectangleCornerRadius→cornerRadius`、`rightSolid→right`，点改为 `{x,y}`，圆底尺寸取 `radius`，`style.strokeRgb/fillRgb` 改 Color Map。删私有重复 `geometry/parts`，后续部件只写 `ref/partIndex`，部件几何直接显示。`shape_core.h:33,78`、`spec:1646,1668,2314,2335,2378,2441`。 |
| Clear、Color | 【码】Clear 已写 `type=6/contentId/undoId/extra`，PPT 已生成边界，桌面快照只采可见尾段。Color codec 已有 `fallback/space/components` 和回退，Draw3 只写 RGB，导入会拒绝扩展色。`uink_codec_encode.cpp:398,1076`、`uink_codec.cpp:726`、`uink_draw3_export.cpp:317`、`DC:1984`、`uink_draw3_import.cpp:39`。 【推】三场景用官方 Clear 独占组，Inkeys 每页至多一个。现有 SDR 路径用 `fallback` 显示合法扩展色，坏扩展字段回退。`spec:2010,2033,2044,2089,2108`。 |

## C. 整页拒绝、额外扫描与资源风险

| 路径 | 拒绝条件与开销 |
|---|---|
| 通用 decoder | 【码】空文件、坏 Header、超预算会拒绝。每块先递归 `ScanValue` 再 `msgpack::unpack`，`ValidateMap` 后按键线性查找，`extra` 递归复制。重复已知键、坏必填值、任一点损坏、无效样式、错误的块位置或 Clear 同组，通常跳过块。坏 Canvas 结束前一作用域，后续内容也会跳过直至下一有效 Canvas。截尾保留前缀，无法解码的中间字节停止。注册表、拓扑、编号、Header 统计与总点数另行遍历。`uink_codec.cpp:197,390,428,609,702,1387,1729,2341,2413,2538,2577,2660,2743`。 |
| 白板 `page_file` | 【码】按页读先整文件 SHA-256、比 revision，再编码并解包全页目录，用目标页字节拼合后完整解码。范围、GUID、页数、任一回退、临时身份、未知块、坏块、编号修复或非 `Complete` 均失败。扫描还要求单层、连续页序、精确页数、仅 Ink/Shape/Clear、尾部完整，最后逐页复读验证。`uink_page_file.cpp:39,53,69,87,94,111,164`。 |
| Draw3 内容导入 | 【码】单个不支持的 Media、Ink 最新组/扩展色/点样式、越界编号、异常撤回组、Shape 样式或重复对象 ID 可使整份导入失败。Shape `extra` 只接受唯一 `inkeysShape`，构造和工具分别要求 4、7 键，引用要求 6 键，部件必须连续且身份、序号、数量、撤回组一致。元数据 `Validate` 重算私有派生几何，用 `4×float epsilon×max(1,abs(a),abs(b))` 比较，再构建本体/辅助几何核部件数。这个比较针对私有派生几何，线上标准部件只核分组，显示从私有对象重建。`uink_draw3_import.cpp:39,141,317`、`shape_uink_import.inl:3,32,71`、`shape_uink_metadata.inl:141,268`、`shape_core.cpp:156,448`。 |
| 场景与 JSON | 【码】白板绘制线程解析唯一 `inkeysWhiteboardHistory`，拒绝重复 JSON 键、错误版本/主题、坏笔、重复/越界引用、非法 redo/floor、坏 Clear 子树。journal 复制候选，撤到底、做到尾、退回原游标并比可见性及形状。桌面索引 JsonCpp 逐条验证 GUID、序号、路径、时间和文件是否存在，单项坏则回退 `.bak`。PPT 每次选轨读五轨 JSON，坏主索引读 `.bak`，根恰 3 键，条目身份、路径、SlideID、revision 严查，保存清理又读主备。PPT 加载另卡会话/绑定、整文件 revision、Header 统计、页/设备/保留页/结束页拓扑、导入及安装。桌面跨 Clear 还卡 `Complete`、坏块/编号标志、文件 GUID 和投影。`DC:845,937,1044,18555,2186`、`AS:387,449,525,697`、`PAS:319,375,423,722,797,1554`、`uink_draw3_import.cpp:398,448,515`。 |
| 哈希及回读 | 【码】整文件读为文件长度分配缓冲、读盘、内存 SHA、解码。通用保存对编码结果算 SHA，临时文件全量自读解码/哈希，覆盖时再查旧目标和备份，提交后再读目标 revision，清理证明也可重哈希。桌面保存后再全读认 GUID。PPT 保存会重读待发布、旧名或索引指向的版本。白板重写时验旧 SHA、复制旧页、Flush、扫新文件含 SHA 和逐页回读，清理前再哈希。`uink_file.cpp:422,530,565,631,1059,1162,1189,1250,1316,1348`、`AS:745,774`、`PAS:1240,1325,1390`、`uink_page_file.cpp:113,133,151,198,211,220`。 |
| 上限与崩溃面 | 【码】通用上限为文件 128 MiB、估算模型 256 MiB、25 万块、单块 32 MiB、深度 32、字符串 8 MiB、容器 100 万项、总点 400 万。文件长度、容器、注册表、点、dash、私有部件/顶点及 JSON 数组会驱动容器分配。白板页扫描分块重试 unpack，哈希循环消费整个实际文件，无总文件长上限前置；JSON 历史递归限 1，Shape JSON 转换限 16。`uink_model.cppm:426`、`uink_codec.cpp:657,1094,1447,1645`、`uink_page_file.cpp:39,69,100`、`shape_uink_metadata.inl:103,170,295`、`DC:798,941,951`。 【推】长父链和拓扑回扫可达二次量级，多边形自交/耳切与超宽长笔的瓦片枚举可远超输入字节成本。桌面 `noexcept` worker 加载分支外层缺少兜底，结果 `make_shared` 失败可终止进程。保留硬预算、几何/瓦片工作量上限和任务级异常边界。`uink_codec.cpp:2029,2234,2282`、`shape_core.cpp:188,236`、`Draw3.InkHistory.cpp:234,336,691`、`AS:732,1145,1187`。 |

【码】C1.5 代码把 `readMs` 记为后台整段。基线记录为 Debug ARM64、2.7 MB 文件、98/246 笔：冷页总计 **290–370 ms**，首次 569 ms；读盘＋SHA＋解码 **41–61 ms**，历史 JSON **76–97 ms**，足迹 **15–39 ms**，journal **2.6–3.4 ms**，GPU 冷重建命令提交 **121–184 ms**。离页采集 **36–52 ms**，后台重写 **121–152 ms**，首次排队 129 ms。日志未单拆哈希，也没有同口径 PPT/桌面基线。`DC:18545,18567,27287`、`WB:87,134`。[C1.5 实测记录](/D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/11-24-whiteboard-uink-load-speed/research/load-timing-logs.md:47)

【推】将块偏移、页 GUID、跳过原因汇总到产品边界，用 `Inkeys::Log::Warn/Error` 输出每次加载的摘要。库继续返回结构化诊断，模型器测试可直接断言。`uink_model.cppm:416,474`、`WB:27,135`、`Log.h:11,98,103`。

## D. 场景存储、Clear 与追加

【码】根目录为 `<exe>\Inkeys\AutoSave`。启动会初始化存储服务，生产路径未发现递归扫描根目录。`IdtMain.cpp:4025`、`Draw3.Host.cpp:2289,2467`。

| 场景 | 当前布局、触发与读取 |
|---|---|
| 桌面 | 【码】`desktop\YYYY-MM-DD\HHmmssfff_<请求GUID前8位>[_n].uink`，日目录 `index.json/.bak`，记 `dailySequence/sessionId/sequenceInSession/trigger`。非空且启用时，Clear 前和退出各提交新快照，退出也可取停放桌面。单 worker，保存队列优先。索引 mutex `Local\Inkeys_Draw3_DesktopAutoSave_Index_<hash>`。启动不读日索引或快照。跨 Clear 从本服务 `records` 的已提交路径读盘，写索引时才会打开当日旧索引并探测其 `.uink` 引用。读日目录最后快照的入口是 fixture。`AS:267,387,525,829,871,1001,1145,1282,1353`、`DC:22394,24087`。 |
| PPT | 【码】`presentation\` 基础轨、`slide-id\`、`page-index\`、`sessions\<会话GUID>\{slide-id,page-index}\`。各轨有 `index.json/.bak`，每次保存生成 `files\<fileGuid>_<transactionGuid>.uink`，旧索引也识别 `<fileGuid>.uink`。根 mutex `Local\Inkeys_Draw3_Presentation_Index_<hash>`。单 worker，普通待写可合并，Clear 按 FIFO。进出放映、换页、Clear、退出触发捕获。首次进 lane 或淘汰后重进查索引，普通同 lane 翻页使用驻留页，跨 Clear 另读整份取一个区间。外来 session 通常选新会话轨，旧索引仍可能被解析或阻断选轨，异会话 UInk 在 `ReadUInkFile` 前拦截。`PAS:145,302,619,797,897,1472,1560,1598,1847,1896`、`DC:22349,23847,23890,23950,24075`。 |
| 白板 | 【码】每个 Store 新建 `whiteboard\<GUID>\`，保存时生成 `<序号>.uink`，`current` 文本指向当前文件，内存留前一版备份。无具名 mutex。单 worker 优先取加载，正在写的请求会先完成，待写最多两个、同页可替换。换页、离开工作区、退出保存脏页，非当前页确认落盘后驱逐，冷读只用内存目录。构造时未读旧 `current` 或旧 GUID 目录。`WB:28,46,73,114,146,160`、`DC:18480,18625,22360,23968,24439`。 |

【码】公共整文件保存及追加使用路径级 `Local\Inkeys.UInk.<hash>`。现有追加计划基于解码模型、源 revision、`safeAppendOffset` 及批次摘要，检查注册表、编号、尺寸，允许向末 Canvas 接内容或添加合法的新页/层，**拒绝相同 Canvas 键**。执行以独占句柄验 revision，必要时截尾，写后可 Flush，失败尝试回滚并复查 revision。产品三个场景均未接该入口。`uink_file.cpp:164,1444,1542,1587,1671,1688,1747,1803,1864`、`uink_tests.cpp:3226`。

【码】桌面 Clear 先抓可见尾段并排队保存，再清空运行时并建立唯一 `DesktopClearRecovery`。提交成功后释放 CPU 尾段副本。撤到底后优先取形状编辑内存历史，其次恢复未释放的快照，最后以文件 GUID 请求 worker。迟到结果须匹配页、历史 revision、空态与待加载标志。恢复后设 `intervalOrdinal=0`，保留内存 Clear redo 后继；第二次 Clear 替换恢复点。`CompleteDesktopClear()` 为空，现有桌面路径没有重写旧区间或删除后继文件的业务操作。`DC:1137,23292,23314,23517,24058,24144,24220`、`AS:948,1318`。

【码】PPT 已有可复用的区间语义：`intervalOrdinal` 在 Clear 后递增，导入遇 Clear 计数，按 ordinal 投影，按 ordinal 合并尾段，`PreviousInterval` 请求 `ordinal−1`，安装时核对并归零以截断更早历史。白板把 ordinal 和边界放在历史 JSON。桌面的 `sequenceInSession` 是写请求顺序。`DC:538,905,1165,2090,23222,23656,24267`、`uink_draw3_import.cpp:254`、`uink_draw3_export.cpp:239,278`、`AS:1334`。

【推】按 11 月 25 日已定规则，白板在新会话目录创建一个 `.uink`，PPT 按文稿目录和进程会话定位唯一 `.uink`，同进程重进复用。用新布局入口与旧 `whiteboard\<guid>`、PPT 多轨、旧桌面索引隔离，状态和文件按所属会话定位。维护末版 Canvas 偏移及末 Canvas 的编号，冷读只取目标范围，C1.6 可在末 Canvas 抬笔追加，编辑旧页或撤回用整页版本。桌面复用区间投影和迟到保护，新增每区间的文件身份及原子 JSON 状态：首个 A 独立，接着 B 文件暂存 `A+Clear+B`，推进到 C 时收敛 B 并写 `B+Clear+C`；撤回后分叉删除 C，退出将当前文件收敛为本区间。用会话身份和状态代号约束旧进程的晚到写入。`spec:679,690,704,730`、`DC:1154,23292,24144`、`AS:525,602`。

## E. 影响 Draw3 的具体行为差异

| 项 | 核对结果 |
|---|---|
| 荧光笔 | 【码】Draw3 几何已用 Fixed 1:8，半高 `max(0.5px, r)`。`0.5px` 下限见旧需求文档，1.1 正文直接定义 `H=width>0`，没有下限。当前写原始点宽且无 `nib`，按规范会用圆形回退。`Draw3.HighlighterNib.h:7,12`、`uink_draw3_export.cpp:170,187`、`uink-1-1-draw3-requirements.md:38`、`spec:1349,2163,2219`。 【推】若盘上尺寸要忠实记录实际轮廓，荧光笔写入高度至少 1px。 |
| 橡皮 | 【码】Draw3 矩形/方形使用轴对齐端点矩形的扫掠凸包，几何与 Fixed 矩形笔尖的语义一致。差距在盘上宽度单位和缺失 `nib`。现行路径保存等面积圆直径，规范保存笔尖高度，也无需额外平头 `cap`。`Draw3.SpeedEraser.h:13`、`Draw3.SpeedEraser.cpp:9`、`uink_draw3_export.cpp:180,187`、`spec:2168,2206,2224`。 |
| 箭头、投影、闭合虚线 | 【码】三个常被怀疑的算法已对齐：箭头长 `3w`、半宽 `1.5w`、实线独立头；投影是 `±0.3535533906/−0.3535533906` 和 `0/−1/3`；闭合虚线四锚点、八段、奇段反向、填充先行及偏移方向均已实现。差距在于部件仍用私有分组，导入用重建结果显示。`shape_core.cpp:92,308,390,727`、`shape_presets.inl:82`、`shape_uink_standard.inl:77`、`uink_draw3_import.cpp:168`、`spec:1671,2396,2421,2437`。 |
| 旧矩形 | 【码】Draw3 描边和填充旧矩形都用 `min(4×显示比例, 半短边)`，旧导入还枚举 Windows DPI 档。1.1 的旧预设由部件决定，几何是 px、文件不记录 DPI，附录只给描边旧矩形注明小圆角。`shape_core.cpp:611`、`uink_draw3_export.cpp:209`、`uink_draw3_import.cpp:100`、`spec:1015,1553,2441`。 【推】用已存圆角显示可避免加载时受当前屏幕 DPI 改形。 |
| 规范允许、当前加载受限的样式 | 【码】有效的非默认 `viewport.scale`、OpenArrow、非预设 dash、扩展色、Advanced Highlighter 或多笔同 `undoId` 都可能触发 Draw3 拒绝。PPT `hostId` 当前由规范化路径或进程内身份派生，规范推荐文稿内的稳定 ID。`uink_draw3_import.cpp:39,238,344`、`shape_uink_import.inl:71`、`Draw3.Presentation.cpp:135,151`、`spec:950,1218,1359,1634,1637`。 |

## F. 测试与预计失效断言

| 测试 | 现有覆盖与迁移影响 |
|---|---|
| `InkeysHeadlessTests` | 【码】`RunWhiteboardUiTests` 覆盖主题、页态与窗口/布局契约，另有 PPT descriptor 测试。项目未编入 UInk codec 或三个存储服务。生产 Whiteboard Store 自检是 Inkeys 自身入口。`InkeysHeadlessTests.vcxproj:221`、`animation_tests.cpp:1894,1918`、`whiteboard_ui_tests.cpp:148,308`、`IdtMain.cpp:2107`。 |
| 模型器 UInk | 【码】`uink_tests.cpp:535,860,964,1263,1600,1645,1857,2699,3226,3461,3586,3624,3703` 覆盖私有橡皮、1.0 语料及 SHA、精确编码、损坏容错、预算、完整保存/清理所有权、追加/并发/回滚、资源策略、最新组、Clear、Draw3 往返。 【推】私有橡皮、1.0 固定字节/摘要、SHA 差异、依赖 SHA 或自读故障的拒绝断言要改。路径/句柄身份与并发保护用例仍可沿用其行为目标。`uink_tests.cpp:907,2024,2350,2375,3315,3424`。 |
| Shape | 【码】`TestParametricShapePersistence`、`TestConstructedShapePersistence` 覆盖预设、辅助、v7、标准回退、坏引用、重复 ID。 【推】v7 字段/版本断言和坏组整页失败断言要迁到官方 `object` 与部件降级语义。对象 `revision` 仍是 1.1 字段。`shape_uink_tests.inl:1,64,157,192,388,452,469,492`、`spec:1651,1671`。 |
| 白板 / 桌面 | 【码】`uink_page_file_tests.cpp:32` 覆盖百页、偏移、缓冲、改中页、篡改/替换、截尾及删除所有权。`desktop_autosave_tests.cpp:244,418,589,659,701,850,976` 覆盖命名、并发、索引备份/上限、Clear 回读、白板/桌面快照生命周期。生产自检另覆盖历史编解码、冷页驱逐和失败保留。`DC:14258,14522,14637,14694`。 【推】页文件的 `revision gate`、强校验、不可重排、旧版本文件及历史 JSON 断言需更新，桌面补区间滚动、分叉删后继、退出收敛与进程重叠。`uink_page_file_tests.cpp:49,57,62,74,99`。 |
| PPT | 【码】覆盖进程隔离、轨道、主备发布与故障、所有权、严格导入、结束页、Clear 区间、队列合并及并发。`presentation_autosave_tests.cpp:582,794,1150,1270,1393,1440,1913,2112,2803,3046,3075,3176,3413`。 【推】五轨/索引/SHA、旧版迁移、物理版本计数、私有页标记和严格拒绝断言会受影响。保留同进程恢复、跨进程隔离、绑定、尾段恢复及失败可见性的行为覆盖。`presentation_autosave_tests.cpp:167,691,819,2181,3075`。 |

## 待产品负责人决定

- 【推】PPT 文稿目录身份是否继续按规范化路径，还是允许写入文稿稳定 ID，让重命名、移动和未保存文稿也有明确的归属规则？`Draw3.Presentation.cpp:151`、`spec:950`。
- 【推】白板/PPT 在页追加中途断电时，若新版 Canvas 已完整而内容只写了一部分，产品应显示新版可读内容，还是优先恢复前一完整页？何时整理被替代页，是否要求正常退出时整理？`spec:558,692,699,723`。
- 【推】桌面 Clear 后尚未写新笔、Undo Clear 后又正常退出，以及新进程接手时旧进程仍在收尾，这三种情况下是否在界面主动恢复当前区间，还是仅保留可打开的快照？`DC:22394,24144,24233`、`AS:1282,1353`。
- 【推】面对外来 1.1 文件，支持目标是完整级加 Inkeys 附加级，还是优先保证 Inkeys 自产文件无损，其他合法笔尖、端帽、颜色和样式按可显示能力回退？这个决定影响渲染范围和测试验收。`spec:320,416,2205`、`shape_uink_import.inl:71`。