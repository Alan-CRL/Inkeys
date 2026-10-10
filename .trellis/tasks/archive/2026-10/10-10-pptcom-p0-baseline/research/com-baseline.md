# Research: PptCOM P0 COM、活动证据与部署基线

- Query: 固定迁移前的 dynamic/ROT、实际调用线程、事件和轮询、COM acquisition/释放、DLL/TLB/manifest 交接、活动性分值与页呈现链路；为后续多来源仲裁提供可核对的源码事实。
- Scope: internal（只读源码、资源与历史研究；本研究不运行 Office、GUI、UAC、构建或 Git 命令）
- Date: 2026-10-10
- Task: `.trellis/tasks/10-10-pptcom-p0-baseline`

## Findings

### 1. 固定快照与核验方式

工作树为 `D:/Project/Inkeys/Repo/Inkeys-draw`，branch=`feature/pptcom`，HEAD=`a50ad143cbe14c7a0e4a4d97bc14733f33eda4e1`。研究代理通过只读 `.git`、worktree 的 `HEAD/commondir` 和 common-dir 的 branch ref 文件获取；根代理独立用 Git 确认相同值。

根代理提供 `git status --short --ignore-submodules=all` 只有两个未跟踪任务目录：父任务 `10-10-pptcom-multisource-architecture/` 与本 P0 子任务。本研究的26个文件字节长度与SHA-256见 [baseline-hashes.json](baseline-hashes.json)。生成目录 `PptCOM/bin/obj`、`PptCOM.Tests/bin/obj`、`Build/`、`Inkeys/Cache/`、`TestResults/` 排除：它们可由根代理测试构建更新，不能充当随附部署基线。

| 随附资源 | 字节数 | SHA-256 |
| --- | ---: | --- |
| `Inkeys/PptCOM.dll` | 67584 | `ab34c31f9510be311439b798a35d6980d5da609d55622c8dfad339e97733c105` |
| `Inkeys/PptCOM.tlb` | 6640 | `c652b5dd0603576033b2e0d15869be103f23ffdcaa2a1984e53f3b6a14dba606` |
| `PptCOM/PptCOM.manifest` | 471 | `e9cbf839648f02f012041508071042b092d2503052a7bdc21c892819146122cf` |

散列固定字节身份，不证明随附DLL与当前源码是同次构建，也不证明它运行于每个目标系统。根代理应在P0测试后再次对JSON列出的源文件/随附资源逐个校验，区分测试输出变化与生产部署变化。

### 2. Files found

