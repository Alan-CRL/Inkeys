# Research: Desktop / Presentation / Whiteboard 状态机的项目合同

- Query: 为待用户审核的最终三工作区切换设计整理不可丢失的 Trellis 合同、可派生状态、真实完成凭据及失败/取消边界。
- Scope: internal；仅研究现有规范与实现，不写最终 design 正文。
- Date: 2026-10-27
- Active task: `.trellis/tasks/10-27-whiteboard-first-open-visibility`。
- 当前授权：只设计，用户审核前不得改产品；现有 Window 补丁冻结。本报告不把任务文件较早的“授权实施”文字当作继续实施许可。

## Findings

### 1. 这里的三态只指三个工作区

权威工作区集合固定为 `Desktop / Presentation / Whiteboard`，与 `Draw3.Bridge.h` 的 Workspace、`IdtState.cpp:44–45` 的三个工具记忆槽一致。用户已明确不增加按钮因后台/前台而切换 Activate/Close 的语义。

- Whiteboard 最小化、恢复、前台/后台是视图/系统事实，不能成为第四个工作区；最小化不切文档、不重放入口。
- Pending 交接、回滚、恢复错误是事务进度或结果，不能混入业务 Workspace 枚举。
- PPT `Loaded / NotFound / IoError` 是加载结果，COM `Active / Inactive / Unknown` 是会话事实，`Valid / EndScreen / Unknown` 是页状态，均不是本次工作区“三态”。
- 主栏缩放基准、浮动/底栏、居中/自由、按钮 hover/press 等 UI 状态不是本次三工作区；不顺便重新设计它们。

### 2. 文件与规范索引

| 文件 | 已确认内容 |
| --- | --- |
| `.trellis/workflow.md` | 设计/实施分阶段，研究证据落盘；本轮只设计。 |
| `.trellis/spec/native-desktop/whiteboard.md:7–29` | 单白板文档、多页主题/保存、Bar 基线、共用分页、最小化与真实背景交接。 |
| `.trellis/spec/native-desktop/draw3-integration.md:57–59,100–106` | 单 Host/单 producer、Window Service 原生所有权、辅助表面边界。 |
| 同上 `:217–233` | 三工作区 CPU slots、PPT lane/目标身份、保存/加载、dirty/warm/eviction。 |
| 同上 `:356–368,398–406` | 工具 Selection、内容/输出 Present 版本、完整帧与窗口互斥显隐。 |
| `.trellis/spec/ppt-interop/native-session-ui3.md:19–38,52–58` | COM 权威、场次/目标/页 UI ACK、真退出条件工具收尾和焦点。 |
| `.trellis/spec/native-desktop/canvas-freeze-boundaries.md:51–86` | IoError 不能当 NotFound；现有四次 current-load 重试和身份门。 |
| `Inkeys/IdtState.cpp:994–1047,1106–1187` | 当前白板 epoch/phase 权限、三槽工具记忆与早发布机制。 |
| `Draw3.DrawingController.cpp:704–785` | 真实 DrawingDocumentSlot 和完整 active/parked swap 字段。 |
| 同上 `:1144–1228` | 当前 PPT load retry、四次退避、PrepareExit/取消的单调边界。 |
| `Draw3.PresentationAutoSave.cppm:97–125` | persistence 结构化结果和完成身份；结果不止三种。 |
| `Draw3.PresentationState.h:17–33`、`Draw3.Host.cpp:807–836,990–1019` | 正常画面交接/输入准入的真实事实与版本。 |
| `Draw3.RealtimeStylus.cpp:926–930,1237–1251`、`Draw3.DrawingController.cpp:11634–11656` | 物理鼠标可走 RTS；现有 Selection Down 拒绝不一致。 |

源码行号为本轮读取时位置，后续冻结补丁恢复/撤回会移动行号。窗口、Bar 细节已有本任务其他研究报告，本文件仅提炼合同，不重新实现或复查原生排序算法。

### 3. 可以简化的权威模型与不能混淆的层

建议最终设计只保留一个权威的“目标 workspace/视图 + 已提交 workspace/视图”，以及一个可取消的 pending 交接，沿用现有用户意图 epoch。UI、Bar、背景、页控和命令资格消费同一权威快照，不能各自发布互相矛盾的稳定业务状态。

