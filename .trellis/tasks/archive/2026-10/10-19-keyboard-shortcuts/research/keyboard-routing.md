# 键盘路由与生命周期调查（仅规划）

调查日期：2026-10-19。只读源码；未安装钩子、未创建 HWND、未构建或修改产品源码。附件是未来建议，以下以最新人类范围为准。父任务转达的最后澄清覆盖早期记录：全局往返在分离式从批注进入 Selection 时先清空；合并式进入 Selection 保留墨迹；从 Selection/ClickThrough 返回最近批注工具不清空。Ctrl 连按两次 Delete 在两样式均第一次清空保持工具，第二次进入 Selection，空白也可第二次进入。按键仅 X / Z / Y / Shift+Z / Delete / Q，全部加指定 Ctrl（X 再加 Alt），每组合独立 enable，不做自定义/角标/工具数字键/PPT 翻页/外部应用自动暂停。

## 1. 当前真实生产路径

- `Inkeys/Inkeys.vcxproj:923` 把旧 `IdtDrawpad.cpp` 登记为 `None`，`:933` 编译 `IdtDrawpadFacade.cpp`。Facade `:42–50` 的 callback 仅链传，`DrawpadInstallHook()` 为空。不能通过“旧函数还在”声称当前已安装键盘钩子。
- 历史 `IdtDrawpad.cpp:63–228` 是旧 WH_KEYBOARD_LL 安装/转译；`:230–328` 是旧 PPT 翻页消费者，`:330–411` 是旧定格/穿透/撤回且等待松键的循环。不得重新加入整文件、调用旧安装器或复制等待循环。本期 PPT 方向/空格/Enter/PageUp/PageDown/Esc 全部保持原生链传；不会同时 COM 翻页。
- 当前 `Other/Other.Inputs.cppm:12–40` 仍是键盘 libcuckoo map；只有三个鼠标键改成固定 atomic bits。LL 热路径不得回写旧键盘 map。新键盘服务使用自己的固定键位数组/位集。
- `UI/Bar/Bar.Interaction.cpp:3201–3219` 的活动 `HandleKeyboardMessage` 顺序为橡皮属性、撤回属性、彩盘，顶层 Run 在 `:6429` 调用。顶层等待 `:3183/3195` 读取 `EM_MOUSE | EM_KEY`。
- 橡皮属性 `Bar.EraserAttribute.cpp:562–578` 已处理 Esc/Tab/Left/Right/Enter/Space；撤回属性 `Bar.RecallAttribute.cpp:374–388` 同样处理；彩盘 `Bar.Interaction.cpp:2970–3023` 处理方向/WASD、松最后一个键保存颜色。保留这些原生消费者及其优先顺序。
- `Bar.Interaction.cpp:7795–7825` 的 `TryQueueColorPickerKeyboardInput` 是现有转译 helper，但其唯一外部调用点是旧 `IdtDrawpad.cpp:73`。它读 live UI、窗口、模式并入 HiMsg，不能直接在新 hook 中调用。恢复旧 helper 的全局方向/WASD/Enter/Esc 捕获会扩大本期范围；本期仅保留原生路径，不自动恢复全局泛键拦截。

## 2. 窗口与文本输入优先

- `Window/Window.cpp:1129–1135` 为 Setting 清掉 NOACTIVATE/LAYERED/TOOLWINDOW 并设普通应用样式；`:1691–1694` 每次 Show 设置前台、激活、焦点。
- `UI/Setting/Setting.cpp:595–618` 的 WndProc 把正常窗口输入送入 ImGui（IO 由 HWND 线程更新，context/render 在渲染线程）；`:8961–8983` 的 Toggle 用真实前台 HWND 和 LastFocusWindow 判断，不可在 hook 中调用带状态锁的 `IsVisible()` 或读取 ImGui live IO。
- `UI/MessageBox/MessageBox.Window.cpp:1336–1379` 已支持真实焦点、Tab/左右/Enter/Space/Esc/AltF4。新功能快捷键不得越过模态输入。
- 建议 owner 发布窄的 `KeyboardRouteSnapshot`：功能是否归批注、内部正常输入/模态门、模式/文档场景/配置 revision、各组合 enable、lifecycle generation。线程 callback 只读原子快照；不得读 live Bar 面板、StateMode 锁、Setting 锁、ImGui 或业务对象。
- 普通功能键在 Selection/ClickThrough、设置编辑/模态输入时放行；批注归属不依赖画布有无墨迹或主栏是否收起。前台外部应用变化只重置连续手势身份，不新增“自动暂停批注”功能。
- 专用系统全局 X 是例外：如果采用 RegisterHotKey，设置普通编辑期间仍然保留并执行往返，设置说明应明确。不能说“文本优先”意味着该已注册组合会到文本框。

## 3. 推荐分工与 RegisterHotKey 取舍

