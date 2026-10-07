# E04 U1：RuntimeMetrics Session 恢复与实施记录

日期：2026-09-30（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。负责人：`resume_draw3_metrics`。当前状态：**U1 Session 已实现、本机 Debug 绿验证通过，独立检查待执行**。下文 HARNESS_READY 身份和占位状态为恢复时的历史记录，未替代当前绿色源码。

## 已恢复状态与所有权

- 基线 commit 为 `e32a5fc0`；当前工作区另有 root 的 E01/E02/E03 与 UI3 源码/记录，原样保留。本 agent 未覆盖或回退这些改动。
- 接管时 `Draw3.RuntimeMetrics.cppm` 已有 CanvasIdentity、LandingProof、Snapshot 和六个新 API；`.cpp` 六项均为 false/no-op/空快照的 RED 占位；Controller 的已有 parked CLI 已调用一个约 80 行 Session probe。这些原有未提交改动是本轮继续实施的起点。
- 唯一允许源码范围：`Draw3.RuntimeMetrics.cppm/.cpp`，以及 `Draw3.DrawingController.cpp` 中该 probe、必要的 JsonCpp/string include 和原 parked CLI 的调用。`RejectStrokeInitialization`、E03 probe 和全部正常 `Controller::Run` 不修改；Main、Helper、Host、Window、HiddenWindowTest、工程、spec、共享账本不修改。
- 所有构建/应用/测试/性能槽由 root 持有。本 agent 没有执行 MSBuild、任何 EXE/CLI/GUI、性能采样或 Git mutation；不递归派发 agent。

## 冻结的小合同补充

root 已批准 frameSerial 纯值补充：它是 **Session loop/frame serial**，并非独立证明的 GPU 可见帧 ID。

- `BeginFrame` 的未来绿色合同为推进当前 serial，保留失败帧 Stored pending；此刻旧代码仍 clear，并未修正。
- `RuntimeMetricsLandingProof::frameSerial` 由 owner 填当前 serial；Stage 只能接受当前帧，Commit 要求当前 presentedProof 的 serial 与 Session 当前 serial 相等。旧帧 serial 不能成为当前成功回执。
- Stored 在失败后持纯值保留；只有当前帧权威 proof 同 canvas/item/content 才确认。U1 仅校验传入数值，U2 才证明真实文档/history/raster/output producer；不将 CPU fixture 升格为实际像素证明。
- 增加 `RuntimeMetricsFrameSample` 的固定数值字段及 `RecordRenderFrame` RED 空实现，后续用于覆盖终态/恢复帧。Snapshot 增加容量/预分配字节、pendingOverflow、legacyUnverified、frames seen/retained/dropped/invalid 和 serial。测试不会根据这些占位字段自行推算另一套状态机。
- 离线 JSON 预定 schema2：`coverage`、`summary`、`toolSummaries`、`landings`；summary 使用 `legacyThresholdMet`，无首发 `strictPass`。每个 device/tool population 给 count/medianMs/p95Ms/p99Ms/insufficientPopulation；小于1000个有效事件时 P99=null，空 population 的总 landing 分位数也 null。
- 原 StageLanding/CommitStagedLandings 无 content proof，在绿色实现中只能成为 legacy/unverified，不允许加入正式成功样本。旧混合200条 strict函数保留为 legacy，不作为首发性能结论。

## 真实生产入口与红用例

路由已经核实：`IdtMain.cpp` 识别已有 `--draw3-parked-desktop-exit-test` → 导出的 `RunParkedDesktopExitAutoSaveTest` → 本文件现有持久化/CPU seal/Laser/E03 子测 → `RunRuntimeMetricsSessionProductionProbe`。本轮不新增 CLI 或产品默认开关，所有正常 Host 仍未构造指标 Session。

probe 直接调用生产 `RuntimeMetricsSession`、生产 `ContactInputCoordinator` 和实际 `WriteJson`；统计预期只比较固定已知值，不复制分位数、哈希表、pending 状态或文件写入算法。QPC 时间/内容 proof 由夹具构造，**不是硬件输入或真实 renderer/Present 数据**。