这是设计约束，不要求新建通用状态机框架、注册表或另一个 Host：

| 层 | 谁拥有 | 不能借简化搬走的职责 |
| --- | --- | --- |
| 用户目标、已提交工作区、pending 意图 | 一个串行协调入口 | 只这里决定逻辑提交/取消，UI 组件不能自行宣告工作区成功。 |
| active/parked 文档、页、history、GPU replay | 原 Draw3 绘制 owner | 准备目标槽、收尾旧 contact、消费异步保存/加载 completion。 |
| HWND 样式/owner/band/bounds/显隐 | 原 Window Service 所属线程 | 真实写入/读回、代际和恢复责任；不能由 UI/绘制线程抢写。 |
| Bar/四分页/白板背景像素 | 原各 renderer 与共享 RenderPipeline owner | 请求与实际呈现分开，发布真实完成事实。 |
| PPT COM 会话/目标与业务调用 | 原 COM/native owner | Unknown 不等于结束、session/target 身份、队列及焦点检查。 |
| 保存/加载文件 I/O | 原 worker | 不碰 UI/HWND/可变 Controller，返回身份齐全的结构化结果。 |

“一次提交 UI”应是**一次权威业务快照提交**，使 Bar/PPT/Whiteboard 看到同一个已提交身份；不承诺所有 HWND 在同一物理 VSYNC 同时显示。异步渲染仍须各自 ACK，而不是让调用方把 `Request/Accepted` 当成已经显示。

### 4. 可派生项与必须保留的真实 ACK

| 现有概念 | 建议处理 | 不能误删的事实 |
| --- | --- | --- |
| whiteboardDesired | 从目标 workspace==Whiteboard 派生 | 目标请求的 epoch 与取消身份。 |
| WhiteboardPhase 的 Inactive/Entering/Active/Exiting | 从已提交/目标及 pending 方向投影；不要再独立写成第四份权威 | pending 的当前进度、失败责任与实际结果仍须记录，不能把“没有独立 phase enum”误解为“没有交接”。 |
| Bar::whiteboardActive、Freeze 的白板业务可用性 | 从同一已提交工作区快照投影 | 原 Freeze/Magnifier 独立业务状态、Bar 原桌面基线不能丢。 |
| expandedLayoutTarget、按钮/页控目标模式 | 从统一目标/提交快照派生；准备动画可以先推进，但交互身份不得先假提交 | 实际布局/像素提交、页 UI ACK 继续由 renderer 产生。 |
| background active 的期望值 | 从 Whiteboard 目标视图/显隐计划派生，不能成为另一个意图源 | `committedBackgroundActive` 所表达的**真实背景显示/隐藏 ACK 不能直接删成派生 bool**。可纳入同身份完成记录，但必须由实际 owner 确认。 |
| whiteboardGroupMinimized | Whiteboard 的视图状态 | Window owner 实际 iconic/visible/bounds、恢复后的真实 Show/Bounds 完成仍需读回。 |
| 输入允许/关闭 | 由 pending、视图、已提交 scene、Selection、load/recovery/stop 等现有事实统一计算 | 不额外堆彼此独立的 phase/Host bool；但过渡必须真实关新 Down，不仅是 UI 显示禁用。 |
| fullscreen/topmost 业务期望 | 从目标/已提交 scene 与视图派生 | 实际 owner/band/样式/显隐、Shell worker 最新绝对状态及 native command 完成不能靠 enum 猜测。 |
| foreground 成功 | 只记录尽力激活的结果 | 不进入业务成功门、不无限重试、不用任务栏点击才推进提交。 |

背景 ACK 的边界尤其重要：没有取得白板背景所有权时，初始 inactive 不能为了“确认 false”隐藏桌面/PPT 共用 Freeze；真正离开已取得所有权的白板时则必须完成 Hide。最小化只暂停 active 背景绘制，不能挡住 close 的 Hide/释放 ACK，避免最小化退出闭环。

### 5. 一个业务 epoch 不等于删除全部底层身份

现有 `whiteboardInputEpoch` 在 `IdtState.cpp:172,994–1047` 用奇偶区分白板意图与 primary 意图，并让进入/退出期间的 PPT business input epoch 为 0、scene epoch 可恢复场景。若最终设计复用该字段，必须明确奇偶与 Desktop↔Presentation 的含义如何保留/迁移；不能只改名字就默认旧调用仍正确。

