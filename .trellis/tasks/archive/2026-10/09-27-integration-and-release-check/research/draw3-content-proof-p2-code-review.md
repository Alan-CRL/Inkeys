# Draw3 U2-P2 实际 Run / Present 接线独立复审

日期：2026-10-01。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本 reviewer 按 root 分工只读实际 check.jsonl 及上下文、已加载完整 hook、PRD/design/implement、相关 native/Draw3/input 规范、最终合同、父最新 handoff、实施报告、四源实际 diff 与调用链。只写本报告，不递归、不自修源，不改工程/spec/其它报告/账本/Git，不执行 Build/CLI/EXE/GUI。

## 当前结论

**最终 U2-P2：STATIC_GREEN。** 实际内容 producer / 寿命与预算 / 默认关闭 / L2与Live资格 / Present分母与proof / 帧封口均未发现剩余静态阻断。初审发现的 bit19 与 Undo/Redo 真实尝试/失败计量两项，已由原 writer 窄修并逐点独立核对关闭。最终四源身份如下；当前没有绑定这些源的 P2 完整构建/绿色 CLI 或真正启用 metrics 的 Host 数据，不能计动态 PASS。

P1 的 normal Run/PresentFrame 字节冻结证明不适用于本批。这次确实改变了正常代码；下面结论来自真实调用点与实际 diff，未拿 P1 数值 helper/旧 Build 当生产链完成。Host 当前构造 Controller 仍未传 Session，故产品默认路径不会启用本 recorder，真实 on/off、全部工具轨迹和 GPU 行为另验。

## 已核源身份

最终合同：`4F6839B450AAE10E298F34B28F8B54C4D77CC8AF9988B5AF6B4E0F67E171545F`。实施报告最终为 `05EBB23388038D0ABF20E9B151FBEE339E9396B750A380E21C531FD1F3627C3C`；初版 P2 为 `A6B911556853F32F7DC0B90292C01720A4F7E01DBA274FF84A82D8329F298594`，bit19 后为 `126101DE85528016CE5E40D7B50DE7E44C086CFEE7956F6425F124BACD669740`，仅作为历史来源。

| 源（均在 Inkeys/Inkeys/Drawing/Draw3） | 当前已读 SHA-256 |
| --- | --- |
| Draw3.DrawingController.cppm | B119EE5B50F9649DD2C29A69F4045F4EE4C26CA39087893E3EB7AF7BC70670FA |
| Draw3.DrawingController.cpp，最终两项修补后 | 0CEF8C50C6D8C71BBF2DA1281F26337757B4C2729B78307A825C742521661876 |
| Draw3.RuntimeMetrics.cppm | 1E935AC95020E0B01987D02BC95D762ABD17850832DDDBD63A41FD9004F7D37F |
| Draw3.RuntimeMetrics.cpp | C4A0DB015CF6F66EA1BF7DBBE904A8AA902E1A5844CA62B288184EA761456932 |

Controller 初版 P2 `9747058137A1CED95717FA3710DA5E45DE059532DF6E56317FB6D5517F1E909C` 已完整读正常路径 diff。bit19 后仅新增12872/12876–12878四行；reviewer在内存内去掉它们，BOM+CRLF原文准确恢复9747。最终0CEF移除本批State helper/U213/计量接缝并还原两处原return表达式后，reviewer独立精确重建 `ECF133C89F6118B116DE6DADB8ADF0F35D780C03D5513F156255058E362AA5E5`，净83行。这仅证明两次窄修之外未变，不能证明P2正常Run/Present仍等同P1。另三源保持初版冻结身份。

## Findings (fixed)

### P2-F01 — 实际 withheld 事件漏接帧原因 bit19

- File：DrawingController.cpp::FreezeCandidates 与 Run 最终合成/Freeze 接缝，2396–2398、12867–12878（本段 ECF 身份）。
- Issue：初版已导出 RuntimeMetricsFrameReason::AuthoritativeWithheld，却没有 owner 置位。真正 L2 stamp 不足或 composite 未完整覆盖 Stored 投影会增加旁挂 authoritativeWithheld，成功 Present 帧仍只记成功等位，漏掉实际 proof withheld 原因。
- Fix：由原 writer 在真实 Freeze 前后比较该计数，只有本次新增才 Mark(bit19)。不是从 `!l2.valid` 或帧尾状态猜测；不撤真实 Present success、不新增绘制/帧/等待/时钟，全部仍在 metrics guard 中。本 reviewer 未改源；ECF 增量已核，动态及 FrameReason 接线回归未运行。

