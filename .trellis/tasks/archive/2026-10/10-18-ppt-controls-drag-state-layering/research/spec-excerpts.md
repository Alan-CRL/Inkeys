# 任务相关规范摘录

2026-10-18吸附修订同步。以下为当前canonical spec窄摘录；用户已授权20DIP中轴/侧边双门槛和140ms UI3吸附Y过渡，覆盖首轮40DIP。历史报告保留基线证据，完整合同以原文件为准。

## .trellis/spec/guides/cross-layer-thinking-guide.md:41-46

### Owner WndProc to render/presentation thread

- owner WndProc 不得阻塞等待一个可能被渲染线程持有、且渲染线程又可能同步等待 owner 执行 Window Service 提交的锁。PageControl 直移只能 `try_lock`；每个候选先发布 latest-wins 绝对布局，锁忙或窗口移动失败时保留 pending 并显式请求 pair render，下一条输入或渲染兜底都必须收敛到最新绝对目标，不能假定一定还有后续 `WM_MOUSEMOVE`。
- PageControl 普通拖动纯平移成功只顺序移动成对 HWND、推进内部 bounds/mailbox revision 并同步第三光源接受区；不得调用 `Scene::SetBounds`、制造 visual damage 或请求渲染；中轴吸附的短促 Y 动画中间直移仅发布过期帧屏障、不确认最终 pair，并显式请求续帧，实际几何到位后才提交。渲染帧在取得 `renderTransactionMutex` 后、进入 Configure/ULW 前必须复核直移 revision，避免旧快照在直移后把窗口拉回。
- `SetCapture` 转移旧 capture 与 `ReleaseCapture` 都可能同步重入 `WM_CAPTURECHANGED`。PageControl Down 必须先在锁外完成旧 capture 清退，再在短锁内复核并建立新 gesture/tracker；不可把捕获简单挪到新 tracker 已建立之后，否则旧 Cancel 会回滚同 pair 新状态。`WM_CANCELMODE` 必须先在 `renderTransactionMutex` 内清 pointer/drag/touch 状态，释放该锁后再调用 `ReleaseCapture`；Window Service 撤销必须覆盖四个 PageControl HWND。对 `SetWindowPos`、同步 `SendMessage` 等其他可重入调用，应按实际重入消息审查其是否会再次取得调用点仍持有的非递归锁，不能用笼统的“线程安全”判断代替锁图。
- 透明 presentation margin 与共享圆角背景之外的像素必须继续返回 `HTTRANSPARENT`。PPT 的“非按钮背景可拖动”只覆盖实际背景 shape，不等于整个矩形 HWND 都可接收输入。

## .trellis/spec/native-desktop/rendering-and-ui.md:429-429

- DragHandle 是 PageControl 自有且仅 PptCompact 存在的 shape/hit region，不属于 Bar 按钮，无 hover/press/click/selected/content 视觉。PPT 成对拖动可从 DragHandle、Page 和未被 Previous/Next 占用的真实圆角背景开始；透明 presentation margin、圆角外像素和 diffuse 光晕继续 `HTTRANSPARENT`，不能把整个矩形 HWND 当作背景。Previous/Next 永远是纯按钮。Page 按下先产生标准 press，超过 `SM_CXDRAG/SM_CYDRAG` 对应系统阈值后取消 press 并转为拖动，阈值内且仍命中 Page 时抬起调用 `ViewShow`，不写位置。Up 必须先按当前 HWND 把事件 client 点转为屏幕点，按同一阈值采纳合法最终候选，再清理拖动；只有 Up 才越阈值的 Page/background 也按最终拖动处理，不能误触预览。Whiteboard 不创建 DragHandle 或 drag candidate。

## .trellis/spec/native-desktop/rendering-and-ui.md:432-432

- Enter 开始即 cancel capture、关闭 DragHandle hit；Window Service 撤销必须广播 Drawpad、四个 PageControl HWND 与 Bar。PageControl 的 `WM_CANCELMODE` 在 `renderTransactionMutex` 内清 pointer/drag/touch 状态，释放该锁后才调用可能重入 `WM_CAPTURECHANGED` 的 `ReleaseCapture`。Down 的 `SetCapture` 同样必须在该非递归锁外：先完成旧 capture 的同步取消，再在短锁内复核本次 gesture/session/epoch/mode 与命中并建立新 tracking；长按依赖的 `GetCapture` 判断位于捕获完成之后，不能用递归锁掩盖旧 Cancel 覆盖新 tracker。背景、三枚按钮、Drag slot/opacity、SVG/文字和 HWND bounds 使用同一批次从当前成功呈现值重定向。PPT 底栏可见时同时移动到 Whiteboard 固定 `5 DIP` 角落并形变；不可见时直接用最终几何渐显。Exit 反向返回 PPT 最新运行时位置，稳定 PptCompact 成功呈现后才恢复 DragHandle hit。