| 文件 | 一行说明 |
| --- | --- |
| `PptCOM/PptCOM.cs` | 旧COM接口、dynamic长期根、ROT选择、事件/轮询、命令和清理 |
| `PptCOM/PresentationDescriptor.cs` | late-bound纯值读取、typed HWND标量例外、temporary ledger边界 |
| `PptCOM/SlideShowSession.cs` | generation/binding/session缓存、owner唤醒、guarded退出与取消 |
| `PptCOM/PptCOM.csproj` | .NET4 AnyCPU类库、Office PIA引用、TlbExp产物 |
| `PptCOM/PptCOM.manifest` | MSIL、CLR4、Both threadingModel、CLSID激活描述 |
| `PptCOM/App.config` | CLR4/.NETFramework4运行声明 |
| `PptCOM/Properties/AssemblyInfo.cs` | 程序集COM可见性及元数据 |
| `Inkeys/PptCOM.dll` | 主程序随附且嵌入资源的DLL字节 |
| `Inkeys/PptCOM.tlb` | native `#import`消耗的随附类型库 |
| `Inkeys/Inkeys.rc` | IDR_RT_MANIFEST1及IDR_DLL1资源来源 |
| `Inkeys/resource.h` | 激活资源ID221与DLL资源ID222 |
| `Inkeys/Inkeys.vcxproj` | native编译与资源Item、无PptCOM ProjectReference |
| `Inkeys/IdtMain.cpp` | 主线程COM/activation/DLL初始化及退出收束 |
| `Inkeys/IdtPlug-in.cpp` | native三个线程入口、COM快照、业务命令与UI/Draw3交接 |
| `Inkeys/Inkeys/Business/PptSession.h` | session状态/生命周期/页状态与native token |
| `Inkeys/Inkeys/Business/PptSession.cpp` | envelope解析/缓存会话语义 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | canvas ready、UI ack匹配及输入门禁 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` | UI页面提交与pagePresented回调 |
| `Inkeys/SuperTop/IdtToken.h` | 既有非提升token与CreateProcessWithTokenW启动辅助 |
| `Inkeys/SuperTop/IdtSuperTop.cpp` | 既有runas路径参考，不是helper生命周期实现 |
| `InkeysRepo.sln` | native三架构配置与PptCOM Release Any CPU映射 |
| `PptCOM.Tests/PptCOM.Tests.csproj` | 既有.NET4无窗口测试及PptCOM ProjectReference |
| `PptCOM.Tests/Program.cs` | fake acquisition/release顺序及descriptor测试入口 |
| `PptCOM.Tests/SlideShowSessionTests.cs` | session代际、忙碌/结束页、owner/mailbox和ABI测试 |
| `.github/workflows/build-windows.yml` | DLL/TLB独立构建、同次artifact向native交接 |
| `.github/scripts/release-artifacts.ps1` | 发布产物选择与嵌入DLL字节核验辅助 |

### 3. apartment、实际调用入口与owner边界

以下是源码事实，不把主线程配置推断为所有线程的实际apartment：

1. `Inkeys/IdtMain.cpp:2221` 在`wWinMain`调用`CoInitializeEx(NULL, COINIT_MULTITHREADED)`。`:2224`接受成功或`RPC_E_CHANGED_MODE`继续启动；`:3340`仅成功时`CoUninitialize`。因此即使主入口也应把“请求MTA”与“本次实际apartment”分开。
2. `IdtMain.cpp:2631`使用资源221创建ACTCTX，`:2644`激活，`:2649`LoadLibrary；`:3205`创建`jthread(PPTLinkageMain)`。`:2622`先按内嵌资源222验证/发布PptCOM.dll；退出`:3337`依次FreeLibrary、DeactivateActCtx、ReleaseActCtx、CoUninitialize。
3. `Inkeys/IdtPlug-in.cpp:985`从`PPTLinkageMain`创建detached`GetPptState`，`:986`创建可join的`PptInfo`。`GetPptState:357`调用`CheckPptCom`，其中`:221`CreateInstance、`:237`CheckCOM、`:261`传状态指针，`:282`安装受mutex保护的服务槽；`:373`在同条native服务线程同步调用长期运行的`PptComService`。
4. 这三条PPT线程入口当前没有本线程显式`CoInitializeEx/CoUninitialize`；文件`:1366/:1399/:1405/:1430`仅存在注释掉的旧初始化。`StatusGuard`不能当COM初始化证据。尚未观测CLR/COM对这些入口的实际调度/apartment，P0不更改它们。
5. `PptComService:1197`启动mailbox，`:1216`先处理descriptor refresh，再处理`:1217`退出请求，慢维护截止后才扫描ROT；`:1593`刷新缓存、`:1594`annotation缓存、`:1595`退出请求。**owner是现有service调用执行上下文，mailbox没有新增COM线程。**
6. `PptCOM/PptCOM.cs:213`的`EndSlideShowIfSession`及`:1727`附近的annotation exit转为mailbox请求；2000ms超时，`SlideShowSession.cs:228`将迟到请求标cancelled，`:325`执行前检查cancelled。队列上限8，`Stop:269`结束未完成请求。callback/COM重入后还有generation校验。
7. **不是每个命令都在mailbox owner执行。** `IdtPlug-in.cpp:1035/:1041/:1048`在`PPTLinkageMain`业务队列直接调用`PreviousPptSlides/NextPptSlides/ViewPptShow`，然后旧COM接口`:400`等直接进入managed方法。`PptCOM.cs:1988/:2011/:2022/:2034/:2044/:2057`仍直接访问dynamic `View.Next/Previous/Exit`、navigation和Activate，旧EndShow兼容路径也保留。`GetPptHwnd`有直接窗口/COM查询路径，不能写成“所有公开getter都只读缓存”。
8. **事件回调也保留COM访问。** `SlideShowChange:838/:840`现场读取页/总数，`SlideShowBegin:862/:879`安装Wn并读取状态；随后`:849/:850/:886/:887`维护session并唤醒owner。事件实际由何线程/apartment回调本次NOT VERIFIED。只把descriptor完整图读取与缓存发布称为owner职责。
9. `GetPresentationDescriptor:189`只在短锁clone，锁外序列化；`GetSlideShowState:208`仅返回缓存。从`PptInfo:610`读取这一纯值接口不会新增Office图遍历。

历史研究 `archive/2026-10/09-27-integration-and-release-check/research/pptcom-live-apartment-investigation.md:19`已推翻“静态未见每线程初始化即可推导服务CreateInstance失败”的早期假设，未实施三线程apartment重构。其限定的PowerPoint16隔离通过记录不是本次真实损坏COM/WPS/退出/Win7验收。用户描述的“STM”不在本研究中自行解释成STA或MTA。

**保留项**：当前线程入口、activation、dynamic/ROT、可选typed事件绑定、现有回调与owner分工、RCW边界、失败轮询。未来helper入口迁移须单独证明兼容，不能用线程安全/跨权限目标顺手把COM路线改为新线程或强类型必要准入。需要新路由协议时先复用现有owner边界，对旧直接命令改变另行验收；本P0不实施。

### 4. ROT、dynamic与当前活动分值

- 三个长期根为`dynamic`（`PptCOM.cs:92`）。`GetAnyActivePowerPoint:959`取得ROT，`:969`枚举moniker；`:988`识别文稿文件及应用moniker，`:997`获取对象；文稿对象通过`:1011 InvokeMember("Application")`获取应用。`:758`按ProgID获得application monikers，`:800`保留fallback monikers；发现路径不是先强转PIA再认定应用有效。
- `AreComObjectsEqual:573`获取IUnknown比较，`:580`平衡Release raw interface pointers。ROT对应用去重并保活（`:1024/:1039`），`:1135`把一个候选的所有权交给`bestApp`。**返回一个application winner，并仅检查候选应用的ActivePresentation.SlideShowWindow；没有向上层导出所有application/所有show的候选集合。** 极端多show能力必须在后续P4扩展枚举，不能靠抽象层推测缺失候选。
- 当前本地分值：有ActivePresentation=1（`:1067`）；有SlideShowWindow=2（`:1080`）；窗口Active为int -1或bool true=3（`:1089`），否则`IsSlideShowWindowActive`得到前台关联也升3（`:1106`）。初始0，不是来源优先级。
- 关联规则`PptCOM.cs:687`读取全局GetForegroundWindow；`:708`只比较前台PID==show PID；`:719`允许前台进程名wps*和窗口归属进程wpp*的关联。**同PID不能分辨同进程两个show；WPS名称关联更弱；Provider.Active也未和真实前台HWND拆成独立等级。** 现有兼容信息有价值，应输出reason/observed/Unknown等纯值事实，让未来native抽象层统一决定。
- winner更新只在更高分（`:1128`）发生；服务已绑定时只有`bestPriority > targetPriority`才考虑重绑（`:1233`），所以同档保留已有绑定。`:1239`还保留同文稿判断，优先比show HWND（`:669`），不可得时比FullName（`:674`）。这个旧fallback不是未来“同文稿就是同show”身份规则。
- 现有逻辑没有独立的观察窗口、反向切换冷却或新鲜证据revision。重绑现有对象后`:1253 Sleep(1000)`是旧流程等待，不是可推广到PageChanged的防抖方案。
- typed事件订阅发生`:1311/:1315`；cast/订阅失败时`:1335/:1343`设`forcePolling=true`，应用仍可保持dynamic绑定。不得把typed cast成功变成发现必要条件。
- HWND有两个不同现状：descriptor accessor `PresentationDescriptor.cs:82/:108`只在HWND标量上借用PIA SlideShowWindow，非PIA走late-bound；旧`PptCOM.cs:1784/:1791`的GetPptHwndFromSlideShowWindow则是typed helper、异常返回0。不应把后者描述成拥有late-bound fallback，也不应为统一措辞删除前者必要例外。

未来建议（尚未实现）：仲裁归native Business抽象层，来源提供事实；先选真实活动show，再选同show最合适来源。Exact show HWND对应全局新鲜前台证据为强证据，PID/WPS/Provider.Active为弱证据。同档保留，双源同show不加分投票。单次无前台、焦点不明、Provider暂忙都不是真实End。

**现有焦点副作用必须纳入迁移**：`IdtPlug-in.cpp:742`在新session建立时主动排队Focus，`:1030`执行业务Focus；`FocusPptSession:464`校验当前session、模态/设置窗口、目标窗口PID、前台属于本程序或当前Office后才`:482 SetForegroundWindow`。managed还保留`:2057 Activate`。因此不能声称“目前完全没有自动focus”，也不能声称已验证发生循环。后续被动多源仲裁不得新增/重复自动Activate；需要审查原新session焦点交接与新逻辑session保留的关系，避免来源切换伪造新session从而触发焦点反馈。

### 5. 释放与acquisition ledger

- `FullCleanup:383`先`InvalidatePresentationDescriptor`。后者`:257`递增bindingRevision，`:258`失效session/generation，`:259`发布生命周期缓存，`:262`发布Unavailable并唤醒owner。随后`:384`清annotation、`:389`UnbindEvents、`:395`依window→presentation→application FinalRelease（final=true），或`:401`同顺序普通Release；`:406`清null，`:413`页槽清-1，`:419`GC.Collect→WaitForPendingFinalizers→Collect。WPS关键释放注释在原位置。服务finally`:1615`先mailbox Stop，再`:1616 FullCleanup(true)`。
- `SafeRelease:322`只Marshal.ReleaseComObject；`SafeFinalRelease:335`只在原最终root所有权边界用FinalReleaseComObject。reader `MarshalLateBoundComAccessor.Release:134`每个temporary acquisition恰好一次ReleaseComObject，不FinalRelease，不释放借用root。
- descriptor root application/presentation/showWindow只借用；`PresentationDescriptor.cs:304`获取View、`:321`获取current Slide，SlideIndex/SlideID同一次Slide；`:343/:344`子Slide先于View释放。`:350`获取Slides，`:371`显式for/Item，每个item`:386`finally释放，最后`:422`释放Slides。失败/返回也走finally，不输出半份拓扑。
- ROT每项finally`:1151`释放showWindow和activePres；`:1159`释放未保活candidate；`:1162`释放bindCtx/moniker/raw object。finally`:1177`释放去重缓存，跳过最终bestApp；`:1188`释放enumMoniker和ROT。新赢家替换旧best时`:1132`也释放旧winner。该行为按源码记录，不声称fake ledger已覆盖整个ROT/root流程。
- 当前页helper `PptCOM.cs:1908`显式temporary View/Slide，`:1921`Slide→View；总页`:1930`temporary Slides，`:1938`释放。遗留控制方法仍有链式dynamic View访问，是现状，P0不顺手清理。
- `PptCOM.Tests/Program.cs:30`fake Acquire记录每次图访问，`:67`拒绝无acquisition的释放，`:81`余额核验。`:163`重复扫描100次、`:167`borrowed root零释放、`:171`current Slide先View、`:174`Slides最后。失败/忙碌/缺失属性及session测试通过`Main:311`执行；session组入口`SlideShowSessionTests.cs:425`。根代理负责本次执行结果，本研究只核对测试覆盖边界。

**不能从fake ledger平衡推导Office RCW/事件连接点/WPS实际退出已通过。** 保存原根释放和GC流程是迁移约束，本P0没有验证真实损坏COM实例可发现、真实Office无残留或PowerPoint不卡死。

### 6. DLL/TLB、激活及当前构建依赖

`PptCOM.csproj:8/:11/:20`仍为Library、v4.0、AnyCPU；Office Interop由`:46/:50`引用随附Package、EmbedInteropTypes=True。`:61`ExportTlb调用SDK40ToolsPath下TlbExp，在TargetDir生成同次TLB；无Copy/自动同步到Inkeys目标。manifest `:6/:10/:12`为MSIL、threadingModel=Both、runtimeVersion=v4.0.30319；这是激活声明，不是线程调度实测。

接口基线：`IPptCOMServer` IID=`65F6E9C1-63EC-4003-B89F-8F425A3C2FEA`，旧14方法顺序（`PptCOM.cs:49`）；独立`IPptCOMSessionState` IID=`D7A63F98-43B8-4BA1-A875-B55A5A04DC3E`（`:80`）；class CLSID=`C44270BE-9A52-400F-AD7C-ED42050A77D8`（`:89`）。native `IdtPlug-in.cpp:163`通过`#import "PptCOM.tlb"`编译。`CheckCOM:163`仅返回`20260627a`，native读取，不是已实施的强制版本拒绝门。

