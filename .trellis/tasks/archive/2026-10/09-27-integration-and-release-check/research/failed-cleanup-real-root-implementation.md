# C3 root 鉴权、截止见证及 Main 共用回退实施

Active task: .trellis/tasks/09-27-integration-and-release-check。2026-09-30。源码已冻结供实际diff/safety与完整构建；尚无新CLI运行PASS。

## 冻结设计与实现

- 已审六case合同6EE00C0A...D34E0F6及独立GREEN_DESIGN；root仅补C07最终普通Close也使用同一有限RunAuthorizedCleanupClose（case7–14），原8–14不变。worker C3-A只C03/C05/C08；C09/C10/C11/readers仍明确90待C3-B。
- 新normal共享header固定magic1430FA04/version1/1024B，不混旧96/128/48B；所有POD size/offset/trivial断言，新增plain bool after-wake gate不改旧gates{} false/C00顺序与size，threearch断言未编译。后续实际source身份以下表为准。
- authorizer early true-return，坏参数不落普通Main；精确继承父HANDLE/PID/镜像fileidentity、ACK/mapping，父源必须同repo，所有驱动器到目录/bin/copied/source组件以OPEN_REPARSE_POINT只读lease核非reparse并禁止delete共享。只有全部1024B/nonce/finitecase/issuer/intent/reserved/zeroTrace/zeroObserved/合法expected验证后授权，之前无HWND/gates/fault/artifact。三个HANDLE_LIST对象，不新增任意路径/process/window权利。此窄fixture目前只支持当前drive绝对repo路径，UNC/extended别名拒绝，不改变普通产品兼容。
- 新explicit --shutdown-supervisor-tests --failed-cleanup-real-only <finite producer case>，旧套件不自动扩跑。四身份/POD/case负例均是create-new own run，全部先通过才进入actual case。reader仅由parent在生产child死亡后同run/bin用新映射启动，public reader-only因无可信baseline返回71；不接外部receipt路径。
- g_authorized observation只在鉴权后建立、所有实际owner/monitor/helper线程真join返回后撤；hold root/worker不返回，保留map/path lease到进程死亡。publisher允许local/outer幂等，真实NoWait前奏+首纯原子tick见证，无锁/log/I/O/heap/新线程。普通close直接SetOffSignal1再记录原deadline/g_armState2或4，因缺证明而拒PASS。
- parent exactHANDLE等待/退出码/原tick/grace/fault/返回/失败与自然死亡分别断言，30s已知失败episode和普通15s不混。超时仅own TestChildGuard清理并FAIL。完整packet仅child死后create-new保存到run/packet-<pid>.bin，last durable reader和oldprocess截止分开合取。receipt有完整GUID/hash/日期/session/有限fixture容量校验，Clear=0/Exit=1保留以date区别Desktop；不是普通数据限制。
- Main 原firstHost->knownfailure/warn/StopProduct/旧Window.StopAndJoin/Complete->新Window/ULW span提取为同一private模板，两调用点用同一逻辑，普通logger/顺序/契约不变，auth C07仅加数字旁挂/禁止普通logger。第一Host的所有真实style请求拒绝，需GraphicsReady/实际style拒绝、firstfalse/旧原grace；旧owners HANDLE signaled/created-destroyed边沿、旧HWND当时失效、旧Signal0后才newGeneration。第二ULW必须真FirstFrame/完整output-content身份、样式无NORD/transparent；超过旧grace+1s后向exact own hidden HWND发真实Down/16逐消费Move/Up，需新Stored/history/content成功Present，正式Close/Stop真结束后自然0。未满足前提90/无DComp91，不把同进程重建冒称进程重启或Win7。
- 主工程新增实际fixture，pure numeric Bar.PresentationProbe在主+Headless各登记一次；Headless不加入Main/Supervisor/实际HWNDfixture。所有source保持BOM/CRLF，未更改系统/库/发布宏/版本/FLIP/DWMgate/HTTP策略。

## 验证状态

- 未构建/未运行。git diff --check及字节检查另记录。新RuntimeMetrics U2-P1绿色候选、UI3 B2-P1 RED、C3-A/root全部已冻结，下一root串行Debug完整Solution检查编译后跑parked/U2 green和strictHeadless预期B201–B209红，再独立实码/safety授权新真实case。
- C07真实privateWindow/Host/RTS/旧thread死亡/成功ULW和全三轮仍未验证；C08 diagnostics时序/存储fresh/RTS成功quiescence/低层未返回API/Win7与最终Release3arch保持未验证。完整人工清单仍父completion-and-manual-acceptance.md，不宣称仅剩人工或发布就绪。

## root 冻结源身份

