# C3 真实清理夹具实施记录

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。worker：`failed_cleanup_module_impl`。

状态：实施中；没有构建或运行结果，GREEN_DESIGN 不等于运行 PASS。

## 写入前计划与接口

- 冻结合同 SHA-256 `6EE00C0A97B7B16308C2DAD644AF2807EACD138F4960DD4C04B104598D34E0F6`；最终独立 design-review 为 GREEN_DESIGN。普通 shared header SHA-256 `9EC25422BC7CEBEC10F3CD686D559E91E0A68F09EE44A0FF06FAF5E3108674D4`，不由本 worker 修改。
- 第一单元 C3-A：实际 style 拒绝的 C03、before/created 两个 C05、成功非首内容 Present 门的 C08；保留真实 owner/Heap State/线程 envelope 强引用，逐 selector 前提与 release 反例，不支持项明确失败90。
- 后续 C3-B：C09/C10/C11 与全新严格 reader，真实 Host input/保存/Close/Stop，与完整值 receipt 编码。C07 同一 Main span 由 root 实施；不在 fixture 复制该 span。
- HostStartOptions 只增加冻结的 `successfulPresentReachedEvent/continueSuccessfulPresentEvent` 默认空事件；`Host::HiddenPersistenceSnapshot()` 返回 `optional<HostHiddenPersistenceSnapshot>`，只显式 hidden caller 读取两个 production service.Diagnostics，普通热路无新锁/时钟。
- Desktop fault 增加 `enteringWriteDelayEvent=nullptr`；非零 delay 的真实 Sleep 前发事件。AutoSave module 新 `ReadLastCommittedDesktopAutoSaveFixture(ownedRoot, localDate)`；owned value 包含 status/date/session/sequence/dailySequence/fileGuid/relativePath/trigger/snapshot/sourceRevision/indexBytes。private `ReadCommittedDesktopPath` 抽取旧 WorkerMain 的 strict read/import/interval 投影，与 fixture reader 共用，普通 SubmitLoad 不变。
- 仅新 Fixture.cpp、Host.h/.cpp、AutoSave.cppm/.cpp 和本报告由本 worker 写。Main/Supervisor/helper/header/工程/spec/账本/build-run 由 root 独占；Window/RTS/Presenter/Controller/RuntimeMetrics/PPT/HiddenWindowTest 不修改。保留 C2 实码，不回退其它作者编辑。
- 新 fixture 第一动作核 `IsAuthorizedCleanupRealLaunch`。Hold 不析构、关闭活事件/线程/mapping，强退 PASS 只接受实际产品001A/0015/0016；前提错误90，C07不足91留 root。普通 config/env 不提供故障入口。
- 本 worker 不执行 Git、构建、EXE/GUI。各 frozen 单元交 actual hashes/编码/静态检查，运行前 root 安排独立 actualdiff/safety CLEAR，再串行完整构建与测试。

## 写入前源身份

