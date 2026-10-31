# 实施与验证记录（2026-10-18）

## 当前结论

代码修复、独立审查及自动化验证已完成：托管双架构、完整native solution与最终headless均通过。真实提供方兼容与退出/下一文稿验收保持NOT VERIFIED，任务不归档。没有Office激活、GUI/Computer Use、commit或push。

## 修复范围与原因

- 常规生产属性、Item、方法和setter改用实际C# dynamic；保留内部可注入测试边界。HWND按用户确认保留类型/受限标量例外，事件使用独立内部适配；不将普通属性反射当成dynamic。
- 原新会话的HWND总准入会阻断仍能dynamic翻页的设备；现区分基础控制、窗口能力和墨迹身份。无可靠HWND时保留新鲜基础页码、上下页及同场退出，焦点/自动接管/墨迹继续要求可靠证据。
- 原busy缓存语义和稳定3秒复核deadline不一致；现统一legacy/session当前状态，独立约100ms描述符重试和500ms维护，旧显示不能冒充新可写页。UI内部pageAckEpoch随实际frame/commit回传，拒绝同页恢复时迟到旧帧；EndScreen还核对fresh raw页/总数。
- 修复ROT候选替换双释放、同identity新取得漏释放、比较/轮询临时返回、View/Navigation链式对象等。每笔取得只转移或释放一次，owner串行使用长期root，getter只读缓存。
- 以raw IDispatch sink+显式IConnectionPoint/cookie替换会物化参数RCW的CLR/PIA事件包装；回调只处理DISPID/代次提示。失败解绑的独立CP转入静态registry，后续owner可继续重试，server reset不丢失所有权；不以GC或FinalRelease补漏。
- fresh Count/show identity负责结束/重开；迟到Begin/End不直接重放场次。明确断连及Presentations.Count=0触发旧binding清理，避免最后文稿关闭后根对象永久保留。普通缺失/busy不猜结束。
- 实施验证捕获了新增诊断的IErrorInfo副作用：native8001010A会被前序GetHRForException保留的InvalidCast异常覆盖。产品日志已改纯读错误码，保持原fixture行为后修前失败/修后通过。清理路径另有throwing TextWriter回归，保证诊断输出异常不中断Release或丢失CP。

## 自动化结果与日志

日志均在忽略目录Build/Validation/pptcom-dynamic-20261018，测试源可在仓库重新构建。

| 检查 | 实际结果 | 主要日志 |
| --- | --- | --- |
| 首轮红测试 | 原生产代码恰有dynamic-only读取/命令及HWND0退出3项失败 | red-managed-* |
| 最终PptCOM.Tests Release/AnyCPU（含PptCOM） | ARM64原生MSBuild，exit0，无warning | managed-verified-build.* |
| x64 CLR实际生产COM夹具+core | 43 cases/0 failures、core PASS，进程exit0；Framework64 CLR PE8664 | managed-verified-x64.* |
| ARM64 CLR实际生产COM夹具+core | 43 cases/0 failures、core PASS，宿主exit0/EntryPoint0；FrameworkArm64 CLR PEAA64 | managed-verified-arm64.* |
| native夹具x64/ARM64 | 现有v143/SDK26100编译exit0、0warning；真实IDispatch/CP、无需注册和Office | Build/pptcom-dispatch-fixture下各架构日志 |
| 完整InkeysRepo.sln Debug/ARM64 | exit0，ARM64 MSBuild与PreferredToolArchitecture=arm64；最后增量24.27秒 | solution-final-debug-arm64.* |
| InkeysHeadlessTests.exe --no-window | exit0、112.26秒、stderr空，PASS animation correctness | headless-no-window-unrestricted.* |
| 类型库ABI | 完整TLB与原始文件字节一致，两个组件接口/方法顺序及旧委托导出保留 | 见下方哈希 |
| 最终EXE嵌入DLL | 现有Assert-EmbeddedPptCom按数据资源读取通过，未执行主程序 | A9A2A059...与交接DLL一致 |
| diff/格式 | diff --check通过；现有BOM/checkout CRLF保留；新源码UTF8/CRLF | format-check.json |
| Trellis上下文 | implement/check各6入口validate通过 | task.py validate输出 |

夹具包括正常类型信息、GetTypeInfoCount=0、GetTypeInfo故障、typed QI拒绝、同identity重复取得、16处普通错误/busy、连续扫描与命令，以及真实连接点事件。104次含重复同identity参数的通知没有增加RCW持有；Advise失败、Unadvise一次busy后同cookie重试、Deactivate迟到通知均验证。最终10个被测节点销毁、引用归零，未以GC/卸载DLL/等待Office自然退出达成。

