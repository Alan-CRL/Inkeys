# UI3 F Source 独立 actual code / safety 复审

Active task: `.trellis/tasks/09-27-integration-and-release-check`

日期：2026-10-01。唯一写本新报告，所有源码/工程/spec/既有报告只读；未 build/run/Git/GUI/Computer Use/递归，未修改或回退其他writer工作。实际读AGENTS/check、Source完整设计和最终实现、三新源及Interaction/Button接缝，追已审Root A/B/C与Auth的实际调用/线程/文件边界。原Auth73CD scoped批准、Root2BB scoped GREEN、H1/UInk/U3-H报告保留；不重复升级其既有结论。

## 当前判决与运行界限

**SCOPED_STATIC_GREEN / SAFETY_CLEAR_FIRST_F_CAPTURE_OFF + EXISTING_AUTH_SUITE。** 本轮发现一个raw invalid Bar stamp漏拒证问题，writer已只追加一个OR，真实新hash已核，当前无剩余must-fix。该结论只覆盖有界copied-own进程、exact Auth cap、新私有树与以下Source/owner边界，不是216目标、两scene性能、像素等价或Win7 PASS。

Root可先用已编译旧ECDB/PE身份执行一个MainFold `capture=0/capacity=0`基础链，及既有Auth suite：实际RunAuthTests使用默认Options(scene1/capture0/capacity0/round1)，其positive仍调用完整真实Bar runner；负例沿已审Helper自行owned测试。new raw OR仅clocks-on，off控制流与旧PE源码逐字一致。**旧PE不可运行或宣称新的capture-on门通过。** on及后续更多scene须新108E源完整主Build0/对应core回归后再取每case结果；所有失效/timeout/ResourceUnverified保留失败分母，不预设216自然成功。

## 精确当前身份

| 输入 | SHA256 |
| --- | --- |
| Bar.Presentation.Source.h（5168B/93行） | `75BBFB73910CA4635B1E297D13161DD5C2FE40AD899E5BC5B6EE358C2405A4A0` |
| Bar.Presentation.Source.cpp（8064B/164行） | `3CE4506F911CCBC5AE366169B94119AC5DEBB47AE905B5CC977A1CF46F2088E5` |
| Bar.Presentation.Test.cpp（81082B/1355行，新raw门） | `108E2B29B677A6CBC2F7DD597C774ECAE931C1E3BCBEE06855FA5DEFE3324D20` |
| Bar.Interaction.cpp | `B45F5849B508B835509E1D8AEAAC74DECA6ACCF88CE8669E5D848029855AD076` |
| Bar.Button.cpp | `97F4CCCA57515679331BEF700DAE8DE75014A5D014C42D145DBF4579F78363DA` |
| Auth h / cpp（保持） | `20AB2AF89744BF52871239891CE97399762CA7FC46B21E63D5FFC62EFA087D13` / `552EEC5AB854D239D5354680DB13433C38D831A2DD428C5D6ED7D5C07737F5EB` |
| Main / RenderLoop（已审Root范围保持） | `00C13D99A4F73915F8375AD38FB14DB6B5AFBADAF971855D9826C2F191FE70C3` / `520190BD6FCB18D7F68E7B5EAE2214FB79C3CCD3C7CA867EF9F1C9E42D382EC4` |
| Source implementation当前 | `21DAF66819CE8BCF421503A6B5EA37350A0B44BCBF134593F6AC8B4C7A9583C3` |
| Source设计 / Root A-B-C复审 | `45AA6DB476559729A17258A3BDB8BFB4AA812AF7D4AE29BFA9C74B0FF905A7C8` / `2BB717478E9CA661F88B46F804C775061458BD9CA858014B821635CB3350DB6E` |

五Source/Caller文件当前UTF-8无BOM/CRLF/bareLF0。Test405的uint32消息显式化在内存逆改精确恢复旧 `8BAB2A1A1C36C136CFB87118ED06F1AAA00C0EF1AE8C6E873BE8EC8A3F0E5A6E`；新1134 invalidstamp OR逆改精确恢复已编译 `ECDB68FF6B58F3D1D5A9B19D8EF88876EA0A26B896B131AE860EE34A0C8FD23F`。未写还原结果，没有Git diff运行；不将机械编译修补当协议/动作变化。