| 用例 | 实际调用与预期 | 当前红阶段意义 |
| --- | --- | --- |
| M01 | Coordinator Down → Register/Stage → 真 PublishUp/Recycle → Commit(false) → BeginFrame，pending仍1/confirmed0 | 原 probe 的未回收contact被改为真正已回收，暴露纯值跨失败帧需求；新 API 缺失由 RED 占位呈现 |
| M02 | 错页、旧frameSerial、异content成功 proof均不能确认旧contact | 不是只按同页或 bool success 给回执 |
| M03 | 当前帧同Stored内容权威proof确认一次，重复回执仍一条 | 实际 Session 去重/失败后恢复；没有测试自算 latency |
| M04 | 同一实际槽复用为真新 generation，再失效pending | 不再使用虚构 `generation + 1`；旧身份不能抑制新Down |
| M05 | 容量2、第三个真实contact，seen3/retained2/dropped1 | dropped分母不能静默缩小 |
| M06 | 一次失败、一次成功，attempt2/success1/failure1 | 旧一次Present计数不能冒充成功帧 |
| M07 | 顺序准入并真Up回收65个contact，pending64/overflow1/unpresented1；失效后unpresented65 | 只限制指标 pending，不改变产品contact容量；预分配字节采样前后不变 |
| M08 | 无效Down QPC、倒序成功QPC、NaN/负Present wall | invalid明确计账，不钳成0ms或漂亮成功 |
| M09 | 四个终態帧数值，容量2，一NaN，seen4/retained2/dropped1/invalid1 | 帧后没有按住contact也有render denominator；不是实际帧性能 |
| M10 | requested=SIZE_MAX，actual容量和预分配字节不超32MiB | 检查共同预算/乘法边界；当前占位尚无这些数据 |
| M11 | 已存在自有sentinel文件 → 实际WriteJson必须拒绝，旧字节保持 | **直接触及旧CREATE_ALWAYS行为**，并非新API占位造成的纯接口失败 |
| M12 | 真contact经旧无proof API成功，另加入NaN活动间隔；正式landings必须空/legacyUnverified明确/JSON合法/schema2 | 直接触及旧未认证样本、NaN导出、首发strictPass含糊行为 |
| M13 | 实际DrawingTool九符号+Unknown，用已知1/2/3秒数据作固定统计断言 | 每群count3/median2000ms/P953000ms/P99null；不是笔迹耗时基准；含指针/GUID/路径字段不得导出 |
| M14 | 同工具1000个实际Coordinator身份，QPC差固定一秒 | P99达到指定每群样本量才为已知1000ms，测试不复制百分位算法 |
| M15 | 新空Session实际WriteJson与JsonCpp解析 | 空数据不能输出假的0ms分位数 |

所有 failure 断言都依赖具体生产返回/快照/JSON，不是 unconditional assert(false)。fixture 的前提失效（QPC/输入准入/临时目录/文件）会报告明确 failure，不当成产品指标通过。

报告 fixture 在 Windows temp 下由系统 create-new 随机名字建立本次目录，仅操作其中 `report.json` 和 `existing.json`；测试先创建自己拥有的sentinel，ReadFile有4MiB边界，用JsonCpp拒绝重复key/extra/特殊浮点，退出仅删除上述精确文件与空目录，未递归清理、不接触真实配置/文档/UInk。测试I/O是离线报告合同测试，不加入真实绘制/采样热路径。

## 静态检查与冻结身份

当前 source SHA-256：

| 文件 | HARNESS_READY SHA-256 |
| --- | --- |
| `Draw3.RuntimeMetrics.cppm` | `0C16A3C85287F254EDA7294FCD404A2228ACF85324A55F489DA9EB3A11B079DD` |
| `Draw3.RuntimeMetrics.cpp` | `932B5C20DD48A19959A702C8980E4D0A9288606CAAD56DDEF9D9BF6B569C3E69` |
| `Draw3.DrawingController.cpp` | `BCF3DEEE5ADAC7B80DF7630ACE26990B66010B9445B92565BB2D6725CD405E88` |

- 三个源码均保持 UTF-8 BOM（EF-BB-BF）、CRLF，无bareLF。`git diff --check -- <三文件>` exit0；检查 actual diff 与 API 声明/定义，stub仍为 RED，未提前实现。
- 当前Controller diff还含已验证E03共享helper和三失败调用点；该部分不是本轮的新改动，保持原样。新增Session probe不会改变正常Run、modeler、输入采样/节拍、GPU、画质、PPT或保存事务。
- 已读取完整hook保存文件、actual implement.jsonl各路径与PRD/design/implement，三个性能设计/独立审查文件、父handoff/performance当前合同、相关native-desktop/build/errors/cpp/Draw3/input和native质量、reuse/cross-layer规范；历史demo不替代生产代码。