建议单独新建 `Inkeys.Input.KeyboardShortcuts` 受管服务，不改已验收 MouseHook，不扩展 Raw/Touch。服务拥有专用消息线程：同线程安装/卸载 WH_KEYBOARD_LL、注册/注销 X、泵消息。借鉴 `Input/MouseHook.cpp:71–125` 的 jthread/promise/wake event/同线程卸载与串行 Start/Stop，但不要把两个 hook 融为一个生命周期。`Bar.Initialization.cpp:203–233` 是当前 MouseHook 启动、失败退化、渲染失败清理、退出先停 producer 的实际位置，可相邻接入键盘服务。

推荐 `Ctrl+Alt+X` 使用 RegisterHotKey（Win7 可用 MOD_NOREPEAT），其 WM_HOTKEY 转成固定业务 intent；Ctrl+Z/Y/Shift+Z/Delete/Q 由 LL hook 在批注态精确匹配。原因：X 的全局保留可以显示系统注册成功/冲突，不用靠轮询按键态；功能组合的条件所有权、按下/抬起和 Delete 连按则由 hook 的状态机处理。

必须准确表达以下限制：

1. RegisterHotKey 注册成功即系统保留。忽略 WM_HOTKEY 或在消费者丢弃，不会把 Ctrl+Alt+X 还给原窗口。启用但暂时业务不可执行时，该组合仍被保留；说明中不能声称无操作就透传。
2. 若产品要求模态期间 X 也交还原窗口，须在展示模态前让注册线程确认 UnregisterHotKey，关闭后重注册且暴露失败状态；这会扩大模态协调范围。本期最小方案可将模态时 X 业务暂不执行但组合仍保留，明确与普通功能键不同。最终由总设计选定并一致写入设置说明。
3. RegisterHotKey 不按物理左右 Alt/Ctrl 区分，不能保证 AltGr+X 自动绕过。建议默认启用固定全局 X，并说明国际键盘可能以 AltGr 产生该组合，可关闭 X 开关；不在 hook 后验判断后模拟补键，也不宣称检测 AltGr 能撤回系统保留。
4. 注册失败显示“未注册/被占用”并记录一次 owner 日志；不用相同组合偷偷回退 LL、不抢占别的程序注册、不模拟输入。功能 hook 安装失败可独立退化，X 仍可用，反之亦然。
5. 统一 LL-only 也能实现所有组合的所有权和 AltGr 过滤，但没有系统注册冲突反馈，且实际管理员前台/安全桌面接收存在边界。若未来坚持自动绕过 AltGr 或全局键随内部输入完全透传，应另选统一 LL 方案并实际验证；不能承诺普遍绕过 UIPI。本期无需切换管理员权限或 SendInput。

## 4. LL 回调合同与按键所有权

- `nCode < 0`、服务未运行、非四种 key/system-key 消息、无效 lParam，立即按原参数 CallNextHookEx。
- 固定 256 项物理按键状态，左右修饰键分别记录、匹配归一化；支持 Shift+Z 与 Z 的互斥精确组合，额外 Alt/Win/Shift 不误匹配。Ctrl 左/右均可；不靠 GetAsyncKeyState/IsKeyBoardDown 轮询，不调用旧 map。
- 以终键首次物理 Down 建立一次 press record，区分“归 Inkeys”与“完整放行”。总设计最终采用：X/Del/Q 长按 repeat 不再次触发；Undo/Redo 可随系统 repeat 产生有界命令，不自建 timer。Up 必须遵守首次 Down 的归属，即使期间设置关闭、模式改变、Ctrl 先松开，也只退休同一按键票据；不能 Down 吞掉而 Up 泄漏，或最初放行而后续 repeat/Up 才突然接管。
- 修饰键本身保持原生链传；只接管匹配的终键。禁止吞 Ctrl 后用 SendInput 补偿、批量清空键盘队列、等待用户释放或 Sleep。严格区分物理 press ownership 与业务 intent 是否因 revision 失效。
- 默认注入标志输入不建立新的物理快捷键动作，按原消息链传。额外信息只是标记不可信权限。若注入 Up 与物理同 VK 混合，不能清掉物理 ownership；测试必须覆盖注入 interleave。系统注入标记和 AltGr 并非可无条件等价，勿把“检测 injected”写成完美国际键盘方案。
- 快照一次有限读取，固定字段复制和有界 try-enqueue；callback 不做 HiMsg 分配、日志格式化、业务调用、配置保存、UI、COM、锁等待。需要 GetForegroundWindow 等窄只读身份时以一次读取为限，不做跨进程查询、名称解析、GetGUIThreadInfo 扫描。
- 明确入队失败策略：已经判定为批注归属的编辑组合，队列满/业务不可用可以消耗而不执行，不能落到下层执行 Undo/Delete。消费者记录可限频的“忙/失效”状态；不得说已执行。无法获得任何有效路由而决定放行时，必须为整次 press 固定 forward，不在中途接管。对于心跳失效是维持已知归属还是拒绝新捕获，应在总设计明确；不可照抄鼠标 wheel 全部 fail-open 而让匹配 Ctrl+Z 随机器性能撤回外部文档。
- Stop 先禁止新 producer、使 generation 失效，安装线程退出并卸载，之后 join consumer。新 Start 不能回放上一代待处理意图；修饰键首次处于已按状态应先等其完整释放/新边沿，不假造首个 Ctrl 会话。

