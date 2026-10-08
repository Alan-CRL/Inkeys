# UI3 F：权限 helper 与有限 parent runner 接口提案

Active task: `.trellis/tasks/09-27-integration-and-release-check`。日期：2026-10-01。Writer：`ui3_fixture_auth_impl`。

状态：**PATCH_READY，停止写入，待 Root/独立实码 review 与后续 Bar F 链接集成**。下文接口提案保留最初 READ_ONLY_PREPARE 历史；Root 已明确 WRITE_ALLOWED，当前完成唯一两新 helper 源。未改工程/Main/ShutdownSupervisor/Bar/Fixture/共享规范/账本，无 build、产品 tests、EXE、GUI、Computer Use、Git 操作。

## 边界与已确认缺口

R2 合同439行（SHA-256 `B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097`）和独立 F GREEN_DESIGN 已读，特别是完整 §6–10。现有 B2 数值接口/observer/latch 已在 Bar.PresentationProbe.h，F 的新 purpose/packet/early dispatcher/目录 validator/parent 尚不存在。实际旧 `IsTestDirectory` 只收旧 shutdown/UEF 树，不能用于新多层 UI3 树。

本 writer 后续唯一源码落点是两个新增普通文件：

- `Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.h`：128B packet、72B frozen immutable tuple、capability、early API、普通链接的后续 Bar runner/编译表 descriptor 声明。
- `Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.cpp`：exact-purpose 解析、专用树证明与租约、capability registry、copy/current-image parent、固定三 HANDLE_LIST、有限等待/结果判定、有限 auth suite。

Root 唯一登记工程/filters、Main 最早调用、ShutdownSupervisor 复用 wrappers。B3 writer 仅 Bar，C3-B writer 仅其 Fixture；本 writer 不修改他们区域。不复制 Supervisor 框架，不实现 GUI/bootstrap/input/business setter，不新增 authOnly purpose。

## 精确普通 header 接口提案

Namespace 使用已有 `Inkeys::UI::Bar`，仅普通头文件声明，不 import/export Bar module；`Ui3FiniteScene` 以 `enum class Ui3FiniteScene : std::uint32_t;` 前置声明，定义仍在现 Bar.PresentationProbe.h。

```cpp
struct alignas(8) Ui3FixturePacketV1 {
    std::uint32_t magic, version, bytes, purpose;
    std::uint32_t scene, capture, capacity, round;
    std::uint32_t sourceVersion, trajectoryCount, authorized, stage;
    std::uint32_t result, received, enqueued, consumed;
    std::uint64_t nonceLo, nonceHi, sourceHash, expectedSteps;
    std::int64_t startedTicks, finishedTicks;
    std::uint64_t completedSteps, unverifiedSteps;
};

struct alignas(8) Ui3FixtureFrozenInputV1 {
    std::uint32_t magic, version, bytes, purpose;
    std::uint32_t scene, capture, capacity, round;
    std::uint32_t sourceVersion, trajectoryCount;
    std::uint64_t nonceLo, nonceHi, sourceHash, expectedSteps;
};

// descriptor 只表明同一已编译表版本，不作为来源认证。
struct Ui3FixtureSourceDescriptorV1 {
    std::uint32_t sourceVersion, trajectoryCount;
    std::uint64_t sourceHash, expectedSteps;
};

class Ui3FixtureAuthorization final {
public:
    ~Ui3FixtureAuthorization();
    Ui3FixtureAuthorization(const Ui3FixtureAuthorization&) = delete;
    Ui3FixtureAuthorization& operator=(const Ui3FixtureAuthorization&) = delete;
    Ui3FixtureAuthorization(Ui3FixtureAuthorization&&) = delete;
    Ui3FixtureAuthorization& operator=(Ui3FixtureAuthorization&&) = delete;
    const Ui3FixtureFrozenInputV1& Input() const noexcept;
    const std::wstring& Repository() const noexcept;
    const std::wstring& PrivateRoot() const noexcept; // rN/sScene leaf
    const std::wstring& BinaryDirectory() const noexcept; // leaf/bin，无尾斜杠
    Ui3FixturePacketV1& Packet() const noexcept; // child 数字输出；输入不作运行时授权源
private:
    friend struct Ui3FixtureAuthorizer; // helper 内唯一构造者
    // 构造函数与 leases/packet/input/path 均 private，无公共有效默认对象。
};

bool TryRunUi3PresentationFixtureEarly(LPCWSTR fullCommandLine, int& exitCode) noexcept;
bool IsAuthorizedUi3Fixture(const Ui3FixtureAuthorization&) noexcept;

// 后续 Bar F 实现；普通 header 声明须先于 module declaration 放 global fragment。
Ui3FixtureSourceDescriptorV1 GetCompiledUi3FixtureSourceV1(Ui3FiniteScene) noexcept;
int RunAuthorizedPresentationFixture(const Ui3FixtureAuthorization&) noexcept;
```

