# 最终独立审查

角色：trellis-check；2026-11-01。当前轮直接读取所有产品diff、未跟踪IdtSingleInstance.h、测试及任务要求；根代理统一构建，本文不把静态结论记为实测。

## 本轮发现

1. **P1 / 待core修复：跨架构wire布局。** Packet以uintptr_t存三个句柄，Win32与ARM64/x64布局不同。用户可选择目标更新架构；父mapping旧布局、子按新布局memcpy将导致Adopt/Ready失败。应固定uint64 wire字段并静态约束布局，转换前校验目标指针宽度。
2. **P1 / 待core修复：一次性handoff参数被再次转发。** Split::Run真实按空格分词；IdtMain默认把旧--inkeys-startup-handoff加入LaunchState::commandLine。重启/更新Adopt成功的root进入SuperTop时，最终命令=旧handoff+新handoff。Adopt拒绝重复参数，但旧root已通过Ready/Accepted退出，导致最终实例消失。应从转发参数中消费旧前缀并加连续二跳回归。
3. **P1 / 待方案确认：旧版本回滚协议代际不兼容。** 原fallbackName明确可能为Inkeys2旧EXE，当前统一StartReplacement要求新Adopt/Ready。旧EXE正常启动但不发Ready，10秒超时后将被TerminateProcess强杀。更新到不含新协议的版本同类。不能把旧目标无Ready等同失败并杀死，需明确能力识别与精确等待的兼容启动方案。
4. **P2 / 静态候选：目录身份策略不一致。** PresenceName按物理目录fileID，WithinRoot按字符串；module路径与QueryFullProcessImageName路径格式/别名若不同则合法接力被拒。建议目录fileID统一归属验证；installer角色同时拒绝reparse向外跳。未做alias实机复现。

上述问题已向root与core发证据；未自行修改core正在维护的header。

## 已核的其他合同

- R1/R2：presence对象所有allow=true进程仍持有句柄，false通过Create存在原子裁决；正常死亡由内核关闭，不依赖运行主循环尾部。参数restart/warn/crash/SuperTopComplete不再永久豁免。不同目录键基于VolumeSerial/FileIndex。
- R3：Debug/Release共用门；已有验真监督器/测试分流位于产品门之前，隔离startupFailure保留其既有入口。
- R4：普通门在update替换与SuperTop提权之前；只读ReadMini避免旧布局迁移写盘，坏/缺键false；错误与duplicate区分。路径预处理仍有原有临时可写探测，未把它误称为“完全无任何磁盘写入”。
- R5：普通restart监督器加入连续lease，argc9生产与argc10私有fixture同逻辑，父精确HANDLE和原deadline未改。new child Adopt先Ready再等supervisor死亡，supervisor Wait Ready后return early，未见互等。
- R6：SuperTop helper获取PROCESS_DUP_HANDLE，并先持lease，再创建暂停final明确DuplicateHandle；Wait root死亡后resume，final等helper死亡，因此双重等待顺序自洽。实际UAC/UIAccess/MIC仍未运行，不宣称PASS。
- R7：更新路径/哈希/原子指令与备份替换代码保留，新协议替换了ShellExecute启动，但协议代际兼容仍为阻塞项。
- 设置schema、三语言键/生成键、实际ToggleRow和事件写回一致，默认false，保存不强退现有实例。General回归实际键盘往返切换，不是纯model镜像。

## 测试静态覆盖及缺口

现有--single-instance-tests覆盖真实私有复制EXE→Adopt→普通probe拒绝、allow probe成功、关闭后仍拒绝、伪造mapping不存在以及最后退出重新准入。它没有并发barrier多进程、不同目录同时运行、真实更新/回滚和SuperTop二跳；不能据此声称A1/A5/A6全动态通过。

