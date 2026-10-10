# P0 全范围 Trellis check 复核

> 2026-10-10；结论：**PASS（仅P0交付范围）**。本次复核直接执行，不递归派发。P1-P5、生产helper、多源仲裁及真实Office/权限/Win7验收未实施。

## 审阅范围与结果

已读取本任务PRD/implement/validation、research全部证据与探针、check.jsonl六项实际文件及父任务PRD/design/implement；对照COM、native session/UI3、构建spec，并核对既有测试构建/运行日志。

- P0-1：26个生产源码/工程/随附资源逐个长度与SHA-256匹配；dynamic长期根、ROT单赢家、可选事件与轮询、既有owner/直接命令/回调线程边界、temporary及root释放分开记录。实际apartment、损坏COM和Office释放没有冒充运行结论。
- P0-2：首发选择本机双向命名管道。当前NetMQ不含net40，clrzmq4有v4.0候选；没有推导全部ZMQ不可用，也没有为了通信升级运行条件或新增依赖。Win7最低API资料与本机实际运行证据分开。
- P0-3：最终探针通过11/11；Unicode双向、分片读取、三种非法长度分配前拒绝、半头/半body EOF、两次全量重连、真实pending连接/读取取消、owner Join、absent peer timeout及新增读取deadline/EndRead收束有具体断言。各用例/Win32 peer退出0，没有把无异常当作全部通过。
- P0-4：根agent以ARM64原生MSBuild构建既有PptCOM.Tests，Release AnyCPU Rebuild与测试退出0；日志显示实际重建DLL/TLB，未复制到Inkeys。复核不重复已通过的无关完整native构建，也未使用来源不明的旧headless二进制。
- P0-5：活动事实归统一抽象层；当前Active/PID/WPS分值、单赢家及新session自动Focus路径明确。源码16/50/100/500ms等周期不是端到端时延，防抖参数留后续可控时间测试与实机量测；正常页事件不等待绑定回滞。
- P0-6：真实net40引用编译、Windows11 ARM64/较新CLR运行、历史日志与修后日志明确分开。Win7/原版.NET4、双向跨权限、损坏COM、真实资源趋势与页呈现时序均标NOT VERIFIED。

## Findings (fixed)

- File: `research/probes/NamedPipeP0Probe.cs`
  - Issue：ReadPart/WriteFragmented超时Dispose后等待完成，却未调用相应EndRead/EndWrite；超时路径的APM生命周期未完整配对。
  - Fix：取消后在有界等待确认完成时恰好调用EndRead/EndWrite，接收预期关闭异常后再退出并关闭已完成的wait handle。新增`read-deadline-abort`：Win32 peer连接后不发数据，读取真实等待3000ms至deadline；断言超时、EndRead收束计数1、阶段耗时有界及peer正常EOF退出。写端拥塞实际取消仍留P3门禁。
- File: `research/probes/NamedPipeP0Probe.cs`、`research/probes/run-probe.ps1`
  - Issue：Kill后的WaitForExit没有时限，进程兜底路径可能再次变成无界等待。
  - Fix：suite、peer Dispose和PowerShell runner均限定终止后最多3000ms等待，失败仍返回/抛出非零结果；只处理本探针创建的进程。runner新增有字符约束的RunLabel，把复核输出隔离在仓库既有忽略目录，避免覆盖原30/30日志。
- File: `research/probe-results.md`、`validation.md`
  - Issue：修后增加用例，原三轮10/10不能继续当作当前源码的执行结果；最终check状态也需更新。
  - Fix：记录历史源码hash/30个PASS与当前源码hash/一轮11个PASS，分别链接日志位置，注明读取deadline的实际断言与残留写端门禁。validation改为最终全范围复核通过。

父任务两处旧“仍仅规划/只建立一个规划任务”措辞及P0参数量测边界已反馈根agent，并由根agent同步；没有并发改写父文档。

## Findings (not fixed)

以下是任务明确的后续运行准入与设计工作，没有在P0中猜测实现或声称已通过；无P0范围内尚未修复的机械问题。

- **NOT VERIFIED**：Win7 SP1 x86/x64 + 原版.NET4运行；当前net40引用编译不替代旧运行时执行。
- **NOT VERIFIED**：高host→普通helper、普通host→高helper、UAC取消/不同管理员凭据、对象完整性、实际SID/logon/session、peer鉴权和远程拒绝。probe只有当前SID FullControl实验ACL，没有Everyone，未提升、未runas；它不是生产最小权限模板。
- **NOT VERIFIED**：最终native server/managed client、写端拥塞中止、父进程失效/heartbeat、epoch与真正broker全量恢复。当前Win32客户端由C# P/Invoke执行，不能冒充生产C++ adapter。
- **NOT VERIFIED**：真实PowerPoint/WPS受损COM发现、无事件轮询、多show/多实例、反复启停、实际RCW/连接点释放与进程/内存趋势；fake ledger不覆盖真实Office。
- **NOT VERIFIED**：event→cache/source→仲裁→target→Present→UI ack/input ready端到端时延、P95/光学测量及最终防抖数值。未为了测量启动GUI。
- 原线程模型、旧接口/ABI及新session Focus行为按基线保留；后续接管与被动换源抑制焦点反馈需要P1/P2/P4合同和真实验收，属于设计/产品边界，本次不重构。

## Verification

- Lint：**PASS（适用静态范围）**。PowerShell AST解析0错误；本任务UTF-8/CRLF/尾空白检查、JSONL目标存在性及`git diff --ignore-submodules=all --check`通过。仓库没有本探针登记的独立lint项目，未新增lint工具/依赖。
- TypeCheck：**PASS**。使用既有VS Roslyn与真实v4.0 mscorlib/System/System.Core引用，`/noconfig /nostdlib+ /langversion:4 /codepage:65001 /target:exe /platform:anycpu`重编译退出0；compile.log无诊断。PowerShell AST同时通过。
- Tests：**PASS**。最终独立执行`run-probe.ps1 -Repeat 1 -RunLabel check-final`，编译0、runner0、11个case各0、所有native peer各0；摘要`passed=11 failed=0 total=11`。读取deadline段实际3040ms、收束计数1，取消读取ownerAlive=False。既有PptCOM.Tests本轮重建/运行日志PASS、退出0（根agent执行并提供退出码，本复核核对日志）。
- Baseline：**PASS**。26个生产文件/随附产物SHA-256不变，Git状态只有两个任务目录；没有生产/工程/接口/部署变更。
- Evidence：历史三轮10/10日志保留在`TestResults/pptcom-p0/probe/`，历史源hash=`f4a51a52deb4549c63fe4776c1efe6e65283ad4b8a9cff6a51fc98d6657ccbb7`。最终一轮11/11及环境/编译日志在`TestResults/pptcom-p0/check-final/`；当前源hash=`4426576940c348f46238203e9abd6297e95b5b15b34a69aa474edd51576b24be`。默认沙箱访问拒绝日志单独保留；自动审批允许在普通本机上下文执行，没有修改SID ACL以绕过失败。
- Archive-path：runner从其当前位置逐级寻找solution与Trellis锚点，静态确认任务归档增加目录层级后仍可定位仓库；复核执行来自当前活动任务位置，未冒充已经归档后的运行。
- Spec sync：本轮不改变生产API/协议，修复仅限实验探针；事实/限制落在研究与父任务。probe帧格式、FullControl ACL和测试时限没有写成已生效全局产品合同。

P0可以按自身交付定义完成；该结论不代表后续实机门禁通过，不授权开始P1、GUI、commit或push。