最终0CEF同一接缝为12955–12961，增量未被后续修补覆盖。静态关闭；严重性：中（原因分类漏记）。

### P2-F02 — Undo/Redo真实GPU尝试/失败可能在idle前丢失（最终静态关闭）

- File：DrawingController.cpp::undoCurrentPage / redoCurrentPage。初审ECF的renderAttempt只在成功尾部置位：Redo真正DrawStoredStroke false可在空dirty下返回idle，Command只OR bit3，Finish因此丢掉栅格尝试/noPresent/bit15；cold restore、base/resolve及rollback实际false也漏分类。严重性：中（opt-in指标分母漏记，未据此判定产品Undo算法失败）。
- Fix：由原writer新增State两个无字段helper，真实调用前保存此前尝试位并置当前位；可观察栅格工作false才Mark(RasterFailed)，合法miss/Empty恢复此前位。当前最终接缝逐项独立核对：

| 0CEF实际位置 | 核对结果 |
| --- | --- |
| State3184–3195 | BeginRasterAttempt/RasterReturned仅帧元数据，无新Clock/分配/请求；既有失败位与真实Present结果不清零。 |
| Undo10114–10119 | 真RestorePreimage前登记；miss/空dirty不生成失败帧，命中copy保留尝试。 |
| Undo10154–10158、10174–10179 | 原rollback/cold RestoreComposition各自在调用前登记，实际path/dirty分类，原返回保持。 |
| Redo10296–10300 | 既有restoreHiddenTiles的base及两条rollback共用真实计量。 |
| Redo10324–10333 | 在原scratch L1/L0 clear前登记，真正DrawStoredStroke false记bit15；空dirty早返回仍有一次尝试。 |
| Redo10363–10366 | 只在原非空redoDirty分支登记实际ApplyOperatorLayers；false记bit15，原rollback顺序保持。 |

分类依据是只读的实际GPU provider：InkHistoryGpu.cpp::RestorePreimage1285/1294的false均在1299–1318 Copy之前，copy后累加dirty；void Copy只有提交事实，没有设备执行失败回执，不能虚称硬件成功。RestoreComposition1362–1367前置拒绝、1378–1383 Empty、1389–1394 excluded拒绝都无tile像素恢复；真正mapped clear/replay失败1494–1503先合入tileDirty再返回Failed，成功tile1505也合入dirty，因此该接缝path/dirty可区分当前实际尝试。Empty仍可能有binding/CPU成本，不宣称成本为零。CPU visibility拒绝不记GPU失败，之前已发生的栅格/rollback失败保留；未知异常前已置尝试不回填成功。无需改变provider接口或加新框架。

最终U2135337–5388七项调用同一State BeginRasterAttempt/RasterReturned/Finish以及Session/PresentReturned，检查Command-only、miss/Empty无帧；false后idle保留一条noPresent/bit15；CPU visibility拒绝不伪记失败；rollback失败不重计；Empty rollback不撤此前失败；之后真输入Present成功保留attempt/success。Finish两次不重计。它是共享helper的固定CPU结果探针，未实际执行Undo/Redo或注入GPU故障，不授动态PASS。

最小范围独立还原hash确认原GPU调用参数、rollback/CPUvisibility/返回/GPU顺序/pacing未变，默认null新增guard不调用helper、不取钟、不分配。State大小/共同预算不变。验收仍需Root运行新probe与真正生产失败交错；静态问题已关闭。

## 实际接线与所有权核对

本节沿用初版P2/ECF实码审查时的行号；对应逻辑已在最终0CEF核对保持，最终新增接缝当前行号见上表。

### State、默认空与共同预算

`DrawingController.cppm` 私有 forward State＋unique_ptr 与出线 destructor 配对，`.cpp` 3169 定义 State、6190 定义 destructor。State 与 Controller 同寿命、仅绘制 owner 写，借用 Session；Host 真 join/最终封口仍属于 U3，不在 Run 退出时假造 sealed。

