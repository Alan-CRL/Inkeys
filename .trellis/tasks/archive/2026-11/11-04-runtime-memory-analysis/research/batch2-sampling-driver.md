# Research: 无Computer Use的第二批功能往返采样驱动

- Query: 不改产品代码、不使用Computer Use、不绕过锁屏时，现有入口能否驱动隔离Inkeys实例的Laser、Setting、Whiteboard往返并采集用后回落？
- Scope: internal；仅源码/规格研究，未构建、运行、发窗口消息或操作Git。
- Date: 2026-11-05

## Findings

### 结论

**Whiteboard有可直接使用的完整外部业务入口与退出消息。Setting只有可靠的外部隐藏消息，没有找到可靠的现成外部Show命令；Laser没有发现直接选择/离开工具的外部CLI或命令管道。** 后两项应复用本批真实离屏/隐藏原生探针作局部证据，并结合完整产品冷启动内存，不猜快捷键/WM_COMMAND，也不把原生ShowWindow当作业务Show。

### 1. Whiteboard：同目录CLI进入，Freeze锚点WM_CLOSE退出

可行序列：

1. 从隔离部署目录启动一个正常Inkeys，记录PID并等待正常启动完成，采普通桌面稳定值。
2. **再次执行同一部署目录的同一Inkeys.exe，参数精确为 `-Whiteboard`。** 已运行实例存在时，第二个进程通过现有命令管道转发请求后退出，采样应继续跟踪原PID。
3. 等待真实白板进入/背景与窗口提交完成，采功能状态稳定值。不能把第二个进程退出0或管道Accepted直接当作白板已ready。
4. 只枚举该原PID的顶层窗口，找到类名以 **`Inkeys1;`** 开头的唯一Freeze/白板锚点，向它 `PostMessageW(hwnd, WM_CLOSE /*0x0010*/, 0, 0)`。不要给Drawpad、Bar或全部窗口发WM_CLOSE。
5. 退出沿原白板保存/场景交接事务返回primary workspace；无Office时应回桌面。等待窗口隐藏/事务完成和计数器稳定，再取尾部稳定值。进程必须仍是原PID且未退出。

代码依据：

- `Inkeys/Launch/IdtCommandPipe.h:16–24`：`WhiteboardArgument=L"-Whiteboard"`，按argv逐项精确匹配；不是用`*`拼接的旧命令行语法。该比较区分大小写，建议使用这里的标准拼写。
- 同文件`:26–37`：管道Request固定16字节，magic/version/command=1/reserved，当前唯一有效command是白板请求，没有Setting/Laser命令。
- 同文件`:54–74`：端点由逻辑目录presence identity、当前session和server PID构成，校验同目录image、同session、同用户SID。before/after各自必须使用自己那份隔离EXE再次发请求，不能跨部署目录转发。
- 同文件`:299–319`：`Admit(root, whiteboard, allow)`白板时先尝试Send；已有实例即Forwarded，即使允许多开也优先转发。直接启动标准CLI比自行拼私有管道和hash更稳妥。
- `Inkeys/IdtMain.cpp:4223–4229`：只有startup已完成且无失败时才消费`TakeReadyPending()`，转为`RequestWhiteboardFromShortcut()`；Accepted仅表示已排队。
- `Inkeys/IdtMain.cpp:3810`、`:3835`、`:3874`：Freeze类名是`L"Inkeys1;"+ClassName`，并安装`NotifyWhiteboardWindowCloseRequested`回调。ClassName为userId，异常回退Inkeys（同文件`:3740–3741`），不应硬编码完整类名或只按标题查找。
- `Inkeys/Inkeys/Window/Window.cpp:219–224`：正式DefaultWindowProc先调用`DispatchWhiteboardClose`，仅未处理时才DestroyWindow。
- 同文件`:728–735`：只接受本服务Freeze HWND、其owner线程，并调正式回调；迟到消息也保留共享owner。
- `Inkeys/Inkeys/Window/Window.cppm:147–154`：生产Freeze有close callback，白板active时回调请求退出；即便刚退出，managedAnchor+callback仍返回true，不落DestroyWindow。
- `Inkeys/IdtState.cpp:1485–1487`：close回调排入`NativeWhiteboardRequest::Close`；`:453–479`按当前workspace revision消费并RequestWorkspace到CurrentPrimaryWorkspace，保留并发与保存交接门。

