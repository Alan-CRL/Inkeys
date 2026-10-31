# 技术设计

## 数据流与根因

RTS Touch Down 或 `WM_POINTERDOWN` → `WindowController` 的触摸通知 → 主光标样本/归属 → `DrawingController` 瞬态光标；窗口线程另外在 `WM_SETCURSOR`/私有刷新中决定系统箭头。Touch contact 圆环由绘制线程按活动 runtime 单独生成。

当前通知只清旧 Mouse 样本和记录时间屏障。持久 Pen owner/样本可继续产生主光标；选中工具不是 Eraser 时系统箭头也可继续显示。Touch Up 不应据此重新恢复旧光标。Pointer 提升的 Mouse 消息若没有被兼容签名识别，仍可能重新接管。

## 修改边界

- 在 `WindowController` 增加一个原子触摸视觉接管标志：Touch Down 设置（活动 Pan 已由真实 Mouse 接管时不重新抢占）；Touch Up/Cancel 不清除；可信的真实 Mouse/TouchPad 或新 Pen 样本清除。状态变化走已有绘制唤醒和系统光标刷新，不从窗口回调触碰 D3D。
- `CursorOwner()` 返回用于当前光标解析的有效归属。活动 Touch Pan 已由真实 Mouse 接管时继续优先返回 Mouse；否则触摸接管返回 Touch，从而让主自绘光标解析器不选择旧 Pen/Mouse 样本。持久 `cursorOwner_` 仍只记录 Pen/Mouse/Unknown；Pointer 事件更新持久值时必须读取该字段，不能把暂时的 Touch 值写回。
- `ShouldHideSystemDrawingCursor` 对有效 Touch 归属隐藏系统箭头；即使当前产品工具不是 Eraser 也适用。当前已有的 Eraser/Laser 与 Touch Pan 规则保留。
- Win8+ 动态解析 `GetCurrentInputMessageSource`，在处理 `WM_MOUSE*` 时拒绝来源为 Touch/Pen 的兼容消息；可识别的 Mouse/TouchPad 正常接管。Win7 或来源不可用时沿用现有兼容签名、消息时间屏障和 Pen fallback。

## 兼容与验证

光标显隐策略不新增全局状态或持久配置。多指 RTS 计数仅表示接触生命周期；接管标志在首个 Touch Down 后保持，避免最后一指抬起让旧箭头回弹。真实笔悬停/鼠标新消息不等待定时器。纯逻辑回归测试覆盖有效归属、系统光标策略、消息来源过滤；现有隐藏 Host 测试覆盖触点可视化清理。硬件上的 Windows 7 和混合设备需要用户后续验证。

## 复测后的诊断阶段

用户报告原问题仍复现，现阶段仅取证。沿用 `Experimental.Inkeys3.ConsoleOutput` 的 Debug 控制台设置与“下次启动生效”语义，新增独立 `Cursor` 开关。启动时先建立控制台，再启用 Draw3 光标诊断；关闭时不做光标热路径格式化或输出。

RTS/窗口输入线程只向固定容量队列记录定长事件（序号、tick、线程、来源及状态），Draw3 绘制线程批量输出，并在溢出时报告丢弃数量。窗口侧记录 Touch 生命周期、Pointer/Mouse 消息过滤、Pen/Mouse 样本与系统光标决定；绘制侧按可见来源或关键状态变化立即记录，持续移动期间每 100ms 记录一次最终主光标、逐触点圆环或激光笔尖的尺寸、位置、透明度和呈现结果。日志不参与光标显隐或笔迹状态机。

## 诊断日志确认的残留路径

三次独立触摸的日志中，活动接触均只有 `source=touch`，Touch Up 后一帧清成 0 visual；之后约 110ms、1610ms 和 1203ms 各有一条 `source=IMDT_UNAVAILABLE`、`extra=0`、坐标等于最后 Touch 点的 `WM_MOUSEMOVE` 被接受，触摸抑制随即清零并出现 `source=primary` 半透明橡皮圆环。日志无丢弃，不能把这三次残影归咎于渲染未清理或活动 Touch runtime 未退休。当前日志没有证明触摸过程中曾出现第二枚主光标。

窗口线程沿已识别 Touch Pointer/兼容 Mouse 事件记录最近 Touch 客户区位置。在触摸视觉归属仍有效时，来源未知且停在该位置的 `WM_MOUSEMOVE` 不得解除抑制；真实 Mouse/TouchPad 来源、鼠标按键/滚轮或来源未知但位置改变的 Move 继续按既有路径接管。此规则不使用触摸后的固定等待时间，以覆盖日志中的不同迟到间隔；其纯判定由无窗口测试验证。

## 最新复测与本轮修改边界

最新附件 bf983934-b136-446e-8777-35729d9c19a6 含 963 条连续记录。seq=123 是带按键状态的未知原位 Move，被调用处的 `!buttonDown` 例外放行；seq=130 实际呈现双圆环，seq=267 抬起后仍呈现按下主光标，seq=272 变为悬停主光标。这纠正了此前仅覆盖抬起后无按键 Move 的诊断结论。用户批准先增强取证，不把本轮构建通过记为光标已修复。

最小差距位于 WindowControl 的来源查询和消息过滤观测：当前 source=0 合并了三种 API 状态，且记录缺少过滤前样本。WindowControl.cpp/.cppm 增加只用于诊断的来源快照、每窗 event 编号、末触点元数据和 before/after 日志；来源返回值与全部接管条件保持一致。Diagnostics.cpp/.cppm 负责记录初始 Raw Input 注册快照、解析既有 WM_INPUT 及显式截断标记；Bar.Interaction.cpp 只接入观测与记录实际注册/注销结果。关闭开关时不读取 Raw Input 或做日志格式化。

Bar Raw Input 注册在边缘光激活时开启、落笔时关闭，本轮仅观察此生命周期。日志必须说明该覆盖限制，不能把没有 raw-mouse 行解释为硬件没有动作；也不能在 Drawpad 再注册同一类设备而覆盖 Bar 的接收窗口。

验证采用任务目录 verify_cursor_trace.py，直接读真实输入处理与呈现日志，关联来源未知原位接管、双光标、抬起按下残留、迟到悬停。检查器用实际日志验证，并以合成的明确 Mouse/TouchPad/Pen 接管、日志丢失、截断及 schema=2 事件配对序列验证自身。它不复制生产判定，也不宣称执行了真实 WndProc 或硬件回归。

## 已授权的系统来源修复边界

最小行为缺口是 WindowControl 接管前忽略 originId。PenCursor.cpp/.cppm 提供统一来源过滤请求/结果：收拢已有来源/时间屏障、位置回退与按键例外，并优先拒绝触摸归属下已确认的系统注入未知设备 Move。WindowControl.cpp 将实际消息快照传入该入口，直接依据结果早退，诊断复用同一结果。无新增全局输入状态或 Win7 API 依赖；保留未知来源的现有回退。Headless 测试调用同一入口，沿消息序列与已有光标解析器检查残留/恢复；不借本轮改擦除几何、Raw Input 注册或其他窗口层。
