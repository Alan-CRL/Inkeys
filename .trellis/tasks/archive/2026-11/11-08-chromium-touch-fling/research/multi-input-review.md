# 多设备漫游独立审查

## 当前审查阶段

以下设置侧为第一轮审查快照；后续Core/DM与最终只读复核结果见文末，不能将静态核对当成功能测试通过。用户已批准当前M1–M9设计，旧Chromium/尺度分节不作为本阶段实施要求。

## Findings (fixed)

无。设置侧未发现需要机械修复的问题，本次没有修改生产或测试源码。

## Findings (not fixed)

未发现设置侧实现缺陷。以下为尚未完成的验证边界，而非已确认源码错误：

- ARM64完整solution编译及真实native配置/ImGui测试由主会话执行，本子代理按授权未编译。
- core/CanvasNavigation/WindowControl/DM生命周期及跨来源整合尚待实现冻结后的全量审查。
- 当前canvas-pan等旧spec尚未同步新增设备/hover/策略合同；主会话将在实现/验证后更新，当前审查以批准的PRD为准。

## 设置侧证据

- Other.Config.cppm:440–447：六个IdtAtomic<bool>、NoUpload、默认{true,false,true,true,true,false}；无旧deploy.json字段或静默Mouse=true迁移。
- IdtMain.cpp:3373：ReadAll后的启动发布六项快照，先于Host运行；新Controller首帧前PumpBridgeState补齐。
- Draw3.Host.cpp:462/1774/2299/2989：期望值编码为一份atomic、只有差异才调用Controller owner setter、重启使applied缓存失效、等值发布不追加control wake。
- Draw3.Host.cpp:2571：owner确认StopTouchpadNavigation后才继续原Mouse/RTS停产；未证明停产沿既有FailedCleanup合同，不把timeout作为成功。
- Setting.Pages.cpp:890 / Setting.cpp:3888：真实ToggleRow+InlineCheckboxes五项按指定顺序，主关闭仅收起/保留子值；只有Changed才发布Host并QueueConfigWrite，不在普通输入或每帧无条件发布。
- config_metadata_tests.cpp:835：真实schema/文件测试覆盖缺项、独立bool组合（含master开且全子项关）、冻结copy、NoUpload不变、Write/ReadAll、逐叶ReadMini、非法JSON值及Reset；没有仅对默认struct做镜像测试。
- setting_design_tests.cpp:2635：真实ImGui回放覆盖空闲无事件、五项不同ID/不重叠命中区、窄宽换列、多语言/DPI/主题、关闭收起与旧位置不可点、五项点击一次事件/只改对应bit、主关闭/重开保留子偏好；接既有general-only和完整集合。
- 二十语言七个新增文案及生成头通过既有i18n检查，文本不暴露DM/API内部细节。

## Verification

- Lint/格式：git diff --check --ignore-submodules，exit 0；仅原有任务jsonl的LF→CRLF提示。
- 国际化：./scripts/i18n.ps1 check，exit 0；十九派生语言均750/750，基准zh-CN参与生成/键校验，合计二十语言。
- 编码：31个设置归属文件严格UTF-8解码、CRLF；30个HEAD已有文件BOM一致，生成snapshot原本未被HEAD跟踪所以仅检查其当前编码/EOL。
- TypeCheck/编译：未执行，主会话统一ARM64原生solution验证。
- Native tests：未执行；静态核对断言不能替代运行结果。
- GUI/Computer Use：未执行。

设置侧源码保持冻结，未commit/push/archive。后续全量审查与主会话真实验证结果将追加于本文。

## Core / DM 全量静态审查（修复跟进中）

审查扩展至CanvasPanSettings、CanvasNavigation、DrawingController、WindowControl、TouchpadNavigation及相应测试。按主会话要求源码保持只读；以下问题由所属实现代理按报告修复，不由review代理并发改文件。

### Findings（已由实现代理修复并静态复核）

