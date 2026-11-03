# Research: Bar/PPT/白板统一 WM_TOUCH 的风险与验证清单

- Query: 统一Touch业务并拒绝全部Pointer生成兼容Mouse后，必须保留的语义、最小回归范围、旧测试调整与未决产品行为。
- Scope: internal
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar
- 已授权方向：业务统一 WM_TOUCH，保留物理 Mouse；本轮仅规划，不实施。

## Findings

### 现有代码已确认的约束

- `Input/UiPointerInput.cpp:54` Session以source/generation/device/contact锁定接触，`:71` 仅同身份终态结束；`:562` BeginTouch和`:584` EndTouch维持跨窗口非Mouse租约，旧End不可释放新owner。
- `UI/PageControl/PageControl.cpp:2861` 起仍有Pen兼容Mouse专用会话；`:2817`、`:2903` 只过滤Touch生成Mouse。本次应移除该业务分叉，改为与Bar一样拒绝全部Pointer签名Mouse，而非让签名在Down/Up两端选不同业务路径。
- `Message/Message.cpp:61` IsPointerGeneratedMouseMessage是全签名去重入口；Raw侧 `UiPointerInput.cpp:716` IsPromotedMouseExtraInfo已有相同签名过滤。`:722` HandleRawMouse保留hDevice可空，当前没有以nullptr拒绝精密触摸板。
- `PageControl.cpp:3000` 起同批primary优先、fallback替换和同source/id处理；`InkeysHeadlessTests/page_control_tests.cpp:1416` 起有真实共享策略用例。Bar有独立producer/selection路径，不能仅改消息过滤后声称完成统一。
- `PageControl.cpp:2529` PPT方向按Down执行；`:2694` 起独立长按repeat；`:2607` 白板方向按有效Up执行。统一输入来源不能统一成同一点击边沿。
- `Bar.Interaction.cpp:3099` 附近当前Touch先capture/入队、之后业务命中；既有日志证明透明/窗外坐标也可能被旧接收窗处理。新路径应在**决定实际目标且确认有效背景/控件后**建立其业务归属。
- `PageControl.cpp:3116` 附近 SetCapture 后复核命中/semantic，说明原生capture可同步重入取消另一窗；新router不能破坏此边界。

### 必须保留的策略，不应作为清理目标

1. 非鼠标lease仍用于活动WM_TOUCH阻断/退休旧Mouse手势。全签名Mouse拒绝不等于删除BeginTouch/EndTouch、nonMouseRevision或MouseGesture::SourceReplaced；真正Mouse+Raw补充仍需要它们。
2. Touch的device仍是hSource，contact仍是dwID，另加业务代次/目标注册代次。笔触觉pointerId只作可选旁路身份，不替换TouchID；不能拿“日志里数值相等”当API合同。
3. Mouse普通按钮仍仅native Down/Up执行业务，Raw只补实际拖动；Raw unavailable、不匹配、pending取消都不应阻断native点击。hDevice=nullptr不应被新统一分类误判为伪Mouse。
4. capture丢失、隐藏/销毁/注销等走Cancel；不造成功Up，不用固定按住时长猜松手；迟到旧Up/旧release不能结束新接触。新路由失败只结束自己的reservation/lease。
5. Pointer仍可用于hover位置/来源观察及可选haptic，不承担第二套Down/Up业务。主栏已接的触觉凭据/接受点保留；本轮不得因为统一Touch顺便给PageControl新增触觉行为。

### 风险与最小回归矩阵（建议）