3169–3227 的派生 State 包含原固定 notes/candidates/mappings/latches 与 frame POD，`Prepare` 用真实 `sizeof(State)`，不是只核较小 P1 base。FitsBudget 2486 先判空及 own/external 上界，再减共同32MiB，满足才一次 nothrow 分配。Ctor6156–6160准备失败仅关闭 metrics 指针并保留 private unavailable，原启动/样式/输入/GPU继续。default-null 先短路，无 Session Snapshot/数组分配/新增 QPC/输入诊断开关；新实际 Clock 都受 metricsState guard。固定表没有热扩容/排序/I/O/GPU query/等待。外部 phase/source/checkpoint payload、unavailable/private counters 导出待 U3，当前预算只保证 Session＋本 State。

### 真实 Down / 采纳 / 精确终态

- initializeStroke 7459 先得到倒转/右键覆盖后的有效 tool，7462–7466从仍活的 handle/Down一次 Register实际 device/tool/QPC，并 NoteConsumed；池、Reset、Down Update三个失败7815/7910/7967先精确 InitRejected，再保留原 E03 Discard、handback/handle清空/状态与返回。没有伪造物理 Cancel 或按旧 key 操作下一代。
- successful reconnect 7686 Update成功并实际Append后，7719在旧handle Recycle前精确终结A，再采纳新B的真实Down；B此前独立登记/consumed，C Stored不受影响，续接失败不终结A。旧 record在metrics路径只作opaque值；generation/Down等原必要读取仍属于存活的输入初始化流程。
- consumeLatestSnapshot 8195–8207只先消费序号、记录真 terminal、精确 Cancel；去抖/模型失败不推进 adopted。普通成功 Update在Append/Extract之后用真实点数/尾点/Shape modeled endpoint变化验证采纳；Down无模型实点的 inputStartPoint为RawDown，失败 terminal 的真实fallback为RawTerminal，Shape最后强制raw Up单列kind。stationary Update只在成功产生非预测变化且所用锚点等于实际raw位置时采纳lastInputSnapshot，不用failed lastModelSnapshot本身授成功。
- admission比较是与该笔 Down 的精确相等；0是合法初始版本。新增U211在真实Coordinator blocked→unblocked到2后用wrong0，不能把这个反例解释为正常0拒收。sourceQPC不早于该Down的检查与消费/采纳分离保持。
- 页边界sealPresentationContacts8446先终结旧metrics键，原最后位置CPU封口/保存照旧；touch转pan、CPU commit失败、未得到Stored归属的终态、scene/fatal/stop精确退出notes。没有run中Reset；已确认项/重复/stale不会增加第二次unpresented，回收前纯Stored note仍能跨失败帧。

### Stored 与权威栅格

- 正常Up 12441在CPU提交前取before signature；CPU commit成功后BeginLocalWrite，12463在handle回收前复制真正item/index/contentGeneration/afterState/Up源。真正 DrawStoredStroke＋L2 Apply的submitted共同完成12552的连续stamp；12550原rasterState即使失败仍推进，指标没有因此授成功。
- Shared L1/L0 清理/结束项后的 RebuildActiveLayers 仍走原步骤；12751之后只在本帧成功共享层、实际非预测贡献和当前surface一致时 ObserveLiveRaster。普通/橡皮使用real points；无模型点需实际L0包含Down；Shape预测矩形不保证覆盖真实边，所以实际primitive末点必须与采纳端点相同。metricVisible和prediction不是proof。
- Slot/page/Clear/Replace/load与拓扑安装在成功CPU事务后换scene；view平移8848/11046、resize6451、recovery11227撤共享资格，recovery还撤旧output编号。undo/redo成功visibility后PruneStored，contentGeneration变化/隐藏项不复活旧Down；失败计量缺口已由P2-F02窄修静态关闭。
- resetGpuForPageSwitch 9594确实先清L2，wholeL2Clear只给当前真实空history或完整可见restore后建stamp；restorePageContent9490对Empty需known whole clear，对非Empty使用真正RestoreComposition结果。partial/replay初始11470锁计划signature，末尾11579只在同signature全部必要计划完成/无失败时认证，不拍最新revision，不改变tile预算/额外请求帧。CPU中途变动无法证明整组覆盖时保守withheld。
- L2-only写入和Live共享失效分开；正常Freeze前对blur/trusted fallback、refresh/recovery pending撤L2 stamp。Stored再核当前history exact id/visible/strokeIndex/contentGeneration/afterState/范围与projection，只有本次actual composite完整覆盖投影才stage。fullComposite来自实际合成rect覆盖整视口的事实，未借PresentFull开关；没有为漂亮样本请求更大dirty/replay。