1. SuppressLaterPendingContacts原先仅按Down时间把更晚来源pending触点抑制，会吞掉不重叠但队列QPC倒序的第二段手势。现读取incoming真实终态，仅生命周期重叠才抑制，同QPC仍算重叠。真实生产probe覆盖重叠/同QPC/严格非重叠，断言净几何75或150、实际样本数及各自终态回收。
2. lastInertiaKind原先按publication顺序更新，随后kernel按QPC排序后可能最终coast来源不同，热开关会停错来源。现CanvasPanFrameOutput::sourceKey由Snapshot交出kernel真值，adapter只按该真值更新；双向Mouse/Touch倒序生产probe断言禁早源不误停、禁真实末源立即停。
3. Window Stop原先在Scope仍访问self之前SetEvent，可能Host立即销毁WindowController后Scope继续访问。已把完成确认移到最外层Finish尾部，之后不再读取self；deferred普通Update及最后通知HANDLE寿命的补充修复仍在跟进。
4. Shutdown原先忽略RemoveEventHandler/Abandon/Deactivate返回值。现bool Shutdown直接调用生产TouchpadNavigationRetirement::Complete，同一routine用headless失败序列验证各步进度保留/重试幂等；初始化失败而清理未完成时也保留partial owner/COM责任，禁止Contact/Update，不丢未退休资源。
5. 原生DM按初始偏好去掉INERTIA会使held期间false→true无法本次释放生效。现始终让系统识别INERTIA，实时偏好在阶段/输出边界抑制，保留held最后Running几何；core补真实Window有效策略热开probe。

### 已核对的核心合同

- CanvasPanSettings默认和编码保留独立子偏好；kernel sample.allowInertia默认true用于原独立算法测试，生产Push逐contact按Down锁存类型与当前策略裁决，避免同帧混源用一个frame开关。
- 禁惯性仍处理全部Move/Up，非末指Up保留方向仲裁证据；只有最终释放清selected速度，disabled reason可诊断，booster清零。未更改产品摩擦/可靠尾段常量。
- Pen/PenTail用真实Down倒置元数据准入；真正suppressed与合法panManaged Pen分开，持续mailbox接触不反复Stop自身；拖动模式全部hover brake关闭，真实Down仍取消旧coast。
- native累计浮点位移只消费差值、不再起自有fling；Running首帧零位移也先取消旧own曲线；Ready最终差值、禁惯性后的Ready尾差、重复identity回调由真实生产probe覆盖。
- native Running/Inertia参与现有Active/活动帧，高频Mouse publisher与新增Pen hover继续保留ordinary位置不control-wake；主/辅助呈现树无改造。

### 当前剩余跟进

- 普通deferred Update必须异步合并投递，不能Finish→Update→Finish递归；最终停止通知HANDLE需独立生命周期或完成态证明，不能store stopped后被Host fastpath关闭再通知。
- 已向主会话报告native历史互斥：NativeRunning(atQpc)只看当前bool时，Ready22已到达可能放行Pen Down20这一实际重叠接触；需按手势阶段/终态时间核对，与Ready18→Pen20的非重叠分支对照。
- Native构建/运行证据仍待主会话；仅初轮编译未报源码错误不等于最终构建/测试PASS。

## 最终只读复核：已冻结完整源码

主会话已开始最终原生构建，本代理保持所有产品/测试源码只读，仅维护此审查文档。上述设置与核心问题及以下补充问题均由所属实现代理完成修复；复核后没有发现剩余确定源码缺陷。

### 补充修复与证据

