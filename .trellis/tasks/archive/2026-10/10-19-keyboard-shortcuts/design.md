# 技术设计（2026-10-19已批准）

## 1. 现状和边界
旧IdtDrawpad.cpp未编译，活动Facade不安装键盘Hook；Bar现有原生键盘消费者仍在，但其旧全局producer不存在。建立独立KeyboardShortcuts服务，不恢复旧KeyboardInteraction，不合并已经验收的MouseHook生命周期。三个交付面（输入、业务、设置）共享按压身份和同一启用快照，作为一个任务集成，避免只显示开关却没有可靠消费者。

## 2. 输入架构与唯一owner
- 新专用std::jthread同时安装WH_KEYBOARD_LL、注册专用全局Ctrl+Alt+X（RegisterHotKey，MOD_NOREPEAT），同线程消息泵接收WM_HOTKEY并卸载/注销。Hook与WM_HOTKEY同一producer，使用新的固定容量SPSC，不写入鼠标SPSC。Bar交互线程是单一业务owner，顶层及嵌套等待处有限batch消费；无新业务常驻线程。
- X只有WM_HOTKEY产生业务意图，LL观察但不再次执行/抑制这组组合。检查RegisterHotKey/SetWindowsHookEx结果，分别显示注册/安装状态；失败记录一次普通上下文日志，不静默用LL强抢X、不改用户键位、不SendInput重放、不提升权限。
- 专用全局X在设置普通编辑期间仍可往返；本应用模态/退出事务时暂不执行业务但系统组合仍保留，不声称交还文本控件。关闭X开关才注销并让系统不再保留该组合。未来若要求模态也原样输入，需展示前确认注销/关闭后重新注册，另作范围设计。
- AltGr/国际布局可能与Ctrl+Alt组合重叠，系统注册不能靠后验Hook放回字符；设置提供关闭X，文案及验收准确描述，不宣称完美AltGr/UIPI支持。
- Settings/UI只发布启用/上下文快照和控制命令，不能作为SPSC第二producer；注销/注册请求由服务线程完成，状态回报不可持ImGui锁同步等待。

## 3. LL匹配与按压归属
固定256键状态，左右Ctrl/Shift/Alt/Win分别跟踪。按回调事件更新新状态，不用GetAsyncKeyState或旧IsKeyBoardDown map识别新事件；负code和无关消息立即CallNext，复制vk/scan/flags/time/extraInfo值。仅本期精确组合可匹配，额外修饰键不误触，ShiftZ优先准确匹配RedoAlternate。

每个终键首次Down固定Forward或Inkeys所有权；首次放行的按压不能因后来按Ctrl在repeat中途接管。修饰键本身链传。已接管按压的repeat和Up继续消费，即使Ctrl先松开、模式变Selection、配置关闭；注入事件不建立新的物理快捷动作，也不能清掉另一来源同VK的按压票据。启动时已有修饰键未知则等可靠新边沿，不把API失败当全部Up。实现审查发现，若所有终键也无条件等首次Up，会让第一次Ctrl+Z直接落到下层应用；因此安装Hook后在普通服务上下文做一次受保护的启动校准：验证同一活跃桌面及HOOKCONTROL访问权、前台HWND/PID在采样前后不变、前台进程完整性不高于本进程，再只取所需键的GetAsyncKeyState高位。任何校验失败均保留未知；同线程回调在采样期间已经观察到的Down/Up不得被覆盖。确认Up的未观察键可立即参与首次快捷键；启动已按住的键整次Forward直到真实Up。受保护快照成功后不再调用该API；失败时只在普通服务边界每500ms尝试，UI明确同步等待，不能把安装成功直接当作可用。这是一次性边界校准，运行期匹配始终只看Hook事件，不能靠API零值修复UIPI。

注入修饰键使用独立阻断位，不能改变物理Press；它们存在时不建立新的物理快捷动作，防止真实Ctrl+注入Alt+真实Z误匹配普通撤回。全局WM_HOTKEY配置退休使用严格晚于配置采样tick的消息时刻；同tick宁可舍弃，不能复活旧代次动作。