## 5. 消费入口、HiMsg 与嵌套手势

- 当前 `Message/Message.cpp:138–141` 的 Channel::Enqueue 调 HiMsg；`additional/HiMsg/HiMsg/HiMsg.hpp:294–300` 为每消息 make_unique，`:363–426` 使用 queueMutex。因此“HiMsg 是通用输入”不等于适合 LL callback 热路径。
- 建议新服务独立固定容量传输 `ShortcutIntent { command, modeRevision, sceneToken, configRevision, foregroundIdentity, lifecycle, pressId, ctrlSessionId }`，Bar owner 有限批量消费并在最新场景核验后调用当前业务，不伪造成键盘 WM_KEYDOWN 再交给旧 Drawpad 队列。没有理由新增 PPT HiMsg 消费者。
- 如果使用 hook 与 WM_HOTKEY 两个 producer，最小办法是它们都在同一安装/消息泵线程提交，以单 producer 的事实支撑 SPSC。Settings/UI 线程只能发配置快照或控制命令，不能直接 Push 该 SPSC。若不是同线程，必须改真正 MPSC 或分别有界队列，不能误用现有鼠标 SPSC。
- `Bar.Interaction.cpp:791` 的 TryReadBarPointerMessage 每次先 PollGlobalMouseInput；`:978–988` 的 Wait 每 1ms 再试。颜色/粗细/Seek 等嵌套等待大多 `EM_MOUSE`，顶层才 `EM_MOUSE|EM_KEY`。把所有新 intent 塞 EM_KEY 会导致拖动时饿死或迟到执行。
- 可相邻增加统一 `PollKeyboardShortcuts()`，有限批量在相同 owner polling 节点执行，确保嵌套等待也能及时核身份；不能直接在 LL 回调调用 BarInteractionSession。若命令会切模式或关闭面板，先走现有 cancel/模式 revision 机制让当前手势退出，避免在嵌套 gesture 栈内同步重入销毁它正引用的控件。保留 Raw Mouse/WM_TOUCH/HiMsg 点击当前合同。
- 现有面板原生消息仍先由 eraser/recall/color picker 处理；本期不新增 Esc/方向/字母全局捕获。快捷键业务入口与原生面板 WM_KEY 路由可独立存在，不能把现有 HandleKeyboardMessage 改成对所有键执行快捷键。

## 6. 连续 Ctrl+Delete 的键盘侧定义

本期不要自行加入 500ms/系统双击间隔。用户表述是按住 Ctrl 连按两次 Delete：同一连续 Ctrl 会话内，两次不同物理 Delete Down（中间必须有 Delete Up），第一次发布 Clear 并保持工具；第一次 Clear 被业务接纳后，第二次才可进入 Selection，即使开始时是空页。第二次不是长按 auto-repeat。

- Ctrl 任一侧仍按住可视为同一会话；最后一枚 Ctrl Up 结束。两侧互换是否延续可按“从未全松开”稳定定义。
- Ctrl 释放、其它终键、额外修饰形成不同组合、焦点身份改变、模式/文档页/工作区/配置失效、服务代次改变、Clear 被拒绝，均清掉第二击候选。
- 候选不能只靠 Hook 本地 bool。第一击的 Clear 接纳由 Bar 业务消费反馈，后续第二击 intent 带同一 ctrlSession/scene 身份验证；不要在第一击排队时假定已清空。快速两击应按序消费，不因异步反馈缺失丢掉合法第二击，也不把拒绝的第一击升级成模式切换。
- 第二击入队后 Ctrl 松开可以结束后续候选，但不得撤销已捕获且业务按序有效的第二击。Clear 组合只有一个 enable，同时管理单次清空和两击进入选择，不增加额外开关。

## 7. 实施时必要验证

纯策略：每组合精确匹配、左右修饰/额外修饰、首次 Down/repeat/Up、按住终键改设置/模式、先松 Ctrl、injected interleave、AltGr 方案边界、注册失败/独立退化、队列满/消费者暂停/心跳失效、start-stop旧代次、不同producer并发。

业务/owner：Selection/ClickThrough全放行普通组合、空页仍拥有功能键、Setting正常输入、模态优先及X保留说明、合并/分离往返最新语义、连续Ctrl两击Delete快速顺序/拒绝/空白/跨页/焦点/配置失效、Ctrl释放后仍有效的已捕获第二击、嵌套拖动时快捷键取消而不重入悬空对象。

本次仅规划，以上均未执行测试或宣称通过。Hook真实时延、Office普通原生按键、管理员前台、Win7、国际布局/AltGr、触摸键盘与辅助技术属于后续明确环境下验收项；不承诺无窗口测试能证明系统级送达。