单一 pending 意图应同时约束旧 Frame、窗口命令和最终 UI commit；新目标/取消退休旧 pending，相同目标重复请求幂等，不反复收面板或重启动画。快速 Whiteboard→Desktop→Whiteboard 不能接受第一轮 Whiteboard 的迟到成功，即使 enum 又相同。

但以下都不是冗余业务状态，必须继续保留各自已有身份：

- 工具 mode revision：拒绝旧 Selection/PPT 退出覆盖用户后来选的 Pen/Shape/Eraser。
- PPT session/binding/target revision、SlideID/page 与 UI pageAckEpoch：防止同页码、同目标数字却不同发布阶段的旧 ACK。
- CPU slot generation、pageGuid、mutation/queued/committed revision：防止旧存储 completion 驱逐或覆盖新数据。
- Host generation、HWND generation、Window command intent/deadline：防止同值句柄重用、过期写入与恢复权限混淆。
- output target/revision、content/presentedContent revision、device epoch/真实背景完成：防止拿旧帧或新 device 前的像素宣告成功。

Whiteboard 最小化/恢复虽然不换 Workspace，也不能让旧 Show 在 Hide 后迟到生效。最终设计应明确是用同一现有意图 epoch 退休这类新视图意图，还是沿用 Window 现有 command 版本；不应新造一套互不关联的 view token 协议。

### 6. 工具、文档与 CPU slots 的硬合同

**工具隔离**：`IdtState.cpp:44–45,1126–1176` 现有三槽记忆不能变成一个全局工具。离开场景只保存一次旧工具，目标工具可在准备阶段作为候选交给 Host，但 Bar/全局已提交工作区应等统一提交；失败重试不能把目标候选反向记进来源槽。初次进入未建工具槽默认 Selection，同时保留宽度、颜色等偏好。白板退出不是无条件选择；PPT 真退出才按旧 mode revision 条件收尾，且必须先保存 Presentation 工具再恢复 Desktop（PPT spec:15,38）。

**单 Host 与独立文档**：Draw3 spec:217 及 `DrawingDocumentSlot:704–785` 明确：Desktop singleton、Whiteboard singleton、按 `(PresentationKey, SlideBindingMode)` 分 lane 的 PPT slots。每槽包含 document、页 runtime/history、当前页、retained slides、fileGuid、slot generation 及保存 revision。切换准备目标 lane，再 park/swap 整槽并完整 GPU reset/replay；不能只切 workspace enum、清屏后借另一槽，也不能为简化创建多个 Host/输入 producer。

**PPT 目标不可拆**：key、binding、topology、page/SlideID、targetRevision 作为一个共享不可变 target。普通翻页/target revision 不等于拓扑变化；warm/parked 槽先恢复自己的旧 target，再按实际旧/新 SlideID 拓扑重映射。StableSlideId 与 PageIndexFallback 严格分轨，不把 ordinal 当文稿身份。

**白板关闭不销毁文档**：同进程重开继续原多页白板，忽略 Desktop 保存开关；新进程新白板，旧文件保留。保存只在离页/离场/正常退出安全点触发；失败页 warm、仍可离场/翻页，不能通过“close 成功”清掉未 durable 的历史。

**已接受命令有责任**：FIFO 的 captured scene→command、Clear Boundary/保存屏障必须完成；新的 latest target 可以取消尚未提交的显示意图，不能丢弃已接受的修改/存储责任。旧页未抬起 contact 按现有规则收尾并隔离到真实 Up/Cancel，不无界等用户抬笔，也不跨场景续写。

### 7. PPT 加载结果：NotFound / Loaded / IoError 不能合并

`PresentationPersistenceStatus` 实际枚举（cppm:97–106）还有 Committed、CrossProcessConflictDeferred、SourceChanged、Invalid。设计中的“三类关键加载结果”只是便于说明，不能删掉严格终态或把它们都归入 NotFound。

