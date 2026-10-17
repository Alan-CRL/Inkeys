# 画布卡死：F1–F4修复设计与调查边界

## 本轮边界

用户已明确要求修复F1–F4，范围授权已记录。最终摘要已获用户“批准，并开始修复”明确批准，父及四子任务进入in_progress，按下列方案实施。以下保留三轮调查结论，其余未证实候选不进入本轮修复。

## 当前可确定的故障边界

第二轮已验证三条具体缺陷，详情与证据等级以 research/expanded-investigation.md 为准：F1同步诊断输出阻塞完整程序的Draw3线程；F2 PPT冷槽读取暂时失败被错误归类后持续关闭输入；F3速率橡皮idle缺少Move唤醒。F1有完整GUI触发/恢复对照；F2、F3是生产组件与当前原码片段实验，不冒充完整GUI现场。

原始视频和所有用户报告的共同根因仍未确认。扩大调查不再根据一个视频的frame日志排除其它用户的GPU/锁/外部等待；同样不把缺少锁环证据说成绝无死锁。

第三轮新增F4：Settings在按住鼠标期间隐藏，capture残留阻断Bar工具点击；无诊断完整产品及真实USER32 owner对照均已留证，WM_CANCELMODE后同进程恢复。同一现场新笔仍成功，所以不能将它命名为画布线程卡死。详见research/window-interaction-investigation.md。

## 无控制台反馈的归因约束

用户再次提供设置/Explorer/选择后笔橡皮失效，明确没有开启任何控制台输出。按该描述，F1已验证的Cursor flush路径不应继续作为主要现场解释；不能自行假设隐藏管道把反馈套入F1。F2仍需PPT冷槽恢复，F3能被有效终态/工具切换wake解除，均不能仅凭当前描述认定。

修复F1/F2/F3/F4的成功与修复这批用户报告是两个独立验收目标：前者必须让既有触发实验通过且不破坏原合同；后者必须在同版本、输入方式和操作顺序下取得匹配故障证据，并以相同条件验证修复。当前不承诺修复四项即可解决用户现场；已授权范围仅为这四项已证缺陷。

## 原始现场的后续诊断设计（本轮暂缓）

修复前的F1是既有Cursor队列虽有界，但flush仍在画布线程同步输出。修复后该标准流路径已复用F1独立有界输出器；后续新增诊断同样必须守住这一边界。优先用外部线程栈和无锁原子阶段观测；保留现有开关，不在实时packet、画布线程或持锁路径新增阻塞I/O。

如当前构建仍不能复现，则下一步最小诊断补丁仅记录以下内容，不自动恢复、不改准入规则：

1. RTS 所有 Enabled/Disabled、TabletAdded/Removed、UpdateMapping、Error 的入口/出口计数、最近时间与 HRESULT；Down/Up 保留已有 arrival/published/reason。
2. writer 进入等待、锁已取得、scale/decoder 查询、发布、退出的原子阶段；同步 callback 外部可读取，不依赖正在可疑的 writer mutex。
3. decoder 失败记录请求 tcid、作为 shared scale 基准的 tcid、cache generation、lifecycleEnabled、具体 COM HRESULT；区分没有回调和回调解码失败。
4. 在绘制循环以低频合并方式记录 ingress Down/terminal 数、active 数、commandBoundaryPending、admission revision/原因、desired/applied workspace 和输出 target revision。
5. 故障采集同时保存 Drawpad/Presentation/Setting 真实 HWND 身份、可见性、enabled、owner、前台、各 GUI 线程 capture/focus 与 WCT。对实时 callback 和持锁热路径不新增 COM 查询；需要 get_Enabled/get_HWND 时在既有 owner 控制路径单独进行受控观测；COM查询本身可能阻塞，不能承诺可以安全强制超时，应先保存外部线程栈与原子阶段。

当前 RuntimeSnapshot 有数把互斥锁，且普通 Cursor 开关没有开启所有 coordinator metrics；不能拿全零计数证明 RTS 没有输入，也不能把该快照当无锁 watchdog。