## .trellis/spec/native-desktop/rendering-and-ui.md:438-441

- PageControl 继续拥有 stable backing、logical/presentation 映射、direct-move revision、ULW/Window 提交与调试覆盖层事务。共享 Bar 运行时返回 animation/damage，不直接调用 Window Service；隐藏生命周期不得被光源动画无限延长。渲染线程可在 `presentationMutex` 内同步等待 Window Service owner，因此 owner WndProc 绝不能阻塞等待该锁。每次拖动采样必须先按原始 drag 起点计算并发布 latest-wins 绝对候选；发布本身不得请求 pair。直移目标必须分别解析上一 feasible layout 与当前 candidate layout，确认尺寸、scale、mode 仅发生平移，再由 candidate logical bounds 加减当前不变的 presentation outset 得到绝对 HWND 目标；不得依赖上一候选已成功提交，也不得为取得目标调用 `ApplySceneBounds`。
- `presentationMutex.try_lock()` 成功后，owner 依次对两个 HWND 调用 `SetWindowPos`；第一窗失败直接保留 pending，第二窗失败必须把第一窗回滚到 original，并记录 first/second/rollback 阶段及 Win32 error。普通最终目标两窗成功后才对两个 `SurfaceState.bounds` 调用 `SetBoundsDirect`、提交 mailbox/direct-move revision，并立即发布两个 cursor-light 接收边界；mailbox/layout/bounds payload 必须先完整写入，再以 release store 发布单调且由 `dragCommitMutex` 串行的 revision。普通完整直移热路径不得调用 `ApplySceneBounds`、产生 Scene damage 或请求 pair。吸附动画的中间直移只推进 direct-move 过期帧屏障，不确认最终 pair，并显式请求 pair 续帧。普通拖动只有锁竞争、非纯平移或窗口移动失败走 pair render fallback。较新的候选可以覆盖旧候选，但 fallback 必须保留最新候选，不能依赖另一条可能被合并的 `WM_MOUSEMOVE`。渲染帧以 acquire 读取 revision，取得 `renderTransactionMutex` 后、`ConfigureSurface/PresentScene` 前必须复核，窗口提交后继续第二次复核；任一过期帧均返回 `Retry`，不得先以旧 ULW 拉回 HWND。松手在合法终点采样后只 `RequestAll` 一次让 Scene/layout 吸收最终位置，并且不得在最新候选成功提交或明确回滚前清除 pending 所有权。正在拖动及 released pending 的同 pair 非吸附纯位置 fallback 必须直接落定，包含同组未设 `dragging` 的另一面；目标相同也必须结束旧位置缓动。只有明确的中轴吸附 Y 修正允许有界短动画，不能恢复普通拖动的旧位置缓动。render 只有实际 logical/presentation 像素几何与本帧拟合目标一致才推进该面 commit，不能以动画中间帧凑满两面 mask；正常显隐、倍率和工作区形变仍沿用原动画。采集 pair ownership 与快照时保持 `dragCommitMutex -> snapshotMutex`，禁止新增反向锁序。
- 中间竖向 pair 的主体中心吸附屏幕水平中线；`middlePairHeight=0` 即居中，不另加边距/阴影补偿。复用 `BarBottomDockThresholdDip` 的20屏幕DIP，同时要求 `abs(middlePairHeight)` 与 `middlePairWidth` 均不超过阈值，含等号；后者已是主体距对应屏幕侧边扣除既有缩放间隙后的额外距离，不读取阴影/outset，也不强制 X 归零。阈值仅乘同一布局屏幕的归一化DPI，不乘控件倍率。先裁剪原候选，再尝试中心候选；中心碰撞保留可行原候选，否则上一 feasible。仅吸附 Y 修正复用现有 UI3 PageControl 曲线/RenderPipeline 做140ms过渡，两面共享起点时刻和单调采样进度，owner采样已推进后不得被较早的RenderPipeline批次frameTime回退；X及时跟随；重复 Move、页码发布和 Up 不重启，同目标完成后不重播，released pending等两面真实几何到位才提交。过渡不新增 inputLocked，离开任一阈值、真正Cancel、新gesture/session/epoch及模式/显示布局失效可打断；Up后无活动pointer的capture取消不能清掉刚释放的pending吸附。只在用户拖动/合法Up采样吸附，普通页码发布不得重写已存偏移。
- 当前 COM→Draw3→页码提交→输入开放、session/target revision、采样/唤醒、位置持久化及有效缩放合同见 [native-session-ui3.md](../ppt-interop/native-session-ui3.md)。Ppt 生命周期不得再由 presentationVisible 派生；成功呈现前不发布 ready，成功页码提交前不开放新页输入。

