# E04 U1 RuntimeMetrics Session 实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。只读核对当前 module/实现、Controller 的实际 probe/parked 路由、U1 设计与实施记录、已保留红绿原始日志和父性能门。按本轮分工没有运行 Git、编译、EXE/GUI，也未修改产品/工程/spec/账本；仅写本报告。

## 结论

**U1 当前实际代码 GREEN；root 已运行的 Debug|ARM64 编译和已定义 Session 回归通过。** 没有发现本批阻断问题。以下 identity 与实现记录 PATCH_READY 一致；Controller probe 与红夹具相同。此结论只覆盖 Session 数值合同，不证明真实 Controller/GPU producer、Host opt-in、输入→成功 Present 的生产延迟或三轮性能。

| 文件 | 本 reviewer 核对 SHA-256 |
| --- | --- |
| `Draw3.RuntimeMetrics.cppm` | `FA0043331A8E9D23C937D504C0A594500D0EB47CF9E659FE8ADECDB8B4DEEA4F` |
| `Draw3.RuntimeMetrics.cpp` | `C65B27BAF4EBA744B516B5E4D17C30A73432D09FD6C91B6FE8B0B26E55700A3B` |
| `Draw3.DrawingController.cpp` | `BCF3DEEE5ADAC7B80DF7630ACE26990B66010B9445B92565BB2D6725CD405E88` |

## Findings (fixed)

本 reviewer 无产品修补。已在实际实现中核对作者关闭的旧合同缺口：Stored 跨失败帧不丢、旧无 proof API 不产出正式 landing、Present Unknown/成功/失败分开、真实工具枚举、固定预算和容量分母、finite/QPC 校验、schema2/CREATE_NEW/空与小样本 null 分位数。下列是实际代码证据，不以实施报告的宣称代替检查。

## 实际路径核对

### 固定存储、哈希和分母

- `Draw3.RuntimeMetrics.cpp::RuntimeMetricsSessionImpl` 324 附近以 actual sizeof(MetricContact/LandingSample/FrameSample/PresentSample/3个double/最坏4倍size_t哈希) 推导容量，扣除固定 Impl 和64KiB余量，requested 先 clamp 到正数、预算上限及1<<20。乘法/二次幂增长在 clamp 后执行，SIZE_MAX 不参与原始容量乘法。
- contacts/landings/frames/presents/三个 duration vector 构造 reserve，hash assign 一次；实际 capacity 总字节复核超过32MiB则 throw。预算是固定payload/容器 storage，未承诺堆分配器/进程RSS/显存上限。构造失败要由未来 U3 标 unavailable，不能改变产品启动；本次 Host 未构造 Session。
- `FindContact` 241 附近最多 table.size 探查；`IndexContact` 的 while 根据 table≥2*maximumSamples、contacts≤maximumSamples 且没有删除/并发 writer 的不变量必能找到空槽。不是开放式等待另一个线程。pending 扫描最多64。所有 push_back 受 maximumSamples 或每个 retained contact最多一次 landing 的状态不变量限制；采样热路径无 resize/sort/map/字符串/日志/I/O。
- `RegisterContact` 375 附近按地址+generation 去重且不解引用 record；同 Down 的 device/tool/sourceQPC 锁定，矛盾重复明确 invalid。成功保留的重复登记不增seen；满容量新contact增seen/dropped，pending第65项将其保留contact终态置Unpresented并单计overflow。明确失效覆盖 Registered/Pending，包含尚无几何的Down；不会遗漏成一个假“完整”成功群体。
- drop 后未保留的身份不能在无限有限表中去重。接口当前要求 U2 在真实 Down 只尝试一次，不能在每帧重试已经 dropped 的key；此调用合同已在实施记录写明，U2接线时必须检查。它不要求扩大为无界drop registry或改变产品输入槽容量。

### Stored/Live/Laser 与成功回执