| 结果 | 必须行为 | 禁止的简化 |
| --- | --- | --- |
| Loaded | 身份/版本/拓扑严格匹配，drawing owner 真实 materialize/install；replay+Present 成功后才 ready | 仅收到 snapshot 就开放输入；旧 completion 安装到新 target。 |
| NotFound | 只有对应独立 track 已严格确认不存在、身份可信时，才可初始化该目标的新空槽；仍须正常 Present | 无 snapshot、锁超时、未知索引均当作“没有笔迹”；把旧非空 dirty lane 清成空白。 |
| IoError | 保留同身份失败，沿既有 250/500/1000/2000ms 四次预算重试；耗尽按原 manual/protected 恢复语义 | 当作空白、改成 SourceChanged 从而跳过原重试，或另建无限轮询/等待下一笔。 |
| Conflict/SourceChanged/Invalid | 保留对应拒绝、隔离和恢复提示，不覆盖未知文件/旧 warm 数据 | 为让工作区 enum 能提交而伪造成功。 |

依据：Draw3 spec:226–233；F2 spec:63–65；真实 `PresentationCurrentLoadRetry:1144–1228`。目标/generation/PrepareExit 改变退休旧重试，不能迟到再排队或安装。

保存与加载需分开：保存失败不能挡住用户切换工作区并把 UI 永久锁住；失败槽保留 dirty/warm、旧 durable 文件及必要 fallback，后续安全点重试。加载失败则不能开放目标内容输入，但必须有有限失败收敛/取消/重试 UI，不能只等正常 ready 永远为真。PPT worker 不读取 Desktop saveSetting.enable。

### 8. 真实 ready 与输入底线

正常交接至少保留以下事实，而不是只比较 target==committed enum：

1. 目标 CPU 槽/页身份已经正确，load/replay 已收敛。
2. 目标工作区和**当前期望工具 Selection**匹配的成功 Frame，output target/requested/ready revision 与 content/presentedContent revision 匹配；空 Selection 辅助表面需要 full-frame clean。
3. Whiteboard 背景的对应显隐/真实提交 ACK，以及所需 HWND 几何、样式、owner/band/内部顺序和显隐读回。
4. Presentation 的 PageControl visible surfaces 真正提交同一 target/page/mode 的 UI ACK；没有可见 surface 时按既有合同直接回执，不能凭“白板把 PPT 隐藏了”假造显示 ACK。
5. 对应请求仍是当前 epoch/身份，失败/取消或新目标没有退休它。

`DrawpadPresentationReady:17–33` 已有 desired/runtime 模式匹配，优先复用；当前 `IdtState::Draw3WorkspaceReady:313–330` 只按 runtime.selectionMode 选目标，最终设计不应继续把旧模式 ready 当成新意图 ready。

输入应在开始交接时真正关闭新 contact；稳定提交后只有当前 scene、视图和原有 load/recovery/PPT UI/shortcut/stop 各门均允许才重开。不能把 `activationAllowed=false`、MA_NOACTIVATE、某个透明样式或 Bar 禁用外观当输入关闭。

物理鼠标可走 RTS TDK_Mouse，不仅 Win32Mouse。最低语义是：**实际 Selection 在三工作区都不接纳新墨迹/擦除 Down**，保留现有 DiscardUntilTerminal 的物理生命周期；电脑操作/系统选区不由 Controller.initializeStroke 实现。选择态已有接纳/末端处理不能被新设计粗暴全清；非 Selection 的右键/笔尾工具保持合法行为。统一 Selection 底线不能代替过渡输入闭合或 HWND 正确路由。

### 9. Bar 基线、分页及 Whiteboard 视图合同

- 第一次 Desktop→Presentation/Whiteboard 记录桌面 Bar 基线一次；仅已有展开底栏贴屏底 5 DIP，保持居中属性。浮动/折叠不强制展开。
- Presentation↔Whiteboard 不重拍桌面基线、不重复折叠或复位；最终提交 Desktop 才恢复原基线。失败重试不能反复收起 More 或覆盖用户正在交互的动画。
- PageControl 是四个分页 HWND 唯一 owner；白板复用 PptBottomLeft/Right，不新建左右窗。共享 Bottom 实例和动画要连续重定向，不能用中间 Hidden/新空列表替代正常互切。
- 白板最小化仅改变其视图：共享页控隐藏、Bar 按既有合同回任务栏居中底栏；恢复同文档且不重放入口。有效 PPT 场景禁止最小化的资格按 COM 会话事实，不能从页控被隐藏推导。
- 白板后台 PPT 结束仅更新 primary 返回事实，不能让当前 Whiteboard 自动退出或清文档。关闭 Whiteboard 时返回**最新可信** Presentation，否则 Desktop；目标在 pending 内再变化须退休旧 target。
- Window 仍保持白板 NOTOPMOST、一个任务栏锚点、双可激活表面的 Shell 全屏语义；最小化/退出先撤销相应标记。foreground 只尽力一次，不参与成功/稳定态定义。