新增shutdown restart-lease fixture在私有复制EXE中Acquire真实lease，并由driver持SYNCHRONIZE句柄模拟另一存活实例；argc10继承testDirectory，RunTestRestarted实际Adopt后写marker并从早期入口退出，不进入产品GUI。它覆盖原testDirectory路径刻意跳过的新relay；根代理需提供真实运行结果。

本文审查阶段未构建、未运行GUI/UAC/产品入口，最终结论待上述P1闭合与根代理验证结果后更新。

## 修复复核与最终静态结论

本轮发现后的源码修复已逐项重新读取：

| 发现 | 源码修复证据 | 判定 |
| --- | --- | --- |
| 跨32/64位Packet布局 | alignas(8)，固定uint64三个句柄，byteSize，sizeof/offsetof static_assert，转换前UINTPTR_MAX范围检查 | 源码关闭；不冒称跨架构实机运行 |
| 旧一次性handoff再次转发 | IdtMain解析时先消费HandoffPrefix，addCommandLine=false；专项新增两跳parent→relay→child | 源码关闭；实际运行由root确认 |
| 老版本不回Ready被杀 | .rc显式StartupHandoffProtocol=1；Protocol三态；Legacy暂停创建、复制presence、Resume，不等Ready超时 | 源码关闭；真实Inkeys2/旧SuperTop仍列兼容范围 |
| 未知capability被当Legacy | Current仅精确1，Legacy缺版本资源/明确缺私有key；未知值、API/读取错误均Unsupported并拒绝 | 源码关闭 |
| 路径别名策略不一致 | WithinRoot用与PresenceName一致的目录fileID；installer分支另核非reparse，目录attribute先验 | 源码关闭；alias/reparse实测未执行 |

最终读取版本中，未发现剩余阻塞性源码问题。上述结论不包括root随后新增或修改的代码；若再变更核心应重新检查相关差异。

验证边界必须保留：
- root负责统一solution与CLI执行；本reviewer未独立构建/运行。已存在Build旧protocol FAIL日志不能作为修改后结果；最终报告应引用同次重新构建后的日志及退出码。
- 旧版自身内部SuperTop/重启不理解本次presence协议，无法要求旧EXE按新协议继续转发。Legacy兼容分支消除误杀并让注入的句柄随该旧进程寿命关闭，不等于旧版所有后续进程能维持新lease。
- 专项仍不等价真实installer替换/回滚、真实UAC/UIAccess、Win7及跨32/64位更新链验收。A1并发barrier与不同目录同时运行、A5真实更新/回滚、A6真实提权没有从已有测试代码获得完整动态覆盖；不能列为全PASS。

## 后续physical guard与Ready次序专项

根代理随后新增installer物理目录guard，本轮重新读取并确认以下变化：
- 初始版本在Adopt Ready之后的main Acquire才发现installer目录已占用，会使旧root先退、新installer随后拒绝，属于确定性P1。core已将物理目录guard创建/冲突检查移到Adopt的SetEvent之前，并在父验真加入Wait(parent,0)==WAIT_TIMEOUT。
- 后续main Acquire不得再创建第二份物理guard误拦自己。已建议handoff>0时不做第二次准入或adopted只返回已确认结果，所有本次新增准入否决均应在Ready前收敛。
- NtQueryObject命名域原先硬编码Sessions/BaseNamedObjects在runner红测中失败；现以OpenMutex(Local+name)取得系统实际解析reference的完整对象名比较，保留Mutant类型检查、有限buffer和错误closed。根代理应引用修复后真实结果，不改写旧红证据。

### SuperTop两个Ready的区别（待root决定/修复）

目前原Ready由ResumeAfterParentExit在final还暂停时发送，原root退出之后才Resume final；final随后才Adopt校验并发新handoffReady。因而更新/普通接力的“子准入Ready前父不退”不能原样套到SuperTop。final的新准入失败仍可能发生在原root已经退出后。建议调整为final先恢复仅跑Adopt预检→发新Ready并等helper→helper收到后发原RootReady/等原root死亡→helper退出放行final。产品业务初始化仍晚于原root死亡，不必让预检也晚于父死。该建议需主代理/核心实现者评估，本文不自行改动被冻结的token合同。