capability 的有效对象地址、mapping view 地址和非零 nonce 在 child-local registry 只发布一次，所有 owner 真 join 后才清。`IsAuthorizedUi3Fixture` 先核 exact registered object pointer，再核本地 frozen nonce/packet pointer；复制类型、公共路径、mutable packet 不能构造授权。Bar 必须只消费 `Input()` 的本地冻结参数，不能事后从 shared packet 重新读取 capacity/scene/path 来决定权限。普通对象是代码内能力边界，未宣称 OS 沙箱。

128B 必须三架构共同 `static_assert`: standard_layout/trivial，`sizeof==128`、`alignof==8`、`magic==0`、`sourceVersion==32`、`authorized==40`、`stage==44`、`result==48`、`received==52`、`enqueued==56`、`consumed==60`、`nonceLo==64`、`nonceHi==72`、`sourceHash==80`、`expectedSteps==88`、`startedTicks==96`、`finishedTicks==104`、`completedSteps==112`、`unverifiedSteps==120`。Frozen tuple `sizeof==72`、`alignof==8`、`nonceLo==40`。全部 fixed width；不得在 IPC 直接放 bool/enum/HANDLE/pointer/string。

## 编译内 source 表 / hash 依赖

固定 sourceVersion1，expectedSteps216，Main433/Draw435；source serial0，warm16/measure200，step1000ms、Up+20ms，Draw 两 Setup rows 与保留 Cancel 遵守 R2。64B `Ui3FixtureInputV1` 和表的真正构造归后续 Bar F；本 helper 不构造动作或投递输入。

选择 **Bar 提供上面的 descriptor 普通函数**，parent 和 child 从同一已编译 scene 表得到相同 hash/count，helper 再逐项核固定版本/count/steps。Hash 定义为固定 FNV-1a-64，按 field 显式 little-endian 编码完整表顺序及 sourceVersion/scene/count/steps/steady-clock period；不直接 hash C++ padding，不把 count 常数伪装 table hash。64B 表 offset32/40/48/56 和 size64/align8 由后续 Bar static_assert；表必须 init 后 immutable，并由其 owner 校验 reserved0/1、baseSerial0、phase/flags/action 与固定时序。

在 Bar F 表/descriptor/runner 尚未存在时，新 cpp 有两个明确未定义链接依赖；不能登记并期望完整 Solution 已能链接。Root 可先保存 helper 源码，再在 Bar F 对应定义到位后登记/构建。**不写成功 stub，也不为 auth 测试另造正授权逃逸分支。正授权测试 pending。** 如 Root 希望避免新增 descriptor 函数，必须先由 Bar F 给出真实 frozen table 的精确 hash 常量及来源；不能由本 helper 猜 hash。

capacity on256..65536、off0，最终预算必须核实际 `sizeof(RawCallbackSample)+sizeof(RawBatchSample)` 及 Bar finite/source/SVG 总 fixed allocation <=64MiB。建议 helper cpp import 现 RenderPipeline 只算真实 raw bytes，固定其他预算4MiB；Bar bootstrap 在任何 owner 启动前核自己的实际 fixed bytes 不超过该保留额，再预分配。超预算 fail closed；不改变普通 raw Configure/产品启动行为。此预算取舍需 Root 在 WRITE_ALLOWED 中冻结。

## CLI 与 early 返回