- `BeginFrame` 367 附近只推进 serial并撤本帧 success flag；64 pending仅保存contact索引与纯值proof，Stored没有 runtime/COM/HWND 借用对象。Session不从已经回收record取新QPC/设备/工具。
- `StageVerifiedLanding` 424 附近只接受 Registered/Pending、有完整非零canvas/content/sequence/current frame的proof；Stored要求itemToken。可重stage同contact的当前实际proof，overflow不会新增输入限制。
- `CommitVerifiedLandings` 457 附近先要求实际RecordVerifiedPresent留下本帧成功、presentedProof有效且serial==当前帧、正QPC。SameContent精确比较workspace/page/scene/raster/output/kind/content/item/consumedSequence。Stored候选允许旧frame等待当前权威内容；Live/Laser候选另要求已当帧restage。错页、错内容或旧成功帧不能借用Down。
- 逆序QPC/非法latency将该contact置Invalid+Unpresented，释放pending；确认后status不再可stage，重复commit不复制landing。Invalidate撤成功flag并为所有未证明contact保留unpresented分母。
- U1只检查owner给定数值的一致性。frameSerial是Session loop序号，canvas/generation也不自动成为真实renderer/raster/output证明。Session本身无法认证caller的RecordVerifiedPresent或proof来自GPU成功结果；U2必须冻结真实 producer，U3再接Host生命周期。当前正常Controller仍调用旧StageLanding/CommitStagedLandings，只有本probe使用新接口，Host构造DrawingController仍未传metrics。

### 非法值、统计、输出与legacy

- RecordVerifiedPresent的attempt/success/failure保留，非finite/负wall不会进入presents数组；非法值增invalid/presentInvalid。旧RecordPresent只产生Unknown，不能计为成功。RecordRenderFrame独立计seen/retained/dropped/invalid并接受terminal/recovery数值，不依赖当前仍按住的contact；不是CPU time。
- active/idle时间在入样/间隔计算时检查finite/非负；逆时不clamp漂亮0。正式CoverageComplete同时要求无drop/invalid/failure/unpresented/pending/legacy/unknown/registered-without-proof。RetentionComplete仅说明原始保留状况，不能独立当场景通过。
- `ToolName` 132 附近用真实 `DrawingTool` 符号：0 Pen、1 HardPen、2 Highlighter、3 Eraser、4 Laser、5–8形状；未知值单群并输出toolValue。不是旧demo的0/1/2/3标签。设备同样有限符号映射。
- schema2分device/tool及frame reason，离线count/median/P95；每群不足1000则P99=null，空总群median/P95/P99也null。没有strictPass，旧混合200门只名legacyThresholdMet，releaseVerdictAvailable=false；CPU/GPU显式null。原始duration与成功/失败/drop计数均保留，四位小数ratio不隐藏整数longFrameCount。
- `WriteJson`/map分群/sort/ostringstream仅owner停止后调用；classic locale、防NaN/Inf入口和有限字符串常量使JSON没有用户指针/GUID/path。`WriteUtf8File` 222 附近CREATE_NEW拒覆盖，逐字节Write+Flush+Close；失败返回false。写失败可能留下本次不完整新诊断文件，不能当成功/用户保存恢复，caller管理隔离目录。
- 旧StageLanding只计Legacy+Unpresented；旧CommitStagedLandings no-op。MeetsStrictThresholds仍是legacy计算，不能将混合工具200条的数学条件升级首发性能判决。正式metrics当前无Host opt-in，不因U1通过宣布产品已采样。

## 真实测试入口与证据

`IdtMain` 已有 `--draw3-parked-desktop-exit-test` 路由到 `RunParkedDesktopExitAutoSaveTest`；Controller 4362 附近在旧Desktop Exit/FatalClosing/FatalActiveInk/LaserIgnoredTouch/InitRejection之后调用真实 `RunRuntimeMetricsSessionProductionProbe`，合并结果返回，不通过关闭旧子套件使新测试绿色。

probe 3530 附近直接用生产 ContactInputCoordinator、RuntimeMetricsSession 和 WriteJson。M01实际PublishDown→登记/Stage→PublishUp+Recycle，检查旧handle读失败和槽已回收，再失败→BeginFrame→当前Stored同内容成功；M04确实复用同record的新generation。M05容量2第三个Down、M07顺序准入/回收65contact、M08非法QPC/NaN/负wall、M09终态frame分母、M10 SIZE_MAX均检查生产返回/快照，不复制状态或哈希实现。

M11测试自己新建temp目录和CREATE_NEW sentinel，再实际调用WriteJson拒覆盖并ReadFile比较旧字节。M12 legacy/非法JSON、M13九种真实工具+Unknown的1/2/3秒已知数值、M14每群1000条、M15空报告用JsonCpp拒重复key/extra/special float解析后断言，未复制分位数算法。IO文件只在该目录，read有4MiB上限，析构只删两精确文件和空目录；不触及真实UInk/config。fixture的数值proof和秒差是CPU合同夹具，不是实际落笔延迟数据。

