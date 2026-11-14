# 单实例与启动交接

## 1. Scope / Trigger
适用于普通启动、防重复启动设置、更新/回滚、正常/崩溃重启及SuperTop三段式。wWinMain测试/监督器验真早退仍位于产品准入前；隔离startupFailure不得占真实产品目录。不同安装目录可同时运行；不做跨目录机器级互斥，也不合并多实例配置/墨迹。

## 2. Signatures
- `Config.AllowMultipleInstances: IdtAtomic<bool> = false`；`Config::ReadMini(paths, bool readOnly=false)`。早期读取该键必须传true，不能触发旧Bar布局迁移写盘。缺键/非法bool/读取失败回缺省false。
- `Inkeys::Launch::Acquire(root, allow) -> Admission::{Accepted,Duplicate,Error}`；`Adopt() -> 0/1/-1`（无包/验真并接任/失败）；`Handoff::{Arguments,Publish,Wait}`；`StartReplacement(image, extraArguments={})`（扩展参数只允许空或 `-Whiteboard`）。
- `Packet`保持固定byteSize与uint64句柄布局；v1/`HandoffMode::ParentExit`等父退出，v2/`RestartHidden`仅供同镜像、同物理根目录的普通重启提前接任。布局/offset/size静态断言。不能用uintptr_t做跨架构wire字段；接收端转换前检查本机指针范围。
- VERSIONINFO `StringFileInfo/080404b0/StartupHandoffProtocol = 1`。三态 `Protocol::{Current,Legacy,Unsupported}`，未知/坏资源/查询失败不可当Legacy。
- `--inkeys-startup-handoff=Local\Inkeys.Handoff.<nonce>`只承载短寿命包名，不构成豁免。消费后从`LaunchState::commandLine`剔除。

## 3. Contracts
- Debug/Release普通产品入口一致。Restart/WarnTry/CrashTry/SuperTopComplete都不能单凭参数绕过准入。重复实例在更新替换和SuperTop提权前退出；错误区别于已运行，不杀已有实例。
- 目录volume/fileID构造Local presence键；所有allow=true实例也保留句柄，避免关开关后无法识别旧实例。只保存开关影响后续启动，不强退现有实例。
- 物理目录归属与presence使用同一fileID；installer必须是已验证安装根下的真实目录，拒绝reparse向外跳。逻辑安装根跨root→installer→root保持，不按installer名字任意上溯。
- presence为内核对象存活引用，不依赖mutex线程ownership或文件锁；RAII/进程死亡关闭最后句柄。交接连续DuplicateHandle，不释放后重抢。
- 接收方在Ready前校验固定包尺寸/版本/句柄范围、childPID、精确父PID/镜像目录、父仍存活且SYNCHRONIZE可用、目录ID与lease、Mutant类型和完整对象名。跨installer的物理guard也在Ready前建立，失败不得回执。
- 对象名以`OpenMutex(Local+预期fileID名)`的reference实际解析结果为准；不能硬编码BaseNamedObjects，因为受限runner可能使用AppContainerNamedObjects。NtQueryObject动态解析，有限缓冲与指针范围检查，错误closed，不能仅比较名称后缀。
- Ready成功后交接父才允许退出。v1 child等精确父HANDLE signaled后才进入业务；v2 child完成全部验真并Ready后可直接进入业务，不等仍在监督旧进程的helper。main已Adopt成功则不再二次准入，避免Ready后再次否决。失败只终止本次精确child，不清理未知文件或已有实例。
- 普通Restart由监督器按精确旧父句柄与PID枚举顶层窗口，确认全部不再可见后仅拉起一次；枚举失败不提前放行。新child走v2接任，监督器继续监督旧进程至原绝对deadline。待更新状态通过`IdtAtomic<AutomaticUpdateStateEnum>`发布；已知`UpdateRestart`选择`Intent::UpdateRestart`，与CrashTry一样等精确旧父退出后才拉起，仍用v1交接。各路径都显式接力原lease，关闭多开后仍允许合法替换已有的一份实例。
- SuperTop区分两个Ready：final先恢复仅做Adopt预检并等helper；helper收到新handoffReady后才发布原RootReady/等Accepted/等原root真正退出；helper退出才放行final业务。不能在final尚未验真时让原root先退。
- update/rollback保留原路径、哈希、指令原子写、备份和回滚校验。Current目标走Ready协议；明确无标记的旧目标用暂停创建+显式复制lease+恢复兼容，不在新协议失败后自动降级。旧目标自身后续SuperTop不理解新协议，不能宣称新lease覆盖其后续所有进程；兼容桥只确认OS创建/注入/恢复，不等于旧应用完整初始化。
- legacy直接启动installer仍走原验真/兼容等待；逻辑根准入拒绝直接退出，不进入删除有效update.json的fail清理。installer物理guard在根重新绑定期间也保持。

