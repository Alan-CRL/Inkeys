# P0 命名管道无窗口探针结果

> 2026-10-10。仅验证隔离通信探针；没有启动 Office/WPS、Inkeys、UAC 或 GUI，不改变生产源码/工程/DLL/TLB。

## 可重复执行与证据

- 源码：[NamedPipeP0Probe.cs](probes/NamedPipeP0Probe.cs)；运行脚本：[run-probe.ps1](probes/run-probe.ps1)。两者均为 UTF-8、CRLF。
- 在本任务实际位置执行 `& '<当前任务路径>/research/probes/run-probe.ps1' -Repeat 1 -RunLabel check-final`。历史三轮使用`-Repeat 3`与默认`RunLabel=probe`；标签只允许字母、数字及连字符，输出仍限定在仓库忽略目录。脚本向上查找 `InkeysRepo.sln` 和 `.trellis/workflow.md`，任务归档改变目录深度后仍能定位本仓库。
- 使用现有 Visual Studio Installer 的 `vswhere.exe` 定位 C# 编译器；不新增 csproj/solution、不安装依赖、不使用下载的包。
- 编译参数为 `/noconfig /nostdlib+ /langversion:4 /codepage:65001 /target:exe /platform:anycpu`，显式引用本机 `Reference Assemblies/Microsoft/Framework/.NETFramework/v4.0/{mscorlib,System,System.Core}.dll`。编译退出码 **0**；这证明所用构造重载、APM、PipeSecurity 和 Thread API 存在于真实 .NET4 引用程序集，不证明本机运行的是原版 .NET4。
- 历史输出保留在`TestResults/pptcom-p0/probe/`；最终复核输出隔离在`TestResults/pptcom-p0/check-final/`：`compile.log`、`environment.log`、`run-1.stdout.log`与stderr。环境日志含源码及三个引用程序集SHA-256，stdout含每用例PASS、CASE_EXIT、native PEER_EXIT和总数。复核没有覆盖历史三轮或沙箱失败日志。
- 历史三轮与最终复核均来自自动审批允许的 `require_escalated` 命令执行上下文；这里是离开 Codex 命令沙箱执行已授权的无窗口探针，没有通过 `runas` 提升 helper，没有 UAC、跨权限或新管理员凭据测试。

## 实际本机结果

现有机器为 Windows11 ARM64；只读注册表记录 build `26200.9457`、DisplayVersion `25H2`，runner 环境 `PROCESSOR_ARCHITECTURE=ARM64`。当前安装框架登记 `Version=4.8.09221 / Release=533509`；probe 指针宽度64，实际 CLR `4.0.30319.42000`，载入系统 `Framework64/v4.0.30319/mscorlib.dll`。这些值是当前已安装 .NET4 系列 CLR 的证据。

没有 manifest 的旧框架程序将 `Environment.OSVersion` 报为 `6.2.9200.0`，注册表 ProductName 仍为 `Windows 10 Home`；报告没有将它们误当实际系统产品版本。沙箱中的 CIM 系统查询受到访问限制，脚本采用只读注册表记录 build，不以修改生产 manifest 或源码绕过环境限制。

修复前源码SHA-256为`f4a51a52deb4549c63fe4776c1efe6e65283ad4b8a9cff6a51fc98d6657ccbb7`，历史三轮各**10/10**，共**30/30**。

最终复核修正异常路径APM收束与终止后的等待上限，增加真实pending读取deadline回归；当前源码SHA-256为`4426576940c348f46238203e9abd6297e95b5b15b34a69aa474edd51576b24be`，真实net40引用重新编译退出码**0**，独立一轮**11/11**。所有用例独立进程退出码0、每个Win32 peer退出码0，脚本退出码0。原30/30只对应历史源码，不作为修复后执行次数。