### 实际 Present 与帧封口

PresentFrame6339在真实presentation.Present返回后的第一个metrics动作是6352 QPC，早于observer/ready/帧末；called=true恰一次Record真实bool/wall，无候选/false/输出不匹配都保留attempt/outcome。按当前/冻结/observation raw tuple与匿名编号确认，多proof用同returnQpc。raw0/极大raw不做加法、checked编号穷尽不复用；失败observation不作为成功proof。

ClearCanvas/PresentFullCanvas外部先开有效serial，PresentFrame不重复Begin；Run内runFrameActive保留同一上下文。原正常wall/observer/fallback顺序保留，metrics-off不加新Clock。冷frame空候选，不从上一Run借proof。

Loop Begin11206＋MetricsLoopExit、RunExit使早continue/异常/stop有退出结算；普通12963在原pacing之前封口，physicalAfter=0也可记录，pure idle/Hold/maintenance不造固定零render。11781等待前Finish，idle唤醒Down 7462会重开实际初始化边界，RAII再封口，等待不混wall；NoPresent与真正Present bool分开。Command只OR原因位，避免把纯CPU/no-op控制当绘制；真正GPU命令尝试现在由P2-F02的实际接缝显式登记。

## Findings (not fixed)

未发现本批剩余静态实现问题；以下是未执行/尚未交付的验证与后续单元，不能由STATIC_GREEN抵销。

### 后续明确门

- **P2动态门**：最终修补后没有绑定本表四源的新完整Debug|ARM64/parked/strict Headless/PptCOM/真实metrics Host结果。父P1绿色Build0/parked0 pid35248属于旧源；Root已报告其它UI/C3冻结并准备完整Debug，本reviewer未运行。绿色结果必须绑定最终四源，没有类型/链接PASS可由本报告替代。
- **U210–U213覆盖范围**：真实Coordinator/Canvas/history＋同helper补了partial/fullComposite冲突、partial不足/完整投影、viewport外/旧extent、actual admission2对wrong0、sourceQPC早于Down、错误geometry sequence、State共预算/null及七项尝试/失败/一次封口。这些仍是CPU合同输入；没有真实Renderer/Present或正常Run的故障交错。FrameRAII、真实reconnect/接管、失败GPU命令与所有工具loop、cold/恢复/输出切换需实际fixture；bit19marker本身目前仅静态核，不能由同helper计数例宣称真实Run帧位已动态验证。
- **Host/U3**：defaultoff、新run Session、外部auxiliary预算、private unavailable/counters、真正join后封口/导出、phase16+200、finite source receipt、功能checkpoint与metrics-off的小stamp未交付。不得dummy Session填off格子。freshDesktop普通SubmitLoad不扫描新service磁盘records，U3恢复格子需与C3已有窄生产读者合同对齐，不能注入records或拿文件存在当Loaded；不在本P2改保存格式/gate。
- **未采集**：首批只有Down软件Present代理，Move→对应几何/成功帧、Up final＋安静窗口未采集。Laser正式landing excluded但不能跳实际frame/Hold/Fade/particle/bake/成本/资源；21原因位不等完整生命周期/CPU分段。线程CPU、GPU/显存、长期/长文档、Release其它架构、真RTS/笔/Touch、Win7 SP1仅KB2670838、光学/HC-H2、Office/新进程可见恢复均未通过。未开放功能、DComp/ULW独立设备、FLIP保留/两个DWM禁用不因本计量变化扩大。

## Verification

- Lint/静态格式：最终四源scoped git diff --check exit0，均UTF-8 BOM/CRLF、bareLF0/bareCR0；独立linter未运行。两次窄修分别在reviewer内存中精确还原ECF及9747 hash。没有用P1全body还原断言当前normal字节不变。
- TypeCheck / Build：未运行，最终P2动态pending；root唯一完整Solution槽，不能复用旧P1/C3/UI构建。
- Tests / EXE / GUI：未运行、无本次动态PASS。实际source/callers/helper/API/static预算与新数值probe均已读。
- Spec：生产接线验证后由root同步opaque/采纳/权威戳/帧原因/封口/预算和真实coverage；本report不修改spec/账本或任务完成状态。