### 10. 最终设计必须明确的失败、取消与恢复规则

| 情况 | 必须明确的收敛规则 |
| --- | --- |
| 同一目标重复请求 | 幂等；不重复存工具、基线、收面板、首开激活或重启动画。 |
| 快速 close/reopen / A→B→A | 新意图退休旧 pending；迟到 ACK 不能因 workspace 同名被接收。已接纳文件/画布修改责任继续按原身份完成。 |
| 新目标到来时旧 native 已部分写入 | 先由原 owner 持有唯一物理恢复/安全收束责任，确认后推进最新目标；不能把超时当从未发生，也不能给新 generation 写旧快照。 |
| Frame/背景/native 暂时失败 | 明确有限节拍/事件重试和当前阻塞 stage；保持可信旧画面或按原安全策略隐藏，UI 状态不得在两套“稳定态”间来回写。 |
| 持久失败/预算耗尽 | 必须规定回到仍有效的已提交 scene，或沿现有 Recovery 提交受保护错误视图/重试入口；后者不能冒称正常 Frame-ready。不可只等 ready、也不可永远全 UI 锁死。 |
| rollback 自身失败 | 保留原 owner 的恢复责任及新 Hide/穿透安全覆盖；业务 pending 可显示失败/允许取消，但不能清责任假成功。恢复不能以原意图已过期为由永久拒绝必要清理。 |
| 保存失败 | 可切换/退出视图，dirty/warm 数据不驱逐，不覆盖最后 durable 恢复点；accepted queue 正常完成，失败结果可见。 |
| 最小化时关闭 | close/hide/release 路径先于 active 渲染暂停；不等待最小化状态下永远不会产生的新 active 背景帧。 |
| PPT 后台结束/Unknown | 可靠结束只更改返回目标；Unknown 保留身份并暂停不可信输入，不把隐藏/失焦当退出。 |
| 前台激活失败 | 正常后台提交仍成功，不重复抢焦点、不再要求用户点任务栏才推进。 |
| 新工具/新 page/设备或 HWND 重建 | 旧 ACK/回调按各自身份失效，重新请求当前所需 Frame/native 完成；不能复用旧成功 bool。 |
| 应用 stop/fatal | 关闭新输入、保存/停止 producer 的既有顺序保持；fatal 只走安全表面释放，不依赖一个已停止消费者再次正常 ready。 |

无论采用哪种终态错误呈现，都必须保留主栏/可取消入口可交互，并由已有 runtime completion、RenderPipeline 请求、Window command 完成和定时重试主动唤醒；**下一次鼠标/笔输入不能成为推进状态机的必要事件**。

## External References

本轮不需要外部检索；所有约束来自当前 Trellis 规范与实际代码。没有据源码分支存在宣称任何 Windows/Office 版本已经实测。

## Caveats / Not Found

- 本报告只是研究事实/设计约束，最终状态机正文由主会话提出并交用户审核；没有实施、编译、GUI、进程/窗口/PDB 采样、Git 操作或提交。
- “一次权威提交”需要设计明确 UI 业务一致性与异步 surface ACK 的边界；不能据此删掉真实 PPT UI-ready、Frame、背景或 native 结果。
- 单一现有 epoch 的奇偶语义、same-workspace 视图取消和旧 PPT business 权限迁移需在最终设计中明确；不能假设删除所有版本后仍安全。
- 正常交接失败后的“保留/回滚旧 scene”与“受保护错误视图”应作为明确可审核路径，而不是隐藏在无限等待里。现有规范允许真实终态错误安全释放 UI，但不允许把错误当空白内容成功。
- 未重新调查或修改冻结中的三份 Window 补丁，也未改任何已有 research/spec/code。