| 文件 | SHA-256 |
| --- | --- |
| Inkeys/IdtMain.cpp | 892DB0445DDC0A4E9566FD4C1C8B224B81BC9AFA5DE26AAE6DC1CA66B218DC89 |
| Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp | AFB1A977D2B042619562E4B8D08B0C4A43CE8A0225D18C49FACA86FCC1E35B07 |
| Inkeys/Inkeys/Helper/FailedCleanupDeadline.h | B6922C3CA6D33F31D194229C78703D586E6BDC0009A96C79463E8DC581A1564A |
| Inkeys/Inkeys/Helper/FailedCleanupDeadline.cpp | F1EDD63CFCB10107C06895716CC5285BE217DBE5B61B14794B6EB17956C62613 |
| Inkeys/Inkeys/Helper/FailedCleanupRealCases.h | E8EE4841373ED51CCB34F27D4C47071B2FAF7D45F013D55D202142F7B790D1E3 |
| Inkeys/Inkeys.vcxproj | 887DE75602CB92D916E314D508980A679461B29EF08D0F5C1E454E6EA8BBD8FD |
| Inkeys/Inkeys.vcxproj.filters | 740461B3662C73E8149144A9EAC0CBFBC1980FABAE4B0896B224E662C219AF05 |
| InkeysHeadlessTests/InkeysHeadlessTests.vcxproj | 1EAD17EB430157181617828E6FFD933209D4F9BACFD17331E17689573CF63856 |

## 首次实际绿色与C3-B身份输入增量

- 首次table AFB1...是当时初稿；F065定族/日期/完整合法expected后的Supervisor DAF636...与AutoSave F502...已由独立code-and-safety review增量核，第三完整Debug0；实际已执行19个C3-A/C07 positive producer观察+每轮四身份负例，自然父0。九startuphold按原grace+15ms范围晚15–47ms，三ordinary render Close按原15秒晚46–63ms；Main重建三轮旧scope取消、新ULW超过原grace后仍可16Move+Up。详情/raw在父validation和c3a-first-candidate-results.json，本report初始未验文案为历史不覆盖失败证据。
- U2-P1 parked0/pid35248（81912+39072bytes）、B2-P1 strictHeadless0/pid34212、PptCOM0/pid7044数值门通过，真实P2/U3/B3/F后续未完成。
- Root C3-B-only增量：PPT两个reader从producer死亡后另传trace.oldDrawpad纯数值（不扩POD/argc）；authorizer只两readcase允许非零<=UINTPTR_MAX，其余Trace必须零，Desktop/producers仍旧规则。供重构实际old bindingToken，无OpenProcess/旧HWND操作权。reader仍未接完整/独立新safety/新build，旧C3-A已复制PE与来源不受当前source新改动污染。
- C10合法初次Current NotFound计入failed，按实际初始基线和后续增量0；hidden-only Host实际NotFound计数提供来源见证。禁止Reset/伪0/preseed/accepted冒Save。
- Root最新未编译共享source身份（C3-B其它writer正在实现，其源码见自身report）：

- Inkeys/IdtMain.cpp SHA-256 892DB0445DDC0A4E9566FD4C1C8B224B81BC9AFA5DE26AAE6DC1CA66B218DC89
- Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp SHA-256 785785AEB48EDB1BBCC3FE2D63DBB1982D2953F3B9BC5970A0C59A190643050B
- Inkeys/Inkeys/Helper/FailedCleanupRealCases.h SHA-256 E8EE4841373ED51CCB34F27D4C47071B2FAF7D45F013D55D202142F7B790D1E3

## 2026-10-01 独立C3-B发现的fixture颜色入口修补

独立reviewer确认 SetPenColor 新建default ProductState（workspace Desktop）会让StateBridge整体replace清PPT目标；RunPptStorage进入601后改B色会错误离开Presentation，C10不是有效A+B。该问题仅新增fixture，无普通产品setter修改。root派发原作者最小修复/消息均因4agent thread limit未启动，作者已PATCH_READY停止，root明确接管仅Fixture.cpp::SetPenColor：改从真实ProductHost().ProductBridge().Snapshot取基线，仅改批准笔tool/width/color/selection/autoSave字段，保留workspace/target/page，加中文合同注释。其余4源/生产Bridge不改，不删C10严格断言。后续作者需读取此段并保留修补；writer再次活跃前须明确交回，不能双写。尚无新Build/实际C10运行，交独立checker复核真实Snapshot/publish时序及新source身份。

Fixture before452D2EDE97F1FC1F029F8EBE749E3C312A52F8C95885BBDBC5BC30738511F7E6；after A66C27B7D9FD810389EEF3D1B666DCB24807C07A97B188C2F6721015B4C1FBC1