## 4. Validation / Error Matrix
| 情况 | 必须结果 |
| --- | --- |
| false同时启动 | 原子准入只有一个赢家 |
| true多实例后改false | 旧实例不强退，新增独立实例被拒，合法重启接力一份 |
| 伪造/重复/过期handoff | 不Ready，不让原实例退场 |
| 不同架构接力 | 固定wire布局，句柄越界拒绝 |
| installer物理guard冲突 | Ready前失败，原实例保留 |
| v1父未死或仅有QUERY权限 | 不提前初始化；缺SYNCHRONIZE在Ready前拒绝 |
| 普通重启旧窗口尚可见/枚举失败 | 不提前拉起；隐藏后v2可先于旧父及helper退出进入业务 |
| 已知更新重启/崩溃重启 | 等旧父真正退出后拉起；未知待更新由新进程沿已有更新等待与超时分支处理 |
| 同目录别名 | fileID归属一致，不能仅路径字符串比较 |
| 未知协议标记 | 明确不兼容，不降级普通启动 |
| helper超时/取消/创建失败 | 原实例继续/降级，不能迟到启动第二份 |

## 5. Good / Base / Bad Cases
Good：parent→relay→child一直持同一lease；v1等精确父退出，普通重启v2在旧界面隐藏后接任，旧清理仍受原截止监督；允许模式也持presence。
Base：普通缺键配置false、单份启动成功；不同物理目录独立。
Bad：SuperTopComplete永久豁免；等固定2秒当精确退出；先Ready再检查物理guard；旧handoff参数转发到下一跳；将旧目标无Ready当作失败强杀。

## 6. Tests Required
- `Inkeys.exe --single-instance-tests`：私有copy真实两跳、并发barrier、不同目录、allow/deny、坏包、最后引用释放、私有VERSIONINFO三态。
- `Inkeys.exe --shutdown-supervisor-tests --instance-lease-only`：真实监督链接力，driver另持SYNCHRONIZE-only引用，替换不被误拦。
- `Inkeys.exe --shutdown-supervisor-tests --ordinary-restart-only`：隐藏屏障前不拉起、旧父/helper仍存活时新child已Adopt、自然/强制退场只拉起一次、握手阻塞不延长外部监督截止；已知更新与崩溃均等旧父死亡。隐藏屏障为私有无窗口测试条件，不能代替GUI显隐顺序验收。
- `--config-boundary-test`：严格默认、bool往返、readOnly Mini旧布局字节不变；`--supertop-token-failure-test`：原无UAC token/握手失败合同。
- `InkeysHeadlessTests.exe --setting-general-only --no-window`：真实三语General安全条目位置和键盘开关事件。
- GUI subsystem CLI必须显式进程等待并取真实ExitCode（例如Start-Process -Wait -PassThru -WindowStyle Hidden），不能用PowerShell直接&后旧LASTEXITCODE冒充成功。
- 未实际测试的GUI/UAC/UIAccess、Win7、跨架构更新替换、旧版本后续内部重启需单列，不以同架构无窗口PASS替代。

## 7. Wrong / Correct
Wrong：先发RootReady让旧root退出，再resume final去检查准入。
Correct：final先完成准入并发新Ready，helper再确认旧root退出，最后放行业务。
Wrong：`sizeof(uintptr_t)`随发布架构变化却直接memcpy包。
Correct：固定uint64 wire及布局断言，转换前验证目标指针范围。

## 场景：受限白板启动转发（2026-11-04）

### 1. Scope / Trigger

任务 `11-04-whiteboard-desktop-shortcut` 有限扩展目录单实例：同目录实例可接收“打开白板”，不形成通用 IPC、远程命令、路径传输或多实例数据合并。

### 2. Signatures

- `Launch::CommandPipe::{IsWhiteboardArgument, HasWhiteboardArgument, Admit, Send}`；`Admission::{Accepted,Forwarded,ForwardFailed,Duplicate,Error}`。
- `CommandPipe::Server::{Start,LifetimeReady,BeginExit,Stop,Queue,TakePending,TakeReadyPending}`、`ProbeLifetime(root)`；请求固定16字节 `{uint32 magic=0x57424B49, version=1, command=1, reserved=0}`，回执1字节。
- `LaunchState::openWhiteboard` 是本次启动意图；`RequestWhiteboardFromShortcut()` 复用既有工作区入口；`RetireLaunchCommands()` 为正常及崩溃退场的非阻塞拒收入口。