## 最终次序闭合复核（第四次构建前冻结源码）

再次只读核对IdtToken.h、IdtMain.cpp、IdtSingleInstance.h及新规范single-instance-and-startup-handoff.md。后续physical guard/main二次准入/SuperTop双Ready发现均已从源码关闭：

| 分支 | Ready前必须成功 | Ready后只等待/放行 | 判定 |
| --- | --- | --- | --- |
| 更新root→installer | child验包/父仍活/目录/lease，取得installer物理guard | child发Ready后等精确父死，main直接接受Adopt，不再Acquire否决 | 已对齐 |
| installer→root/回滚Current | child验逻辑root与同一lease，根物理目录无需新抢对象 | Ready后等精确installer父死，随后main不重复准入 | 已对齐 |
| 普通/Crash重启 | supervisor验继承lease与同文件父，沿原deadline等旧产品死；新child验supervisor与lease | child发Ready后等supervisor返回退出，main直接接任 | 已对齐 |
| SuperTop final预检 | helper复制root lease并Publish；先恢复final仅跑Adopt；final校验后发新Ready并等helper | helper先Wait新Ready，才ConfirmParentExit发原RootReady/等Accepted/等原root死；helper返回退出，final才从Adopt放行 | 已对齐；不再沿用父先死才验final的错误顺序 |
| Legacy目标 | 明确缺能力标记，暂停创建并复制lease | Resume后不等待旧程序不支持的Ready，不因超时误杀；不把OS创建等同完整应用就绪 | 保留明确代际限制 |

main仍在Adopt后只读Mini偏好，但handoff>0直接Admission::Accepted，不再执行Acquire，因此本次新增准入否决都已移到旧owner退出之前。后续真实业务初始化本来仍可能因资源等失败，不能将准入Ready夸大成全部UI初始化成功。

新规范七节与本次源码/命令接口一致；特别是实际Local reference验名、固定wire、physical guard在Ready前、双Ready次序、旧版本能力边界和GUI subsystem必须显式等待ExitCode，均没有用静态编译代替实测的表述。

### VERSIONINFO fixture修复复核
生产HandoffProtocol函数未为测试放宽。fixture原先改已执行过的私有EXE，资源引用造成写入失败；现每个variant先从Image新CopyFile到独立metadata-{unknown,absent,current}.exe，未执行/查询前BeginUpdateResource→EndUpdateResource，修改后才调用生产HandoffProtocol。三个target均位于本次PrivateTestRoot；读取原resource后FreeLibrary；失败只影响该私有fixture。此变更没有改变生产能力判定，也没有共享复用已执行target。静态检查通过，第四build/运行结果由root补充。

### 根代理已报告结果与边界
根代理明确报告第三build exit0；严格restart-lease/config/token/general均exit0；protocol两跳、不同目录、allow/deny、坏包、并发race=0/1通过，唯旧VERSIONINFO fixture写入失败。本reviewer按来源标注为root报告，不宣称自行执行；第四构建运行前不把旧FAIL覆盖成完整PASS。

最终冻结源码未见剩余阻塞性问题。真实GUI/UAC/UIAccess、Win7、实际跨架构更新换包、旧EXE内部后续SuperTop/重启仍须分列未覆盖；与新模拟/无窗口原语结果不混写。

## 最终动态证据复核（收束）

根代理完成第四次完整Debug ARM64 solution构建并明确报告实际进程ExitCode=0；本reviewer只读复核Build/single-instance-build-green.log，确认同次输出Inkeys.exe、PptCOM及既有C4244/pwsh环境提示。未独立重跑构建。

