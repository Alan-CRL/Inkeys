# 技术设计：白板相对排序与纯层级恢复

## 证据与范围

20261021a 与 77f76038 的直接父版本：白板模式只提交 CanvasPolicy，非置顶刷新没有完整隐藏 PPT 全组置前；普通命令失败没有通用画布恢复屏障。77f76038 同时引入完整组成功硬门、两种 band 均逐成员 HWND_TOP，以及 snapshot.canvas=group。

此前获准 GUI 证据详见 research/authorized-gui-evidence-20261027.md。初次 mode 失败重试发生在背景启动前；进入后稳定 Active 但 Recovery 持续，真实隐藏 PPT 在 Bar 上方且 Primary 显隐变化。实际九成员连续，不能只移除 continuous。未捕获最早某次 SetWindowPos BOOL，因果判断基于确定的新增控制流、实际内部违约及切换前台后恢复的对照，不声称 Windows 内部调用细节已被捕获。

用户已禁止后续 GUI/运行时采样。只实施已被历史及现场共同支持的三点：白板后台内部排序、纯 Z 恢复写集、同一意图 UI 准备去重。保留真实 Draw3 首帧/背景/可见性准入，不用伪成功。

## A. 白板有限相对排序

Desktop/PPT 正常 Refresh 保留原政策。白板分支固定捕获角色 HWND+generation，核对命令及身份；必要时修复 owner 和 band，根降为 NOTOPMOST。基础 Mag→Freeze→Presentation→Drawpad 及其他 owned 关系仍核验。

Bar 保持当前全局位置，不调用其 HWND_TOP。捕获请求前 PPT 顺序，promoted 只调整目标子序列。anchor=Bar，按期望 PPT 从上到下处理；若 PPT 在 anchor 上方，仅用 NOACTIVATE|NOOWNERZORDER 相对下调到 anchor 后。每次实际写入后重新采样，防止旧 ordinal 使后续判断失真。没有 Bar 时以所需首个 PPT 为锚，只下调其他兄弟，不能把 owned PPT 放在其 owner 之下。最多两轮，身份或当前命令改变立即失败。

生产路径和无窗口测试共用 Window.ZOrderPolicy.h 内有限 Ops helper。成功要求实际 owner、band、基础内部序、Bar>PPT、PPT 子序列和代际；白板不要求外部全局连续性。Setting 不加入写序。保留既有 Shell 组绝对状态和一次显式异步激活。

## B. Z-only 恢复提前分流

Z-only 命令为 RefreshTopmost、PromotePpt、SetOverlayTopmost、SetOverlayFullscreen。快照保存角色身份、owner/band、原 PPT 子序列及对应政策旧值。Restore 在任何公共逐窗 FRAMECHANGED/Show/CanvasPolicy 之前显式分流；仅 canvas=false 不足以隔离。

纯 Z 恢复只修复实际不符的 owner/band/内部序，仅 SetOverlayTopmost 恢复其 topmost 政策、SetOverlayFullscreen 恢复其 fullscreen 政策；不写 mode/minimized、不恢复原几何/显隐/非 TOPMOST 样式，不碰 CanvasPolicy。PPT 恢复请求前兄弟序，但不刻意恢复非法 Bar/PPT 前态。

恢复核验 HWND/generation 和最新安全覆盖，不复用已过期原命令的 Current guard。恢复自身使用内部物理合同，不依赖外部全局连续或前台许可；普通 Desktop/PPT Refresh 的严格政策保持。真正物理失败仍持有原唯一恢复责任，成功后退休失败回执；禁止超时清空。后到 Hide/穿透、窗口退休/新代际、stop 不被旧快照覆盖，Shell 仍发布最新绝对状态。

完整模式/最小化/Canvas 几何或显隐事务仍走原完整恢复。不得通过全面删除恢复/准入解决闪烁。

## C. 同一进入意图只准备一次 UI

StateMonitoring 本地持有已准备 epoch 和原生模式重试时间。仅新进入 epoch 执行 CollapseAuxiliaryPanels 和 CancelPointerCapture；同 epoch 回退 Inactive/Exiting 后不重复。原生失败设置 steady_clock 250ms 期限，提前 runtime 唤醒不重复提交，判断先于设置 Entering。取消、新意图不受旧期限阻挡。真实 Active→Exiting 的清理保留。