建议重复策略：X/Del/Q每个独立按压仅一次，Del autorepeat永不构成第二击；Undo/Redo可沿系统repeat逐次产生有界命令，不自建长按timer。重复仍核有效上下文，失效只停止业务，整次press的消费归属保持。

快照全原子载荷+单次一致性尝试，回调只复制标量/匹配/有界提交，不读取live Bar/Settings对象，不调用StateMode/Host/HiMsg/任意callback/磁盘/日志格式化或业务mutex。所有权与业务入队分开：已确认批注归属的组合，队列满或业务无能力时消费但不执行，计数由普通owner限频呈现/记录，绝不fail-open撤回或删除下层文档。无可信route时整次Forward；owner心跳失效但前台/场景仍与最后可信批注一致时，仅保守保留已知组合所有权、暂停新业务；前台身份变化/无法建立可信route时不按旧场景执行业务。消费中再次核身份。

## 4. 上下文、优先级和重入
功能键归属来自实际工具/工作区，空白及主栏收起不关闭批注归属。Setting获得正常键盘输入或MessageBox模态时，普通五组合不接管；现有Bar原生面板keyboard stage维持优先级，不从已废弃producer恢复全局箭头/WASD/Esc。未绑定/PPT导航键原样链传，不发COM翻页。

外部前台变化退休双Del候选及旧事件，但本期不自动改变绘制/选择或另做指针暂停；普通快捷键仍以实际批注状态为准，不制造“指针仍批注、键盘已经操作电脑”的新稳定状态。设置文本输入是内部优先场景。

新Intent包含command、pressId/ctrlSessionId、config/lifecycle/foreground身份、稳定页面scope及必要mode revision。global往返按owner顺序以合法当前状态决定方向，不把连续两个X简单固定成相同旧方向；清空pending时事务有界串行。连续两个X按压按同一受管序列先完成退出再返回，前一个自身成功导致的模式变化由具名结果衔接，不把第二个X当作外部取消，也不凭旧快照再清空一次。前一个失败时退休依赖该次完成的后续切换，保留实际状态。外部新工具/页面变化退休旧请求，不能因旧回执覆盖新意图。

嵌套拖动只做有限读取、验证和请求取消；会改变工具/关闭当前面板的快捷键先由原cancel/revision路径让嵌套手势退栈，再在顶层安全点分发，不能在仍引用控件的栈中同步销毁/重入。普通编辑命令也必须按真实Controller身份执行，不伪造普通键消息进入旧队列。

## 5. 条件业务命令和真实结果
现有Bridge::Publish在锁内绑定的是发布当下workspace/PPT target，不等于快捷键触发时身份。添加仅快捷键使用的窄条件发布：比较捕获scope与当前scene同enqueue同步边界，Host继续携带到真实CanvasCommand/Controller消费处核验。旧事件不重新认领当前页、不切回旧页执行；现有鼠标命令合同不批量改写。

区分稳定页归属（Host生命周期、工作区、文稿/page GUID或页epoch、PPT完整target/session）与内容/clear修订。原acceptedStrokeSceneGeneration会在Clear和跨Clear恢复推进，不能直接作为全部编辑键的失效戳；Clear→Undo→Redo的同页FIFO必须有效，A→B→A和同页号不同放映仍须拒旧。

增加有requestId的窄ShortcutCompletion（Applied/NoContent/Stale/Blocked/Failed等），携带source/result context；接纳请求时先保证有受管回执槽，回执不丢到无人等待，无detached waiter。现有commandProcessed/count在失败也增长，不能当成功证明。普通owner有限消费回执；Stop退休slot，无旧回调访问已销毁对象。

复合清空/选择使用同一受管ticket事务：按页作用域保留当前工具，保证成功Clear后的选择提交间不会插入新落笔而留下未清除墨迹；优先复用已有command/admission屏障，必要时加仅该事务拥有的短期新输入准入租约，不能无条件解开别的工作区门禁。控制器真实Clear成功/真实空页NoContent后，owner用普通快捷键专用IfRevision状态内核提交Selection并结束/退休租约；失败/新工具/换页/停止须幂等释放自己的租约并保留最新用户状态。不得持模式锁等待绘制/窗口/业务线程。