unsafe三个`int*`由native`:261`传全局TotalPage/CurrentPage及稳定offSignal槽，属于同进程地址交接，不是跨进程共享内存mapping。服务槽mutex只保护COM pointer，不能当作页整数的跨语言同步。offSignal managed `:1214`为VolatileRead。P0不改ABI或页字段同步。

当前`InkeysRepo.sln:6`的Inkeys Project没有ProjectDependencies；`Inkeys/Inkeys.vcxproj`无PptCOM ProjectReference。实际依赖状态与用户提供的旧AGENTS说明“依赖PptCOM”不同，但**完整native验证仍须遵从用户的Solution Debug|ARM64要求，不因依赖删除改为单构建native**。Solution `:54`等把PptCOM映射为Release|Any CPU；`PptCOM.Tests.csproj:27`保留对PptCOM的ProjectReference。只验证managed fake测试时，由根代理构建既有测试项目即可正确解析此依赖。

`Inkeys/Inkeys.rc:214`嵌入`../PptCOM/PptCOM.manifest`；`:222`嵌入`Inkeys/PptCOM.dll`，ID定义`resource.h:24/:25`。本地TLB供native编译，DLL/manifest供运行激活；二者同次构建交接应显式维护。CI `.github/workflows/build-windows.yml:185`单独构建PptCOM，`:438/:445`按本次选定DLL与同次TLB artifact ID下载到Inkeys，`:467`核嵌入字节。P0不更新随附DLL/TLB，不把测试bin结果当部署同步。