## 下一步与证据边界

1. root 审阅 HARNESS_READY，独占输出目录构建完整 `InkeysRepo.sln Debug|ARM64`（native host、Path规范、至少5分钟），运行已有 parked CLI保存真实红日志与退出码。该步已执行，详见下节；红结果不是产品通过。
2. 收到 `GREEN_IMPLEMENT` 后，本 agent才实施 Session：固定预分配/有界hash/pending、失败保留和明确失效、成功/失败分母、真实工具映射、离线schema2/create-new/finite与小样本门。该步已实施，等待绿色动态验证。
3. 绿色后由root构建/相关CLI/headless和独立checker。U2真实Controller因果producer、U3 Host opt-in与DComp/ULW固定轨迹三轮另行冻结交接，不能由U1绿色宣称完成。
4. 当前没有可用的新性能数据。Win7仅SP1+KB2670838、Hardware FL11.0/无硬件FL11.0→WARP、FLIP/两DWM禁用、真笔/Touch、HC/H2和光学可见时间继续保留对应门禁。

## 真实红证据（由 root 执行）

- `TestResults/release-hardening/e04-draw3-u1-red-debug-arm64-build.log`：完整 Debug|ARM64 Solution 实际 exit0（root 报告）。本 agent 没有发起编译。
- `e04-draw3-u1-red-debug-arm64-parked.{stdout.log,stderr.log,status.txt}`：本 agent 已完整读取三文件；status为 `exit=1 pid=26844`，CLI自然退出。旧 Desktop Exit、FatalClosing、FatalActiveInk、LaserIgnoredTouch、InitRejection五项 PASS；新的M01–15因实际旧行为/新合同占位 FAIL。sentinel覆盖、legacy正式样本/非法数JSON和空分位数等旧函数路径确实出现失败。
- root 已据此发出 `GREEN_IMPLEMENT`；随后才进行当前绿色修改。红日志保存，不因未来通过而删除或覆盖。

## 绿色实现与成本边界

仅 `Draw3.RuntimeMetrics.cppm/.cpp` 发生绿色改动；Controller probe与E03/normalRun保持红冻结 SHA，未缩减用例。

- 一个 Session 由唯一绘制 owner 使用，constructor一次预分配contact表、hash、landing/frame/present/活动间隔数组；共同样本容量由实际 `sizeof` 与32MiB预算推导，保留64KiB分配器余量，计算requested/effective/allocatedBytes。payload预算包含Impl固定对象/64 pending；不等于进程RSS或显存上限，真实分配器/进程趋势需另测。
- dedup为预分配open-address表，二次幂slot、负载<=1/2，正常key查找预期O(1)、最差探查不超table大小；Stage/Commit扫描固定最多64 pending。contacts/landings/frame/present等所有热写入在构造reserve的capacity内，热路径无sort、字符串、日志、文件I/O或vector扩容；性能收益尚未实测，不将它称为产品速度提升。
- Register保持同Down immutable device/tool/QPC，已保留key重复登记不增加seen。新Down一次登记，满容量明确dropped；**U2必须在实际Down只尝试一次**，不能每帧重试已掉样的无保留key（有限表无法记无限掉样身份）。pending overflow按已登记contact结束一次，并保留unpresented；失效也覆盖已注册尚无几何的contact。
- BeginFrame只推进Session serial、撤销本帧成功标志，Stored pure value不清。Stage只收当前帧完整非零的匿名canvas/scene/raster/output/content/sequence proof；Stored需itemToken。Commit要求本帧RecordVerifiedPresent成功、当前serial和精确canvas/kind/item/content/sequence；Stored跨失败帧可确认，Live/Laser需当帧restage。Invalidate不允许借前一次成功回执继续确认新内容。
- Session不解引用record；只按地址+generation比较。JSON输出anonymous contact ordinal与运行时数值，未输出record指针、页面GUID或文档路径。此处scene/raster/output为owner定义的诊断版本，不能擅自称GPU device epoch；U2/U3需提供可信producer、资源失效与content可见性。
- actual Present attempts/success/failure和旧Unknown分开；非法QPC/wall、无证明、未呈现、overflow/dropped逐项保留。成功之后仅一次landing。正式JSON coverage在未呈现/invalid/failure/未知结果/掉样时不报complete；retentionComplete可单独说明原始保留状况。
- 旧StageLanding/CommitStagedLandings只兼容调用并记录legacy未验证，不产生正式landing；旧RecordPresent记attempt+Unknown。MeetsStrictThresholds保留混合200样本/旧阈值为legacy条件，并排除缺失/失败，JSON命名legacyThresholdMet且releaseVerdictAvailable=false。
- schema2按device/tool与frame reason提供count/median/P95；P99每群不足1000时null，空群体null。当前frame time是wall，CPU/GPU写null；旧活动区间与完整render帧分开，intervalSeen/longFrameCount保持分母，不凭四位小数ratio掩盖稀疏长帧。
- WriteJson只在owner停止后进行map分群/sort/格式化，classic locale且所有入样浮点先验finite/非负。实际文件创建改CREATE_NEW；已有文件原字节保持。新建文件写/flush失败返回false，可能留下本次不完整诊断文件，不能当成功报告；最后已提交用户数据没有覆盖。