| 检查 | 读取的最终记录 | 结果来源/结论 |
| --- | --- | --- |
| 严格单实例协议，常规Windows环境 | Build/single-instance-protocol-green-native.stderr.log、对应stdout.log | 根代理显式等待进程，报告ExitCode=0；日志有末行PASS，metadata unknown/absent/current均write=1，protocol=2/0/1与预期一致 |
| 双跳/占位存活/普通拒绝与allow/坏包/最后释放 | 同上 | 每个copied worker均有exit及read=1；普通拒绝10、allow成功0、坏包30；后续恢复准入0 |
| 并发/不同目录 | 同上 | concurrent admissions=0/1；不同目录probe成功；before capability ok=1，最终PASS |
| 重启监督真实lease | Build/single-instance-lease-verified.stdout.log | old_pid=61556、exit_code=77、restart_count=1；根代理严格等待ExitCode=0 |
| 配置实际读写与只读Mini | Build/single-instance-config-verified.stderr.log | ConfigBoundary PASS；根代理严格等待ExitCode=0 |
| token/原无UAC失败握手 | Build/single-instance-token-verified.stderr.log | SuperTopToken PASS；根代理严格等待ExitCode=0 |
| General安全页三语布局与真实事件 | Build/single-instance-general-verified.stdout.log、stderr.log | 根代理严格等待ExitCode=0；stderr为空，未以缺少文字PASS替代退出码 |

第三次verified构建下lease/config/token/general已全部通过；第四次相对该版本只改私有metadata资源fixture，生产函数未变，root没有重复无理由运行其它已绿专项。

### runner权限与源码结果分开

同一最终EXE在受限runner执行的Build/single-instance-protocol-green.stderr.log仍有metadata write=0/error=5；在相同目标代码常规Windows授权执行的green-native日志中三个write均为1并最终PASS。故当前剩余受限runner红测属于私有PE资源修改权限差异，不是生产HandoffProtocol源码失败。未放宽三态判断、未移除断言或仅跳过失败测试；保留旧红日志作为证据，不能将其改写为PASS。

最终结论：本reviewer发现的源码问题均闭合，最终可运行专项证据已读取并与root实际退出码报告一致，没有剩余阻塞性审查项。真实GUI/UAC/UIAccess、Win7、实际跨架构换包和旧版本内部后续重启仍未验证，不能由本次无窗口PASS推导。未创建commit/push。

## installer真实双跳/物理冲突fixture补充复核

根代理最后仅扩展TryRunTests私有测试区；本reviewer重新只读检查endpoint边界及正负证明，未发现问题，不建议扩大本次scope。

- endpoint先取真实Image物理目录，只接受PrivateTestRoot本身，或其直接installer目录且非reparse；仅该私有根内写trace/adopted.pid。错误mode/Adopt失败均从测试早期入口返回，不进入产品GUI/真实更新。
- parent实际创建top/installer/Inkeys.exe的relay；relay接力后实际创建top/Inkeys.exe的child，不再只是同目录两跳。使用生产Handoff.Publish/Wait/Adopt与真实物理guard；relay直到第二跳Ready后退出，physical installer占位寿命与该阶段一致。
- 负例driver先持installer物理presence对象。relay在生产Adopt的Ready前物理准入失败，返回测试码30；parent Wait失败时记录IsPresence(root)==1，再从私有测试返回code1。driver必须同时满足code1、trace retained-parent=1及没有adopted.pid，证明拒绝没有提前确认父退场，也没有生成最终接任marker。
- 释放blocker后再跑正链。最终child在精确relay父死亡后才写adopted.pid，随后受driver release事件控制；原allow/deny/不同目录/race/能力测试继续执行。
- 此补充真实覆盖“安装根→installer→安装根”的接力和installer冲突失败点，仍不等于实际下载/换包、磁盘失败回滚或真实UAC验证。生产代码没有为fixture放宽准入。

上述新增fixture的最终动态结果待根代理build-installer与常规Windows严格专项；此前green-native记录属于补充fixture之前版本，不挪作新fixturePASS。