## 6. 往返与Ctrl+Delete
- 合并式退出：无Clear，条件进入Selection；分离式退出：发送独立ClearAndSelect意图，使用上述真实回执事务，失败/过期不进入Selection。
- 返回：从Selection或已有ClickThrough，锁内读取合法lastAnnotationMode，恢复Pen/Shape/Eraser和既有宽色/激光记忆；缺省Pen。不恢复Freeze；往返成功进入电脑操作前按freeze revision取消旧定格。只对快捷键路径补此收尾，不顺改按钮语义。
- 普通ChangeStateModeToSelection()可复用状态内核；按钮选择入口分离式会清空，不用于无Clear路径；现有SelectionIfRevision带真退出专用标识，不能借用。
- Delete第一次Down立即单Clear，保持工具，空页也不切选择；候选以同连续Ctrl会话和精确首Clear requestId建立。两侧Ctrl从未全部释放可延续同一会话，不额外加500ms或使用鼠标双击间隔。
- 第一次单Clear完成后不长期阻止落笔。候选记录具名Clear的后置内容修订；两击之间若有新笔迹或Undo/其它内容变更，退休原候选，后续Del重新作为第一击。只有第二步已经形成时才进入短期复合操作准入屏障，再核真实页/空态/具名后置修订，不能把新画出的内容误当作已清空。
- 中间Delete Up、第二个真实Down形成Selection意图。首Clear未完成时第二击可排队等待具名结果，Applied/NoContent后条件提交一次Selection，第二击不再发布Clear，避免覆盖清空恢复点。
- 最后Ctrl Up发生在第二击之前：取消未完成两击候选；发生在两个按压都已捕获之后：只阻止后续识别，不撤销已形成的第二步意图。第一次单Clear同样不因松键而消失。
- 另一终键、额外修饰、焦点/模式/page/config/lifecycle变化退休候选或过期意图；首ClearFailed/Blocked/Stale不能授权退出。失败后下一Del作为新单Clear起点，不能假定第一次完成。
- Ctrl+Z/两Redo复用普通历史命令，不触发SuperRecovery；Ctrl+Q仅批注且Freeze可用执行，PPT/白板不可用时消费但无动作，不接管PPT CtrlQ到其它功能。

## 7. Config与设置
新增Input.Shortcuts六个NoUpload IdtAtomic<bool>：ToggleAnnotation、Undo、RedoY、RedoShiftZ、Clear、Freeze。建议默认均true，六组合独立；Clear开关同时控制单击与两击，没有额外开关/键位字符串。沿schema bool严格codec、Reset/ReadAll/ReadMini/copy及冻结FIFO；不迁移或bind旧setlist。

现有HotKey页改为固定组合+作用范围+逐项toggle，长说明按字体测量换行；变更只写新Config并异步发布配置generation。关开关退休未执行旧意图但不破坏按压Up配对；重新启用不重放旧包。X的实际注册状态区分已启用与已生效；冲突/失败文本显示真实状态。三语明确本期能力、两样式清空差异、双Del无需限时、全局X例外、AltGr可能冲突、定格限制。无录制框、自定义或工具角标；仅正式i18n sync生成资源。

## 8. 获批后的最小文件范围
- 新Input/KeyboardShortcuts模块和纯matcher/transport，Inkeys.vcxproj登记；Bar.Initialization/Interaction/Main必要入口与生命周期。
- IdtState.h/.cpp窄条件往返/Selection事务；Freeze仅需条件状态入口时扩展，不更改渲染实现。
- Draw3.Bridge/Host/Product和CanvasCommand定义所在WindowControl.cppm、DrawingController observer/消费点，传递新身份/ticket/回执；不改RTS/拟合/模型算法/COM ABI。
- Other.Config.cppm及确有必要的codec；Setting.cpp、三语与正式生成物；既有headless与状态生产探针。
- MouseHook/PPT滚轮/Raw/WM_TOUCH不在本期改动范围；若实际实现须扩大到这些区域或自动暂停系统，返回规划而不顺带实现。