## 修复分支与准入条件

| 被证实的条件 | 最小修复方向 | 必须保留的合同 |
| --- | --- | --- |
| F1已验证：诊断stdout读端停读，画布线程停在同步WriteFile | 画布线程仅有界入队/丢弃；标准流输出由独立且只拥有诊断状态的消费者处理，退出有总期限和取消竞态防护 | Cursor/Draw3/TouchArea实际同步sink均需核对；不能只增大缓存或将无限join搬到退出；读端停读时绘制和关闭仍前进 |
| F2已验证：PPT临时读取IoError误归SourceChanged、manualOnly持续阻止输入 | 保留暂时IoError并走既有四次退避；真实内容变化仍保留SourceChanged；预算耗尽沿用既有保护，不增加恢复UX | 不能将未读取旧墨迹视为空白或解除身份保护；只影响PPT冷槽恢复 |
| F3已验证组件协议：Speed橡皮idle后Move不wake，Up能解 | 进入eraser idle前arm一次Move唤醒并复查已消费sequence；成功Move消费arm并复用SignalWake，保留原帧wakeGeneration | 真Touch/Pen路径与隐藏注入区分；维持command边界和终态、限制空闲CPU |
| HWND 正常命中、Win32 Down/Up 正常，但 RTS callback 停止，或 enabled/binding 已失效 | 在既有 RTS owner 线程实现有原因、串行的输入恢复事务；先闭合/取消旧 contact，再重绑同一有效 HWND 并重新准入 | 唯一 producer/Host、终态不丢、无双重输入；避免每帧抢焦点或定时无条件重启RTS |
| 有 Down arrival，固定 no-decoder；基准 tcid 查询失败牵连其它有效设备 | 限制 shared-scale 查询失败的影响域，按已验证的目标/有效 context 重建或保留合法旧 decoder | 显示/DPI/方向 generation 与坐标映射身份正确，不能接受旧代坐标 |
| Down 已发布，controller admission 或命令边界无法收敛 | 修正明确的交接/终态收束条件，按 route generation 安全取消旧 contact并排空边界 | 不靠清空整个输入队列或直接复用 Producing slot恢复；不能破坏 PPT身份/保存事务 |
| F4已验证：按住鼠标期间隐藏Setting，正常Up后capture残留，Bar工具点击失效；取消捕获后点击恢复，新笔始终可用 | 在Setting HWND owner线程隐藏/会话结束时，收束仍属于自身HWND的capture和backend按钮状态 | 不跨线程ReleaseCapture，不释放Bar/Drawpad新获得的capture；不直接移除visible guard导致backend生命周期外访问；普通X关闭与重开亦需回归 |
| 故障栈确认 Render持ImGui锁→DXGI同步窗口消息→Setting WndProc等锁 | 收窄 ImGui锁作用域，令CreateSwapChain/Present/Release等可等HWND操作不持WndProc所需锁；或将WndProc输入变为可排队快照 | 保持共享 RenderPipeline唯一设备/渲染线程与session drain顺序；recursive_mutex仅解决同线程重入，不能解除跨线程闭环 |
| 工具状态停在owner提交之前，且有实际未返回命令链 | 让绘制状态发布与可重试的Setting owner收敛安全解耦，处理版本/迟到命令 | 不以随便加超时或禁用Owner破坏Z序；窗口命令仍由owner线程执行 |

F1/F2/F3及F4已有各自限定范围的实验证据，其余仍是候选。F4的强触发为按住期间Alt+F4，普通X完整点击尚无相同失败证据；完整产品触摸注入在窗口操作前未建立绘制基线，独立RTS矩阵成功不替代真实触摸硬件验收。后续按四个子任务分别实施最小修复，其他候选不随本轮修改。

## 仅对第一轮视频样本降低优先级的解释