## Findings (fixed)：raw无效真Bar stamp不能冒Passed

- **实码缺口：** Test1133–1134原只拒raw.invalidCallbacks/invalidBatches。SDK RenderPipeline.cpp471仅按RawCallbackTimeValid计invalidCallbacks，482另外计invalidBarCommitStamps；两者不等同。即使216目标和图成立，无效Bar stamp本可不设置state.failure，FunctionalResultValid/packet/summary仍Passed。
- **已修：** 1134仅增加 `raw->invalidBarCommitStamps` 非零Fail(Target)。全部失败/原始前缀及计数继续导出；不将合法unverified/failed/恢复历史粗略一概禁止，不改SDK或筛样本。当前108E新门静态闭合；无该门的GUI故障注入或动态PASS。

## Source数据、权限与实际输入链

1. Source.cpp编译两个immutable64B表：Main433、Draw435；216个真实Down/Up对、Draw的2个保留Setup、1个Cancel，索引/sequence/phase/action/flags/reserved/base0逐逻辑字段校验。LE/FNV包含版本/scene/count/steps/steady-clock period；未知scene/count拒绝，不哈希padding。schedule为固定1秒步/20ms Up，最末216/217秒，source不移due来掩迟到。
2. Test FixtureState只从exact注册cap读取Frozen72和路径getter；不存在从mutable Packet重新获得业务权限。PrepareStorage633–642复核真实descriptor/hash/rows且无其它Source/Host/Window/Pipeline已跑，installed单一CAS。Packet只写mutable数字输出；Auth返回前仍验证Frozen inputs不变。
3. Window接收354–413核actual own HWND、own PID/thread、valid index/extra0、Posted、exact FIFO/phase和当前SourceOpen；Down由Root A当前Ready真anchors/serial0取得点，Up/Cancel沿对应Down sidecar。short范围/nonfinite/odd mapping/generation/flags等由共用TryBind真实验证，不clamp成其它按钮。
4. Dispatch把fingerprint生成touch-screen-tagged ExMessage，真正Inputs私有键态/Window.Enqueue。Received完整wire在入队前release；consumer可能早于Window成功回执，ObserveDequeue444–447只需要Posted+Received且未Consumed/Failed，避免把Enqueued回执延迟当非法。Enqueued/Consumed分别原子计数；普通Clear真实removed>0即保留Dropped/Failed，不重发。
5. Interaction实际Wait/TryGet在prepare坐标前验证全wire字段，当前consumed index在该线程TLS中；finite request用真正Up/source/step。Window的latest Up不会污染尚未消费Down。ReadBarPointerPosition只有NotInstalled才GetCursorPos；安装但Unavailable不OS回退。TouchSeek沿真实touch分支消费相同tagged Up/Cancel，不走鼠标光/普通rawinput/OS坐标循环。
6. Main Down与Seek返回Commit前两个门、主栏temp按钮Down/Up门、Draw callback第一业务写前门，核currentConsumed/scene/phase、paired Down和每动作仅一次、Frozen Pen/profile/closed aux。其它按钮映射action0在pressed/callback前拒；main stage未命中即Shutdown，后续Geometry/FineDial/DrawAttribute写不会获授权。早先color/overlay/more/eraser受frozen closed-aux前提；ordinary keyboard/raw/timer/快捷入口被拦，默认NotInstalled保原业务。

## OS消息与文件边界

