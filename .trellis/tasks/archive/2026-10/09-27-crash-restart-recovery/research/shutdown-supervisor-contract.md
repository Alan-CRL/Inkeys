# F-043 统一 15 秒外部保护接口合同

用户选择无条件 15 秒最终结束；即使保存 worker 仍有未 durable 请求，到期也优先终止，但不能破坏此前 durable 的 PPT 主/备 index 与 UInk。F-044 版本化物理文件故障修复/验证是产品激活此 supervisor 的前置门；不得在 F-044 未闭环时宣称“旧恢复点安全”。

实现分工：独立 implement worker 仅拥有新 `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp/.h`、必要的自包含 no-GUI 测试代码；主 agent 唯一写 `IdtMain.cpp` 的最早内部模式分派、`SetOffSignal` 首请求仲裁/arm、旧直接重启删除、`Helper.CrashHandler.cppm` 与 Window Service 非阻塞 Hide 接线、工程文件/filters。先冻结头文件导出 `ArmShutdownSupervisor(Close|Restart, 15000ms)`、`RunShutdownSupervisorChild(...)`、显式测试入口，再串行集成。helper 不读取真实配置/文档、不调用 UI/COM/Draw3/UEF。

精确父进程 handle（非 PID/进程名）通过 `DuplicateHandle` 与 `STARTUPINFOEX` HANDLE_LIST 单独继承；helper 早期校验父 PID、同一可信 EXE 路径及模式参数，缺合法继承 handle 立即返回。15 秒用单调时间，从首次正式接受请求起算；自然父进程结束时取消强杀，Restart 仅在确认旧进程句柄 signaled 后由 helper 一次性用绝对 `lpApplicationName` 启动 `-Restart`。到期先仅终止此精确旧进程，等待其真正终止；未终止则禁止启动新实例。helper 无 GUI/进程名扫描/文件心跳、不继承 mutex/文件句柄、不新建提升路径，Win7 SP1+仅 KB2670838 仅使用有效 API。

主入口任何 Close/Restart/受控 SetOffSignal 非零都经同一首次请求仲裁；确认框取消不进入。请求的发布/arm 不能排在 Window Service 同步 Hide 或业务锁之后。正常旧进程尾部不得再 ShellExecute，避免双启动。用户曾提的历史15秒未由可得 Git 对象证实：旧代码仅约10秒轮询/日志，且旧按名杀进程助手已移除，不能恢复旧实现。

测试只对本任务创建的隔离子进程：自然 Close、自然 Restart 测试模式、卡住后 15 秒真实强杀、重复请求、损坏内部参数/句柄、空格/中文路径与旧进程终止后新进程至多一次。测试内部 child/restarted mode 必须在配置/互斥/HWND 前退出，不能启动产品 GUI；短 deadline 仅验证状态机，不能替代一次真实 15 秒样本。`TerminateProcess` 不冒充 UEF 或保存完成。真实画布/窗口/Win7 仍人工。
