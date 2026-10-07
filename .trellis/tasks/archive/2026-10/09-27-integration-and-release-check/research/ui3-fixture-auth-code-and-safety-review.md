# UI3 fixture Auth 新源码独立审查

日期：2026-10-01。Active task：.trellis/tasks/09-27-integration-and-release-check。本 reviewer只读新Auth header112行/cpp845行、完整R2 §6–10及F GREEN_DESIGN、implementation与既有SS wrappers。唯一写入本报告，不改源/其它报告/工程/Main/Spec/账本，不Build/run/EXE/GUI/Computer Use/Git或递归。

## 判决

**SCOPED_STATIC_APPROVE：新Auth组件形状符合R2，无本批必须修补的源码阻断。** 128B独立purpose、72B冻结输入、exact object registry、同repo文件/目录证明、有限parent/negative、原子数字发布和父超时FAIL均按实际代码核对。

**整体F、auth suite、benchmark当前NOT RUN-APPROVED。** 两源未登记工程、未接Main、未编译；GetCompiledUi3FixtureSourceV1和RunAuthorizedPresentationFixture的未来真实定义尚缺，不能造success stub。auth suite唯一正例也执行同一Bar runner，所以不能用“只Auth”绕过尚未审的窗口/业务/config/输入/退出路径。后续接线和完整实码safety门见下文，不凭header或Sealed字段授权GUI/外部effect。

## Findings (fixed)

无reviewer机械自修，也没有发现需要当前修改的类型/import/本层判断错误。下面列直接核到的边界；实际编译未执行，静态断言不记类型/链接PASS。

## 实际protocol、capability与预算

- Header明确magic1430FB21/version1/purpose55493301。Packet使用fixed-width字段/align8，128B/全部关键offset断言；Frozen十个32bit＋nonce/hash/steps四个64bit共72B，nonce offset40，无额外ABI padding。POD无pointer/HANDLE/container；mutable32bit按Interlocked，64bit输出只在最终child自然死后一次copy。
- RunChild:360–378 fixed argc8；父PID有效/非self、三个数值非零/uintptr范围且互不相等、HANDLE_FLAG_INHERIT；GetProcessId精确匹配并要求parent HANDLE仍WAIT_TIMEOUT。三个owned inherited句柄被RAII管理，MapView精确128B，然后freeze输入与核初始全部outputs零。
- ValidInput:185–196有限scene1/2、round1–3、on容量256–65536/off0、source version1、nonce非零、216 steps和scene count433/435。GetCompiled descriptor同时比version/count/steps/nonzero hash；这是编译表版本一致性，不是发行者认证。没有手编常数source hash或读取任意轨迹/路径/坐标。
- raw预算使用真实sizeof(RawCallbackSample)+sizeof(RawBatchSample)，通过除法核cap≤(64MiB-4MiB)/sampleBytes并保上限65536，没有乘法溢出。4MiB只是留给finite/source/SVG/table/sidecars的预算声明；未来F allocator必须实际累加全部fixed bytes，不能靠这个helper给数据结构分配记PASS。
- Ui3FixtureAuthorization不可copy/move、没有可用公共构造，constructor只friend authorizer。const Input()/Repository/PrivateRoot/BinaryDirectory与proof按同一寿命；registry先比exact对象地址，再核packet地址和本地nonce，默认registry null。shared输入从Freeze后不再决定运行权限，Packet()只用于mutable数字输出；最终SameInput再核shared header未变。
- RunChild:385–403成功证明后才注册/发布Authorized/ack；ack失败清registry直接85，不调Bar。真实runner返回后仅Sealed才允许撤registry/destructor；未Sealed直接UnsafeRunnerReturn自Terminate90循环并保留cap/view/leases所在栈。Sealed只是一项返回保护合同，**helper没有真实thread join证据**，必须审未来Bar每个成功/失败返回，不把该字段当全部owner已静止。

## 路径、文件identity和替换边界