抽取最小私有生产 helper，加入既有 --state-mode-production-test：多次失败只准备一次、虚拟时间退避、新 epoch/取消。保持 IdtState UTF-8 BOM+CRLF，不改状态、背景协议或公开 API。

## 不在本轮扩大

旧方案中的背景 token/ABA 重构、公有 Show/Hide/Bounds guard 扩展、最小化退出后台 ACK、成对 Bounds 快照及通用诊断系统不属于本次已证实回归主链，暂不实施。已有修复保持，不以顺手改动增加风险。

## 验证边界

真实生产 helper 故障注入覆盖后台排序拒绝、隐藏 Middle、外部穿插、代际/owner/band/readback、纯 Z 重试写集及安全覆盖；完整 solution、生产状态 CLI、无窗口全套验证。新结果不能证明 USER32/各系统修复后实机表现；按用户要求不再做 GUI 验收。

## 后续调查边界（退出异常）

当前研究以已提交ab79dcb9为基线，新增报告独立保存在research/exit-*.md。重点核对白板模式退出之后是否回到仍含全局TOP/continuous硬门的Desktop路径、完整模式回滚是否重建APPWINDOW/全屏状态、Exiting期间桌面绘制是否早于工具/Bar归位，以及UI主栏工作区快照的恢复时机。上述均为待验证问题，不是当前既定根因。

本阶段先调查，不改产品、不做GUI/进程现场采样；源码或离屏推理不能冒充已捕获此次真实失败API/阶段。

调查结果与下一修复边界见 research/exit-investigation-summary.md。当前保留既有离场顺序，优先评估物理交接/回滚与全局刷新分离；工具/输入因果待定向验证，不提前写成已实施方案。

## 桌面退出简化修复设计（本轮授权实施）

用户确认Desktop+物理鼠标，并要求简化。保留现有Fullscreen(false)→Leave→Topmost(true)→ClickThrough顺序，不增阶段标志、scope枚举或新Host输入协议。

Window新增一个私有CommitOverlayGroupPolicy：Capture当前固定窗口身份→复用现有ReconcileRelativeOverlayGroup(保留当次PPT序，真实owner/band/内部关系)→ApplyOverlayFullscreen→当前请求确认。三个mode/topmost/fullscreen政策命令和完整group Restore末尾使用它；周期Refresh/Promote/Setting全局路径不变，白板Refresh保留相对路径。物理操作失败仍保留完整恢复，不吞false。

不扩大完整mode快照的原PPT序保证，不修相邻已存在跨gen全组CanvasPolicy风险，不新建通用事务框架；现有Z-only/Promote保存原序和退休子树的严格恢复维持。无窗口验证复用真实生产内部提交/排序helper，并通过源码复核私有dispatch；不冒充真实Service/USER32动态运行。

输入研究已确认物理鼠标可以通过RTS TDK_Mouse，MouseInput fallback的Selection拒绝不覆盖它。仅在确认所有工作区的实际Selection都不得落笔、没有合法选区/工具副作用依赖该Down后，才把现有白板选择Down抑制收敛为一致语义；不靠增加Exiting bool或更改activationAllowed充当输入门。具体结论随后写入research/exit-desktop-mouse-followup.md，未确认不改该部分。

### 鼠标选择输入的确认修复范围

已查明RTS成功时鼠标经TDK_Mouse→MouseLeft，Win32 fallback的Selection拒绝没有覆盖它。Selection是电脑操作态，Controller没有合法选择绘制Down消费者；右键/笔尾临时擦除也要求非Selection。当前新Down仅对白板Selection Discard，Desktop/PPT仍可能初始化stroke，故改为所有workspace的实际Selection统一DiscardUntilTerminal。无需增加phase或Host协议。

仅改Draw3.DrawingController.cpp：把选择态丢弃新Down的现有操作抽成最小私有共用函数，Run的实际新Down路径和已有RunDraw3ControlFenceProductionProbe调用同一操作。既有--draw3-control-fence-test在GUI初始化前早退，只使用未Attach WindowController和真实ContactInputCoordinator，不创建HWND。覆盖Desktop/PPT/Whiteboard、RTS MouseLeft/Right/Touch/Pen、Selection中Down及后续Move/Up不因切回Pen补画、新一轮非Selection Down可用。CLI动态证明共用操作+真实DrainIngressBatch，完整Run接线由源码复审，不冒充全Run/Win32原生分发实测。