可选最小化/恢复采样同样走正式消息：对该Freeze HWND发送`WM_SYSCOMMAND /*0x0112*/`与`SC_MINIMIZE /*0xF020*/`或`SC_RESTORE /*0xF120*/`。`Window.cpp:200–215`会投递白板view请求，`IdtMain.cpp:3834–3843`安装正式回调；PPT活跃时最小化会按现有规则被拒绝，不能强制绕过。不要用直接ShowWindow来替代事务。

### 2. Setting：隐藏可靠，显示缺少外部业务入口

- `Inkeys/Inkeys/UI/Setting/Setting.cppm:25–31`导出`Show/Hide/Toggle/IsVisible`，但这是本进程C++接口，不是外部RPC或DLL控制接口。
- `Setting.cpp:5385–5401`的ShowSetting同时RequestShow、投递owner业务FIFO并请求RenderPipeline；仅把HWND显示出来不等于设置session进入visible，也未保证交换链/首帧/输入状态。
- `Setting.cpp:1753–1757`：向该PID的`Inkeys.Setting;*` HWND发送`WM_SYSCOMMAND/SC_CLOSE /*0xF060*/`会调用正式Hide，不退出软件；`:1882–1884`的WM_CLOSE也调用Hide。
- `IdtMain.cpp:3905`设置完整类名为`L"Inkeys.Setting;"+ClassName`，应以PID+前缀过滤。
- `Bar.Button.cpp:694–717`里实际Settings按钮调用`Setting::Toggle`。未发现处理“打开设置”的普通WM_COMMAND编号或独立CLI；不猜菜单ID，也不靠坐标点击覆盖锁屏。

本批2主要移除初始化即常驻的CPU图片副本，完整产品冷启动本来就覆盖此成本；恢复正确性应使用`--setting-texture-recovery-test`，它在`IdtMain.cpp:1712`严格早退，实际实现`Setting.cpp:5577`，不把字体测试替代图片恢复。

### 3. Laser：外部没有选Laser命令，使用真实资源探针与Host回归

- `KeyboardShortcuts.h:7`业务命令只有ToggleAnnotation、Undo、RedoY、RedoShiftZ、Clear、Freeze、DismissWhiteboardMenu，没有Laser选择命令。
- `KeyboardShortcuts.cpp:27`的HotkeyId是0x4b58；`:88`仅注册Ctrl+Alt+X，使用`RegisterHotKey(nullptr,...)`投递到专用线程而非Bar/Drawpad HWND。该组合也只是往返批注/Selection，不是选择Laser。
- 同文件`:48`识别LLKHF_INJECTED/LOWER_IL_INJECTED；transport与键盘规格要求注入不建立物理按压票据。不能把SendInput或向某个窗口伪造WM_HOTKEY当可靠工具切换。
- `Bar.KeyboardShortcuts.h:448`出现ChangeStateModeToPenTool(Laser)是在ProductionTest的seed，不是普通用户快捷键。实际Laser按钮逻辑在`Bar.Interaction.cpp`的实时布局/命中分发里，没有发现稳定外部命令号。
- 本批`--draw3-laser-resources-test`入口在`IdtMain.cpp:1723`，真实实现`Draw3.Renderer.cpp:778`；结合`--draw3-hidden-test`（Main约2018行）验证Host/窗口/控制流。前者是真资源层离屏证据，不能单独表述为用户在完整产品中点击过Laser按钮。

### 4. 既有UI3 fixture不是通用远程控制