| 文件 | Bytes / BOM / CRLF | SHA-256 |
| --- | --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h` | 9824 / 无 BOM / 285 | `C2EE47AF7198B26BA4BA8BFCC782300E46D1539E0180F1031083674F8CEF9B2E` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 83125 / 无 BOM / 1716 | `60E84B339CB16B8A17AA72A6A62CC83E02A15CAE5513E3292ADFE9C0617FC595` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cppm` | 4169 / BOM / 145 | `653452134BF937674D237CD9AAED2381D42A41E7497C8A6E4DF62D34AFF90FED` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp` | 40173 / BOM / 1176 | `5354DEF6DF4C06E484758FB26D87C5E478DE22B136C40945DC4F170B0B3C946D` |

新 Fixture.cpp 写入前不存在。上述四源均严格 UTF-8、纯 CRLF。

## C3-A PATCH_READY（源码冻结；没有动态 PASS）

本单元按 root 允许的 staged 顺序只实现 C03/C05/C08 的 hold/release。C09/C10/C11 和三个 reader selector 在 switch default 明确 Failed/error90；C07 直接交 root 的 Main entry，不复制生产 fallback span。存储场景的完整 receipt/digest 和相应 runner 仍待 C3-B，不把模块 reader 已写成夹具已通过。

### 实际调用点

- entry 第一动作 `IsAuthorizedCleanupRealLaunch`；失败返回83，不创建 gate、artifact、COM、窗口或 fault。通过后才创建 heap State。新文件不调用 SendInput、OpenProcess、Office、设置、配置、文件写入/删除或运行普通 wWinMain。
- heap State 由 runner、四角色 WindowSpec lambda 和每个 Win32 thread envelope 共同保活。HostStyleCallbacks/startupContext 借 State 成员，State 不持 Host，不形成业务 owner 环。辅助 thread 只做实际 Start 或 root 正式 Close；异常不返回析构活对象。
- C03：先真实 Window.Start+Dormant Complete，ULW required/allowDComp=false；实际 style callback 拒绝，核 GraphicsReady、精确 set LAYERED/clear NOREDIRECTION mask、rejection=1、首帧未完成、四个本 child hidden/owner链。外层 signal 的 after-wake 门已进入才发布 FaultReached；没有额外 Close。local/outer 实际 grace 由 signal 已 Armed 值取得。
- C05 before：required Drawpad.beforeCreate 真 false，旧 Drawpad=0、前三角色仍 owned/hidden；created：beforeCreate=true、真实 HWND 的 created lambda 记录并 throw，核四角色仍活。scope 的真 Rollback Begin-after-wake 门停在 readyPromise/销毁前，未伪造函数 bool。
- C03/C05 hold：runner 永不返回，不 Complete/关闭事件/析构 scope；实际 helper monitor 自守。release：250ms 放门→exact starter HANDLE signaled→Start真实false→StopProduct（C03）/Window.StopAndJoin→四/三角色销毁边沿和旧 HWND 无效→Complete真join→旧Signal0→超过原grace+1000仍活且没有fatal/intent见证→Finished普通0。before=false 的 Drawpad destroyed 必须0，不错误要求被创建。
- C08：真实 ULW Host/RTS Start，startup Dormant Complete 后发布 Desktop Pen/nonselection。仅 owned Drawpad 的 private contact 消息，Down等待真实新stroke、每个Move等待 consumed sequence、Up等待新terminal/Stored/history/completed kind/current content成功版本。只有实际 Present门已到且上述前提成立才 FaultReached；若诊断时序不满足，明确error90，不能靠event名计绿。
- C08 hold：另一个 owned thread 调 root RunAuthorizedCleanupClose，读取真实ordinary tick/state2或4，再真正 StopProduct，原final-save/drain/join等待render owner。没有给普通 Close 增 startup grace。release：250ms放门后同正式 Close/Stop，Window join及辅助close HANDLE真signaled，完整自然0。产品停止若意外返回hold，明确error90。
- Fixture `TerminateProcess` 只有一个出处，固定90，只供前提/异常失败在活 owner 时保留全部对象直到死亡；不会用它制造001A/0015/0016，也不发布Passed。所有成功Finished前已完整join，hold的result仍Pending，父从exact HANDLE自然死亡独立判。

### Host 与 Desktop 窄增量

- Host 两事件默认nullptr，ObservePresented位于原快照/ULW统计/PublishRuntimeRevision之后；只 hidden injection+成对事件+succeeded+firstFrameReady+hasStoredContent+正确contentRevision 才 SetEvent/等待，普通帧无新时钟、无新锁。错误事件会抛到已有绘制异常边界，fixture缺前提90而不是误计绿。
- `Host::HiddenPersistenceSnapshot()` 只显式调用时读两个生产 service.Diagnostics；许可来自当前 generation 不变的 startOptions.hidden flag，不无锁读 Stop 会写的 bool，不扩普通RuntimeSnapshot。
- Desktop `enteringWriteDelayEvent` 默认nullptr；只非零真实 delay 在 Sleep前触发，SetEvent失败该事务明确失败。事件仍由测试持有到worker join/死亡。
- `ReadCommittedDesktopPath` 最小提取原 WorkerMain 的完整 production ReadUInkFile/strict provenance/import/fileGuid/interval投影。旧WorkerMain共用该函数（默认不要求sourceRevision），原SubmitLoad和records逻辑不变；fixture读者额外要求真实sourceRevision。
- `ReadLastCommittedDesktopAutoSaveFixture` 在同一root/date NamedMutex下使用原ReadIndex/ValidateIndex：有效primary优先、有效backup才回退、两缺失NotFound、损坏失败、不NewIndex。最高dailySequence唯一末项，拒绝目录/文件reparse与相对名冒号，再严格读索引指向的UInk。返回纯owned值/原index字节/真实sourceRevision，零写盘/records注入/业务ready。
- Desktop reader API 已具实码，本 C3-A fixture 尚未调用它；完整恢复receipt和readability仍未验证。

### 静态检查与待验证

- 五源严格UTF-8/原BOM/纯CRLF、无trailing whitespace的byte检查通过。未执行Git命令；actual minimal diff/git diff --check 由 root 在冻结后核对，未借共享其他作者源的格式化形成噪声。
- 第一授权检查顺序、fixture自终止仅固定错误90、无全局输入/外部process lookup、Host事件段无GetTickCount/chrono、Worker与reader共用同一个strict read接缝已静态检查。
- C2前置接线、Window/RTS/Presenter/Controller/RuntimeMetrics/PPT算法/HiddenWindowTest/helper/共享头/工程/spec/账本未由本worker修改。没有构建/运行/GUI/性能/Git结果；运行前独立actualdiff+safety CLEAR及root完整Solution仍必须完成。
- root需把新 `Draw3.FailedCleanupFixture.cpp` 登记到主项目/filters；不登记Headless、不新增构建系统。共享头和root函数链接、module SourceRevision可达性、Win32/x64/ARM64布局、真实RTS/ULW和C08诊断时序尚需实际编译/运行确认。
- C3-B仍要补C09/C10/C11完整真实Host trajectory/worker到达/正式Close/Stop、新reader、完整geometry variant/depth32编码及receipt一次封口。当前不能运行这些selector当作通过；root已经接受staged unsupported90策略。

### C3-A 冻结身份

| 文件 | Bytes / BOM / CRLF | SHA-256 |
| --- | --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.FailedCleanupFixture.cpp` | 22407 / 无 BOM / 495 | `BAB8B65749FDEE676DDAFD31D497D00154048D746BBBFD523A03DAB877D5154F` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.h` | 10616 / 无 BOM / 299 | `1B12F3901E5474A595D0B1DBA935DCA6B89BCF899CFC4BE799D227462F9E4814` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 84549 / 无 BOM / 1740 | `823EA958822E05C17F6546477183968B9BE64E9EE117D621294CBA217FE7FE10` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cppm` | 5088 / BOM / 163 | `0C7186F5D3FB6C93898CFEA81D00BAC802B3F6CC24D439C7A0B509F416F3A8C6` |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp` | 44306 / BOM / 1277 | `3C009E6577DC6B60D8C4E7B45061594DE1817771525D0E6F44B40D8C49296C1A` |

五源自此停写，交root独立review/共享输出槽。C3-B开始前须串行恢复写入窗口并另记源码身份，不能在root构建扫描期间继续半写同一fixture。

## C3-A 首次组合构建源码错误与最小修补

root 执行首次冻结组合完整 `InkeysRepo.sln Debug|ARM64`，session77323 已结束，实际 `TestResults/release-hardening/c3a-u2green-b2red-debug-arm64-build.status.txt` 已只读核对为 `exit=1`。第一个有意义错误是 AutoSave.cpp:805 的 initializer_list 同时收到 `std::wstring*`（可修改的 indexPath）与 `const std::wstring*`（const path），无法推导统一元素类型，产生 C3535/C2440；807 的初始化错误为后续连锁。这是本次源码类型错误，不归因本机或 Codex 环境。

本 worker 只将这一列表改成显式 `std::array<const std::wstring*, 2>`，两指针仍指向同一原索引/UInk路径，校验/读取算法和顺序不变。没有修改其它四源、header或开始C3-B，没有运行构建/Git/EXE。root 提供的 Headless 目标成功输出不等于整个 Solution 成功；修补后候选尚未构建，不预填 PASS。

- AutoSave.cpp 修补前 SHA-256 `3C009E6577DC6B60D8C4E7B45061594DE1817771525D0E6F44B40D8C49296C1A`。
- 修补后 SHA-256 `E897076C015CDA579E256B292F5505E64C2E286A14810A0F3DA62C9FE0DB3F4F`，44340 B，UTF-8 BOM、1277 CRLF行、无 lone CR/LF。
- 仅一个源码行改变；再次交 PATCH_READY，源码停写，由 root 安排后续完整构建及 checker 对应新身份只读复审。

## F-065：fixture strict-reader 预读租约修补 PATCH_READY

独立 `failed-cleanup-real-cases-code-and-safety-review.md` 确认原 reader 在 ReadIndex 后才查 index/UInk reparse，且 root/date 只有属性查询。原普通 ReadTextFile 会按路径跟随链接，这是本次实际源码缺口；当前 fixture reader caller 仍未实现并明确90，不能写成当前已运行的产品恢复入口风险已验。

root 单独 WRITE_ALLOWED 后，本 worker 只修改 AutoSave.cpp 与本报告，未开始 C3-B，其它四源/CPPm/Header 均未改。

- 新匿名 namespace `DesktopFixtureReadLeases` 仅 fixture 使用，禁止 copy。CreateFile 先 OPEN_REPARSE_POINT/BACKUP_SEMANTICS，再核 GetFileType==DISK、真实 BY_HANDLE_FILE_INFORMATION、预期 directory/regular 类型、nonreparse；句柄由 RAII 保活到读完。
- NormalizeFullPath 后，窄 fixture 只接受 drive 绝对形状；从 drive root 到所有 ownedRoot/artifact/desktop/date 组件逐层开句柄。拒绝异常组件/设备或UNC形状、点/点点/尾空格点；普通产品其它路径入口未改。日期仍用原完整日历验证。
- 目录与文件均 FILE_SHARE_READ，不共享 write/delete；目录额外拒 write，用于避免已核目录原地修改 reparse 的竞态。目录 FILE_READ_ATTRIBUTES，文件 GENERIC_READ，不作写操作。
- Index/backup 的成功 lease 先于对应 ReadIndex。真实 FILE_NOT_FOUND/PATH_NOT_FOUND 保留 Missing，并完全跳过按路径重开的 ReadIndex；不在 missing 检查后被新链接诱导读取。reparse/错误type/访问或sharing失败直接 fail closed。
- 原 primary Valid优先、Missing/内容Invalid可用有效backup、两缺失NotFound、无有效副本失败的选择规则保留；绝不NewIndex。选中UInk也先持真实只读 lease，再调用原 ReadCommittedDesktopPath；index原字节读取仍在同一lease与NamedMutex内。
- NamedMutex先声明、leases后声明，使所有预读文件/目录句柄在全部读取后先关闭，再放 mutex，避免后续正常提交被自持readonly句柄挡住。分配失败会关闭尚未入vector的唯一句柄再传播到既有reader catch，不泄漏未登记lease。
- 原 ReadTextFile、ReadIndex、ReadCommittedDesktopPath 和 WorkerMain 段落在编辑脚本中逐字对照保持，普通SubmitLoad/records/worker算法不变，没有新增cold recovery业务入口。

### 证据与仍待验证

- 写入前 AutoSave SHA-256 `E897076C015CDA579E256B292F5505E64C2E286A14810A0F3DA62C9FE0DB3F4F`。该旧 typed-list 候选已由 root 后续构建越过 AutoSave 编译错误，但完整Solution仍因另一UI源错误退出1；不是本轮新lease候选通过。
- 新代码 byte检查通过：原UTF-8 BOM、纯CRLF、无trailing whitespace；预读顺序、真实 C++ 反斜杠字符字面量、lease/mutex析构顺序和其它四源冻结hash已核。
- 未执行 Git/build/CLI/GUI，root独占actualdiff/git diff --check、独立增量safety及下一完整Solution。没有运行reparse正/负例或成功primary/backup/缺失/租约失败真实数据，未把静态修补写成已验证安全。目录更严格share规则的实际fixture可用性亦留给这些隔离测试。
- C09/C10/C11 trajectory/完整receipt/reader仍未接；其它F-065 expected校验与后续C3-B由root/相应writer继续，当前不能扩大reader CLEAR。

- 当前 AutoSave.cpp SHA-256 `F502CEAD1F4F06CD60F09759309A728616339D392F4E39CD6CC8340FBD8DE533`，47629 B、UTF-8 BOM、1345 CRLF行。
- 再次交 PATCH_READY，源码停止写入，等待 root 与独立增量safety审当前身份。

## C3-B 写入前计划（WRITE_ALLOWED，尚无新动态证据）

root第三完整Debug Build0、strictHeadless0/PID34212、U2 parked0/PID35248、PptCOM0/PID7044，以及C05-before-release父1044/child5408自然0，均是先前冻结PE/source的实际证据，不转记为本批C3-B通过。原C3-A gate/owner逻辑保持。

- 本批只在原五源与本报告补完整Desktop/PPT/NaturalClose/3reader。先完整value编码/SHA与receipt一次封口、严格actual reader，再真实Host trajectory→worker到达→RootClose→HostStop。
- Digest对完整snapshot的全部字段与11种MessagePack variant按原序、定tag/little-endian/optional/length/depth32编码；finite/计数/长度边界严格失败。Desktop effectiveActive取active为空时canvases，PPT3active含独立EndScreen，counts跨完整有效文档。
- A receipt仅hold在BaselineReady封口；release/NaturalClose内部保存A，最后B在Finished封口一次；reader只在ReaderDone封口。actual loaded target/index/source/model字段填值，不用expected构造observed。
- PPT metadata只从production fresh service.Load的已严格验证completion和对应owned index/UInk取证。origin PID/旧数值HWND仅重构bindingToken，不发消息/OpenProcess。已向root指出原信封缺旧HWND输入，建议仅两PPT reader使用既有Trace.oldDrawpad的bounded数据，Header/argc不变，由root实现与增量审。
- F065有限反例只在授权ReadDesktopCommitted内、固定owned子根克隆实际已提交文件/index；成功/missing/backup/corrupt与仅指本run owned target的reparse，权限不可得明确NOT VERIFIED，零全局配置或外部path参数。
- root继续独占所有Header/Main/Supervisor/Deadline/工程/build-run。两其它writer的UI/Controller/RuntimeMetrics不碰；新candidate必须重建+增量actualdiff/safety CLEAR。

C3-B起始身份：

| 源 | SHA-256 |
| --- | --- |
| `Draw3.FailedCleanupFixture.cpp` | `BAB8B65749FDEE676DDAFD31D497D00154048D746BBBFD523A03DAB877D5154F` |
| `Draw3.Host.h` | `1B12F3901E5474A595D0B1DBA935DCA6B89BCF899CFC4BE799D227462F9E4814` |
| `Draw3.Host.cpp` | `823EA958822E05C17F6546477183968B9BE64E9EE117D621294CBA217FE7FE10` |
| `Draw3.AutoSave.cppm` | `0C7186F5D3FB6C93898CFEA81D00BAC802B3F6CC24D439C7A0B509F416F3A8C6` |
| `Draw3.AutoSave.cpp` | `F502CEAD1F4F06CD60F09759309A728616339D392F4E39CD6CC8340FBD8DE533` |

## 2026-10-01 C3-B PATCH_READY（实码冻结，未构建/运行）

本批C09/C10/C11与三个reader分支已从原error90 stub替换为真实调用；有限未知case仍错误90，C07继续交root同一Main span。原C3-A before/created/Presenter/Render gate、after-wake和成功放行逻辑保留，没有触碰Window/RTS/Presenter/Controller/RuntimeMetrics/PPT存储算法/SharedHeader/Root函数/工程。

### 真实保存和Close/Stop

- C09/NaturalClose都从真实ULW ProductHost、private AutoSave根、hidden contact trajectory开始；A的Down→6个逐consumed Move→Up→Stored/成功content版本后，真实Clear accepted/command count推进。等待actual accepted1/committed1/failed0/pending0，调用F065严格磁盘reader得到A模型和完整index/UInk身份，不用standalone保存worker。
- B换真实Pen颜色后同一trajectory和Clear。C09 hold设置actual60秒writeDelay+entering事件，gate到达后核accepted2、committed仍1、pending非零；另一owned close thread调用真正RootClose，再管理线程真正Host.Stop的final capture/实际worker drain。hold若Stop提前返回错误90，只有root exact自然产品0015/0016可判截止。
- C09 release实际400ms delay及进入事件，第二Committed/pending0后同正式Close/Host.Stop/Window join；C11 fault全空，同A/B两次保存和正常排空。最终getter核绝对accepted2/committed2/failed0/pending0，严格读最后B；不是拿accepted当Saved。跨午夜读入仅尝试实际capture日期与当前生产日期，receipt日期/序号仍取实际index。
- C10初始化same-nonce storageSession，真实Root-owned SourceIdentity/601、602和独立EndScreen。Actual current Load NotFound+601 empty-ready先完成，hidden实际completion计数核NotFound与failed基线；后续failed无新增，trace保留绝对failed数，accepted含Load不伪称Save。
- 601写A→实际切602→actual whole-document Committed。fresh生产PresentationAutoSaveService真实Start/SubmitLoad/CloseAndDrain/TryTakeCompletion并严格Loaded/Base/target/file/workspace/mutation，生成基线。返回601增加真实B，再切602，在已有UInk已durable/index未commit门停；索引完整字节hash仍基线，committed未推进。索引短lease读完立即释放，不把fixture自持句柄留到放行以阻止正常commit。
- C10 hold是真正RootClose→Host.Stop→原Presentation worker drain，原15秒先于旧30秒门上限；release250ms放门→实际第二commit→同Close/Stop→fresh Loaded A+B新mutation。三个active Canvas的GUID/601/602/EndScreen标记和A/B实际style分别核，未写Office、不打开closed入口，不拿新物理version冒Committed。

### 1024B receipt和完整value digest

- `PublishReceipt`是唯一observed写点，private observedSealed禁止第二次写。hold只在BaselineReady封口A；release/NaturalClose基线A只在State私有纯值，最终B在Finished一次封口；reader在真实结果完整后先发布readerSucceeded再ReaderDone一次封口。
- `FixtureValueEncoder`编码全部snapshot字段：file/workspace/名称/host/类型/currentPage/dpi/undo组、devices/hardware/geometry/extra、canvases/active/retained、所有GUID/页/SlideID/viewport/interval/retained/extra、全部stroke style/texture/undo/flags/x-y-width、完整operation顺序。固定little-endian、float bit_cast、optional1B、DWORD长度；没有排序、端点抽样或JSON化几何。
- actual MessagePack 11variant带index tag，Map逐原序key/value、Array原序、Extension type+bytes，深度32、finite/长度/8MiB编码上限严格失败。使用与已有production UInk相同BCrypt SHA256能力，生产UInk SHA/length直接取实际ReadUInkFile.sourceRevision，不改codec/hash算法。
- Desktop effective active为空时取canvases，counts按实际有效文档单页；PPT3active+retained从完整loaded模型计数，不因当前602空页而计0，也不重复累加镜像canvases。GUID/interval/actual date/session/daily/session sequence/trigger/index bytes/from-source SHA都来自实际reader/index/model。
- PPT target字段取真实Load completion.target，mutation取真实completion/index；sourceIdentity/key/bindingToken由verified run固定source路径+originPID/旧数值HWND重构。target/session revision用已验真receipt作新请求的合法输入，结果是实际request echo，未把它冒文件内独立版本；doc.currentPageIndex仍完整进入digest。Header/POD没有扩容。

### 新process reader与F065数据反例

- C09-read-committed不建窗口/Host/GPU，从expected的已验证日期定位，调用strict AutoSave reader，observed全部重新从actual结果生成再逐352B对比。C10同session/foreign使用全新真实service、无pending缓存，实际Loaded/Base或CrossProcessConflictDeferred且无snapshot/fileGuid，未放宽生产session/binding/SlideID。
- PPT metadata仅在private固定presentation根，directory/index/selected UInk在任何读取前OPEN_REPARSE/BACKUP真实type/nonreparse/readonly lease。唯一entry仅取证，完整schema/身份/provenance仍由实际productionLoad复核。index/selected文件lease由State.disk保留到fresh worker真CloseAndDrain，异常也由outer State保活到失败死亡。
- Root已经接受并自行实现仅两PPT reader的Trace.oldDrawpad纯值输入；fixture从header.originPID+该数据构造bindingToken，缺字段90，不OpenProcess、不向旧HWND发消息或查旧窗口有效性。
- F065有限反例只在已授权Desktop reader和固定 `_fixture-reader-boundaries` 子根，克隆本run真实committed index/UInk：valid primary、backup-only、corrupt-primary+valid-backup、missing、corrupt-only；另index/UInk/directory symlink只指本run owned原数据。没有修改original file/index、注入service records、任意external path或删除未知文件。
- CreateSymbolicLink权限/文件系统不可得单独NOT VERIFIED，写固定owned status.txt并输出标签，不把缺权限算reparse通过，不改系统/工具链。主fresh readability结果与这些专项状态独立；root应实际核status.txt。此处尚未运行，不能宣布F065动态安全已验。

### Host单个实际NotFound增量

root明确批准的唯一新增Host字段为HostHiddenPersistenceSnapshot.presentationNotFound。Impl原子计数在原ResetRuntimeDiagnostics重置，原PumpPresentationCompletions只有hidden injection且operation Load/status NotFound才增；explicit getter读取。普通default false没有新clock/lock/额外service工作，保留一个短条件分支/启动重置的成本，不声称CPU零指令。Caller在successful Start、不并行下一Start、601 empty-ready之后读取基线。

### 静态核对、root登记与证据限制

- 五源严格UTF-8/原BOM/纯CRLF/无trailing whitespace，finite case/auth先行、唯一observed writer、ReaderDone发布顺序、错误90是唯一fixture自终止、无OpenProcess/SendInput、文本括号/字符串平衡已检查。AutoSave.cppm/cpp保持C3-B之前身份；实际只新增Fixture与Host两源本批差异。
- 未执行Git/build/run/GUI/递归派发。root需对这最后身份actualdiff/git diff --check、独立源码+safety增量（PPT oldHWND输入、NotFound计数、新路径/符号链接/receipt），再完整Solution Debug|ARM64和全部适用回归/真实pair/fresh三轮。既有Build0/C3-A运行0不作为新C3-B PASS。
- 没有自然GPU/RTS/IO卡死复现；普通provider callback quiescence、Win7/Release三架构、Office、用户现场根因与新产品自动/可见恢复仍独立未验。Crypto/索引/目录share可用性、601/602 readiness、实际保存模型/完整receipt匹配和reparse正负均待新candidate验证。失败前提始终90，不能被父kill、缺gate、sentinel或文件存在升级通过。
- 新代码仍仅主fixture，工程登记已由root完成；BCrypt pragma沿项目已有bcrypt库，不新增依赖/构建系统，不把真实窗口测试加入Headless。

### C3-B最终冻结身份

| 源 | Bytes / BOM / CRLF | SHA-256 |
| --- | --- | --- |
| `Draw3.FailedCleanupFixture.cpp` | 69391 / 无 BOM / 1186 | `452D2EDE97F1FC1F029F8EBE749E3C312A52F8C95885BBDBC5BC30738511F7E6` |
| `Draw3.Host.h` | 10710 / 无 BOM / 300 | `E502A08950DE9F26D6B6E84B3910E5B7C439A8BE6B06100E1B16FAA4BA3159AB` |
| `Draw3.Host.cpp` | 85016 / 无 BOM / 1747 | `BAA95903091C33A96A7D8A7C647290A43AD1E615B0413AE0523159D485C46254` |
| `Draw3.AutoSave.cppm` | 5088 / BOM / 163 | `0C7186F5D3FB6C93898CFEA81D00BAC802B3F6CC24D439C7A0B509F416F3A8C6` |
| `Draw3.AutoSave.cpp` | 47629 / BOM / 1345 | `F502CEAD1F4F06CD60F09759309A728616339D392F4E39CD6CC8340FBD8DE533` |

PATCH_READY_C3_B：原五源停止写入，由root独占后续组合构建和运行；checker按上列准确身份增量review。

## 2026-10-01 C3-B 独立review后最小修订计划

root正式交回Fixture.cpp单writer，起始SHA `A66C27B7D9FD810389EEF3D1B666DCB24807C07A97B188C2F6721015B4C1FBC1`。已读 `failed-cleanup-real-root-implementation.md` 最后Oct1段与checker增量：root已把SetPenColor改为实际Bridge.Snapshot基线，保workspace/target/page；该修补保留，当前未有新C10运行。

- 根因1：fixture将contentRevision误当perStroke。真实Controller只在hasContent布尔边沿增version；601已有A再B为true→true，B可真实Stored/Present而原等待必超时。仅已有内容、非pause路径另核本笔真实terminal消费/terminalLocked/同strokeId/inactive/新回收，再取新Present基线（避免旧live snapshot混读），向exact owned主窗发一个有限WM_DPICHANGED刷新pulse。实际生产链为WindowControl无条件compositionChanged→Controller RefreshAfterCompositionChanged→RequestFullPresent→真实PresentFrame，ULW可达；不用只有gpuTransparent生效的WM_PAINT，不改生产version/Host/Controller。原空→第一笔/C3-A pause predicate保留，后续whole-PPT A+B严格模型/receipt仍必须。
- 根因2：F065已存在的corrupt/link对象断言只拒Loaded/snapshot，NotFound也被误计验证。改仅Invalid或IoError且无snapshot；missing专门NotFound，symlink权限缺继续独立NOT VERIFIED，不改reader算法/系统权限/外部路径。
- 本次只Fixture+本报告，其它四source/Header/Controller/RuntimeMetrics均冻结；未build/run/GUI/Git/递归，actual producer/C10/F065证据尚未运行。此前19项C3-A/PPTroot测试属于旧PE/source，不转为新candidate最终PASS。


## 2026-10-01 两项最小fixture修订 PATCH_READY

root确认无payload的WM_DWMCOMPOSITIONCHANGED可优先于DPI消息。本最终实现选择它：四角色spec中Drawpad的实际WndProc是DrawpadMsgCallback，运行期转ForwardProductMessage/Host::ForwardMessage/WindowController::HandleExternalMessage；WindowService直接使用该WndProc、无中间RECT解读。WM_DWMCOMPOSITIONCHANGED只置compositionChangedRequested并wake，不改实际DPI/viewport或读lParam；Controller消费后RefreshAfterCompositionChanged，当前ULW返回true，再RequestFullPresent/ConsumeFullPresentRequest到真实PresentFrame。系统notification不等于选择已禁用的两个DWM presenter，未改DWM/FLIP/quality。

- 已有内容且非pause分支先发布真实Up，核同新strokeId、terminalLocked、last consumed sequence超过Up前、本轮terminal+1和recycled+1、inactive、completedDrawing。之后另取Present计数基线，避免较早live计数与较新pen快照拼接。只向PID/Handle已核的当前owned hidden Drawpad以2秒有界SendMessageTimeout发送一个无payloadcomposition刷新pulse，随后必须新success/full attempt、last success、同workspace/page/PPT-ready/UI-ready/input门、primary requested/ready完整output版本、presented当前content版本。
- 这条额外受控刷新只作当前软件呈现与数据正确性佐证；不记录为第一次落笔时间、真实DPI/系统composition变化或优化收益，不进入性能采样。后续whole-PPT A+B模型、两笔颜色、mutation/file/index/receipt严格读入仍须通过，不能从旧CompletedKind/live帧或CPU快照单独计通过。
- 原空→first和C3-A pauseAtPresent整个Up/原contentRevision变化predicate逐字保留。root SetPenColor的Bridge.Snapshot修补及中文原因也逐字保留；所有workspace/PPT/target/closed gate和真实accepted≠save计数判断不放宽。
- F065 corrupt-only改明确status必须Invalid或IoError且snapshot空；成功建立index/UInk/directory symlink后同样只该两拒绝状态，NotFound不再写verified/reparse-rejected。missing专门NotFound，symlink权限/FS缺口原NOT VERIFIED/status证据保留。未改ordinary reader/算法/系统或外部路径。

静态字节核原UTF-8无BOM、纯CRLF、无trailing whitespace；其它四source哈希与冻结表完全相同。未Git/build/run/GUI/递归，新C10及负例actual producer尚未运行；此前19个C3-A正例是旧PE/source证据，下一完整Debug新candidate须适用复验、独立incremental actualdiff/safety，不能把此源码修补当最终PASS。

- Fixture before `A66C27B7D9FD810389EEF3D1B666DCB24807C07A97B188C2F6721015B4C1FBC1` → after `2846921497D3F6296D9FBB892C883CAD776EE3198885BEA53938D5FD3D4FC457`。
- 当前 72986 B、UTF-8无BOM、1232 CRLF行；只本fixture和本报告写入，再次PATCH_READY停写。

## 2026-10-01 C3-B 首次真实失败调查（诊断前记录）

已只读root冻结 `c3b-frozen-debug-candidate.json`：完整Debug Build0，strictHeadless26160/parked22032/PptCOM17700均0；这些是类型/既有逻辑门，不是新保存case通过。实际三prefix status各exit65，四授权negative全PASS，positive均自行90/PrerequisiteFailed，未到正式Close：

- C09-desktop-release parent33944/child28416：start46758062、gate46759000、death46764062、actual2accepted/1committed/0failed/1pending、close0。
- C11-natural-close parent33604/child34864：start46765031、death46770453、actual2/1/0/1、无fault/gate、close0。
- C10-ppt-release parent29332/child30240：start46771343/death46776703、Host Start已返回、尚无保存/gate/Close字段，当前需精确ready链诊断。

只读private历史两Desktop目录，确认index仅A/sequence1且两UInk已存在：C09 A/B均1545B；Natural A1545B/B1571B，无bak/tmp。B已越过CommitUInk，不只是completion-pump缺通知；NamedMutex或index后续具体停点没有动态栈/producer阶段证据，未确认产品死锁，未随机改算法、sleep或延长Wait换通过。旧目录/文件未修改。

本轮仅fixture及本报告可写：先在失败处增固定数字phase与实际runtime/bridge-ready/input/persistence快照、固定owned失败文件；三具体等待断言保持。若仍停实际CommitIndex，已向root提案单独授权AutoSave现TestFaultInjection默认空entering/acquired/readCompleted三事件，在真实private mutex/ReadIndex边界发信号，无等待/日志/timeout/算法变化；当前其它四source仍冻结，未实施该probe或产品修补。


## 2026-10-01 失败定位版本 PATCH_READY（未验证实因）

Root批准将本次唯一写入扩大到AutoSave.cppm/cpp的现TestFaultInjection三个默认空事件；Host与其它共享源仍冻结。没有改历史private数据、数量/颜色/身份/15秒断言、sleep/WaitUntil预算、NamedMutex等待、ReplaceFile/UInk/reader lease策略。

### 诊断输出与phase

- FailFixture先按原协议发布error90/resultFailed/PrerequisiteFailed，再best-effort写本次新run根下固定CREATE_NEW `fixture-failure-diagnostics.txt` 和 `fixture-index-probes.txt`，随后仍固定90 noreturn自退，不回业务析构。只授权launch内部调用；不覆盖旧文件/用户路径，不把任何诊断输出当PASS。
- runtime数字包含actual running/workspace/selection/page/content/presented/counts、pen stroke/consumed sequence/active/terminal、ingress/recycled、requested/ready output、command pending、PPT actual ready/UI/input及与真正Bridge当前wanted identity的match、实际Desktop/PPT服务绝对计数/NotFound。无producer内部栈或磁盘等待原因的推断。
- phase201=Desktop根/启动，202=Clear A，203=等待A终态，204=读A，205=B trajectory，206=Clear B，207=delay gate，208=等待第二Committed/pending0，209=正式Close/Stop，210=最终读取。
- phase301=PPT根，302=Host开始，303=合法empty NotFound前提，305=baseline strict读者；Select页额外phase `320+page*10`=resolve/publish、`321+page*10`=等待renderer-ready/内容/成功版本、`322+page*10`=真实UI-ready接受、`323+page*10`=等待input-ready。拆开同一原assert只是使停点可见，条件/6000ms/原input等待预算不变。
- 失败写诊断有I/O和getter的best-effort成本，只在已宣告夹具失败后，可能仍被父FAIL上限结束；无产品成功路径热输出，没有把它用于产品deadline owner。实际失败原因尚待下一候选数据，当前不声明已修复产品死锁。

### AutoSave实际索引边界（Root批准）

`DesktopAutoSaveTestFaultInjection`追加默认null `enteringIndexMutexEvent/indexMutexAcquiredEvent/indexReadCompletedEvent`。ProcessRequest把其已有同一fault snapshot窄传private CommitIndex：真实Acquire前、成功后、真实primary/backup读入与选择结束后SetEvent；读入选择后明确失败返回也发read-completed。signal失败被忽略，不改变正常保存结果；未新增等待/clock/日志/算法/重新读/超时。

Fixture仅在已授权Desktop C09/Natural第二请求前创建3个manual事件，A保持原fault-empty；State强持句柄直到真实worker join/死亡，global alias只用于该唯一run失败诊断，C3-A/其它case全null。`index_probe=enter/acquired/read`的0=未到/未设置、1=真实已signal、2=Wait错误；缺信号不当成成功。用它区分未进索引/Acquire未返回/ReadIndex未返回/索引后续，不猜mutex死锁；正常产品默认空，SetEvent空分支之外没有动作。

### 源码与验证限制

- 三源原UTF-8/BOM/纯CRLF/无trailing whitespace、词法平衡、probe实际顺序、failure先发布再诊断已检查。Host h/cpp仍E502/BAA精确身份；PPT/Controller/RuntimeMetrics/Bar/Main/Helper/项目/spec未写。
- 本agent未Git/build/EXE/GUI/ComputerUse/递归。原三case真实FAIL记录保留；没有已证实夹具前提可合法放宽，目前只提供准确数字停点和实际索引probe。Root需新独立增量safety/完整Build与相同selector复验，实际数据再决定下一最小修补；旧C3-A19PASS不升级这些保存行为。

| 本次冻结源 | Bytes / BOM / CRLF | SHA-256 |
| --- | --- | --- |
| `Draw3.FailedCleanupFixture.cpp` | 79373 / 无 BOM / 1312 | `9671E99A4A74EE0132559825FF6AB799F2FE05C0333AA328EDC2F2B816D6BA58` |
| `Draw3.AutoSave.cppm` | 5318 / BOM / 167 | `A2AF7DD2ABDC87C6EF3C21840D82CE3E72C8409DB7A8FB6EEFA96D9DC99A4B8A` |
| `Draw3.AutoSave.cpp` | 48068 / BOM / 1356 | `CC20BFEF3BA127117E8C95037ED412D9468CB0C08EE7699C77E12AA52486DABF` |
| `Draw3.Host.h` | 10710 / 无 BOM / 300 | `E502A08950DE9F26D6B6E84B3910E5B7C439A8BE6B06100E1B16FAA4BA3159AB` |
| `Draw3.Host.cpp` | 85016 / 无 BOM / 1747 | `BAA95903091C33A96A7D8A7C647290A43AD1E615B0413AE0523159D485C46254` |

PATCH_READY_DIAGNOSTIC：三可写源停止写入，交Root串行构建/运行和独立对应身份review。

## 2026-10-01 索引探针实际FAIL后的只读取证与下一诊断提案

本段只读Root已结束的session95736产物与当前保存实现；当前五源码全部冻结，只追加本报告。Root报告该精确诊断候选完整Solution Debug|ARM64退出0，Headless13540、parked32812、PptCOM12848均0，独立增量E571…AD60 CLEAR。下面三个positive仍是父65/child90，不是保存、恢复或deadline通过；四授权负例通过单列保留。

### 当前真实停点（与旧gate快照分开）

| selector / parent / child | 私有run（`TestResults/release-hardening/`下） | 失败诊断实际终态 |
| --- | --- | --- |
| C09-desktop-release / 34268 / 10968 | `Inkeys Shutdown Test UEF 测试 34268 51945843 0` | phase208；desktop accepted2 / committed1 / failed1 / pending0；index_probe=1/1/1 |
| C11-natural-close / 30260 / 30924 | `Inkeys Shutdown Test UEF 测试 30260 51952312 0` | phase208；desktop accepted2 / committed1 / failed1 / pending0；index_probe=1/1/1 |
| C10-ppt-release / 16028 / 22392 | `Inkeys Shutdown Test UEF 测试 16028 51958687 0` | phase333（page1 input-ready等待）；PPT accepted2 / committed0 / loaded0 / failed2，actual NotFound1；index_probe=0/0/0（该探针只Desktop） |

- 已逐字读取这三个run的failure-diagnostics/index-probes、对应`c3b-index-probes-...status.txt/stdout.log/stderr.log`及1024B原packet。raw中的desktop `save=2/1/0/1`是gate时的TracePersistence值；失败诊断后来真实采样为2/1/1/0。因此第二Save已经以Failed终态出队，不能继续称永久pending或mutex死锁。
- Desktop的index probe111只证明真实Acquire前、成功Acquire后和真实primary/backup读入选择均已返回。read-completed在primary/backup不合法而返回false的分支也发，不能把111当ValidateIndex成功或ReplaceFile已调用。两个新run都保留A、B UInk文件，index.json只有A且daily/session sequence=1，无backup/tmp；只读未修改历史目录或文件。
- C10最后快照workspace2/page1 empty、content=presented4、实际renderer-ready与UI-ready都与wanted完整identity匹配，inputReady1。但这采样发生于原等待失败后，不能倒推原5000ms预算中已到达。PPT absolute failed2扣除实际NotFound1后新增Save失败1；该run目前无UInk/index文件。不能先把它归为输入时序或仅慢保存，也不能放宽等待、failed/committed条件。

### 已核当前失败路径

Desktop CommitIndex依次真实NamedMutex、ReadIndex+ValidateIndex、append新entry、JSON写临时文件+Flush、临时ReadIndex/ContainsRequest、ReplaceFileW（已有primary）或MoveFileExW（首次）、最终ReadIndex。当前三个probe没有后续临时验证/发布结果；ProcessRequest已有`stage=index-commit`、`exception`、`uink-commit`等stderr，但没有保存ReplaceFile原GetLastError。严格reader的本地lease在返回前析构、mutex随后释放，当前失败终态和探针排除了先前“reader漏mutex释放导致永久pending”的假设；仍不能从无tmp/backup唯一确定失败调用。

PresentationAutoSave的现有worker在Save非Committed时已经fprintf实际`status`和`mutationRevision`；首Save可能在SelectStorageTrack/建files/strict merge/export/UInk等阶段返回，当前目录状态不确定具体分支。它不需要先改算法才能得到实际Invalid/IoError/SourceChanged等产品结果。Parent的copied child使用精确三个HANDLE_LIST、CREATE_NO_WINDOW且未设置STARTF_USESTDHANDLES，早期fixture跳过普通Main的AllocConsole/freopen，因此parent现有stdout/stderr文件没有child产品日志。

### 提案：仅auth storage producer捕获已有stdout/stderr

先向Root冻结提案，尚未获本段源码WRITE_ALLOWED，未实现：

1. 仅Fixture.cpp，在`RunAuthorizedFailedCleanupRealFixture`第一行实际`IsAuthorizedCleanupRealLaunch`成功之后、任何State/Host/Window/fault设置之前，对有限DesktopHold/Release、PptHold/Release、NaturalClose五个producer建立两个日志。readers、C3-A、C07、未授权负例不捕获，正常产品无调用。
2. 固定run直接子文件`fixture-product.stdout.log`和`fixture-product.stderr.log`，由已验证launch.directory加常量得到；不接CLI任意路径，不增加1024B字段或argc。Root在整个调用至真实join/死亡持有run和所有ancestor nonreparse/deny-delete lease。
3. 采用`_wfreopen_s(..., L"wbxN", stdout/stderr)`。已只读本机Windows SDK UCRT源`stdio/freopen.cpp`、`stdio/openfile.cpp`、`lowio/ioinit.cpp`、`lowio/open.cpp`及`inc/corecrt_internal_stdio.h`：x只和w组合，最终_O_CREAT|_O_TRUNC|_O_EXCL确实映射CREATE_NEW而非CREATE_ALWAYS；N同时设置FNOINHERIT和bInheritHandle=false；_SH_SECURE写模式share=0。这能重建GUI无console时原FILE._file=-2，不依赖dup2(fd1/2)猜测，也不先CreateFile再使用w截断。
4. 打开后从实际_fileno/_get_osfhandle验证非负fd、FILE_TYPE_DISK、真实handle信息非directory/nonreparse、inherit flag0与初始size0；任何已有文件/重定向/核验失败在Host前明确90，不继续保存前提。两流不共用一个share0文件，不增加传给helper/reader的继承HANDLE。
5. 两FILE*设_IONBF，按需要仅fixture内对cout/cerr启用unitbuf。CRT拥有文件句柄，没有局部RAII fclose/CloseHandle/恢复旧无console描述符；必须保持至Host/Window/辅助thread真实join后正常CRT teardown，hold/error90保持到进程死亡。成功返回前只能在所有worker join后fflush/FlushFileBuffers；失败不把诊断写入成败变PASS。父仍在精确child死亡后读取新文件。
6. 下一Root候选先重跑同三个selector，捕获现有Desktop stage和PPT status，保持所有5000/6000ms及15秒/原数量/颜色/identity/receipt条件。如果Desktop仍仅index-commit，再按实际日志提两个有限temp-validated/published marker或当场GetLastError，另行冻结；不盲加全框架、不先改ReplaceFile/索引schema/生产ReadIndex/普通恢复。

本提案取证会给已授权失败诊断版本增加真实日志I/O成本，不能拿该版本用于性能结论。日志也不证明已进入或通过deadline/drain；任何90仍是FAIL。两个文件由CRT CREATE_NEW实际创建且share0保活；没有修改旧数据、系统权限、配置、Office或产品默认日志行为。

### 冻结与未验证

五源码实际hash仍精确保持上一段表：Fixture9671E99A…D6BA58、Host E502A089…3159AB / BAA95903…C46254、AutoSave A2AF7DD2…99A4B8A / CC20BFEF…DABF。已核原UTF-8/BOM/CRLF，未写任何源码；只追加报告，本次report-before SHA-256为68787F2A307EE9C5500BF8A18BBEBA008BD0593B168F04907D6D239D330227F4。

本agent没有Build/EXE/GUI/ComputerUse/Git/递归。实际Save失败原因、上述CRT重定向候选编译/动态可达性、安全增量及新三case运行均未验证；下一步须Root冻结写窗口、独立actualdiff+safety及串行完整构建/实际同case取证。原C3-A19通过仍不代替这些保存或reader通过。INVESTIGATION_READY_SOURCE_UNCHANGED。

## 2026-10-01 auth storage producer CRT日志捕获最小实施（PATCH_READY_LOG_CAPTURE）

Root已明确WRITE_ALLOWED仅Fixture.cpp与本报告；按上一段提案直接实施。唯一源增量为`<io.h>`、private `PrepareStorageProducerLogs`与鉴权之后/任何State之前的一个调用，共40个纯新增CRLF行。实际existing失败诊断、三index probes、全部保存/readers/trajectory和原等待条件未改。

- `PrepareStorageProducerLogs`显式switch仅DesktopHold/Release、PptHold/Release、NaturalClose五case；其余已授权case直接return，第一鉴权失败仍原83且不触及日志。入口调用在默认shared_ptr State声明之前，因此打开/验证失败也没有Host/Window/fault/worker需要释放。
- 固定两个私有run直接子文件，stdout/stderr分别`_wfreopen_s(..., L"wbxN", ...)`。已核本机SDK源：corecrt_internal_stdio.h:636–650为N/x解析，lowio/open.cpp:263–276把EXCL组合映射CREATE_NEW，:307–311的secure写模式share0，:695–702将N映射非继承；stdio/openfile.cpp实际重设FILE._file，freopen.cpp重建原FILE。不是先创建文件后w截断，也没有假设GUI fd1/2有效。
- 每个打开的实际FILE检查reopened指针、非负_fileno、_get_osfhandle对应FILE_TYPE_DISK、GetFileInformationByHandle无directory/reparse、GetHandleInformation无INHERIT、GetFileSizeEx精确0，最后setvbuf(_IONBF)。任一步或allocation异常明确调用原FailFixture90，在任何Host之前停止；不能因打不开日志而继续并称场景通过。
- 未增加SetStdHandle、dup2、fclose、CloseHandle、flush循环、继承名单、环境开关、SharedPOD、argc或普通日志。两个CRT流分别持有share0文件，保持到所有现有owner/worker实际join后CRT正常退出或hold/error本child死亡；没有局部析构提前关闭活worker借用stdout/stderr。现有cout/endl和fprintf沿旧实现输出，本次没有启用额外unitbuf或改日志内容。
- 日志仅取证：预期下一候选保存现有Desktop `stage`和PPT实际`status/revision`，不由它生成PASS。它的I/O成本仅此auth storage测试进程，不纳入性能结论；root父原精确HANDLE死亡/POD/退出码判定仍不变，诊断不足才下一次有限proposal。

### 静态检查与冻结身份

已使用byte-preserving脚本读/改/核本文件，从最终字节删除三个新增段后精确恢复原9671E99A…D6BA58哈希，unified diff只这40个新增行。已核UTF-8无BOM、1352 CRLF/零loneLF/无trailing whitespace；静态核first auth→capture→State顺序、五有限case、实际handle/创建mode条件与无提前close。其它4源逐字SHA核仍旧冻结；未执行Git命令（Root独占）、Build/EXE/GUI/ComputerUse/递归。此静态自查不是独立CLEAR或新编译/保存/恢复PASS。

| 文件 | before SHA-256 | after SHA-256 | Bytes / BOM / CRLF |
| --- | --- | --- | --- |
| Draw3.FailedCleanupFixture.cpp | 9671E99A4A74EE0132559825FF6AB799F2FE05C0333AA328EDC2F2B816D6BA58 | 5DAC3852AB7CDD035175D5F6F3389087A64A7C4C254FFAD6EF58FC53C14878E6 | 81194 / 无BOM / 1352 |
| Draw3.Host.h | E502A08950DE9F26D6B6E84B3910E5B7C439A8BE6B06100E1B16FAA4BA3159AB | 同before | 10710 / 无BOM / 300 |
| Draw3.Host.cpp | BAA95903091C33A96A7D8A7C647290A43AD1E615B0413AE0523159D485C46254 | 同before | 85016 / 无BOM / 1747 |
| Draw3.AutoSave.cppm | A2AF7DD2ABDC87C6EF3C21840D82CE3E72C8409DB7A8FB6EEFA96D9DC99A4B8A | 同before | 5318 / BOM / 167 |
| Draw3.AutoSave.cpp | CC20BFEF3BA127117E8C95037ED412D9468CB0C08EE7699C77E12AA52486DABF | 同before | 48068 / BOM / 1356 |

本段report-before为99BC52474B7874DDE101B5A08F42EF756A57A544D51930B823C535427A348162。Root须登记这份新Fixture身份并进行独立增量actualdiff+safety；全部writer停写后串行全Solution Debug|ARM64和适用回归，再复验原三个失败selector/死亡后读取新日志。三旧positive真实父65/child90及pending0/failed1、PPT新增Save失败1完整保留；目前没有已确认产品实因/合法断言修补，没有新运行PASS。五源停止写入，PATCH_READY_LOG_CAPTURE。

## 2026-10-01 私有产品日志实际FAIL后的第二次只读调查

Root报告40行日志捕获版本已独立6C3EB34…C032B CLEAR、完整`c3b-private-log-b3-red-debug-arm64-build0`退出0。已只读session11655三个新run的固定stdout/stderr、numeric失败诊断/probe、raw父status/positive行及原文件头；三个positive仍父65/child90、四授权negative通过。当前仅本报告可写，五源码未写，保存算法和超时没有修补。

| selector / parent / child | 私有run（release-hardening下） | 现有产品日志与实际终态 |
| --- | --- | --- |
| C09-desktop-release / 33120 / 2072 | `Inkeys Shutdown Test UEF 测试 33120 53990906 0` | A request116cbcfc…committed，B request76dbaa08…`stage=index-commit/result=failed`；phase208、desktop2/1/1/0、probe111 |
| C11-natural-close / 21612 / 10292 | `Inkeys Shutdown Test UEF 测试 21612 53997656 0` | A request3184947e…committed，B request4d875d4f…`stage=index-commit/result=failed`；phase208、desktop2/1/1/0、probe111 |
| C10-ppt-release / 12640 / 30168 | `Inkeys Shutdown Test UEF 测试 12640 54004562 0` | actual empty NotFound1/failed baseline1；Save `status=io_error revision=1`；phase333、ppt2/0/0/2，新增真实Save失败1 |

三个stdout均实际到ULW enabled、RTS initialize/MultiTouchEnabled；不是fixture省略RTS成功链的证明，也不能以此证明provider callback quiescence。失败是明确保存终态，不能继续称mutex/Present callback永久卡死。所有私有文件仅只读、未删除/重写，现有A/B及packet保存原样。

### Desktop静态缩小范围

- 已按production `PackHeader`实际编码窄读原UInk头GUID：C09 A=`2fc8d5db-ea93-49f5-a175-7c03649941ff`、B=`566c27f7-1b15-4b48-8885-ff38edc2419d`；C11 A=`04f82cfd-8f2d-472c-9b89-960b08ecd097`、B=`00b28777-b1bb-459f-a2f0-87e8ebacd65b`。两对不同，request UUID/文件名也不同。生产Capture每次fresh fileGuid，Submit真实nextSequence++，第二请求没有观察到同identity的确定冲突；这不是替代生产完整模型reader。
- actual run绝对根109 WCHAR，Desktop index162、临时index203；当前这些路径不支持以MAX_PATH解释第二索引失败。首index已MoveFileEx成功且baseline同一ReadIndex/ValidateIndex/strict importer实际Loaded，第二index还可能是临时验证或ReplaceFile等后续失败；probe111不足以区分。
- 生产已有ReplaceFileW使用REPLACEFILE_WRITE_THROUGH，微软[ReplaceFileW文档](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)把该flag列为不支持。当前还没有actual ReplaceFile是否到达、BOOL或当場GetLastError，不能由文档/文件残留直接归唯一实因，更不能先删除flag/放宽schema/ACL/share策略换PASS。

### PPT首version的可核实路径疑点

- PresentationAutoSave现SaveVersionedUInk创建`files/<fileGUID36>_<transactionGUID36>.uink`，在本run的实际root下绝对version路径230 WCHAR。uink_file.cpp:473–483的UniqueSiblingPath真实再拼`.`+GUID36+`.tmp`，增加41，为271 WCHAR；SaveUInkFile:929–935的真实CreateNew失败已保存`UInkSaveResult.systemError`与WriteFailed/temp诊断。
- 只读PE解析当前`Build/ARM64/Debug/Inkeys.exe`（SHA-256 `9E17E476E00975E8556F58AC2B7B03E965230D11341BFAE5F9411E2C21E9662D`）：只有RT_MANIFEST `[24,221,2052]`的PptCOM manifest471B，无application resource ID1或longPathAware标记；工程GenerateManifest=false，源码搜索亦无longPathAware。只读注册表LongPathsEnabled=1，没有改设置。
- 微软[最大路径文档](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)说明普通Win32默认260字符，Win10后的新行为须同时registry与应用manifest opt-in。当前uink NormalizePath不添加extended前缀，WriteNewFile直接CreateFileW；所以271临时路径是强静态疑点。actual presentation/files目录已经存在但无UInk/index，与temp创建失败一致，仍必须取得saved.systemError（例如206）和actual diagnostics后才能确认；不能称已经证明权限问题或直接修改产品长路径实现/manifest/系统环境。

### 下一最小诊断提案（尚未获源码写许可）

优先复用现有explicit TestFaultInjection及私有CRT日志，继续真实断言，不另建完整I/O框架或新的任意路径入口：

1. Desktop fault追加默认false `logIndexCommitDiagnostics`，Fixture仅批准storage producer设置。private CommitIndex在temporary ReadIndex与ContainsRequest真的通过后写有限`after-temporary-validated`，原ReplaceFile/Move真的成功后写`after-index-published`。真实发布失败立即捕获GetLastError，在DeleteTemp/SetEvent/任何日志之前；只该测试flag输出numeric stage/error。primary/backup和temporary-invalid输出实际IndexReadState，不把枚举当Win32错误。
2. temp-write失败如果需要Win32来源，仅private WriteNewTextFileDurable加可选DWORD*结果，在原CreateFile/WriteFile/Flush失败调用当场回填；普通null、原算法/顺序/清理全部保留。不能用Close/Delete后的stale GetLastError伪造原因。成功两marker仅观测，不额外等待或重读。
3. PPT现有fault追加默认false `logSaveIoDiagnostics`。只在真实已判IoError的Acquire、SelectStorageTrack、EnsureFiles边界和private SaveVersionedUInk实际返回处打印有限stage；UInk使用原saved.status/systemError/真实WriteFailed等diagnostic数字及实际WCHAR路径长度（无需底层UInk模块hook）。参数沿同一fault snapshot进入共用版本函数，涵盖首save/update两调用点，保持返回status、重试/commit/index/transaction算法。
4. 本提案需Root冻结最小写范围AutoSave.cppm/.cpp、PresentationAutoSave.cppm/.cpp、Fixture.cpp与本报告；Host/Controller/UInk底层/SharedHeader/Main/helper仍不写。所有flags普通false，不新clock/wait/配置或正常打印；不改变原数量/颜色/保存身份/15秒/5000和6000ms预算，也不接受Pending代Committed。
5. Root取得实际stage/error后再决定产品问题、夹具路径边界或环境问题的最小修补。当前PPT长路径未确认，Desktop唯一失败API未定位；新源码必须独立增量actualdiff+safety+完整新候选构建后才同3case运行。失败90和原父65不因诊断提案覆盖。

### 源冻结与验证边界

Fixture仍5DAC3852…14878E6，AutoSave仍A2AF7DD2…99A4B8A/CC20BFEF…DABF，Host仍E502A089…3159AB/BAA95903…C46254，已逐字SHA复核，源码没有写入。仅追加本报告（before SHA-256 0716F9D461C57834DCE275D3904FC1366206FF8E41E71445AFE4D32EA4E43DE8），保原UTF-8无BOM/CRLF。一次只读PE打印受PowerShell runner的GBK stdout/BOM字符编码阻断，改为JSON ASCII输出后完整解析成功；该命令错误是诊断脚本打印问题，不是产品运行/Build失败。

本agent没有Build/EXE/GUI/ComputerUse/Git/递归或系统配置写入。只读binary解析、文件头/尺寸与现有日志证据不替代动态保存验证。B3/其它writer仍按Root串行槽推进，当前只提供IO_PROPOSAL_READY_SOURCE_UNCHANGED。

## 2026-10-01 精确I/O诊断获准后的写入前冻结

Root明确授权Fixture+AutoSave.cppm/.cpp+PresentationAutoSave.cppm/.cpp及本报告，Host/CoreHeader/UInk底层/正常算法/B3/source/Main仍冻结。先记录before身份及最小实施顺序，本段不是已运行修复：

| 可写源 | before SHA-256 |
| --- | --- |
| Draw3.FailedCleanupFixture.cpp | 5DAC3852AB7CDD035175D5F6F3389087A64A7C4C254FFAD6EF58FC53C14878E6 |
| Draw3.AutoSave.cppm | A2AF7DD2ABDC87C6EF3C21840D82CE3E72C8409DB7A8FB6EEFA96D9DC99A4B8A |
| Draw3.AutoSave.cpp | CC20BFEF3BA127117E8C95037ED412D9468CB0C08EE7699C77E12AA52486DABF |
| Draw3.PresentationAutoSave.cppm | C3D658003EFB14E7F42191F5D69FFE44CC325791D48C454FE223AF26B72EAB06 |
| Draw3.PresentationAutoSave.cpp | 54DC60115592813DED66AE8F09C5E0370405E6CB83D0BE62081DB98C3306ADA8 |

- 两existing TestFaultInjection末尾追加默认false bool，保旧aggregate前置字段/默认false兼容。Fixture只5个auth producer在已有相应函数内设置，Desktop A也提供真实初次new-index marker；第二请求同一faults值带既有delay/3event，success末尾依旧reset。PPT fresh reader的fault副本仍false。
- Desktop日志固定numeric stage：1=primary/backup选择失败（actual read enum），2=temporary写失败（实际API error），3=temporary验证失败（actual read enum），4=temporary真实通过，5=publication失败（立即GetLastError，先于日志/Delete），6=publication实际成功，7=最终index验证失败。IndexReadState的Missing0/Valid1/Invalid2是当前enum，不冒Win32 error。只有fault true打印；原Acquire/validate/append/commit/reader次数和顺序保持。
- private临时writer仅可选DWORD*输出；CreateFile/WriteFile/Flush真实false立即捕错误，随后原Close/Delete；WriteFile成功却零byte时不伪造Win32 LastError（保持0）。null默认无额外错误读取，不改原返回结果或重试。
- PPT固定numeric stage：1=Acquire已经失败，2=SelectStorageTrack已判IoError，3=EnsureFiles已经失败，4=原SaveUInkFile非Committed的actual status/systemError/pathWCHARlen与首个非零systemError diagnostic。前三处没有可靠已存Win32错误则明确error_available=0，不拿caller stale GetLastError假称当場。stage4沿生产原saved result，Win32 error非0才称available；没有改Acquire/track/files算法或UInk底层。
- 保留PPT271路径为未确认hypothesis、Desktopunsupported flag为待actual error，不改manifest/registry/最低Win7/索引flag或schema/数量/timeout。仅真实之后错误来源才能批准另一次最小修补。

## 2026-10-01 五源有限I/O诊断实施（PATCH_READY_IO_DIAGNOSTIC）

按上一段Root精确WRITE_ALLOWED完成五源最小改动，当前只提供新的定位候选，未修索引/持久化算法、夹具目录长度、manifest或系统环境，原三case真实FAIL完整保留。

### 实际接口、错误来源与默认行为

- existing `DesktopAutoSaveTestFaultInjection`末尾新增bool `logIndexCommitDiagnostics=false`，existing Presentation对应新增`logSaveIoDiagnostics=false`；保持旧aggregate既有前置字段位置，旧测试不足字段自动false。没有新增共享POD/argc/环境/普通配置/外部路径或底层UInk接口。
- private `WriteNewTextFileDurable(path,text,DWORD* failureError=nullptr)`只供CommitIndex可选诊断。在CreateFile/WriteFile/Flush真实API false当场取GetLastError，随后依旧原Close及仅删除本事务临时文件；WriteFile成功但written0仍原返回false，但error明确0、不虚构Win32 failure。普通pointer null不新增错误API调用，Create/Write/Flush次数与返回条件相同。
- CommitIndex只取得原ProcessRequest同一fault snapshot；stage1实际primary/backup无可用index才打印两个真实IndexReadState enum，stage2临时写失败用上条已捕获error，stage3临时验证失败保actual read enum，stage4仅真实通过temporary ReadIndex/ContainsRequest后打印；stage5只原ReplaceFile/Move BOOL=false，当场error先于日志/Delete；stage6只BOOL成功后打印，不代替最终read；stage7仅最后ReadIndex/ContainsRequest失败。ReadIndex仍原四个调用点，ReplaceFile/Move仍各一个，append/schema/durable/备份/strict validation顺序保持。
- PPT private SaveVersionedUInk从原同一fault snapshot接bool。两个生产first/update调用点均窄传该值，原SaveUInkFile、SourceChanged重试8次/返回status/revision/relativePath不变。只有flag真且actual nonCommitted时输出stage4：原saved.status、saved.systemError、error_available、实际version path.size() WCHAR数、原diagnostics中首个非零error的code/error。没有以caller LastError、expected或path猜测回填。
- SavePresentation现已判IoError的Acquire/SelectStorageTrack/EnsureFiles边界只加stage1/2/3，明确error_available=0；没有在它们未保留的Win32错误上冒认真实lastError，也没有修改这些函数或增加文件/命名mutex读取。原worker通用failed/status日志保持原样。
- 两flag的唯一true setter仍新增fixture内部：RunDesktopStorage（只DesktopHold/Release/NaturalClose）在Start前设置，A、B复用同一fault对象；RunPptStorage（只PptHold/Release）设置本对象，第一save与第二gate都保留。鉴权第一动作和两个CRT fixed CREATE_NEW文件在任何State/Host/fault之前保持原版；readers/其它case不启用，旧延时和manual gate强引用/held-to-join不变。默认false只空条件分支，无新增正常日志/clock/wait/worker/重试。

### 实际源diff和自查

已以每个before实际字节读入、所有after先在内存完成再写这五源，unified diff范围仅上述fault字段/私有writer错误输出/有限日志及fixture两个setter。CPP模块保原UTF-8 BOM+CRLF，Fixture无BOM+CRLF，零loneLF/尾随空白；五源词法分隔符平衡检查通过。已核only5case经first auth、bool默认false、两个PPT真实调用点、native error在cleanup前、Desktop四个ReadIndex及每个API次数/顺序、无新clock/wait；Host两源仍原E502/BAA完整hash。

一次静态检查因依赖中文注释切片字符串不匹配而脚本ValueError，改为既有枚举符号切片后自查0；无源码随机调整。没有Git/build/EXE/GUI/ComputerUse/递归。自查不是编译、独立safety或真实保存/恢复通过。

| 本次冻结源 | after SHA-256 | Bytes / BOM / CRLF |
| --- | --- | --- |
| Draw3.FailedCleanupFixture.cpp | 9DBB4C60B1AC58EA8813F180B03AE06C9BD6251D01EC2CB7444F4B6437DE568F | 81374 / 无BOM / 1354 |
| Draw3.AutoSave.cppm | A5833346838F2E47732116612A2CA3499B7A7FF1634B3A46BA3D826F382D5962 | 5454 / BOM / 169 |
| Draw3.AutoSave.cpp | 02C246B674C482E3C3C47346848F8FFB426D94EE1167B473A6DEA18809329432 | 49783 / BOM / 1389 |
| Draw3.PresentationAutoSave.cppm | B54E83A5DA1993FE6487D275E84736E8A5D539DB35754F816C0E0E705DCB937A | 5772 / BOM / 183 |
| Draw3.PresentationAutoSave.cpp | 6D0FD0A7659379D77E7A3686DABD7FFE08FF9ECA8289F6044436CCC0E3971A8F | 64189 / BOM / 1582 |

所有before hash见写入前冻结表，报告本段before为956EDD6090264C18CDCA8D2D8D51B94D58F589298C1CA8D3B468C94B2BD7E54B。Root需登记五源准确新身份，独立actualdiff+safety后等B3/所有writer停写再串行完整Debug|ARM64及适用回归；随后同3selector取真实stage/error/PPT result，而非沿用旧PE日志结论。只有原SaveUInk result error来源确认206等才能升级PPT271假设；Desktop真实错误未定位，仍不改REPLACEFILE_WRITE_THROUGH/平台或schema换绿。五源码和本报告停止写入，PATCH_READY_IO_DIAGNOSTIC。

## 2026-10-01 同PE sandbox外对照与Win32路径边界提案（只读）

Root报告I/O候选全Debug0/155warnings/0error、四core/offscreen套件0、独立A7FBDD…31AC CLEAR；B3另外H1缺口仍保留。已只读sandbox及unsandboxed各3selector的父raw/status，实际PPT新run私有日志和目录。当前源码全冻，只追加本报告，不生成或运行repro、不操作原Root/private/User数据。

### 实际环境对照（原失败全部保留）

| 同PE selector | sandbox结果 | 同PE outside结果及真实fresh reader |
| --- | --- | --- |
| C09-desktop-release | parent28024 / child27788，父65/child90；stage4临时已valid→stage5原error5 | parent29188 / child16552，父0/producer0，actual2/2/0/0、Stop/Window真返回、Close原arm2；新reader34472 exact0/readerSucceeded1 |
| C11-natural-close | parent3800 / child30596，父65/child90；同临时valid→publish error5 | parent29716 / child34076，父0/producer0，actual2/2/0/0、Stop/Window真返回；新reader23996 exact0/readerSucceeded1 |
| C10-ppt-release | parent31564 / child21436，父65/child90；UInk stage4/status5/error3 | parent35652 / child19284，父65/child90；`Inkeys Shutdown Test UEF 测试 35652 58460015 0`仍stage4/status5/system_error3/path_chars230/diagnostic_code26/error3 |

全部六组各四auth negative通过。sandbox Desktop ERROR_ACCESS_DENIED与同PE/同参数原逻辑outside成功是本轮runner环境实证；不改ReplaceFileflag/schema/share/readers/产品去绕环境。两个outside自然0+严格新reader有效，不抹旧FAIL，也不自动升级hold三轮、Win7/Release/可见恢复或所有保存能力。

PPT error3已有原SaveUInkFile结果与WriteFailed/temp diagnostic源，不是猜caller LastError；presentation/files真实父目录存在。原error206要求是候选示例，本段明确error3也可由普通Win32过长路径产生，不能以不是206排除假设，亦不能仅靠error3单数值确认长度实因。

### Root可执行的最小纯Win32 boundary设计（尚未运行）

1. 在本repo `TestResults/release-hardening/`仅CreateNew一个短nonce子根，拒已存在/reparse；核并持有drive到root组件的nonreparse/deny-delete目录lease。再CreateNew一个短固定parent目录，并核它真实存在/非reparse。全部生成数据保留，不触及之前run或用户文件，不递归删除。
2. 在同一已存在父目录构造四个不同固定case叶名，按absolute .NET string.Length（WCHAR）补ASCII padding，使ordinary-short=229、ordinary-long=271、extended-long对应基础271（API带前缀275），另有missing-parent-short。叶名自身均<255，无ADS/dotdot/尾空格/特殊device。extended和ordinary用不同叶名，避免普通长path意外成功后已有文件影响CREATE_NEW对照。
3. Add-Type仅声明Win32 P/Invoke，使用与production uink_file.cpp::WriteNewFile完全相同的CreateFileW参数：GENERIC_READ|GENERIC_WRITE、share0、CREATE_NEW、FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN；记录实际BOOL/IntPtr与`Marshal.GetLastWin32Error`（SetLastError=true，必须在任何Close/其他调用前取原值）。成功实际handle核disk/非directory/nonreparse/identity，再Close。此probe无需写数据/GUI/Inkeys或复刻UInk算法，只隔离创建边界。
4. 首short229必须真实成功；ordinary271记录真实失败（3或206等原值，不预填expected原因）；另`\\?\`长271必须成功且实际base路径/length与对应文件一致；missing-parent-short必须ERROR_PATH_NOT_FOUND3，证明同error数字不是单独原因。若short或extended失败，环境/父目录前提不足，不能归产品长路径。若ordinary-long成功，需记录host path/manifest，不可强行记RED。
5. **Host差异必须控制。** 已只读PE resource：当前Codex runtime `C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe`（SHA DA8DA15A54291D952249AD9C708BBF7B59125B9C91D43CE52AE729E2274C83E8）RT_MANIFEST无longPathAware；inbox `C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe`（SHA 82BD6637ED8B7F8CCD445D601E5D672BC4CDB839F61F10734ABC88F60EDD197E）manifest明确true，不能用后者ordinary271成功否定Inkeys无opt-in的边界。Root应使用已核no-opt-in当前runtime host，核实际process image及manifest；registry保持当前1，不改系统/manifest。
6. 在Root授权outside串行槽执行、保存host/hash/命令/参数/所有长度/实际error/父存在/目录lease的raw。这是Win32边界证据，不能作为C10 whole-presentation Save/strict receipt/自然Close PASS；后续仍需同产品Save实码复验。

默认260及extended路径兼容含义沿前段微软[最大路径文档](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)。这里是对已观察271 temp的可验证因果设计，尚没有probe结果，不改变最低Windows7要求。

### 确认后最小产品修补选择

**建议优先A（适用当前合法230目标的内部路径膨胀）：** 只底层UInk private UniqueSiblingPath从`完整target + .GUID + suffix`改为同父目录GUID-only sibling leaf，保原GUID唯一/CREATE_NEW/同卷atomic/原命名mutex、logical final path、身份/provenance、SHA/self-validation/backup/recovery/status算法。当前target230不变，内部tmp/bak/new-recovery不再重复78字符目标叶名。这是产品可达合法路径在内部事务名越界的修补，后续必须在新root调用实际SaveUInkFile(CreateNew→SaveExisting→strict Read/sourceRevision/旧点保护)作有意义RED→GREEN并重跑原未缩目录的C10，不只改fixture目录称产品已修。它不声称任意>MAX_PATH最终路径已经支持。

CrashHandler当前已有等长pending后缀以避免Win7深目录被内部后缀额外越MAX的局部先例；本建议仍需Root冻结底层UInk唯一writer和真实无GUI测试范围，不擅改第三方/公共测试/原数据。

**B（若任务明确承诺任意extended长目标）：** 另冻结Win7安全IoPath合同，在所有相关Win32 I/O边界转换已规范绝对drive/UNC路径为`\\?\`/`\\?\UNC\`，已extended幂等，relative先GetFullPathName，canonical logical路径/sourceIdentity/mutex哈希不变。Create/Open/attrs/Read/Move/Replace/Delete/backup/recovery必须完整一致，设备namespace/ADS/尾点空格/UNC权限等边界需独立审核；不能只改NormalizePath返回前缀，造成同文件多mutex identity或来源比较变化。它的范围明显超过当前内部temp名问题，真Win7/UNC运行不得由本机推导。

当前只提交选择/设计，没有写底层UInk或缩fixture路径。Root决定并授权后实施；普通Desktop runner问题不混入产品修补。五新source hash9DBB/A583/02C2/B54E/6D0逐字保持，底层uink_file.cpp仍0E516BFB09D5CCCA8C11A730CD52763A8DAA38F51C0F6B6E0F946936AB3E6348。本agent没有Git/build/EXE/GUI/ComputerUse/配置修改，report-before D8E75404D508E0E922D0C5F8C18C88B2C43AC2EF1CFDE52C495873254FE2F5EF。WIN32_BOUNDARY_PROPOSAL_READY_SOURCE_UNCHANGED。