- Public benchmark: argv1 必须精确 `--ui3-presentation-benchmark`，argc10；scene/round/capture/capacity 四唯一 pair（可任意排列，无 unknown/重复），仅 main-fold/draw-attribute、1..3、on/off、上述容量。Public auth suite 精确 `--ui3-fixture-auth-tests`、argc2。
- Child 唯一 `--inkeys-internal-ui3-fixture-child-v1 <parentPid> <parentHandle> <ackHandle> <mappingHandle> <privateRoot> <expectedParentImage>`，argc8、三个唯一非零 uintptr 范围 HANDLE，全部 HANDLE_FLAG_INHERIT；PID 有效非 self，GetProcessId 精确相同。
- magic0x1430FB21/version1/bytes128/purpose0x55493301，固定 scene/capture/capacity/round/sourceVersion/count/hash/steps/nonzero nonce，全部 mutable 初值0。精确 MapView 长度128；不借用旧48/96/128/1024包。
- 先身份/树/packet冻结再构造 capability、发布 authorized1、SetEvent ack。ack失败直接非0，不调用 runner。授权后 dispatcher 直接调用普通 Bar runner，返回 true/exitCode，绝不返回普通 wWinMain 初始化。
- 在任意 argv 出现内部 word 而位置/数量/结构不对也 return true/non0；CommandLineToArgvW 失败但 raw 含此内部 word 同样 fail closed。public word 出现但坏形状也 early非0。Root 最早接线先新 dispatcher 再旧 Supervisor，避免内部词被其它旧模式吞掉。

## 专用目录证明与生命周期

validator 为 cpp 内方法（auth suite 直接调用同一 production 方法），不扩大普通 public API。绝对 drive path 限定 `X:\...`，拒 UNC/extended alias、forward separator/empty component/dot/traversal/ADS/trailing-dot-space。完整尾组件必须严格为 repo/TestResults/release-hardening/ui3-finite-32lowerhex/r1..3/s1..2；bin 其下。尾组件逐项拆，不能 rfind marker + prefix 追认；round/scene 与 frozen packet一致。

repo InkeysRepo.sln regular/no-reparse，.trellis directory/no-reparse；源 parent 实际 image 与 expected image 同 file ID 且都在推导出的同 repo separator boundary。child current image 必須同 leaf/bin/Inkeys.exe 的真正 file ID，且 image directory 恰是该 bin。目录/file identity 保存 volume serial + file index，不把路径相同或另一标记仓库当同一次 root。

从 drive root 到 repo、源 EXE、leaf/bin 每必要 ancestor 均 `CreateFileW(FILE_READ_ATTRIBUTES, FILE_SHARE_READ|FILE_SHARE_WRITE, OPEN_EXISTING, BACKUP_SEMANTICS|OPEN_REPARSE_POINT)`，GetFileInformationByHandle 核 regular/directory 及 no reparse；**不 share DELETE**，HANDLE 留到 child 返回真 join/进程死亡。Config/opt/log 的 parent 新建目录、bin/Inkeys/Config，以及仓库标记也同样核/保活。任何打不开或不符 fail closed。

parent 从已校验 cwd 同 repo 的 current EXE 复制，CoCreateGuid 生成32lowerhex master，一次 CreateDirectory create-new；master存在立即失败。只有 TestResults/release-hardening 两共享祖先可在检验后复用，新 round/scene/bin/Inkeys/Config/opt/log 都必须本 parent 新建；没有任意 output/root/path CLI。私有 cfg 合法默认内容由后续 Bar bootstrap 用生产 defaults/Write 创建，本 helper 不设置业务或图开关。实际 Config::GetFilePath/opt/output 写前须核 capability 固定 root 与 ordinary no-reparse 文件；Bar F 具体写点由下一次实码 review闭合。

parent RAII 持 source/created-path/mapping/ack/child process HANDLE；child RAII 持 inherited HANDLE/view/directory proof/capability；registry/paths/frozen tuple/view 到 source/Interact/Render/Window **真 join**后才撤。runner 若停止失败，不允许返回并析构仍借用的 capability，保留到进程死亡，由正常监督或 own parent 上限兜底。只保留 private 副本/报告，不递归清理未知目录；确需删本次自建单文件，先关闭对应 file lease并再次核 identity。

## parent 与 Root 复用接口

Root 已指定在 `Inkeys::Shutdown::DiagnosticsProcess` 增以下窄 wrappers（原私有实现/旧 validator 原义）：

```cpp
std::wstring QuoteArgument(const std::wstring&);
bool CurrentImage(std::wstring&);
bool ProcessImage(HANDLE, std::wstring&);
bool SameImageFile(const std::wstring&, const std::wstring&) noexcept;
bool StartInherited(const std::wstring& image, const std::wstring& quotedArguments,
    STARTUPINFOEXW& startup, PROCESS_INFORMATION&) noexcept;
```