- Dispatch私有index和拒绝输入都返回Message::Action::Handled，不能用Discard落回原WndProc。WindowMessageMustBeHandled536–563覆盖mouse/NC/hover/key/hotkey/appcommand/touch/raw/pointer/gesture/timer、相关private辅助message、close/syscommand/capture/cancel；WM_TOUCH CloseTouchInputHandle、WM_GESTURE CloseGestureInfoHandle、WM_INPUT直接DefWindowProc完成系统清理，再Handled，不重复送业务。显示/DPI/设置变化在活动Source期记UnexpectedSource，结构消息仍交真实窗口实现。
- Root A/B/C/Source没有获得任意HWND操作权限。WindowSpec只Bar，created callback核本PID及其Window owner thread，API指向该真实own handle；普通Main配置/单实例/Hook/Office/PPT/update/self-start/DDB/完整Initialization不运行。ChangeStateModeToPen实际只该child状态和SyncDraw3State；前提ProductRunning=false，不给外部Host启动授权。
- globalPath/pluginPath在owner前绑定cap.BinaryDirectory；实际config.GetFilePath==bin/Inkeys/Config/main.json，legacy deploy==bin/opt/deploy.json。Auth保持drive→repo/private/copiedExe等已审nonreparse/FILE identity租约；Source另持bin/Inkeys/Config/opt四directory deny-delete租约至alljoin/output。写前ordinary/no-reparse/single-link检查；现两配置写者均调用PptSettings::WriteAtomically的同父CREATE_NEW temp→MoveFileEx替换，未修改普通配置schema或用户路径。
- Source seed合法defaults，16组件关闭，仅child logger无sink。原embedded TTF1/7/3/8与zh-CN资源/真实InitializeUI/format/preset/load/state/position；InitScope包含所有真实SVG资源构造/稳定绑定，InitializeWindow外部部分仅设置真实窗口范围、没有提前SVG初始化。没有调用外部shortcut callback或启动Office/用户process。
- capability不是OS sandbox；file/proof限制依赖本轮新可信private tree，不扩为任意并发攻击者可写目录的通用安全证明。输出NewOutput只固定private leaf名称CREATE_NEW，HANDLE验证non-dir/nonreparse/single-link，流式4KiB buffer、Write/Flush结算；不删未知文件，已有文件失败，不补PNG/图或覆盖源数据。

## Bootstrap、owner job与退出的真实寿命

| 阶段 | 实际次序与证据 |
| --- | --- |
| 准备 / 预算 | sizeof(FixtureState)含optional publication/observer、SVG固定表、435sidecars、216goals、单job；加两表/auth/POD/path容量/64KiB scratch核≤4MiB。raw真实callback+batch stride与两R double数组（16R），先除法检查再乘；destination16MiB+readable保留16MiB，共同≤64MiB。大capacity拒绝，未降图/数组。所有准备在owner前；不将D3D/字体/RSS称该payload硬上限。 |
| A真seed / 启动 | publication+observer从实际初始signature建立，EnableBootstrapBaseline/绑定SVG先于Rendering；A真实Fit pending保持未ready，到自然一致成功帧AcquireReady后才创建Interact。InteractionReady再Acquire，随后SourceOpen release/启动Source；普通bitmap/alpha/动画/pacing未加Request/fullDirty/Flush。 |
| 真实输入与B | 每次Up核exact accepted.run/step/source及CompletedRevision，不接受NoChange引用当新完成。Setup若实际发送、warm16和final分别Post单owning job；B在Scheduler真正owner线程复制第一次canonical完成row。Source记录216序列，不伪全页完成或CPU poll时刻。 |
| 单任务回执 | PostControl实际只在running且未stop下push；失败前不排队，成功后wake/returntrue。job捕获state引用，body有catch，B/C借用返回后写succeeded、done release，SetEvent为最后借用操作；event保持到Scheduler真join。2秒超时先Close后一直等真实event/自身监督死，不能凭done/超时提前返回Auth。 |
| 正式Close | 正常/失败Cleanup820–826先停publication、关闭Source、SetOffSignal(1)接受原15秒，再Source.join和Interact.join；取消只自有contact、有界250ms，无contact明确SkippedNoContact。任何join异常进入KeepFailedOwnersAlive实noreturn循环，保存state/cap/job直到监督死亡，不展开借用栈。 |
| 本地等价阶段与C | 两线程真join、216及exactMeasuredEnd成立/contact清/无失败后release EquivalenceReady。C只在同一个Scheduler owning job调用，完整BGRA一次，MeasuredEnd目标时间不换成readback时刻；Root同步C的Map/COM借用返回后才发event。失败保持状态；job真回执前不Unregister/reset。 |
| 全owner停止 | content flag/Display tracking停→Bar.StopRendering同步Unregister并reset→Window overlay/empty-setting两真join→Display.Shutdown drain→Pipeline.Shutdown/Scheduler真join。Source未用SDK Unregister代替control task回执。observer Seal/Absorb/最终纯signature、清全局binding在alljoin之后。 |
| 输出 / Auth | TakeRaw一次(on)，off不配置数组/不补计量钟；离线所有文件/统计完成后关闭event及Source目录租约，最后Packet result/stats→Sealed。OOM无owner分支只允许空owner集合Sealed Failed；部分初始化flags在调用前记录，异常同cleanup。Auth398–403只有Sealed才撤registry，否则self-failed noreturn；不返回带活线程的cap给析构。 |

