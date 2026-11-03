# R2 五窗独立 Touch 与跨窗转交设计（用户已批准）

## 目标与边界

用户要求修复保持数位板感应、悬浮跨窗口后不能点击的问题；PPT/白板翻页使用与主栏一致的WM_TOUCH输入逻辑，拒绝所有带Pointer来源签名的兼容鼠标副本。真实鼠标/触摸板保留原生Mouse和可选Raw。输入接入统一，PPT按下翻页/长按与白板有效抬起单击的业务时机保留。

用户进一步明确五个窗口可各有一套Touch。本方案采用五套独立TouchContext，复用相同处理规则；只改当前五个UI窗口（Bar、PptBottomLeft/Right、PptMiddleLeft/Right）的Touch归一、业务归属和收尾。不改变Draw3绘制输入、Hook、共享样式、后台PPT业务、窗口布局、通用HiMsg或系统驱动，不加入分页震动。已有诊断输出保留，并增加接收窗→实际目标→route generation与取消结果。用户已明确批准五套独立状态方案并要求开始实现；当前进入实施与无窗口验证，不GUI、不commit/push。

## 确认事实与不能采用的局部修补

- 真实8677行日志中同一范围ID的Pointer会去Bar/左/右，但Touch接收窗在该段保持一个旧窗口。L5664-5681主栏真实点(1079,1856)的Touch在左下被background_miss丢弃；L6174-6188/L7104-7117的Pen分类Down和Touch分类Up又被拆开，26次跨窗lease拒绝。证据见research/tablet-hover-root-cause.md。
- Message.cppm已有同接触分类不稳定合同。因此统一过滤用IsPointerGeneratedMouseMessage高位签名，不用Pen/Touch低位区分Down/Up。Page独立Pen兼容业务分支应删除。
- 仅把Page过滤改成与Bar相同仍不能修复跨窗；仅在Touch旧窗口miss时忽略仍让真正目标收不到；仅在旧窗清lease也不能补回主栏业务。必须共同实现五窗Touch业务路由。

## 接收、选择与唯一目标

1. 保留五个原生窗口各自的RegisterTouchWindow、WndProc与TouchContext；WM_TOUCH分支复用同一份解码/选择规则代码，不共用可变业务状态。HiMsg callback不解码Touch、不改原消息默认流程。
2. 每个原始Touch handle只读取一次、按原协议恰好关闭一次，包括空批次/失败/忽略。复制为值型触点记录：hSource、dwID、flags/mask、原始时间/屏幕点，以及原生MSG/接收HWND和生命周期。不能把原handle通过SendMessage/PostMessage再交目标窗。
3. 各窗口独立保存primary/fallback、接触、代次、按压及capture状态，处理规则一致；保留现有来源互斥约束，不借本轮新增多指同时操作多个UI窗。独立状态不意味着同一次接触可由两个窗同时接纳。未接纳的新contact不能申请capture/lease。
4. 无活动接触的合格Down，使用TOUCHINPUT真实屏幕点选择实际目标。Down阶段所有业务/注册/provider锁外调用WindowFromPoint作唯一原生候选；只接受恰好属于五个已注册UI HWND之一的结果，不沿owner/父链把设置/画布/外部窗口强行映射成UI。
5. 再用该候选的已呈现输入probe确认可见、可交互/阻挡、真实背景/按钮/面板命中与语义版本。候选不属于五窗、ProbeMiss、native或组件未就绪则不接纳，不搜索后面其他窗口、不捕获，不向外部/画布转发合成输入。
6. WindowFromPoint会同线程重入WM_NCHITTEST，因此查询时不持router/注册/渲染/provider锁；查询后复核目标registration generation、semantic/display状态序号。它提供Win32原生命中资格，不是任意第三方透明/disabled窗口的光学遮挡证明，不为本任务建立桌面命中引擎。设置/模态native候选直接拒绝并保留既有组件enabled/modal准入；不以可见设置窗矩形一刀切遮挡，因为它可能在主栏背后。

## 真实输入区域与遮挡

- Page复用CaptureInputSnapshot(index,local)、BarPageSurface::SamplePointer及同源背景/菜单/主题遮挡策略；不能只比窗口矩形。输入locked但仍可见的控件背景应阻挡触摸，不应穿透触发后面的另一UI。
- 为让原生候选查询保留本进程locked遮挡，使用极小的owner线程TLS TouchQueryScope：仅在本次WindowFromPoint触发的纯NCHITTEST中，可见locked背景返回HTCLIENT；router拿到后按Blocked拒绝。常规鼠标NCHITTEST原策略不改，不在query期间触发业务或视觉状态。
- Bar必须覆盖主按钮/实际圆角背景、More、绘制/形状/橡皮/撤销重做属性、选色板、粗细浮窗/FineDial及其遮挡；复用现有已呈现命中/底栏Rigid-Body-Grip变换。必要时发布一个专用小型不可变输入区域快照，含现有几何/变换/可见遮挡及版本；不读取render线程可变对象、不复制整套Bar UI、不建通用节点树。
- 原生alpha=0区域可能不会成为WindowFromPoint候选；目前未发现必须把非UI原生目标强制映射回Bar的输入区域。现有透明包络不接纳，控件边缘/色板/FineDial以回归验证，按真实呈现可输入区域处理。