最后只调用原 StartSameExecutable inherit=true、EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW，核 cb/attributeList。新 helper 建 exact3 HANDLE_LIST：parent 只 SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION（不授予 terminate parent），childAck 只 EVENT_MODIFY_STATE，mapping FILE_MAP_WRITE；其它 lease/自身普通 handle不继承。ack 与 child HANDLE 并等；握手有限，整 child 上限300s（包含原15s退出）。成功必须 exact own hProcess自然 signaled、没有 parent cleanup kill、合法 child final stage/result/count；stage release/acquire或死后一次读取纯数字。父亲不发 system input，不按 PID/name 枚举/杀别的进程。

packet 的 mutable 32bit发布字段使用 Windows Interlocked；64bit时间/counters由 child 封口 owner 在真 join 后写、最后发布 Sealed，parent只在相应 acquire 且 child自然死亡后读。parent不并发 memcpy整个 mutable包。捕获关闭 started/finished 不当0ms性能；Bar输出明确 timing unavailable/null。

无继承负例提案：仅 auth suite 的 cpp 私有固定 case 使用直接 CreateProcessW、固定 copiedImage+同参数、bInheritHandles=FALSE、CREATE_NO_WINDOW，不外露 flag/路径 setter；因此能够启动真正 child并触发继承校验拒绝，不能只把 CreateProcess 参数拒绝算 child auth PASS。这个例外需要 Root 在 WRITE_ALLOWED 明确准许。

## 独立 safety / 验证门

拟套件保留同 production validator：坏magic/version/bytes/purpose/scene/capture/capacity/round/hash/count/steps/zeroNonce/nonzero初输出；错 exact parent PID/file、重复/无继承 HANDLE、坏 argc；层级/marker/wrongrepo/rN/sScene/同名不同EXE/源外根与每层reparse。reparse造景只可用当轮自建路径，在全部租约关闭后建立/测试；不能替换已有 user目录。实际 reparse创建平台前提若不可得必须写未验证，不用缺目录或单纯命令退出替代。

positive/new-tree auth 需要真实 Bar descriptor+runner才可执行；当前 pending。真正 scene结果、on/off等价/输出/源序列/anchor/业务allowlist/图/输入/ready/真join全部归 Bar F与之后 Root验收，不由本 helper自报 PASS。新 helper负例也须 Root+independent checker 的实际diff safety CLEAR 后运行。Root独占完整 InkeysRepo.sln Debug|ARM64与strict Headless、Release/其它arch以及EXE/TestResults报告。

## 当前读取与后续工作

已读取 full hook保存文件、AGENTS、workflow、skill before-dev、G prd/design/implement/implement.jsonl；实际native build/errors/quality/cpp/index、guides/reuse/cross-layer、UI diagnostics及相关render/config/input合同，完整R2+design-review以及SS实际 private primitives/三继承链/cleanup drive leases、Main最早顺序、现 Bar Probe。G其它历史not-inlined上下文仍分段核对中；不以旧设计/旧GUI通过代替F。

此处为最初准备阶段记录；随后已完成 G manifest 实际20条上下文读取，Root 冻结 interface/descriptor/预算/私有无继承负例并发 WRITE_ALLOWED。当前实际状态如下。

## WRITE_ALLOWED 后实际交付

只新增两个未登记的普通 helper 源，112行 header、约845行 cpp；没有复制 Supervisor 框架，参数转义/镜像查询/file ID/常规继承启动共用 Root 的五个 wrappers。其余私有代码是此 purpose 必需的 parser、逐组件 lease、有限 parent 和 auth/filesystem cases。32/64位布局全部固定 width+align8/offset断言；它们尚未经任何架构编译，不能记类型/链接通过。

Header 为上述普通 namespace 接口，新增可读 enum `Ui3FixtureStage` 数字固定 None0/Authorized1/Initializing2/Ready3/Running4/Closing5/Joined6/Sealed7；`Ui3FixtureResult` Pending0/Passed1/Failed2。packet 中仍是 uint32，不将 enum 本身传 IPC。Class constructor只 friend authorizer，可读的72B input/path、packet ref和private lease保持同一寿命。Nonce/raw pointer不从可变packet重新决定权限。

