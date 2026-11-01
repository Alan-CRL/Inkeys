# 启动合同独立压力审查

日期：2026-11-01。角色：trellis-check，只读源码审查；未改产品、未运行编译、GUI、UAC或commit。

## 已直接确认的危险交错

1. **P1：仅删除 SuperTopComplete 豁免会把合法接任误判为重复。** IdtMain.cpp:1721–1795 的旧门先于 SuperTop；IdtSuperTop.cpp:307 与 IdtToken.h:238–279 当前是在 helper 中创建暂停final，Ready→Accepted→等精确父句柄退出→Resume。父持有占位而final开始Create时仍可能存在helper的接力句柄；final必须验真并认领同一占位，不能无条件检查ERROR_ALREADY_EXISTS就退出，也不能恢复永久参数豁免。

2. **P1：释放占位后再让final重新创建存在抢跑空档。** 父先Close/exit→竞争普通启动取得新对象→final被拒。更稳妥的接力是同一presence对象在parent/helper/child之间至少保留一个句柄，并且只有验真本次交接的child能够认领；若父握手失败则父保留原句柄继续运行。mutex线程所有权不能跨进程直接转移；presence对象句柄寿命与mutex owner是不同概念。

3. **P1：installer链当前不能保证旧进程退出及新实例准入。** IdtMain.cpp:1960附近的isProcessRunning(old_name)+20×100ms无法替代精确父HANDLE，超过约2秒会误走fail；2033附近ShellExecute新根返回>32只证明Shell接受启动，后续单实例拒绝仍可能导致installer退出且没有产品。2070附近回滚与2230附近根→installer同类。交接必须携带逻辑安装根并保留其占位；不能按installer子目录为独立安装，也不能把更新链全部豁免。

4. **P1：allow=true不登记会破坏关闭后的限制。** 三个允许模式进程运行→持久化false→第四个Create首次创建对象成功。这会违反R2。所有允许模式实例也应持有presence；关闭开关不强退老实例，但新请求只要看到其中任意一份仍在就拒绝。

5. **P1：普通重复启动不能先执行更新或提权。** 仅将门移到SuperTop之后能保护final，但两个启动都可能在门前改installer/update.json、替换EXE或弹UAC。普通请求应在这些副作用之前获得目录准入；仅验真内部接任走接力。

## 可复用原语与实现限制

- ShutdownSupervisor.cpp:515–585已有精确父HANDLE、GetProcessId、可继承标记、SameExecutableFile、握手确认和父signaled后一次StartSameExecutable。保留该路径及15秒deadline，不改成Restart命令行即放行。现有SameExecutableFile不能直接授权新旧更新EXE（它们物理文件确实不同），更新需另核既有目标路径/哈希和发布者身份。
- IdtToken.h:66–80 的GetProcessToken仅取QUERY_LIMITED_INFORMATION|SYNCHRONIZE；若复制句柄要补PROCESS_DUP_HANDLE，不能假定当前句柄已有权限。
- IdtToken.h:249–279 的CreateProcessWithTokenW没有普通bInheritHandles机制。不能照搬CreateProcessW继承列表；可由helper暂持占位并针对已创建暂停final做明确DuplicateHandle/验真接力。具体凭据不能仅为公开可猜命名event或任意-Pid。
- 命名对象默认CreateMutexW会按ALL_ACCESS打开已存在对象；高完整性创建、普通完整性复开时可失败。presence读取尽量采用最低所需访问，ACCESS_DENIED不能误当无人运行。UIAccess/管理员的真实DACL/MIC结果需要单独记录，不能由普通进程测试推出实机通过。
- Config::ReadMini可在早期调用但依赖globalPath。更新helper必须读逻辑安装根的main.json；若读installer/Inkeys/Config/main.json，allow=true会被误读为false。坏配置按false，不为早期读取写默认配置。
- 路径键建议基于真实目录身份，保留Local/session及用户边界；大小写、长路径和目录别名不可被旧替换/255截断任意分裂或碰撞。禁止按EXE名归并不同目录。

## 无GUI多进程最小验收矩阵