- 普通deferred Update已改为清理被嵌套消费的posted名额后合并PostMessage，再return；不再Finish→Update→Finish无界递归。只有deferred Stop在最外层直接处理。
- Finish在最后使用self之前取得TouchpadOwnerNotification独立DuplicateHandle，随后只Signal/Close局部owned通知，避免Host看到stopped快路后关闭原HANDLE。真实无HWND生产probe在原句柄关闭后通知副本并等待observer，且已接control-fence。
- native互斥改为TouchpadNavigationRunningAt(qpc)的64个有界Running区间查询，含终态同QPC边界和begin admissionRevision过滤；已结束的latest Ready不再提前放行过去重叠Down，多个区间之间的真实间隙保持可接管。历史淘汰范围保守拒收。
- CancelNative(beforeDownQpc)把逻辑接手截止传给Window/Bridge：保留真正过去的Running证据，裁掉未来预排队区间并截断跨界区间。不会用旧pad区间误挡Pen16/Up19后的Mouse20，也不会因清空过去历史而放行旧Touch12。NativeRunning拒收发生在修改其他pending触点之前，拒收旧Down不得退休别人。
- TouchpadNavigationSample在首个Running锁存admissionRevision并保持到Ready；ConsumeTouchpad及CancelNative保尾段路径均比对当前revision/blocked。已有Running→Ready队列即使Active=false，gate关后重新开也不能在新代应用旧净位移；新的同代begin仍正常。
- INERTIA阶段边沿先以旧Running阶段读取最终content transform，再决定是否释放系统尾滑；Content调用后复核accepting/epoch，Ready亦如此，避免终态前丢最后真实跟手更新或在取消后继续发布。

### 最终测试断言检查

真实生产probe覆盖Source默认、最后Move/Up、持续Pen接触、hover不停/Down停旧、hot禁/重开、同帧混源、两段倒序QPC几何/回收、真实末coast类型门控、native累计双轴/Ready/identity、native held热开、互斥历史的前/间隙/后/同QPC、接手裁剪与旧拒收Down、gate关开后旧队列隔离、真实event通知。Headless使用生产retirement routine注入Remove/Abandon/Deactivate失败与重试、生产bridge验证普通累计更新不反复唤醒及overflow隔离。上述为静态确认断言存在及路径真实，尚不等于执行通过。

### Verification（最终静态范围）

- git diff --check --ignore-submodules：exit 0；仅已有任务jsonl换行提示。
- 40个当前改动/新增产品与测试文件严格UTF-8/CRLF，38个HEAD已跟踪源码BOM一致；ignored生成snapshot在设置阶段已另验。
- i18n check：前轮独立运行exit 0，二十语言750键齐备；之后没有改文案。
- 编译/TypeCheck/native tests：仍由主会话记录最终产物的真实退出码。本子代理没有构建或运行native进程，也未运行GUI。
- 真机PTP消息、RTS共存、异步pump分段延迟与光学体验：未执行，维持既有明确未验范围；不作为本轮外重构理由。

### Spec同步提醒

实现对DM始终配置原生INERTIA、在实时阶段/输出边界门控，与初稿“按偏好开启TRANSLATION_INERTIA”的技术细节不同，但外部行为仍是批准的热开关语义；主会话应在design/最终canvas-pan规范同步此执行合同，以及Running区间/admissionRevision/通知退休协议。无新增产品决策待定。

## 实测失败后的生产入口与探针栈复核

主会话实际验证曾得到：完整ARM64构建PASS（43.453秒）、config-boundary PASS、独立headless-pan PASS、setting-general PASS（38.984秒）、diagnostic-output PASS；control-fence先报Pen tap终态回收失败，随后0xC00000FD栈溢出。上述是修复前产物证据，不代替本节修改后的最终复跑。

### Findings（已由core实现代理修复）

- 前次静态审查遗漏ContactInput.cpp:78的实际producer分类。IsNavigationDevice仍仅Touch/MouseLeft，导致Pen/尾的真实Down/Move/Up没有进入导航历史，Controller放开准入仍无法闭合held。现仅在同一predicate加入InputDeviceType::Pen；AppendNavigationLocked、Close终态、Down容量检查及Move发布均复用此predicate，笔尖/倒置尾走真实有序环，MouseRight和InputDeviceType数字不变。
- TestNavigationHistory由两类扩为Touch/Mouse/Pen/PenTail，保留原5样本、末反向Move与Up顺序、Down水位及绘制latest mailbox合同断言；新增Down到终态实际倒置元数据检查。
- RunCanvasPanProductionProbe中39个固定容量Rig局部及FeelRun内嵌Rig在/Od保留大量栈槽，实际触发stack overflow。现用unique_ptr持有Rig，所有Rig&引用在storage之后初始化并同作用域生存，已有线程join及对象内部生命周期顺序保持；FeelRun的storage成员先于Rig&成员。未改项目/链接栈配置，未删减场景或放宽断言。