### 3. Contracts

- 仅 `CommandLineToArgvW` 精确参数 `-Whiteboard` 产生意图。Adopt 通过者继续交接，不向即将退出的原实例转发。其他启动先探测管道：Accepted直接退出，失败提示退出；不存在才 Acquire，Duplicate再有限重试约3秒。允许多开也先转发，无可用实例时才启动。
- 转发位于 update.json、安装目录写权限探测和 SuperTop 之前，已有实例分支不改文件、不应用更新、不提权、不启动替代实例。
- 管道名含会话、目录 fileID presence 身份及监听进程 PID；消息模式限制整包长度，拒绝远程客户端。DACL仅当前用户和SYSTEM，Medium完整性标签；双方校验实际对端PID的用户SID、会话与逻辑安装根。
- 每个监听者使用独立 PID 后缀端点，客户端通过只读进程快照发现候选，并要求系统返回的实际服务端 PID 等于后缀，再校验用户/会话/目录。候选探测、重试与 I/O 共用一个绝对截止；只有端点全部不存在才按 Missing 准入，其他失败不允许另开一份。同名多实例在本机实测会保留首创建者 PID，不能靠放松身份校验兼容；独立端点确保首监听者退出后其余可用。
- 只在准入后建立 `Local\<PresenceName>.CommandLifetime` 事件（当前用户/SYSTEM DACL，客户端仅 SYNCHRONIZE 只读），独立于 Stop 和原 ReleaseForClose 持有到进程退场；重新绑定安装根时保留已登记根。禁止多开的白板入口在端点不存在时探测该标记，存在则有限重试/失败，不在旧进程清理期间另开一份。两个未准入冷启动没有标记，仍由原 Acquire 原子决定赢家；普通应用重开与允许多开策略保持。初次或重新绑定根的必需标记建立失败应提示退出，不能默默继续；管道监听本身失败但标记已建立时允许本地冷启动意图继续消费。
- 启动中只设置一个合并意图位，真实启动进度全部完成后主流程一次消费；已有白板保持幂等，最小化只恢复同一窗口组。Accepted只表示接纳，不承诺白板呈现成功。
- 更新/SuperTop前停止监听并冻结尚未消费的意图，仅在该合法交接携带 `-Whiteboard`；交接或更新失败丢弃本次意图。正常/崩溃重启不保留该参数。
- 正常退场、监督器退场和已确认的崩溃重启均先拒收，再进入Window关停门；不可只检查offSignal，因为崩溃意图槽可为3而offSignal仍为0。I/O用overlapped事件与停止事件取消，主关停屏障join，不能阻塞Flush或无限等客户端。

### 4. Validation & Error Matrix

| 条件 | 行为 |
| --- | --- |
| 无参数重复启动 | 原重复启动提示，原实例不变 |
| 白板参数且现有监听者 | 回执后退出，待更新文件留给原实例 |
| 原实例启动中 | 合并排队，就绪后一次处理 |
| 退出、坏包、版本/长度错误、错误目录/用户 | 拒绝，不退化为第二份实例 |
| 新进程冷启动经过更新/SuperTop | 合法交接携带一次意图；失败作废 |
| 多监听者，首份退出 | 其余继续可连接 |

### 5. Good / Base / Bad Cases

Good：白板快捷方式唤醒原目录实例且不碰待更新文件；Base：没有实例时普通启动完成后进入白板；Bad：转发前先做目录写入探测，或崩溃退出仍回Accepted却无人处理。

### 6. Tests Required

`--single-instance-tests` 在原专项通过后追加白板管道专项；`--whiteboard-command-tests` 可独立定位。私有copy真实进程覆盖多监听、首份退出、启动排队/重复合并、并发冷启动唯一赢家、监听失败保留本地意图、原 presence 已释放但进程仍清理时拒白板重开、退出/超时、错误目录/抢注服务端、坏包/超长/版本、更新文件独占哨兵、参数白名单及真实handoff携带意图。`--state-mode-production-test` 覆盖活动幂等、最小化恢复与桌面/PPT沿原工作区入口。完整Debug ARM64 Solution、shutdown lease/config专项须记录真实退出码。

不同用户、真实UIAccess/跨完整性、Win7和真实更新/回滚/SuperTop全链的运行证据须独立注明；同用户私有copy通过不能代替这些实机边界。

### 7. Wrong vs Correct

Wrong：将连接失败解释为允许绕过presence另开一份，或把已接受的请求交给退出中的窗口线程。
Correct：有限截止并失败退出；所有退场入口先拒收，Accepted仅排队固定意图，工作区服务就绪后主流程消费一次。