Bar 未来一切返回（成功或失败）必须在 owners 真 join 后发布 Sealed。helper 在非 Sealed 返回时直接自终止90、保留 cap/view/lease所在栈；**stage 只是失败保护门，不能单独证明 join，Root 必须审未来 Bar 的真实join路径。** 返回正常的最后一步才清 registry。默认普通进程 registry null，early dispatcher只有参数解析/释放；不创建目录/线程/设备/窗口、不写配置/注册表/用户数据。recognized parent 才 create-new 私有树/复制当前EXE、创建3继承对象和自己的child；recognized child只有全部校验/冻结/ack后才调普通 Bar runner。

parent 自然精确 hProcess死后一次copy输出；父强退/退出不符/未封口为FAIL，句柄/目录lease保持到读取完成。benchmark还要求 Passed、216 completed、unverified0、实际input received>=432且received=enqueued=consumed（仍受433/435上限），meta/finite/source/SVG/summary fixed文件存在且ordinary；on另需两raw文件，off无需raw且started/finished必须0（数据导出由Bar标null）。PNG/BGRA若存在也查ordinary，实际独立equivalence结论仍由后续Bar/Root验证，文件存在本身不证像素等价。

`--ui3-fixture-auth-tests` 实码包括22个真实child负例（各新master/copied EXE；无继承case真正创建child）、13层本轮私有filesystem reparse、源/复制EXE file reparse、7个shape/boundary/fileidentity反例；使用同一生产validator，不复制授权逻辑。filesystem用本轮new-marker/ordinary空文件只测试路径规则，**不冒充真实process授权正例**。真正authorization正例还是同一 main-fold/off Bar runner，无新增authOnly信封/skip-init；其parent verdict明确只认证授权，benchmark完成另判。

reparse造景保留private树和实际copied PE；只把本case create-new的两个空文件经exact创建file ID、带DELETE的同一HANDLE删除后创建file symlink。父目录/target ordinary lease保活，既有user目录及真实源/复制PE不删。目录junction或file symlink创建前提失败输出NOT_VERIFIED且套件非0；不改系统权限、不以缺文件替代reparse PASS。整套件/所有新purpose当前未运行，仍需独立安全CLEAR。

source hash依赖仍是后续Bar真64B immutable table的 `GetCompiledUi3FixtureSourceV1`，version1/count433或435/steps216/hash!=0全部校验，没有手编常数、成功stub或输入发送。raw预算按真正两sample sizeof除法先验，fixed reserve4MiB，总<=64MiB。Bar allocator必须实际核自己的所有finite/source/SVG/table/sidecars<=4MiB；helper不会代替它虚报预算。

### 当前检查 / 未验证

- 已做两新源 UTF-8严格解码、CRLF/no BOM/no lone LF、trailing whitespace、只读词法括号平衡以及入口/唯一3handle/lease/registry撤销/输出读取的静态自审；修正构造实参move次序和末尾多余括号。该词法检查没有编译或执行产品/测试，不证明类型、链接、Win32 API、线程静止或运行安全。
- 未执行Git/diff命令，按Root禁Git范围交其检查唯一三新文件及整个actualdiff；其它writers不受本writer回退/格式化。
- build/typecheck/runtime/tests/GUI/Computer Use全部未执行；原因是两Bar依赖尚不存在、Helper未工程登记，以及Root唯一build/run槽与F实际diff独立CLEAR门。没有从当前C3/B3/headless旧结果借PASS。
- Root后续需在真实Bar两个普通定义到位后登记.cpp/header、Main最早先调用新dispatcher，再完整Solution Debug|ARM64并核三架构assert/链接；实际negative/auth、两个scene/on-off/源序列/action/anchor/ready/真正join/输出与Release三轮仍 pending。Win7/真实笔/光学/其它scene/family/HC-H2/发布门保留。

### 源冻结身份（最终值由下方记录）

Header SHA256 `20AB2AF89744BF52871239891CE97399762CA7FC46B21E63D5FFC62EFA087D13`（112行/5785B）；cpp SHA256 `552EEC5AB854D239D5354680DB13433C38D831A2DD428C5D6ED7D5C07737F5EB`（845行/44613B）。两源严格UTF8无BOM/全CRLF，最后只读词法括号与trailing whitespace检查通过；未编译/未运行。PATCH_READY 后本 writer不继续修改，后续修补须 Root 交回或明确接管。