### 7. 采样源码值与呈现门禁

| 路线 | 源码锚点 | 当前值/行为（不是实测延迟） |
| --- | --- | --- |
| managed owner慢维护/ROT | `SlideShowSession.cs:278/:286`；`PptCOM.cs:1216/:1227` | Stopwatch单调deadline，维护完成后约500ms；事件先唤醒刷新，不提早重新跑ROT |
| managed descriptor低频复核 | `PptCOM.cs:283/:312` | DateTime.UtcNow +3秒；pending/binding/页变化可提前刷新，不是固定3秒端到端 |
| managed原页轮询 | `PptCOM.cs:1483` | 事件3秒未更新或forcePolling；末页另有polling路径；受500ms维护与COM调用耗时约束 |
| managed重绑等待 | `PptCOM.cs:1253` | 有旧app时1000ms Sleep；不作为全局仲裁/翻页冷却 |
| managedbusy重试 | `PptCOM.cs:536/:559` | 500ms连续性重置判断，200ms Sleep；生命周期先设Unknown；不等于真实End |
| native新版Active | `IdtPlug-in.cpp:101/:939` | runtime revision唤醒或有界16ms等待 |
| nativeInactive | `IdtPlug-in.cpp:102/:941` | 100ms等待 |
| native旧DLLActive | `IdtPlug-in.cpp:940` | 50ms兼容等待 |
| 可见性/annotation维护 | `IdtPlug-in.cpp:103/:890/:917` | 500ms，状态变更可提前发布 |
| native业务请求等待 | `IdtPlug-in.cpp:1001` | 队列condition notify；100ms等待上界检查offSignal |