| 场景 | 必须验证的结果/风险 |
| --- | --- |
| 单批多点：primary先/后，fallback已按住，旧fallback Up同批迟到 | 先取消旧归属再选primary；旧Up无点击；一批无primary仅首个合格Down；不得把第二点路由到另一窗口形成第二次业务。 |
| 同dwID不同hSource；同hSource/dwID新代次；TouchID重用 | 各身份隔离；旧Move/Up/Cancel不污染新owner；数据来源与触觉身份独立。 |
| 一个TOUCHINPUT同时带Down/Move或Move/Up；分批同毫秒不同点 | 保留原顺序/末段真实坐标，终态最多一次；不能只按通知数量更新。 |
| Hover A→B、笔始终在感应范围，无活动触点 | 未来Down命中B；A不得仅因上次收到该pointer的Touch而继续抢处理；Hover不持活动Touch租约，不需离范围重置。 |
| 已在A按下，按住/拖动越界到B再松手 | Down owner固定A，后续Move/Up不能因Hover或新hittest转给B；B无点击，A按控件规则提交或取消。 |
| 原始WM_TOUCH仍投A，真实点在B；A点位无有效背景 | 由统一router选择有效目标B并只执行一次；不得A先SetCapture/BeginTouch后才发现miss。无任何合法目标则不建立业务按压。 |
| 表面重叠：属性浮窗/白板菜单/PPT按钮/透明光影包络 | 按实际可见可交互层和已提交hit决定目标；popup背景遮挡不能穿透点后面的按钮；透明装饰不应抢Touch。 |
| PPT上一页/下一页 | Down一次即执行；Up不再重复；长按按既有键盘repeat时序；移出/取消/隐藏/场景失效停止repeat；EndShow不误连发。 |
| 白板翻页/加页/More/主题按钮 | 仅同页身份有效Up触发；加载/切页generation改变拒绝旧接触；新增页和主题编辑不被repeat。 |
| Bar主按钮抓取、底栏拖动、展开/折叠 | 屏幕抓点、阈值、缩放/显示切换与现有业务语义保留；Touch统一后不借新目标重抓或把拖动转成其他表面点击。 |
| 色板拖选/二维色盘/粗细Slider/预览/FineDial/橡皮属性 | 连续候选、有效Up提交、cancel不提交、revision失效和可选haptic轻点规则均保留；不能为普通按钮统一而替换这些连续状态机。 |
| 真Mouse/精密触摸板与笔/手指交替 | native普通点击和Raw拖动继续；Touch/Pen签名Mouse全丢；Touch接管版本退休旧Mouse；lease结束后的新Mouse立即可用；同一物理事件不双发。 |
| 隐藏/最小化/工作区切换/页语义/DPI改变 | 旧target token取消一次；新target注册代次不受旧取消影响；显示变化重读屏幕/逻辑转换，不把Bar zoom误当screen坐标比例。 |
| FIFO溢出、窗口注销/销毁、退出、投递失败 | worker与producer/router各自归属均收尾，不能只清下游点击状态而遗留共享lease；终态失败需明确Cancel，下一次合法Down可继续。 |
| SetCapture同步重入WM_CAPTURECHANGED/WM_CANCELMODE | 不死锁、不重复释放、不清后来建立的新owner；返回后复核目标registration generation/semantic/hit，再commit该接触，失败只释放自己的reservation。 |
| 缺Touch/不合法Touch包 | 拒绝整个不可信接触、释放HTOUCHINPUT，不用Pointer或兼容Mouse偷偷补业务；失败日志清楚区分缺消息/路由失败/合法性失败。 |

**锁/重入最低要求**：不在router/provider/注册表互斥锁内执行SetCapture/ReleaseCapture、SendMessage、控件回调或等待worker。先在锁内保留带代次的候选身份，锁外执行原生效果，回锁复核仍为本次候选；重入产生的新代次优先，旧事务不得清它。末尾CloseTouchInputHandle由唯一原始消息owner执行，内部路由传按值样本而非让多个目标重复读取/关闭同一个HTOUCHINPUT。

### 旧测试与合同应如何调整