- 永久停在 Draw3 Present/GPU Map：视频相同时段仍有新的 frame；需真实GPU错误或故障栈才重新提高优先级。
- 全部canvas command被旧active堵死：末段清除旧墨迹不吻合；除非证明是另一路独立显隐而非命令完成。
- 已知 10-13 Setting Sync/ImGui死锁：当前有defer guard，且该旧闭环会影响共享UI；不可直接当作本视频根因。
- 旧Debug副本设置白屏：启动版本标记不同，WM_CLOSE后绘制及组合步骤仍通过，不足以解释本案。

## 验收与回退

修复只允许改被现场证据确认的分支，逐文件保持 BOM/CRLF 并添加适量中文注释。用修复前相同脚本/配置/OS复现，再证明修复后同条件消失，同时验证笔、橡皮、Clear/Undo、设置反复显隐、Explorer及selection/pen往返。

需要覆盖鼠标、真实触摸/笔、CloneOrMixed与单屏；本机ARM64单屏鼠标成功不能代替用户环境结论。先完成完整 InkeysRepo.sln Debug|ARM64及适当headless，再进行用户已授权的GUI复测。保留故障前后日志/窗口快照及二进制hash。

相关诊断关闭且无有效输出端时不创建输出worker；有继承输出端时保留普通错误日志。诊断修复可独立回退。任何恢复事务必须在已有生命周期中可停止、可排空，不创建新的后台永久任务。

## 当前选择的实施架构

- F1：仅隔离标准流背压。实时生产者发布有界记录；单一诊断输出worker只拥有独立队列、停止状态和输出句柄，不持有画布/HWND/COM对象。正常pipe/file/console输出保持既有字段。规划初值1024×4096字节记录，约4MiB有界payload；生产者只try-lock，容量/争用/停止时可丢弃。停止总预算1000ms，其中最多100ms drain，所有阶段共用绝对deadline。停止采用重复取消与总期限，处理“尚未提交写入”的竞态；期限后也不能销毁仍被worker使用的状态或无限join。满队列和停止允许丢诊断。现有业务logger不更改；默认关闭的专用尾轨迹文件协议暂缓。
- F2：ReadUInkFile返回IoError时单独映射IoError，后续保留严格Complete/provenance/sourceRevision/import身份校验。控制器沿用现有四次退避和预算耗尽终态；不新增恢复UX、不安装未读成功的空白文档。
- F3：在速率橡皮确定进入idle前arm Move通知（有/无deadline均覆盖），然后按runtime最后消费sequence复查真实mailbox。成功发布Move消费arm并复用现有SignalWake；沿用帧初generation消除检查/入睡交接窗口。活动帧不逐packet新增SetEvent，Up/Cancel/control wake保持。
- F4：在Settings HWND owner执行原生Hide事务时、ShowWindow隐藏前只取消本窗口的capture。Hide同时使当前输入session失效，防止Hide→Show合并保留旧backend按下状态；context的销毁/重建仍在原render/session所有者完成。owner清理不等待settingImguiMutex、不读已销毁context、不清其他HWND捕获。

各项准确函数、预计文件和回归位置保存在research/f1-fix-plan.md、research/f2-f3-fix-plan.md、research/f4-fix-plan.md及四个子任务design.md。仅在生产函数和测试共享一个权威实现的必要范围做局部提取；不新增通用日志框架或输入架构。

## 生命周期与失败处理

F1输出worker不得进入RTS、Draw3、UI或窗口的停止依赖图；输出句柄/独立状态必须在真实worker不再访问后释放。极端写入无法取消时，有限期限后放弃诊断尾部并保留隔离状态至进程结束，不能TerminateThread、关闭他人stdout句柄或遗留访问已析构产品对象的线程。

F2加载失败继续保护尚未初始化的slot；分类修复使短暂I/O错误重新获得既有自动预算，而非绕过安全准入。F3休眠意图及Move通知只服务一个真实消费者，需覆盖多contact和生命周期重置。F4隐藏意图与实际owner清理/渲染session失效按既有队列交接；迟到旧Up不可污染新session。

## 规划审阅状态

用户已请求实施F1–F4。本方案最终摘要已获后续用户明确批准，覆盖四个子任务；实施已启动，构建/回归结果另行记录，原调查实验通过记录不转记为修复通过。
