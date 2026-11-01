# 核心实现与待执行验证

## 已实施接口
- Inkeys/Launch/IdtSingleInstance.h：目录volume/fileID presence、只读对象验真、固定wire Packet、精确父进程握手、能力三态及旧目标兼容桥，附无GUI私有copy专项。
- IdtMain：测试/监督器早退保留；readOnly Mini启动策略；Debug/Release同门且无Restart/WarnTry/SuperTopComplete参数豁免；已消费handoff不转发下一跳；更新成功/回滚改为连续接力。
- SuperTop：helper从精确父HANDLE复制lease，暂停final后Publish远端句柄，先Resume只运行Adopt预检，新Ready成功后才用原Ready/Accepted确认root退场；final在helper死亡前不进入业务初始化。final包的parent是helper（仍活且可验真），不是已经退出的root。
- Inkeys.rc：私有StartupHandoffProtocol=1。原版本/资源语言不变。
- ShutdownSupervisor由root接入原lease继承、同对象保活和新child接力，原绝对deadline保持。

## 关键先后顺序
child先验固定包尺寸/版本/句柄范围、精确父PID、父仍活且SYNCHRONIZE可用、父/自身物理目录与逻辑根关系、Mutant类型与当前Local解析对象全名；installer还在Ready前取得自己的物理目录guard。之后才Ready，再无限等待精确parent HANDLE死亡，然后收为全局presence。父等待Ready或child退出，超时只取消本次精确child，原lease仍保留。

## 第一轮真实失败的修正
Codex runner的Local对象实际位于\Sessions\1\AppContainerNamedObjects\SID，不是普通BaseNamedObjects。只读P/Invoke调用NtQueryObject class1/type2直接得到该证据。IsPresence改为OpenMutex(SYNCHRONIZE,Local+期望fileID名)引用，并比较完整Nt对象名和Mutant类型；不硬编码namespace，也不退化为名称后缀验证。

## 兼容边界
- 新目标marker=1：Ready协议；缺字段：暂停目标+显式复制lease+Resume的旧协议兼容；未知/损坏/资源读取错误：显式失败，绝不Ready超时后降级。
- 兼容桥只由已经过原update路径/哈希校验或安全rollback候选选择的调用点进入，不是普通参数豁免。旧目标沿其正常参数启动，不添加-Restart跳过旧版自己的门。
- 旧V2/V3自身后续SuperTop链没有新协议，无法声称其最终UIAccess实例持本次新lease；此为不可修改旧二进制的覆盖边界。兼容桥只确认OS创建、占位复制与恢复，不等于旧应用完成初始化。
- legacy直接运行installer保留原指令/哈希验证以及原最多2秒全映像等待；验真后再绑定根lease，且保留物理installer guard。新协议路径完全跳过该历史轮询，已经等精确父HANDLE。根准入拒绝直接退出，不能走fail删除有效update.json。
- NtQueryObject动态解析，无新增静态ntdll依赖；对象名类属于低层NT接口，Win7和真实UAC尚需实机，不以Win11 runner PASS替代。

## 验证入口
- Inkeys.exe --single-instance-tests：私有copy两跳真实接力、barrier双进程竞争、不同目录、allow→deny、坏包拒绝、最后占位退出后重开、私有VERSIONINFO真实1/2/缺字段三态。
- Inkeys.exe --config-boundary-test：缺省、严格bool、只读Mini不触发布局迁移写盘、持久化false回读。
- root新增 --shutdown-supervisor-tests 的restart-continuous-lease覆盖真实监督器继承链。
- GUI subsystem CLI必须Start-Process -PassThru -Wait -WindowStyle Hidden并读ExitCode，不能直接&后取旧LASTEXITCODE冒充通过。

构建/回归最终状态由root补充，本文不预先声称通过。尚未运行GUI、UAC或Win7，未commit/push。

## 能力fixture文件引用修正
已运行多进程测试并调用version reader的同一EXE，随后原位UpdateResource在runner返回ERROR_ACCESS_DENIED，常规权限也可见ERROR_OPEN_FAILED。独立P/Invoke用同sandbox新建、未执行和未查询的copy，修改相同raw604-byte资源offset532，Begin/Update/End均成功，排除了API/资源偏移错误。fixture现为unknown/absent/current各创建未加载的新文件，先更新资源再调用真实HandoffProtocol；生产分流没有放宽或改写。

## 物理installer接力fixture补充
无GUI copied EXE由top root父启动top/installer relay，再由relay接力回top root最终子进程；逻辑lease/事件/marker始终属于top。测试endpoint只允许私有temp root或其直接、非reparse的installer子目录。负例由driver先持物理installer对象，验证relay Adopt在Ready前拒绝、parent收到失败且仍持root lease，不写最终adopted marker；释放blocker后正例再完成两跳。没有执行真实更新入口或改动生产协议。