- CanonicalPath:121–142限定drive绝对形状，逐组件拒empty/dot/dotdot/ADS/forward slash/control/trailing-dot-space，GetFullPathName后要求相同规范path；普通产品UNC能力未改变。
- ValidateOwnedLeaf:270–285依次pop sScene/rN/master32lowerhex/release-hardening/TestResults，从固定尾层推导repo；separator-aware Within检查actual parent及expected source在同repo，不能以Repo2前缀或任意rfind marker追认。repo marker为真实regular InkeysRepo.sln与directory .trellis。
- LeasePath从drive root逐层到repo/source EXE/所有private树节点和copied EXE，以OPEN_REPARSE_POINT/BACKUP_SEMANTICS/FILE_READ_ATTRIBUTES查真实BY_HANDLE_FILE_INFORMATION、directory/regular/no-reparse；每层HANDLE不share DELETE，proof记录volume/file-index并持有。parent/expected同file ID之外仍分别核同repo、完整ancestor和marker；child current image须精确leaf/bin/Inkeys.exe路径/file ID，同名其它file不通过。
- seed/output目录bin/Inkeys、Config、opt、log都核/持租约；optional main.json/deploy.json若存在也须regular/no-reparse并保活。不存在文件只表示当前缺失，未来真实Config写/输出必须在写前再次核cap边界，不能把auth时间OptionalFile的缺失永久当安全。
- MakeParentRun只从已校验cwd repo与自己的current EXE开始，复制源须同repo；TestResults/evidence可已存在但先Lease校验，master/rN/sScene/bin和全部私有目录必须create-new，CopyFile TRUE拒覆盖。无任意output/root CLI，保留私有证据，不递归删用户/未知树。
- 这些证明是此进程内路径/对象合同，**不是OS沙箱或cryptographic源码来源证明**。lease仍share WRITE，只降低delete/rename替换风险，不保证文件内容或reparse元数据从auth后永不可修改；hardlink也未由nNumberOfLinks拒绝。parent正常CopyFile create-new给本次新副本，新增helper自身不写既有config/output；未来F对Config::Write/所有输出必须实码核create-new/精确identity/ordinary no-reparse及alias/替换边界，不能由两个路径名字或当前helperapprove包办。这是已冻结R2的写前复核/非OS-sandbox边界，当前未扩大为新业务权限。

## Parent与真实negative

- RunParent:553–573 exactly3 HANDLE_LIST：父process只SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION、ack只EVENT_MODIFY_STATE、mapping只FILE_MAP_WRITE；互不相等。其它proof/lease句柄不继承，没有parent terminate权限或按PID/name找未知进程。常规创建复用已审SS Quote/Image/FileID/StartInherited，不改变旧C/E auth。
- NoInheritedHandles私有negative:587–595确实用同copied image/参数直接CreateProcessW bInherit=FALSE、CREATE_NO_WINDOW，成功创建真实child后判其83拒绝；不是CreateProcess失败冒auth PASS。这个有限例外不外露创建flags/root/path setter。
- handshake3s/negative3s、正常总300s；WaitChild每次≤1s且预算单调剩余。ChildGuard只持CreateProcess返回的exact child HANDLE；cleanupRequested一旦强退即不能自然PASS，5s收尾失败也不改成成功。output在exact natural death后copy；输入同tuple、authorized/stage/result/count/capture timing均核，parent不并发读取64bit半包。
- auth-only positive分开授权与benchmark verdict：仍真Bar runner、仍Sealed合法输出/根边界；requireBenchmark=false不要求216目标成功，不能拿授权PASS当性能/像素/整体场景PASS。benchmark要求自然exit0/Passed/216/无unverified、received≥432且received=enqueued=consumed，并受trajectory上限433/435。
- 输出路径由CheckOutputPaths在owned叶内核regular/no-reparse。capture on要求raw两文件；off无需raw且数字time0只作absence sentinel，未来JSON应null/unavailable。文件存在/计数只是artifact门，不证明CSV/schema内容、paint/BGRA等价、统计分母或光学FPS。
- 22个真实child negative、13层本轮private reparse、两个源/副本file reparse、7 shape/fileidentity cases都用相同生产validator。PathFixture只create-new ordinary空文件/marker，标明filesystem规则测试，不冒进程授权正例。
- reparse设置失败输出NOT_VERIFIED并整suite非0，不以缺文件/缺权限伪拒绝。造景全部在当轮owner.master下，先关闭该测试proof才改新节点；LinkOwnedEmptyFile仅同case创建、记录ID、empty/ordinary文件，用带DELETE的同一HANDLE核volume/index/size再disposition，随后指向own master中的target。真实源/复制PE和用户目录未被删/替换；无递归cleanup。