当前PATCH_READY SHA-256：

| 文件 | 绿色 SHA-256 |
| --- | --- |
| `Draw3.RuntimeMetrics.cppm` | `FA0043331A8E9D23C937D504C0A594500D0EB47CF9E659FE8ADECDB8B4DEEA4F` |
| `Draw3.RuntimeMetrics.cpp` | `C65B27BAF4EBA744B516B5E4D17C30A73432D09FD6C91B6FE8B0B26E55700A3B` |
| `Draw3.DrawingController.cpp` | `BCF3DEEE5ADAC7B80DF7630ACE26990B66010B9445B92565BB2D6725CD405E88`（红夹具未变） |

三个源码仍UTF-8 BOM/CRLF/零bareLF；当前三文件diffcheck0。只读核模块import：新增真实window_control无反向runtime_metrics依赖；实际编译由下节记录关闭本机 Debug 门禁。运行均由root执行，本 agent只核原始证据，不混写自己执行。U1未激活Host、未证明真实GPU/光学/用户笔输入，也未为Draw3给出性能胜出数据。

## 绿色证据与最终交付（由 root 执行）

| 验证 | 原始位置（忽略的 `TestResults/release-hardening/`） | 已核结果 |
| --- | --- | --- |
| 完整 `InkeysRepo.sln Debug|ARM64` | `e04-u1-nohold-green-debug-arm64-build.log` / `.status.txt` | 实际exit0，0 Error(s)/14 Warning(s)，33.31s完成；本 agent读取status、收尾与相关warnings，未发起构建；warnings位于thirdparty和Main，本轮不顺手改动 |
| 已有生产 parked CLI | `e04-draw3-u1-green-debug-arm64-parked.{stdout.log,stderr.log,status.txt}` | pid18304自然exit0；原五子套件与`[Draw3Metrics] PASS: production Session pending, bounded outcomes and report contract`共六标签；已完整读取三文件 |
| strict Headless `--no-window` | `e04-u1-nohold-green-debug-arm64-headless.{stdout.log,stderr.log,status.txt}` | pid11036自然exit0，216 layouts failures0、PASS animation correctness；已核status与摘要，stderr为空；不将它当真实GPU/窗口帧性能 |
| 最后编码/diff/身份 | 三源码SHA和`git diff --check -- <三文件>` | exit0；当前源码SHA与上述PATCH_READY绿色SHA完全相同，Controller夹具/E03正常Run未变 |

U1 的工程实现与本机已定义 Session 自动回归已通过；独立review与Release/其余架构检查由root串行安排。停止源码写入，所有权交回root，接收checker发现后只经root指定进行窄修复。

剩余职责：U2真实Controller/Stored history/Laser内容因果与即时成功返回QPC；U3 opt-in Host/生命周期/真实DComp/ULW三轮及metrics off/on语义成本对照；完整Move/Up尾延迟/长期资源/UI3/Canary/Inkeys2/Win7/真笔等门禁。U1 CPU数值夹具通过不能关闭这些项，报告仍不宣称发布就绪或任务completed。本 agent无commit/push、无branch/worktree/GUI/工具运行、无spec或共享账本改动。
