# 2026-11-01 正常退出超时的最小定位准备

## 实测与授权边界

ROT修复版采样130949-399-0a27afde PID87084正确DLL351A...，15导航/7191事件/3564span/0issues，捕获一次rot-deferred再forced，最长926.754ms较上一轮1610ms小，但整体/Previous中位未降且节奏不同，不声称稳定改善。

进程exit -515702762 =0xE1430016，用户明确从Inkeys菜单/设置正常退出。此为15秒fallback强退，退出验收失败，不能归为用户手动结束，也不能由代码值判断外部helper创建失败。managed最后Perf约13:11:43.955，而强退约59.365，不能误称descriptor一直运行到强退。FullCleanup和主线程join/组件Shutdown暂无阶段标记，故不能指认为ROT/RCW泄漏。

继续原任务的安全退出验证，先加默认关闭数字阶段标记，准确区分卡点，不改释放/退出时序/15秒截止/安全配置，不无依据修关闭流程。用户人工操作仍由用户负责；AI只构建/无窗口测试，不自动commit/push。

## Native边界与格式

使用独立固定前缀[ShutdownTrace] v=1 source=native pid tid qpc frequency span event=begin|end stage固定词。不能与现有PptPerf source=native span序号冲突；独立前缀且统一inline计数器即可。记录仅固定词/数值，无路径标题、截图、COM读取。不走可能停用/阻塞的IDTLogger，复用安全原始stderr机制或小型无依赖WriteFile helper；任何输出失败不抛，必须保存/恢复GetLastError，禁用无格式化/QPC。

- SetOffSignal成功接纳首次关闭意图后，BeginShutdown/Arm之前只能捕获纯POD时刻，不能写日志/文件/等待；保持先建立独立监督的原合同。Arm与紧随GetLastError必须相邻，Failed时先进入原不返回截止守护，之后才允许写出缓冲marker；重复/非法请求仍原返回。Arm返回后的发布offSignal、RequestHide、Wake、CrashHandler/StopMagnifier按原顺序包阶段。若Arm永不返回，前段无持久化marker是保留的定位限制，不新增IPC或更改退出码/超时来绕过。
- 主线程退出序列：topmost取消、StartupPreview、Whiteboard、各thread join、StopProduct、Setting、Window、RenderPipeline、display/diagnostic/output、COM卸载等原步骤。仅原调用外加RAII边界，不增加等待或UI调用，不改函数/线程顺序。
- PPTLinkage收尾：infoThread join、Ppt.Shutdown、既有GetPptState等待/服务槽reset等（实际有哪些只包既有步骤，不新增清理行为）。
- 新header若计数器需跨TU共享，使用正确inline/全局归属；不依赖当前PptPerf私有Recorder。无可见控制台创建。

## Managed边界

复用INKEYS_PPT_PERF=1和现有PptPerfTrace（同前缀亦可，stage=shutdown-*），owner finally里的Mailbox.Stop、FullCleanup(true)、retired event retries；FullCleanup分别计时失效cache、UnbindEvents/CP、window/presentation/application SafeRelease及原纯值reset。final与rebindcleanup需可区分，不能把普通重绑误当退出。

只包原调用，不读COM来做诊断，不新增获取或改变Release次数/顺序/借用规则；actualdynamic、受限HWND例外、IErrorInfo和finally不变；不GC/FinalRelease。日志异常不影响cleanup。必要测试比较启用/禁用操作及释放账本和子span失败闭合。

## 所有权

- native实施：Inkeys/IdtMain.cpp、Inkeys/IdtPlug-in.cpp、必要小型ShutdownProgressTrace.h、对应既有无窗口headless测试（ppt_session_tests.cpp可用）。不改ShutdownSupervisor策略/窗口架构/产品UI。
- managed实施：PptCOM/PptCOM.cs、必要原managed测试；不改publicABI/计时协议/调度修复。
- 主会话：任务/spec/构建/手工退出交接；独立检查新诊断不会制造退出问题。

## 验证与人工交接

Managed core及真实finally ledger/异常Writer/IErrorInfo；native helper默认关闭/LastError保持/输出失败/嵌套配对；原完整Debug|ARM64、同次DLL/TLB和EXE资源、入口PrepareOnly无GUI不改配置。分析器可忽略新ShutdownTrace前缀，单独按qpc查看最后未闭合阶段，不能因此把导航分析标正常退出通过。

用户沿同一入口启动，按原菜单/设置正常退出一次即可；若需要保留PPT附着情形，可只进一次放映再退出，不要求重复大矩阵。不能自动kill/开关Office来制造验收通过。新数据前只报告退出定位准备完成，关闭故障未修复。