## Early dispatch与未接F边界

TryRun…Early:810–844在raw解析失败时case-insensitive/boundary-aware找全部三mode并early reject；parsed成功时任何位置出现known mode也recognized，错误位置/引用/额外参数不落普通Main。只有argv1的exact mode与形状正确才执行对应child/parent/suite。未recognized普通调用只解析/释放argv，不创建目录、线程、设备、窗口或写配置。Root必须在Main任何normal config/互斥/HWND前接入，与SS early顺序一起实审。

两个未来链接函数只有声明，无实现/工程项/Main调用。必须提供真实immutable64B source descriptor及同Bar module有限runner，并复审：

- private globalPath/bin/opt/log与真实Load/Write路径；字体/I18n/default graph，合法Pen只已核守ProductRunning=false/NotReady的路径；不调用完整Bar Initialization/MouseHook、Office/更新/自启/外部业务。
- own HWND/index→screen-tagged queue→实际nested/top consumption、action allowlist、Unavailable不OS fallback、按真实committed anchor绑定source；Descriptor hash、compiled table和所有fixed budget分配来源。
- capture-off真实pure-ready/txn/layout/SVG latch，无raw/B1 clock补救；on/off源语义与最终BGRA等价，resourceUnverified/截断/失败分母准确，不仅文件存在。
- 每个返回都source/Cancel consume→正式Close原15s→Interact/Display/同步Unregister/Window/Scheduler真join→Take/封口→撤source/probe/cap。helper Sealed guard不能替代这个实际调用链；状态/计数也不是OS线程signaled证明。
- root完整Solution Debug|ARM64（三架构布局/Release另门）、negative实际结果与一个受控hidden scene实码运行前CLEAR；之后才两个scene三轮Release。auth suite正例有真实Bar调用，当前也不能运行。

## Findings (not fixed)

- 未登记/未接Main/两个真实F定义缺失；类型/链接、全部新auth/路径negative/positive、真正Bar bootstrap/来源/业务/闭合写边界/join/输出/等价/数据尚未验证。没有动态PASS或GUI运行许可。
- lease shareWRITE、optional absent/hardlink及shared mutable packet不是全OS隔离；未来写前与真实owner/latch/source使用必须按上面具体边界审核。当前helper没有执行这些写入/外部业务，未据此改模块边界或另造sandbox。
- packet/sourceHash/Sealed/file-artifact不能证明性能/成功像素/Thread quiescence。Win7/真笔/光学/其余scene/family/Settings/HC-H2/发布门保持其它任务范围。

## Source identity / Verification

| 核对对象 | SHA-256 |
| --- | --- |
| Ui3PresentationFixtureAuth.h（112行/5785B） | 20AB2AF89744BF52871239891CE97399762CA7FC46B21E63D5FFC62EFA087D13 |
| Ui3PresentationFixtureAuth.cpp（845行/44613B） | 552EEC5AB854D239D5354680DB13433C38D831A2DD428C5D6ED7D5C07737F5EB |
| implementation-report | 05A7F1906B460E9854951FC11BCFA4A797054179ED2EF2FD26CEEB20D2420530 |
| R2 contract | B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097 |

SS.h/cpp1270AF02…802C0A / C5F4BE3E…182EC3保持已审/已编译委托层，不以其旧Build0升级新helper链接。两新源严格UTF-8无BOM/CRLF/static字段与实际调用链已核；Lint/TypeCheck/Build/Tests/GUI均未运行，按本只读分工没有Git操作。静态断言和源准备完成不等于类型/运行通过。

唯一新增本报告；SCOPED_STATIC_APPROVE允许root继续具体真实F定义/接线和下一独立实码审查，不授予尚未review的窗口/外部effect或结束整体任务。