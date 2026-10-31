# C-P1 生产失败清理 primitive 的 copied-child 验收

日期2026-09-30；Active task `.trellis/tasks/09-27-integration-and-release-check`。此方案落实已独立审查C00/C01/C02，C-P2真实Host/Window/RTS与UInk另行接线，不以primitive通过替代它们。

## 固定边界

root唯一修改ShutdownSupervisor/Main/项目及本报告。worker只拥有已冻结两个新helper和未来调用点；当前RED无monitor模拟此前缺失的保护。不打开GUI，不进入普通wWinMain业务初始化；所有testchild都经独立magic/version/96B mapping、exact父PID/HANDLE/镜像文件identity、私有非reparse bin/current EXE认证。

新独立purpose `--inkeys-internal-failed-cleanup-child-v1`，公开显式父套件 `--shutdown-supervisor-tests --failed-cleanup-only [expiry|allocation|monitor|join|ordinary|cancel|dormant|earlier|begin-cancel|expiry-cancel]`；当前方案不是对旧48/128B信封的复用。parent仅创建并持有自己本轮child HANDLE；上限后强杀只记FAIL，内核对象由持有者关闭，未清理未知文件。

## 用例

- expiry：同一生产helper Prepare→BeginKnownFailure→重复Begin不延长→child保持已知失败状态；绿色约grace15s+fatal15s自行以001A退场，RED40s仍活记FAIL。
- allocation/monitor：constructor固定auth-only fault，实际Prepare边界；返回到afterComplete则失败，绿色001B约15s，RED无保护自然0仍FAIL。synthetic故障不称真实OS/OOM复现。
- join：生产monitor在真实Cancelled分支的event gate停止，Complete真实HANDLE管理join超时；publisher确认gate已到达，保留栈直到001C退场，afterComplete必须0。不能假造WAIT_TIMEOUT代替监视器仍活。
- ordinary：实际SetOffSignal(1)先接受Close，随后Begin失效清理；不重置原15s，退出码必须现正式监督0015/0016，原published tick和过程退场分别记录。
- cancel：已知失败后Complete正常返回，旧Signal再次Begin仍0；等待16s超过原grace仍无fatal发布、普通exit0，证明不会杀下一轮工作。
- dormant：普通Prepare无失败，等待16s无冷启动timer，再Complete正常0。

scope测试门的两个bool/六个HANDLE只有验真child传入，test gate是child局部HANDLE、持有到join/进程死亡；helper State按值持有，不关闭借用gate。核心publisher调用真正Main静态CAS/BeginShutdown/offSignal/Wake函数；wrapper仅对本次固定POD发布tick/monitor gate见证，不进入logger/业务锁。原普通Close仍用真正SetOffSignal与独立helper。

各案父等待上限25s（expiry40s），force death的exitcode、activation/grace/publisher/现普通tick/death tick、afterComplete和Last已Flush sentinel分别报告。sentinel仍不是UInk恢复。Negative wrongparent/no-inherited严格拒绝在任何产品初始化前，未验证不记PASS。

## 红绿/独立检查

root完整Debug|ARM64主Solution，>=5min/PATH修正；先RED expiry与allocation，保留FAIL/parent-owned清理。GREEN_IMPLEMENT后同cases和其它primitive自然运行，strictHeadless/PptCOM。源码及运行前auth/lifetime由独立checker核对后才能运行新信封。C-P2要真实调用点与正常ULW反例、原Armed渲染/Desktop/PPT停滞和fresh生产Load；此处暂未实现。

## 独立复核后验收细化

- expiry明确断言首grace在激活后的14990..15500ms，最终死亡沿同一个graceDeadline+15000绝对tick（迟到publisher不重新加15秒）；重复Begin间隔250ms再核不重置。
- ordinary是真正原监督的集成反例，本身不会隔离证明scope取min；新增earlier在auth-only publisher wrapper提供6秒的明确更早绝对tick且不Arm普通监督，实际production helper必须自行守该tick退场，错误无min会FAIL。该字段标injectedFlag，不叫真实普通Close截止。
- cancel现只验证旧Signal仍0；尚未覆盖下一scope及Begin/Expire CAS暂停竞争，留C00补强，不冒称全验收组完成。

## C00竞争/下一scope补强（待绿色源码新构建）

- cancel在真正Complete/join后新建独立nextScope，旧Signal Begin仍0、下一scope跨250ms重复Begin同tick，下一scope Complete后再等16s。记录next_deadline，不以同一个旧scope反复调用冒称代次隔离。
- begin-cancel：线程持进程寿命静态owning Signal，停在真实Begin CAS后/SetEvent前；主线程Complete让Cancel先赢并真join monitor，再释放producer gate并真join，旧Signal均0、等16s无fatal。测试premise失败必须join或自终止，不能让局部gate在活线程仍用时析构。
- expiry-cancel：monitor到原截止后停在Expired CAS前；主线程真实Complete看到已到期须竞争Expired并noreturn守原grace+15秒，不得返回下一次初始化。gate HANDLE和State保留到进程死亡；afterComplete=0、原绝对tick/专用码分别断言。
- 以上只测试生产primitive真实线程/CAS生命周期，不升级RTS provider/Host startup或保存恢复。独立增量safety及新Build后才能运行。