| 场景 | 真实原语/顺序 | 必须观测 |
|---|---|---|
| 16个同键并发false | 启动无窗worker，统一barrier释放 | 恰好一个admit，其余duplicate；不靠sleep制造串行 |
| 不同目录 | 同时两个真实目录键 | 各自admit |
| true、true、false | 三个worker分别保留真实presence句柄 | 第三拒绝；第一个退出仍拒绝；最后一个退出后恢复 |
| 异常死亡 | 精确终止隔离worker并等待其HANDLE | 对象无残留占位，下次admit |
| 接力竞争 | 父→暂停子已备妥→普通竞争者→父退出→子认领 | 竞争者始终拒绝，子成功，整个窗口对象连续存在 |
| 接力失败 | 子创建/凭据/确认失败或超时 | 父仍有保护，子取消且不会迟到启动 |
| 延迟旧退出 | barrier延迟父signaled而非固定sleep | 子产品准入在父真正退出之后；原deadline不重置 |
| 更新/回滚 | 私有复制树的真实入口分流与handoff | root身份沿链不变；Shell成功但child拒绝不能当PASS |
| 伪造凭据 | 错父HANDLE/PID/镜像/目标/nonce、过期数据 | 不跳门、不清未知文件、不弹产品GUI |
| 权限失败 | 可控失败注入+真实最低权限对象 | 明确错误，不当成首次；实际UAC/UIAccess标为待实机 |

上述测试应复用生产占位与交接函数。仅测试纯policy返回值或将状态位直接设成“已交接”不足以证明本次最高风险已关闭。

## 针对实施者状态表的二轮压力审查

已读research/admission-state-table.md。目录VolumeSerial/FileIndex作为对象键、连续presence lease、CreateProcess暂停后明确复制、只读handoff包和Ready后等待parentdeath方向可行。以下为验收前必须明确/检查的边界，并非已确认实现缺陷：

- 表中Restart/CrashTry写“普通准入，不豁免其他活实例”。两份allow=true老实例→关false→其中一份Restart，旧的这份死后仍有另一份presence，新的这份会被拒。这与“合法替换保证”和“关后不新增”的产品语义需主代理明确；若承诺替换本份仍成功，应沿监督helper接力本份lease而非仅普通准入。
- NtQueryObject应验证Mutant类型、当前session命名域及逻辑目录ID对应完整名称，不能只检查可伪造后缀。动态解析失败closed应有独立结果，不能错误提示已运行。
- root→installer与installer→root/回滚应检查明确路径角色及既有有效更新指令约束，不能凭父镜像“处于root下”或目录组件“installer”普遍认亲。
- Ready发布前验完整包、目标PID、parent HANDLE/PID/镜像、root实际身份、lease身份；Ready发布后才等parent。父只在Ready后转移，防止双方先等对方退出的死锁。
- high helper创建的可修改event可能被medium+/UIAccess final的MIC写入规则拒绝。尽量保留原medium root创建的Ready/Accepted，或使用明确适用的对象label；不需要final写的共享包只申请只读。

## ShutdownSupervisor 接线专项静态复核

已读取新增约45+/10-行diff及同次工作区IdtSingleInstance.h接口；此时core仍在编辑，不能作为最终冻结版本全范围审核。

- real relay识别为argc==9且argv[7]以--instance-lease=开头；testDirectory在该情况下为空，测试的8/9参数路径仍原样走IsTestDirectory和原test开关校验。Close拒绝relay，原无lease 7参数模式保留。
- relay仅!testDirectory且非Close且已有presence时启用；父端复制SYNCHRONIZE、可继承lease并列入显式HANDLE_LIST。helper核继承标志、root、同文件父镜像和IsPresence后接管句柄；未改变原精确父HANDLE校验。
- 原deadline在创建/握手前固定，Arm fallback/强退/等待真正signaled的代码未改。新增接力创建发生在旧产品进程已确认死亡之后。
- Handoff默认父为supervisor；新product Adopt发布Ready后等待supervisor死亡，helper Wait先观察Ready，然后return0经wWinMain early分流退出。未见Ready前等parent造成的互等。
- SYNCHRONIZE-only句柄足够保活对象；NtQueryObject查询不需要MUTEX_MODIFY_STATE。仍要求使用真实低权限duplicate做实际查询回归，不能仅由ALL_ACCESS句柄测试推导。
- **覆盖要求**：原testDirectory fixtures有意不relay，所以原shutdown tests通过不能单独证明新增45行路径通过。需要真实无GUIrelay worker调用相同启动/解析/Publish/Adopt链，并覆盖低权限lease、关多开后仍有另一实例、重复请求和无效lease。

暂未见该文件本次接线的阻塞性源码问题。header角色/目录/权限/包验真仍归core完整实现后的全范围检查；未运行编译或进程测试。