| 用例 | 实际断言 | 最终复核结果 |
| --- | --- | --- |
| native-duplex | managed `NamedPipeServerStream` 与独立进程 Win32 `CreateFileW/ReadFile/WriteFile` 客户端双向通信；汉字、emoji、Unicode路径逐字符串匹配；双方分片写，header每次最多读1字节、body最多3字节，managed实际28次读；ACK精确匹配 | PASS |
| oversized-length | 对端仅发送 `1MiB+1` 长度头；先拒绝长度，payload分配计数0 | PASS |
| negative-length | 对端发送 signed LE int32 `-1` 长度头；拒绝且payload分配计数0 | PASS |
| zero-length | 探针协议要求非空payload，发送0长度头；拒绝且payload分配计数0 | PASS |
| half-header | 对端写2/4字节header后退出；识别header EOF且不接受半帧，payload分配0 | PASS |
| half-body | 对端声明128字节而写2字节body后退出；识别payload EOF且不接受半帧 | PASS |
| reconnect-full-state | 同一端点名先后新建两个server实例/peer进程；分别核对文稿A/epoch1及B/epoch2完整状态和ACK，peer关闭产生EOF | PASS |
| cancel-connect | 没有client的`BeginWaitForConnection`确为pending；Dispose后APM回调/End完成，观察ObjectDisposedException并有界返回 | PASS |
| cancel-read-owner-stop | 已连接的闲置client不发数据；owner进入pending BeginRead，Dispose使读取EOF/中止，owner完成并Join，确认ownerAlive=False | PASS |
| client-connect-timeout | 不存在的随机端点；.NET4已有`Connect(120)`实际抛TimeoutException，耗时在断言范围80..3000ms内 | PASS |
| read-deadline-abort | Win32 peer连接后闲置、不发数据；managed pending读取达到3000ms deadline后Dispose，完成EndRead收束计数1，再抛TimeoutException；peer观察EOF正常退出0；实际deadline段3040ms | PASS |

每次APM等待/中止完成等待及join上限3000ms（deadline中止路径包含先等deadline、再等取消完成），用例进程独立watchdog9000ms、suite等待用例10000ms，脚本等待suite120000ms；Kill后的进程等待也最多3000ms，超时为非零退出。进程监督只终止自己创建的probe/peer，不查找或结束Office/产品进程。先前失败期间的pending APM清理已修正；本次最终复核进一步确认ReadPart/WriteFragmented超时关闭后，在操作完成时恰好调用对应EndRead/EndWrite，再关闭完成的AsyncWaitHandle，不以Dispose代替End调用。读取deadline路径有新增运行断言；写端背压中止仍未实机验证。正常路径不靠watchdog或Kill结束通信owner。

## ACL 与初次沙箱失败

探针server构造显式受保护DACL，只Allow **当前用户SID的FullControl**，没有Everyone、其他用户或宽松组规则。FullControl为隔离同用户实验确保请求掩码一致；**不作为生产跨权限helper的最小ACL模板**。Win32 client请求 `GENERIC_READ | GENERIC_WRITE (0xC0000000)`，该权限在上述规则内包含。后续最小ACL必须分别核对具体ReadData/WriteData/Synchronize等访问位，不能忽略`FILE_GENERIC_WRITE`中的`FILE_CREATE_PIPE_INSTANCE`同值位。探针没有实现native服务端`PIPE_REJECT_REMOTE_CLIENTS`。

在默认Codex沙箱中，首轮 **2通过/8失败**：创建server及cancel-connect/absent-endpoint-timeout可执行，但Win32打开管道返回 `ERROR_ACCESS_DENIED=5`，连同进程managed client也无法连接。带诊断的失败日志保存在 `TestResults/pptcom-p0/probe/sandbox-failure/`，包含`token-diagnostic.log`；当前SID、FullControl及server `IsTokenRestricted=False`均已记录。没有据此猜测某个具体restricted SID/token机制。

不修改这份SID ACL或Win32访问掩码，将同一探针放到自动审批允许的普通本机命令执行上下文后，三轮全部通过。因此本记录将初失败限定为 **Codex执行上下文限制**，不把它算作net40缺API、PptCOM/COM释放缺陷、生产跨权限成功或失败，也没有通过扩大到Everyone掩盖问题。

## 结论边界与后续门禁

- 本探针采用 LE int32长度 + 非空严格UTF-8，用1MiB上限演示有界读取；不冻结未来产品协议、不实现broker、鉴权、候选优先级或COM命令。
- 这是托管server对Win32 ABI客户端的真实跨进程互通；客户端由同一C# probe通过P/Invoke调用Win32，并非已经构建/验证了生产C++适配器。
- 首次连接、ACK和关闭耗时包含子进程启动/JIT；不是Office PageChanged到UI延迟，不支持P95/资源趋势或防抖参数承诺。Dispose的本机观测只证明本探针owner能有界停止，不替代保留COM owner/RCW释放路径的要求。
- **NOT VERIFIED**：Win7 SP1 x86/x64上的原版.NET4运行；管理员host↔普通helper双向完整性/ACL、不同管理员凭据、logon session和真实peer身份；远程客户端拒绝/恶意预连接；实际PowerPoint/WPS、损坏COM、Office退出与内存释放；生产broker重连/去重/活动切换/快速页事件。
- 这些是P3/P4/P5上线前实机门禁。历史30个PASS及最终11个PASS与真实net40引用编译不代替它们。
