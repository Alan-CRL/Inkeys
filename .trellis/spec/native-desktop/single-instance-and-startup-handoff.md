# 单实例与启动交接

## 1. Scope / Trigger
适用于普通启动、防重复启动设置、更新/回滚、正常/崩溃重启及SuperTop三段式。wWinMain测试/监督器验真早退仍位于产品准入前；隔离startupFailure不得占真实产品目录。不同安装目录可同时运行；不做跨目录机器级互斥，也不合并多实例配置/墨迹。

## 2. Signatures
- `Config.AllowMultipleInstances: IdtAtomic<bool> = false`；`Config::ReadMini(paths, bool readOnly=false)`。早期读取该键必须传true，不能触发旧Bar布局迁移写盘。缺键/非法bool/读取失败回缺省false。
- `Inkeys::Launch::Acquire(root, allow) -> Admission::{Accepted,Duplicate,Error}`；`Adopt() -> 0/1/-1`（无包/验真并接任/失败）；`Handoff::{Arguments,Publish,Wait}`；`StartReplacement(image)`。
- `Packet`固定版本、byteSize与uint64句柄字段，布局/offset/size静态断言。不能用uintptr_t做跨架构wire字段；接收端转换前检查本机指针范围。
- VERSIONINFO `StringFileInfo/080404b0/StartupHandoffProtocol = 1`。三态 `Protocol::{Current,Legacy,Unsupported}`，未知/坏资源/查询失败不可当Legacy。
- `--inkeys-startup-handoff=Local\Inkeys.Handoff.<nonce>`只承载短寿命包名，不构成豁免。消费后从`LaunchState::commandLine`剔除。

## 3. Contracts
- Debug/Release普通产品入口一致。Restart/WarnTry/CrashTry/SuperTopComplete都不能单凭参数绕过准入。重复实例在更新替换和SuperTop提权前退出；错误区别于已运行，不杀已有实例。
- 目录volume/fileID构造Local presence键；所有allow=true实例也保留句柄，避免关开关后无法识别旧实例。只保存开关影响后续启动，不强退现有实例。
- 物理目录归属与presence使用同一fileID；installer必须是已验证安装根下的真实目录，拒绝reparse向外跳。逻辑安装根跨root→installer→root保持，不按installer名字任意上溯。
- presence为内核对象存活引用，不依赖mutex线程ownership或文件锁；RAII/进程死亡关闭最后句柄。交接连续DuplicateHandle，不释放后重抢。
- 接收方在Ready前校验固定包尺寸/版本/句柄范围、childPID、精确父PID/镜像目录、父仍存活且SYNCHRONIZE可用、目录ID与lease、Mutant类型和完整对象名。跨installer的物理guard也在Ready前建立，失败不得回执。
- 对象名以`OpenMutex(Local+预期fileID名)`的reference实际解析结果为准；不能硬编码BaseNamedObjects，因为受限runner可能使用AppContainerNamedObjects。NtQueryObject动态解析，有限缓冲与指针范围检查，错误closed，不能仅比较名称后缀。
- Ready成功后父才允许退出。child等精确父HANDLE signaled后才进入业务；main已Adopt成功则不再二次准入，避免Ready后再次否决。失败只终止本次精确child，不清理未知文件或已有实例。
- 正常Restart/CrashTry通过原监督器精确等旧进程，沿原绝对deadline；监督器显式继承原lease，之后把它转给新child，关闭多开后仍允许合法替换已有的一份实例。
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
| 父未死或仅有QUERY权限 | 不提前初始化；缺SYNCHRONIZE在Ready前拒绝 |
| 同目录别名 | fileID归属一致，不能仅路径字符串比较 |
| 未知协议标记 | 明确不兼容，不降级普通启动 |
| helper超时/取消/创建失败 | 原实例继续/降级，不能迟到启动第二份 |

## 5. Good / Base / Bad Cases
Good：parent→relay→child一直持同一lease，child确认后等精确父退出；允许模式也持presence。
Base：普通缺键配置false、单份启动成功；不同物理目录独立。
Bad：SuperTopComplete永久豁免；等固定2秒当精确退出；先Ready再检查物理guard；旧handoff参数转发到下一跳；将旧目标无Ready当作失败强杀。

## 6. Tests Required
- `Inkeys.exe --single-instance-tests`：私有copy真实两跳、并发barrier、不同目录、allow/deny、坏包、最后引用释放、私有VERSIONINFO三态。
- `Inkeys.exe --shutdown-supervisor-tests --instance-lease-only`：真实监督链接力，driver另持SYNCHRONIZE-only引用，替换不被误拦。
- `--config-boundary-test`：严格默认、bool往返、readOnly Mini旧布局字节不变；`--supertop-token-failure-test`：原无UAC token/握手失败合同。
- `InkeysHeadlessTests.exe --setting-general-only --no-window`：真实三语General安全条目位置和键盘开关事件。
- GUI subsystem CLI必须显式进程等待并取真实ExitCode（例如Start-Process -Wait -PassThru -WindowStyle Hidden），不能用PowerShell直接&后旧LASTEXITCODE冒充成功。
- 未实际测试的GUI/UAC/UIAccess、Win7、跨架构更新替换、旧版本后续内部重启需单列，不以同架构无窗口PASS替代。

## 7. Wrong / Correct
Wrong：先发RootReady让旧root退出，再resume final去检查准入。
Correct：final先完成准入并发新Ready，helper再确认旧root退出，最后放行业务。
Wrong：`sizeof(uintptr_t)`随发布架构变化却直接memcpy包。
Correct：固定uint64 wire及布局断言，转换前验证目标指针范围。