实际运行时为本机.NET Framework4.8.1的CLR4（4.0.30319.42000），目标仍为.NET Framework4.0；不把本机运行等同原版.NET4.0/旧Windows验收。

## 构建/运行环境故障的对照

- 首次普通sandbox完整构建exit1/C1902，79.04秒，多个未改模块也失败。cl=19.44.35229，mspdb140/core/srv均14.44.35229，安装组件版本匹配。
- 全新无header、仅一个函数的/Zi /FS探针在普通sandbox同报C1902/exit2；同命令、同环境参数经自动审批在sandbox外exit0。证据pdb-probe-sandbox.log与pdb-probe-unrestricted.log，归因本轮PDB服务/IPC执行边界，不是源码或旧PDB版本。
- 相同完整solution源码/配置在允许PDB服务的环境构建exit0（114.98秒），最后guard修复后再次完整solution增量exit0（24.27秒）。没有改SDK、依赖、工程或关闭PDB检查。
- 相关微软说明：[Fatal Error C1902](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/fatal-error-c1902?view=msvc-170)。本轮sandbox归因来自本机对照，不仅由错误文字推断。
- 普通sandbox headless实际exit1/108.69秒/35项失败，均属Config/启动偏好/原子写临时文件链；未修改这些代码或降低断言。最终相同EXE/参数在允许临时文件写入环境的结果以上表为准。
- solution保留既有第三方hashlib/HiMsg、其它headless代码的类型转换/布局等warning；最后增量仅hashlib++的C4267。输出有既有pwsh.exe PATH提示，实际solution退出0；未因这类提示修改工程/全局配置或无关源码。

## 配套产物

- Inkeys/PptCOM.dll已显式从同次Release输出交接：A9A2A059A6B599E5ECF3D67F317C0DEBD0764DE0DC78032A634BED79754084E2。
- Inkeys/PptCOM.tlb：C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606，与最初文件完全一致。只保留旧PIA事件委托typeof元数据锚，不重新使用旧事件包装。
- Build/ARM64/Debug/Inkeys.exe：5F97DC0A784C522E223FD94488BF6071BD1281DC561A5F7834860248B46C57AB；嵌入DLL与上述交接文件一致。
- Build/ARM64/Debug/InkeysHeadlessTests.exe：C8643976A1ACAE6B993693425D9B808C81C6A7E920A40AE731A6DFC4696EDE64。
- 未增加COM GUID、重排vtable或改变descriptor12字段/session7字段，.NET/PIA/依赖版本不变。

## 兼容性与剩余验收

- PowerPoint2007/MSO12、PowerPoint2010/MSO14、WPP/WPS2013：保留现有声明，逐功能静态矩阵见legacy-version-compatibility.md；无目标提供方实机，均NOT VERIFIED。
- 本机现代Office只有只读文件/注册盘点；没有本轮实际Office激活、放映、跨进程事件、退出或下一文稿验收。in-proc原生COM夹具不等同Office/WPP真实跨进程Automation proxy。
- typed QI失败但IDispatch可用的夹具功能通过；Count=0类型信息模式仍可用，Count=1但GetTypeInfo返回TYPE_E_CANTLOADLIBRARY时保留80029C4A安全Unavailable。dynamic不修复所有类型库、ROT/权限或COM激活问题。
- 无独立进程锚/放映类证据时Win32回退返回0；不猜WPP窗口类，不以唯一标题授权墨迹。仅基础控制降级。
- 原多实例同文件路径的保留binding偏好、同COM identity/HWND重开完全落入观测间隙、legacy Next/Previous无expected-session参数、raw元组跨层完整同步边界均未无证扩大承诺。
- 外部COM持续拒绝Unadvise时，首次失败后停止阶段再作最多3轮显式重试，仍失败返回-1并保留可重试CP/cookie；已进入外部COM的阻塞调用不能由本地timeout撤销。未把未完成清理伪报成功。
- 真实关闭/退出后进程及时消失、立即打开下一文稿、各旧提供方事件/轮询及墨迹全链仍待单独设备验收；任务保持in_progress，不自动归档。

## 用户指示收尾（2026-10-18）

用户在自动化验证和独立检查完成后，明确要求“为改动提交commit并结束相关任务”。据此先提交修复、测试、规范和本任务证据，再归档任务并记录会话；不执行push。此收尾不代表新增设备验收：PRD AC10及implement第7节的PowerPoint2007/2010、WPP2013、现代Office、损坏COM反馈设备、真实进程关闭与下一文稿仍为NOT VERIFIED，保留原有未勾选项和结果边界。本轮不再修改生产代码，不运行GUI/Office激活。