## 同一接触的生命周期

- 复用Bar的一份producer/session与Page的四份pointerClients，各窗口各有TouchContext：本地选中接触、业务/route代次、Preparing/Active状态、capture票据与取消状态。共享helper只操作显式传入的context，不新增全局currentTouch/active gesture业务状态。五套都在现有overlay HWND owner线程推进，不新增线程/全局hook。
- 跨窗转交层仅维护专用五槽目标登记和有界取消意图；按(device,contact)查询五套context中已确定的owner。Down无既有owner时选择实际目标，由目标自己的context接纳；Move/Up转交同一owner。它不持有第二份业务Session，不替任何窗口保存按钮按压或长按状态。
- 每个context以(device/hSource,contact/dwID,本地route generation)识别接触，跨窗身份同时带目标HWND+registration generation。代次归目标context，不能只拿数值相同的contact或generation跨窗匹配。范围pointerId不充当每次按下generation，也不据Pen/Touch低位改变归属。
- 目标确认后才走其值型Touch适配函数并建立原有Touch Session/lease/capture；不重入目标WndProc，不制造系统WM_MOUSE输入。适配函数把Down/Move/Up/Cancel按值送原共享worker，保持业务回调/长按仍在worker。
- Move/Up不重新选择窗口，沿Down锁定目标处理，即使原Touch仍到旧窗口或手指移出目标。Hover不转移活动接触；Down不命中UI后，后续Move进UI不能凭空开始点击。新的合法Down才重新选目标。
- 保留现有MouseChannel/SourceReplaced/Raw过滤和必要的非鼠标lease互斥，作为底层输入源/捕获协调，不作为全局Touch业务会话。移除Page的Pen兼容会话入口不等于删除租约；它仍保护真实Mouse与Touch接管。全局Windows鼠标捕获不能由五个窗口无条件互抢，取得/释放只作用于本窗已接纳接触。
- SetCapture可能同步重入旧窗取消；不持router/注册/provider锁执行捕获、释放或适配回调。新route用准备代次，捕获/语义复核后提交；中途重入失效则只回滚该代次，不挤掉更新的手势。捕获成功/失败规则沿原目标适配合同，失败/拒绝必须完整收尾，不能留下只申请的lease。
- 真实Up在原顺序排队后退休route并释放匹配资源；业务是否点击由原按钮状态机判定。取消不能伪造成功Up，也不靠固定超时/GetAsyncKeyState猜释放。

## 生命周期取消与旧消息

目标或原Touch接收窗隐藏/销毁/注销、工作区/页语义失效、设备/显示上下文失效、桌面失效、capture取消及共享队列溢出，必须取消匹配route、原客户端按压与长按、lease和捕获责任。

worker不能直接修改五个native TouchContext或释放另线程捕获；复用原生私有取消/释放消息带route generation+registration identity，回owner退休。captureTicket可能0，不能只靠capture票据判断是否还有路由。旧worker取消/Up、旧窗口同数值HWND和旧代次确认不影响新Down。跨窗按住/拖动期间不能把普通PointerLeave/hover当物理Up；可核验取消才撤销业务。

## 业务与触觉兼容

- PPT方向按钮Down立即执行，开启长按时内部tick持续重复，无需Move；移出/取消/Up按原策略停止。普通PPT翻页不误推进scene semantic导致长按自取消。
- 白板仍有效Up单次；Bar拖栏、底栏吸附、色块拖选、二维选色、FineDial及弹窗遮挡保持现有语义。统一的是接收/路由/会话，不把所有控件强改为同一触发边沿。
- Bar笔触觉保留原始Touch字段、源窗口与逻辑目标；目标为Bar时可与Bar已被动观察的Pointer凭据关联，不能因Touch原接收窗不同而错误丢掉元数据。凭据缺失只省略触觉，不能阻UI。分页本轮不新增触觉。
- 真实鼠标和精密触摸板按现有路径，Raw仅补充原本可拖动操作；所有带Pointer签名兼容鼠标在Bar和Page一致过滤，source低位只诊断，不决定UI归属。

## 兼容及验证边界

路由依据WM_TOUCH和Win2000已有的WindowFromPoint，不依赖Win8+Pointer API；原Pointer旁观继续动态可选。用户已选择统一Touch并拒绝兼容副本，不新增仅提供兼容鼠标而完全不提供Touch的笔驱动回退。当前两份实测都有Touch，只是目标窗滞留；不承诺所有设备/第三方遮挡情形。

五套独立状态可以避免窗口间业务状态串用，但不能改变系统把Touch投旧窗的事实；跨窗转交仍是本次修复必需部分。主要风险是Bar完整probe、同线程重入、native/worker双边取消收尾及被遮挡/锁定目标。确定性测试应使用实际router/适配接缝与已有helper，注入候选/呈现快照/捕获重入，不复制算法；GUI授权另行取得。

官方边界依据：
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-windowfrompoint
- https://devblogs.microsoft.com/oldnewthing/20101230-00/?p=11873
- https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-touchdown