现有页链：事件页更新/owner刷新 → `GetSlideShowState`缓存 → native `IdtPlug-in.cpp:610`解析并校验stateRevision/binding/show窗口/PID → `:826`发布Draw3 target → Host `Draw3.Host.cpp:829`接收canvas_presented/ready → native`:878`只在匹配presentationReady后更新PptInfoStateBuffer → `:912`发布UI页状态 → `PageControl.cpp:1697/:1713`窗口提交后pagePresented回调 → `IdtPlug-in.cpp:134/:149`校验session/target/page/total → Host`:2155`再次匹配current target + canvas ready + 非suspended → UI ack后刷新输入门禁。新抽象层只能改变来源/路由入口，不能把“新页包已到”当作Present/UI/input都ready。

native明确Inactive（`IdtPlug-in.cpp:657`）或旧窗口销毁/被不同PID复用（`:680`）才结束已知session；服务槽/COM短暂不可用不等于退出。`PresentationDescriptor.cs:310`明确View.State==5才认EndScreen，3/4黑白屏仍是当前页；缺State失败不猜结束。未来transport断线也必须在源健康/绑定逻辑处理，不能伪造show End。

已有TracePptTiming锚点`:840`native_observed、`:841`descriptor_accepted、`:842`target_published，Draw3`:829`canvas_presented，PageControl`:1711`page-ui-commit，Host`:2160`page_ui_ack与`:564`input_open/closed。这些可复用，但本P0未运行目标应用，不能计算实际event→UI/P95/光学延迟。

### 8. 后续测量与防反复切换参数（待实机决定）