- `Ui3PresentationFixtureAuth.cpp:37–39`有`--ui3-presentation-benchmark`及内部child模式；`:202–206`限制purpose/sourceVersion/nonce和scene=1/2。
- `Bar.PresentationProbe.h:12`仅MainFold与DrawAttribute两类scene。`Bar.Presentation.Test.cpp:553–570`在已授权fixture安装后拦截普通键鼠/热键/WM_CLOSE等来源以保证有限轨迹来源。
- 因此不应为本次内存采样手工伪造nonce/继承句柄/内部消息，或把这个fixture解释为可任意控制Setting/Laser的API。它也不控制已运行的普通产品实例。

### 5. 锁屏与证据范围

白板命令管道与投递已知HWND消息不依赖前台键盘输入，是最适合尝试的现成路径；仍须核实同session、同权限级、消息投递结果以及实际workspace/窗口提交，不能保证锁屏下所有呈现和焦点行为都成功。项目白板合同允许焦点激活失败仍完成后台进入，但未授权跳过ready或隐藏回执。

键盘服务在`KeyboardShortcuts.cpp:111–126`检查input/thread desktop同名与可信前台，锁屏时不满足时采取保守状态；本次不尝试解除安全桌面或规避校准。若真实白板往返被环境挡住，使用本批`--freeze-resource-lifecycle-test`（Main约1746行）、`--draw3-auxiliary-resource-lifecycle-test`（Main约1734行）、`--page-control-hidden-test`与`--draw3-hidden-test`作局部资源/原生生命周期证据，明确不能替代完整产品GUI交互。

### 6. 正常采样结束码6的解释

`IdtMain.cpp:4215–4221`将“Draw3 Host意外停止，受控退出”设置为developerModeExitCode=6。旧采样器向所有自有窗口发送WM_CLOSE时，生产Drawpad的WindowController会设置exitRequested并唤醒绘制线程（Draw3.WindowControl.cpp:1626），触发这一受控停止；不应简化为该回调直接DestroyWindow。可以保留为主动终止测量的清理结果，但不应称为应用正常业务退出码或正确性测试通过。白板回落采样只关闭Freeze锚点，必须确认原进程持续存活；最终清理再独立记录。

## Files Found