### Verification

三文件严格UTF-8、原BOM、CRLF检查通过。生产predicate和堆ownership修改后的第四轮构建/全部native复跑由主会话进行中；最终结果需引用该轮真实退出码，当前不补写PASS。review代理仍未修改生产/测试源码、未编译或启动GUI。

## 第四轮实测及第五轮fixture复核

主会话第四轮原生构建PASS（34.422秒）；control-fence不再崩溃，仅剩旧fixture错误地要求视口原点。前段Pen已按新需求成为合法平移设备，100→400应产生300px减15px slop，即viewport X=-285。core现把前段强化为-285±1e-3/Y=0，并在Up后保存afterPen双轴；后续Touch持有期间拒收MouseMove必须严格保持该基线，Owner/quarantine/真实终态断言均保留。此为旧预期修正与几何断言加强，不是放宽门限。review只读复核无问题；主会话第五轮构建与全部专项/完整回归结果仍待最终记录。

## 第五轮最终快照与六处规范一致性

第五轮原生构建实际PASS（126.532秒）；本代理只读核对主会话结果JSON，quick五项全部exit 0：config-boundary 2.641秒、control-fence 0.687秒、pan-feel 2.578秒、headless-pan 1.141秒、setting-general 34.203秒。完整Headless仍由主会话等待最终退出；all-results按case顺序先记录diagnostic-output，单见该项不能认定full结束。

六处规范已只读复核：canvas-pan为七节主合同，whiteboard/input/runtime同步新增设备与hover/终止边界，settings-performance及configuration-i18n仅追加短指针，避免重复另一份协议。曾发现runtime错误矩阵残留“Pen/尾丢弃”和canvas连甩泛指“笔清候选”，主会话已修为Pen/尾可准入、MouseRight拒收及程序Stop清候选。DM始终识别INERTIA再按实时偏好门控、历史Running区间、admissionRevision、接手截止、独立通知和真实producer predicate均与最终源码一致，无剩余已确认spec冲突。

六spec严格UTF-8/CRLF检查通过；Build/Logs/multi-input-artifact-hashes.json中42个源码条目重新计算SHA256仍与冻结清单一致。源没有在构建/测试途中变化。本段不代替尚在运行的完整Headless结果。

## 本阶段完整质量门：最终结果

主会话完成最终测试后，本代理独立核对build/quick/all三个JSON的case集合与全部退出码，逐个重算42份冻结源码和两个EXE的SHA256，确认与最终manifest一致。主会话统一执行进程退出0，完整日志末尾为PASS animation correctness。

| 检查 | 退出码 | 耗时/结果 |
| --- | --- | --- |
| 原生ARM64 InkeysRepo.sln Debug | 0 | 126.532秒 |
| config-boundary | 0 | 2.641秒 |
| control-fence | 0 | 0.687秒，无崩溃/剩余断言失败 |
| pan-feel | 0 | 2.578秒，2112行，failures=0，no_gpu=1/no_hwnd=1 |
| headless-pan | 0 | 1.141秒 |
| setting-general | 0 | 34.203秒 |
| diagnostic-output | 0 | 1.828秒 |
| 完整headless-all --no-window | 0 | 575.61秒，PASS animation correctness |

最终二进制：Inkeys.exe SHA256 d02f763a18a47f2f668ccfc4994d804e9e4a7a103bddb5fdb5c3e29f89600b18；InkeysHeadlessTests.exe SHA256 d2af06489e6f9aaeb8bc89c906ae4b0682aec36968da5fbbfb2f48443c57686a。

六处spec最终与实现一致，i18n二十语言750键、diff检查及原编码/BOM/CRLF检查通过。没有剩余确定源码问题；本阶段自动质量门通过。真实PTP消息/RTS共存/光学与设备手感未执行，不能以这些无窗口结果代替硬件验收。未运行GUI/Computer Use，未commit/push/archive，任务继续in_progress。review代理只修改本审查文档，生产与测试源码均由所属实现代理修复并由主会话构建运行。