| root 已执行 | reviewer 读取的实际结果 |
| --- | --- |
| 红 parked | `e04-draw3-u1-red-debug-arm64-parked.status.txt` 为exit1、pid26844；stderr旧五子套件PASS、新M01–M15具体FAIL。不是无条件assert(false)。 |
| 绿完整Solution | `e04-u1-nohold-green-debug-arm64-build.status.txt` exit0；log收尾0 Error/14 Warning、33.31s。warnings在thirdparty/Main，未顺手改动。 |
| 绿 parked | `e04-draw3-u1-green-debug-arm64-parked.status.txt` exit0、pid18304；stderr旧五PASS和Draw3Metrics production Session PASS；stdout为实际AutoSave capture。 |
| strict Headless | `e04-u1-nohold-green-debug-arm64-headless.status.txt` exit0、pid11036；stderr空，216 layouts failures0、PASS animation correctness。 |

上述文件都在忽略的 `TestResults/release-hardening/`。本 reviewer 没有重新运行测试；源hash与绿记录一致只能支持这个U1单元。root后续Main观察字段和C helper等变更仍需新完整构建/相应复验，不把这次旧全Solution gate当当前最终HF通过。

## Findings (not fixed)

- **P2，测试补强**：M13有Laser同帧成功，但尚无Live/Laser“失败→BeginFrame，不restage拒确认→当前帧restage成功”的专门反例；当前实际分支正确，未发现此路径的源码错误。建议在U2接线前加入同生产Session的两个有限kind用例，并覆盖Invalidate后旧success不可借用。只写本报告，未擅自修改Controller probe；这是后续明确回归门，当前U1数值实现可继续。
- **P2，caller合同**：唯一owner、每Down一次注册、每实际Present同步RecordVerifiedPresent、结束后导出与构造失败unavailable都需要U2/U3实际实现证明。表值校验不是完整权威内容/历史/光学proof，也不能自行关闭真实GPU/Host/Win7门。
- **P2，平台/性能**：只取得当前Debug ARM64门；Release/Win32/x64编译与metrics on/off语义/成本、三轮真实DComp/ULW、Move/Up尾延迟/长期资源、HC/H2及真笔/Win7均未由此单元验证。

## Verification

- Lint：未运行独立linter；按只读分工没有执行Git或自行修产品。当前三个source的SHA已核。
- TypeCheck / Build：root完整Debug|ARM64 **PASS**（实际status exit0）；本 reviewer 未重新编译。不能升级当前后续全部改动的最终完整构建门。
- Tests：root parked和strict no-window **PASS**，如表；本 reviewer读取原始证据，未运行EXE/GUI。
- Spec：后续由root同步唯一owner、32MiB payload/64 pending、legacy/Unknown、Stored/Live/Laser和schema口径；目前不写U2/U3已实现。

## 2026-09-30 M16数值反例增量

root在同一production probe加入Live/Laser两kind的M16；当前Controller SHA-256 `5FBBEB09C5E120E537FAFEB33CA3E9E3955F71FF4D8EA87C1F72CD2403AEA4F5`。上表BCF3是M16前已审基线；RuntimeMetrics两源仍为FA004333/C65B27BA原绿色身份，未改变Session实现。

本 reviewer已核实际3651附近循环：真实Coordinator contact→第1帧Stage并失败→第2帧success且只更新presentedProof，不restage，confirmed0/pending1→第3帧重新Stage当前proof并success，confirmed1/pending0，最后真实Up/Recycle。分别使用HardPen/Live和Laser/Laser，走生产Session而非复制算法或直接修改快照。这个反例能使错误允许旧活动层proof的实现失败。

已读取 `TestResults/release-hardening/u1-m16-green-debug-arm64-parked.status.txt` exit0、pid22020与stderr旧五PASS+Draw3Metrics PASS；root记录为当前构建自然运行，本 reviewer未执行。**前述P2的Live/Laser跨帧restage专门测试缺口在数值Session范围关闭**。Invalidate后的新contact/旧success和真实producer交错仍应在U2因果接线验证；不以M16关闭GPU/Host/光学/完整性能门，也不复用基线fullSolution给后续全部修改最终PASS。