## .trellis/spec/native-desktop/rendering-and-ui.md:1711-1713

- 四个分页 HWND 的业务显隐只由 PageControl 提交：`Acquire()` 注册客户端后以 `RequestAll()` 唤醒，成功呈现后 `SetBounds/Show`，Hidden 目标使用 `Hide`。`TopWindow()` 不得无条件显示分页窗；`IdtWindowsIsVisible.pptWindow` 只表示客户端注册就绪，否则尚未布局的 `(0,0)` 初始矩形可能滞留左上角并干扰新接触。
- 基础 overlay owner 链只在创建时建立：`Mag -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar`；Mag 缺失时 Freeze 为根。Drawpad 是 DrawpadPresentation 的顶层 owned popup，不是 `WS_CHILD`；该传递链必须保证 Bar/PPT 高于两套画布表面。Presentation mode 中 overlay 保持 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`。Whiteboard mode 是显式例外：Freeze 切为唯一 `WS_EX_APPWINDOW`、可激活和任务栏锚点；Drawpad 清除 `WS_EX_NOACTIVATE` 但保留 `WS_EX_TOOLWINDOW`；其他成员仍为非任务栏辅助 UI。Bar 必须高于所有 PPT；共享底窗或其他 PPT show、`PromotePptWindow` 只把目标窗放到 Bar 正下方。置顶刷新只对链根调用一次 `HWND_TOPMOST` 或 `HWND_NOTOPMOST`，且 Whiteboard mode 强制 NOTOPMOST。Win32 会把根的 topmost band 变化传播给 owned popup；刷新后非根出现 `WS_EX_TOPMOST` 不能证明代码对它执行了独立置顶，必须审查 `SetWindowPos` 调用点。白板期间对 Freeze 调用 `ITaskbarList2::MarkFullscreenWindow`，退出和销毁前清除。
- PPT 可见性 `false -> true` 发布完成后立即请求一次根置顶刷新；成功后连续可见状态去重，失败时保留 pending 并由既有 500ms 发布节拍重试，离开放映取消 pending。PageControl 的 present 成功不代表 HWND 提交完成：`SetBounds/Show/Hide` 任一步失败都返回 RenderPipeline `Retry`。Draw3 surface 切换失败同样保留 reconciliation pending，由既有 250ms 状态节拍重试；只有窗口提交成功后才更新 drawpad ready 事实。

## .trellis/spec/ppt-interop/native-session-ui3.md:42-49

### 位置、保存和有效缩放

- facade 拥有运行偏好；PageControl 候选和成功提交按 session、layout epoch、pair version 交接。tracker 候选身份使用同一 D→S 短事务取得的 published 原始布局，runtime fit 只用于手势起点/可行几何及实际呈现；released pending 上开始下一次手势不能在相同 revision 上以 fit 倍率覆盖原始候选。全局 direct-move revision 只用于过期帧门禁，不是另一组的提交版本。旧完整快照在 ownership 释放后也不得覆盖新位置。
- 拖动候选的最终位置交接必须等同 pair 两面实际几何到位；released pending 继续持有布局，非吸附渲染兜底的纯位置移动不沿旧动画继续滑行；仅中轴吸附Y修正允许140ms的UI3过渡，中间帧不得提前确认pair。Up 使用该事件屏幕终点，保留 Page 阈值内预览及 Arrow/Whiteboard 边界；取消与旧代次不写回新手势。中间竖pair仅在距水平中线及距对应侧边扣除既有间隙后的额外距离均<=20屏幕DIP时吸附，复用BarBottomDockThresholdDip、只按既有布局DPI换算，不随控件倍率放大且不绕过组间碰撞；X仍跟随、两面同一动画时刻且进度单调，较早的render批次时间不得回拉owner已呈现坐标；重复采样/页码/Up不重启，真实打断与Up后旧capture取消分别处理。
- 拖动永不触发写盘。开关打开/关闭瞬间保存；记忆开启时真正结束放映或正常退出软件保存；关闭后保持冻结位置。每场进入恢复最近一次明确保存请求的冻结基线（当前进程可含尚未完成的请求），新进程恢复实际磁盘值。重置无论开关均更新默认值。
- 冻结请求基线与 durable saved revision 分开，不能将待保存称为已保存。两个业务队列统一版本仲裁；旧 JSON 跳过。临时文件完整写入/flush 后原子替换；失败保留原文件和原请求重试，不采集后续临时拖动。
- DPI、用户缩放、最终有效缩放分别归一化；最终值传入 Scene 后不重复 DPI 上下限裁剪。绘制、文字、命中、阴影、presentation 使用相同值。主屏布局边界与 DPI 来自同一显示快照；运行适配不写保存偏好，backing capacity 不决定呈现大小。白板继续独立布局。