Source/Interactor/Window callback/Render/Display和event/表/cap/shared packet/CPU pixels全由同一state或Auth拥有到该链结束。ordinary Close/父300秒只能解决自身进程不返回；强杀/timeout不成为Passed。此源级论证仍不是设备API永返回、真实driver/Win7或每异常动态演示。

## 离线数据语义与剩余范围

- 每planned216/Setup/Cancel/source row均输出，SkippedAlreadyOpen/SkippedNoContact不冒执行；SourceRejected/RejectedByBusiness/Superseded/ResourceUnverified/未完成保持全分母。CountComplete与B/production严格Completed合同，不削required SVG/phase或重画换成功。
- MeasuredBegin是真实第17步Down owner receive，MeasuredEnd为精确final原commit tuple/ticks；callback/batch只整段contained且匹配generation/epoch才计wall cost，重叠/afterEnd保留原CSV，不混读回/退出时钟。commit gap来自实际valid Bar stamp，推进/GetDC/ULW/软件成功分开。callback wall不等于thread CPU/成功FPS/GPU/光学，CPU/GPU/光学字段仍null。
- on raw缺失、未sealed、掉样、invalidtime/invalidBarStamp或无匹配warm generation均Fail，不以216/像素单独覆盖数据缺口。P99样本不足1000为null，off所有性能time null；不选择最佳轮。当前一run的Passed不是release/全链性能判决。
- svg-cold scope=owned_initialization；svg-warm scope=render_all，steady_phase_collected=false、warm/measured phase counters=null。未收真实边界差分，不称steady。
- Root C完整viewport BGRA/stride/alpha/epoch/attempt及两个backing serial都保留；same-child pixel_file只有待fresh on/off整字节比较，不从hash或文件存在宣布等价。未知shape/text/light、其它family/Retained沿原保守未证；失败可自然发生，不关闭光影、改画质/多画帧/放宽完成制造绿。

## 已读构建 / 测试证据与新门缺口

本reviewer未执行任何命令启动应用。直接读取Root `f-u3h-uink-wire-compilefix-debug-arm64-build.status/log`：完整InkeysRepo.sln Debug|ARM64 exit0、6warnings/0errors、22.94s；对应ECDB消息type修补前的新raw门尚未入此PE。first Host DXGI头、second C2397均保留原failed记录，无SDK/ABI/工具链绕过。

直接读同current前缀strictHeadless pid32828自然0、216layouts/animation PASS；PptCOM33572自然0；offscreen28404自然0/failures0；parked29836自然0。这些包含已登记pure Source/F210–214及Root A/B/H1辅助代码，不是窗口/输入/TLS/权限/216真实F证明。Root此前A/B headless34412/0同样只保其限定范围。U3smoke19444/C10及reader结果不在本Source报告扩判。

当前直接PE hash `9AA0A04D6402F69422A7B348CD44238AE912ABBCDDE97665DC5A28D4277B9D31`。108E新增raw invalid门未新编译；仅默认off Auth suite/首MainFold off可按上述分支一致性有限先取证。Auth suite的positive仍真实runner，授权通过与benchmark/216失败分别记录；on必须后续新完整Build0，当前无wholeF动态通过。

## Findings (not fixed) / Verification

- 当前没有剩余Source must-fix；新108E raw consumer门仅静态关闭，未动态fault/on/wholeF覆盖。directory lease/普通配置写、输入动作、job/partial-init alljoin虽已源级核，仍须首owned实际run验证。任一unknown/超时按实际失败，不预填成功。
- Lint：未运行产品lint；五文件UTF-8/CRLF、真实声明/定义/注册及两修补前像已静态核。
- TypeCheck/Build：Root旧ECDB组合完整0已读，新108E与以后同文件更改不可沿用该PASS。
- Tests：Root上述pure/core证据范围保留；本reviewer无run。specific first-off/authsuite安全范围CLEAR，首真实F/216/像素/on-off/两scene三轮Release/Win7/真输入/发布仍NOT VERIFIED。
- 唯一新增本报告，其它文档/spec/账本/产品未改；后续源hash或窗口/文件/控制任务接口变化须独立delta复审。