- `Inkeys/Launch/IdtCommandPipe.h`：按用户/session/逻辑目录身份约束的唯一白板启动命令。
- `Inkeys/IdtMain.cpp`：生产窗口类名、白板回调绑定、ready后消费启动请求及自检CLI。
- `Inkeys/IdtState.cpp`：白板原生关闭/视图请求到workspace事务的桥接。
- `Inkeys/Inkeys/Window/Window.cpp/.cppm`：Freeze锚点的WM_CLOSE/WM_SYSCOMMAND正式处理与共享窗口保护。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp/.cppm`：设置业务Show/Hide、系统关闭消息和图片恢复探针。
- `Inkeys/Inkeys/Input/KeyboardShortcuts.*`、`UI/Bar/Bar.KeyboardShortcuts.h`：固定热键、注入限制和生产owner测试。
- `Inkeys/Inkeys/Helper/Ui3PresentationFixtureAuth.*`、`UI/Bar/Bar.Presentation*`：受能力授权限制的有限UI3轨迹，不能当通用控制API。

## Related Specs

- `.trellis/spec/native-desktop/whiteboard.md`：白板进入/退出、共享Freeze、真实提交、最小化和焦点失败边界。
- `.trellis/spec/native-desktop/keyboard-shortcuts.md`：物理按压票据、固定命令、注入与桌面同步。
- `.trellis/spec/native-desktop/single-instance-and-startup-handoff.md`：逻辑目录单实例与白板转发。

## External References / Versions

本题所有结论来自当前本地源码与规格；没有引用外部文章推断本工程消息协议，也未实际验证锁屏环境。源码由其他实现代理并行修改，行号为研究时快照，函数名/常量名为稳定定位依据。

## Caveats / Not Found

- 未找到无需实时UI布局且可外部可靠打开Setting/选择Laser的现成普通产品命令。
- 未运行任何入口、发任何Win32消息、修改源码/规格/系统设置或执行Git操作；本文只是驱动建议，不是采样成功证据。
- 未证明UI显示状态与Host全部ready可单凭EnumWindows确认；实际驱动应结合正式日志/回执与稳定采样，不能把PostMessage成功或CLI转发Accepted当完成。

## Parent follow-up: 全软件退出与白板可观察状态

### 没有找到稳定的全软件正常退出消息入口

继续核对生产Root/MagnifierHost、Bar、Setting和Drawpad的窗口过程后，未找到可从外部发送的普通WM_COMMAND/WM_CLOSE消息会直接走正式CloseProgram：

- 正式`CloseProgram()`在`Inkeys/Inkeys/Helper/Helper.CrashHandler.cppm:61–67`，先`SetOffSignal(1)`再`RequestHideAllUserWindows()`，属于进程内函数。
- 设置内部`QueueClose`在`Setting.cpp:1408–1412`调用它；设置关闭软件按钮在同文件约4953行。设置窗口的WM_CLOSE/SC_CLOSE仅Hide，不能拿它们请求整软件退出。
- Bar的关闭业务在`Bar.Interaction.cpp:6325`通过按钮/确认动作回调CloseProgram，没有发现对应的外部固定命令号。普通Bar WM_CLOSE走默认DefWindowProc，不是正式业务关闭。
- `MagnifierHostWindowWndProc`在`IdtMagnification.cpp:247–250`只调用DefWindowProc；向Root/MagnifierHost发WM_CLOSE会走窗口销毁，不能当正式程序退出。
- `Draw3.WindowControl.cpp:1626–1630`对Drawpad WM_CLOSE设置exitRequested并唤醒绘制线程，正是会导致Main检测Host停止并返回6的路径。

因此建议完整白板周期内始终保持原PID，只对Freeze发退出白板消息。周期全部采完后再单独标记“测试清理”（现有受控Host停止或精确自有进程终止），不要把最终清理码包装为正式CloseProgram退出测试。若未来确需真正优雅全软件自动退出，需要额外受控harness入口；本研究不建议猜内部消息/伪造系统关机消息。

### 白板进入/退出的外部必要条件

`Window.cppm:121–134`的ResolveOverlayActivationStyle给出了可靠的样式差异，`Window.cpp:2899–2930`由正式owner应用：

| Freeze锚点状态 | GWL_EXSTYLE必需条件 |
| --- | --- |
| Whiteboard模式 | WS_EX_APPWINDOW(0x00040000)置位，WS_EX_TOOLWINDOW(0x00000080)和WS_EX_NOACTIVATE(0x08000000)清除 |
| 返回普通Presentation/Desktop策略 | WS_EX_APPWINDOW清除，WS_EX_TOOLWINDOW和WS_EX_NOACTIVATE置位 |

样式只检查这些mask，保留其他layered/transparent运行时位。进入时可要求PID与同一HWND仍匹配、APPWINDOW模式、IsWindowVisible=true、IsIconic=false、窗口RECT有效并连续稳定若干秒；退出时要求恢复普通样式并确认锚点隐藏，再等待计数器稳定。窗口标题从创建起就是本地化白板标题（Main约3874行），**caption不能区分白板active与普通模式**。

这些是外部必要观测条件，不是Host全部ready的充分证明；进入过程中样式可能先改变，仍应等待原业务门完成并结合已有正式日志/状态与稳定时间窗。不要要求GetForegroundWindow必须等于Freeze：白板规格明确允许焦点激活失败后后台完成进入，锁屏时更不能把前台焦点作为强制门。

`Window.cpp:157–174`已有[WhiteboardWindow]诊断字段event/hwnd/visible/iconic/groupMinimized/bounds；是否落到当前采样日志取决于原诊断sink，不能假设一定有文件。隔离目录应确保只有一个目标实例，因为命令管道会选择同root peer；不要在同root多实例时假定必定转发给刚记录的PID。