- `InkeysHeadlessTests/ui_pointer_tests.cpp:369–379` 跨channel lease排他/旧End、`:711–731` SourceReplaced、`:755–775` 排队Mouse来源快照均保留。`:723` “Pen compatibility can use nonmouse ownership lease” 是旧生产路径描述，改为统一WM_TOUCH/非Mouse拥有权场景；不必删除共享Session的Source::Pen枚举或重构泛用状态类。
- `ui_pointer_tests.cpp:1069–1074` 诊断leased owner/旧End/RawCancel不释放Touch及锁外输出继续保留；router可以管理何时申请/释放，但不能把Raw Cancel扩大为全局非Mouse释放。
- `InkeysHeadlessTests/page_control_tests.cpp:87` Mouse/native+Raw、`:1247` 工作区/PPTDown/白板Up策略、`:1416–1503` Touch primary/fallback与hSource区分保留。新增跨目标路由集成测试应调用真实统一router+目标提交接口，不能只复制HitTest公式。
- `UI/PageControl/PageControl.cpp:3878` 附近真实handler离屏回归：PPT Down不重解释成白板、Whiteboard generation变更Up拒绝、过期registration/菜单instance不作用于新接触等保留。其构造SurfaceInputEvent不证明原生消息接线，必须增加统一入口的生产CPU接缝回归。
- `.trellis/spec/native-desktop/ui-pointer-session.md` 现有“Pen保留PageControl兼容Mouse”合同需更新为本次已授权方向；其他Raw/Touch/lease/版本合同保留。`pen-haptics.md`仍要求不猜pointer身份，不能因本次路由修改放宽。
- 两段真实日志反例必须转为回归：Down 0x72d / Up 0x7ad 全部Pointer兼容Mouse不入业务；同一contact的WM_TOUCH经真实目标一次DownUp，不再留下Pen兼容会话；旧接收窗收到远离其rect的样本不能先capture/lease。

### 是否还有产品决策

本次输入路线、全部兼容Mouse去重及物理Mouse保留已明确，不需再向用户询问是否采用。PPT/白板不同触发边沿、按住跨窗不转移、primary/fallback单接触、取消不点击等均有现成代码/合同可保留，属于实现约束，不制造新的选择题。

若设计打算改变以下任一项才需要用户决定：PPT按Up而非Down、长按重复次数/时机、同批多点允许操作多个窗口、PageControl新增笔触觉、缺WM_TOUCH驱动回退Pointer业务、扩大Win7/API支持承诺。当前均应列不变/不新增，不把这些作为本轮阻塞问题。

### Files found / Related specs / External references

- `Input/UiPointerInput.cpp`：共享来源/lease与Mouse+Raw策略。
- `UI/Bar/Bar.Interaction.cpp`、`Bar.InteractionWorker.h`：Bar复杂手势、共享FIFO/overflow及触觉入口。
- `UI/PageControl/PageControl.cpp`：Touch选择、旧Pen兼容分叉、不同业务触发与生命周期。
- `InkeysHeadlessTests/ui_pointer_tests.cpp`、`page_control_tests.cpp`：现有纯策略/来源回归接缝。
- `.trellis/spec/native-desktop/ui-pointer-session.md`、`pen-haptics.md`：应保留和需要更新的合同。
- 本轮仅内部代码与现有日志结论，不使用外部资料。

## Caveats / Not Found

- 只写本研究文件，未改产品/spec/其他任务材料，未运行Git/build/GUI/commit。
- 本文是风险/测试清单，不宣称新router已经存在或通过测试；跨窗router接口与窗口目标集合由主会话及另一研究者负责。
- 无窗口接缝测试可证明路由与状态，不证明Windows原生消息分发、实体数位板、多显示器或真实Win7表现。用户未授权本轮GUI验证。

## 后续用户澄清（五套状态）

用户指出四分页加主栏可有五套Touch以防串用。主计划已明确五套独立窗口Context复用规则；本文router均指对显式Context的纯路由决策/薄跨窗转交，不是另建global currentTouch业务FSM。每包扫描五套本地Context找已拥有的(device,id)，Down无owner才选目标，Move/Up交给本地Down owner；本地generation加目标注册身份隔离。现有单UI接触/Mouse与Touch资源互斥仍保留，不借本轮扩为五指并发。