后续P1纯逻辑测试先固定规则：同档保持、重复包不是多次观察、同show多源不加票、新鮮信息版本单调、弱证据持续优于已有目标才提出切换、反向/恢复需稳定、强exact foreground或当前目标确定失效可绕过软等待。PageChanged走既有即时刷新，不共享活动仲裁debounce。只有确认的候选/epoch/revision才能prepare→confirm→commit，旧ACK不能撤销更新意图。

实测计划使用每进程monotonic clock（managed Stopwatch/native QPC）和共同关联ID：source实例epoch/candidate/show身份、foregroundRevision、stateRevision、desiredSelectionRevision、routeRevision、native targetRevision。避免跨进程只比较DateTime wall-clock；需要比较远端时间时先记录频率和时基校准，否则以host接收时刻计延迟。

未来轨迹依次记录：①前台窗口通知/复核与旧、新HWND/PID，②provider事实产生，③host接收，④候选资格/等级及decision reason，⑤prepare发送与ACK，⑥bind commit，⑦PageChanged与owner cache发布，⑧native target，⑨canvas Present，⑩UI提交/ack与输入开放。使用编号/数字诊断，不写文稿路径/标题/墨迹。

需量测后定值的参数：前台重采样周期/事件兜底频率、弱证据最少独立observations及持续窗口、同show来源健康切换窗口、反向/恢复stable窗口、旧证据失效TTL、prepare/确认timeout、源失联等待与硬失效触发条件。源码500/1000/3000ms不自动继承为这些参数。

至少覆盖真实同进程双show、双PowerPoint实例、WPS双进程/非全屏、两源同show、不同源不同show、真实Alt-Tab/NULL前台、控件短暂抢前台、UAC/设置/模态、弱证据往返噪声、源断线恢复、A→B→C迟到ACK和命令结果不明。报告连续切换次数、误切换率、prepare失败/回滚、事件到各门禁分布与退出资源趋势，并区分采样理论上界、软件阶段差和光学测量；本次不设未经实测的最终数值。

### 9. Related specs / 外部资料

- `.trellis/spec/ppt-interop/com-contract.md`：旧IID/vtable、unsafe ABI、late-bound/typed HWND例外、root/temporary释放、同次DLL/TLB交接。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：Unknown/EndScreen、新接口采样、Draw3/UI/Input ready顺序、计时隐私边界。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：当前项目依赖/产物交接、目标架构与宿主MSBuild区分、声明/配置/实测分开。
- 父任务 `research/current-contracts.md`：既有官方IPC、启动权限、前台API资料；本研究以源码baseline为主，不重新做IPC选型。
- 历史COM调查引述的Microsoft COM初始化与activation context资料：[COM library](https://learn.microsoft.com/en-us/windows/win32/com/the-com-library)、[Using Threads, Asynchronous Procedures, and Window Messages](https://learn.microsoft.com/en-us/windows/win32/sbscs/using-threads--asynchronous-procedures--and-window-messages)。本研究未重新联网验证文档，仅列原调查引用；不能从文档替代当前线程实测结论。

## Caveats / Not Found

- **NOT VERIFIED**：真实损坏COM的发现/绑定；PowerPoint/WPS实际RCW/连接点释放和退出无残留；用户既有“STM/安全保护”回归原因。本P0没有制造损坏COM、启动Office或修改线程/清理。
- **NOT VERIFIED**：Win7 SP1 + 原始.NET4运行、Office位数矩阵、双向跨提升权限、不同管理员凭据、ACL/完整性/session与用户身份、跨权限前台观察实际可见性。
- **NOT VERIFIED**：event实际线程/apartment、native创建/消费/业务线程的实际apartment与CLR隐式行为。不得由主线程MTA或manifest Both给它们贴已验证标签，也不得因缺显式初始化重演被历史记录否定的直接重构。
- **NOT VERIFIED**：本轮软件阶段延迟、P95、光学切页、防抖阈值、真Office繁忙退出期间内存趋势。上述周期只是源码值。
- 未找到导出全部多application/多show候选的现有协议；现在仅一个ROT赢家。尚无统一foregroundRevision/三级活动证据reason协议或全局回滞器。
- 当前新session会排队Focus，已有防抢焦点保护；自动反馈是否出现未实测。未来被动仲裁设计必须明确来源切换是否保留logical session及如何避免重复focus。
- 原DLL/TLB与当前源码是否同次构建不能由散列单独证明；根代理测试构建只更新隔离输出，按任务规则不部署它们。
