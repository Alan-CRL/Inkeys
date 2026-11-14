# 白板移动模式：可靠输入、释放与诊断

## 1. Scope / Trigger

适用于Draw3白板Selection移动模式的MouseLeft、Touch、Pen/PenTail。接触输入保留已修复的连续手势正确性和呈现节拍；Surface Pro 11触摸正常不表示旧距离/时长最优。2026-11-13收尾移除未被产品使用的Chromium参考估速器与PhysicsBased/Legacy曲线，当前只保留可靠尾段估速和向量摩擦。2026-11-12起PTP双指扩展暂时停用：本机探针证明RTS与DM在同一HWND的Activate/Enable存在输入委托冲突，独立DM HWND也无法跨窗SetContact；`TouchpadNavigationOwner::Initialize`必须在任何COM/Activate之前按quarantine返回，不得只Disable viewport或盲目重建RTS。下文Touchpad/DM类型、Owner和数值测试是保留的停用实现与未来前端参考，不是当前真实输入能力；配置字段及设置偏好保持，但不产生PTP运动。2026-11-13起接入可信物理尺度及设备profile；触控板按压拖动独立识别、绘制模式漫游和缩放继续延后。绘制latest mailbox、页面存储、Freeze/背景与呈现树保持。

## 2. Signatures

- `ContactNavigationEvent { ContactHandle handle; ContactSnapshot snapshot; uint64_t ordinal, downWatermark; }`；`ContactNavigationHistoryStatus { epoch,published,dropped,pending }`。
- `ContactInputCoordinator::{NavigationHistoryStatus,DrainNavigationHistory,WaitForFrameDeadline}`；每个record的`NavigationEpoch/NavigationSequence`在Down锁存。
- `CanvasPanInput::{Register,Step,Stop,Reset}`消费导航流；`CanvasPanDynamics`是四类接触输入的唯一产品手势/惯性真值；Touchpad只消费系统数值，业务视口仍唯一。
- `CanvasPanContactSample::{sourceKey,allowInertia=true}`携带class/context与该接触按当前策略允许释放的标志；`CanvasPanFrameOutput::sourceKey`交出kernel真实消费顺序的来源，`CanvasPanReleaseReason::Disabled`表示禁惯性。来源/热开关不能按publication顺序猜当前coast，仍在实际Down边界清跨来源boost。
- `CanvasPanInputKind::{None,Mouse,Touchpad,Touch,Pen,PenTail}`；`CanvasPanInertiaOptions::{Allows,Encode,Decode}`及`Host/DrawingController::SetCanvasPanInertiaOptions(options)`。`Drawing.Whiteboard.PanInertia.{Enabled,Mouse,Touchpad,Touch,Pen,PenTail}`六bool默认`{true,false,true,true,true,false}`，NoUpload。
- `TouchpadNavigationSample {epoch,sequence,qpc,cumulativeX,cumulativeY,phase,admissionRevision}`；phase为Running/Inertia/Ready/Cancelled。`WindowController::{SetTouchpadNavigationEnabled,CancelTouchpadNavigation(takeoverQpc=0),RequestTouchpadNavigationUpdate,StopTouchpadNavigation,DrainTouchpadNavigation,TouchpadNavigationRunningAt}`。
- `WindowController::TouchpadNavigationNeedsUpdate()`表示interaction待识别或实际动画所需调度，不能用于判held；`TouchpadNavigationFrontend::{Associate,Interaction,CanPublishStatus,PublishStatus,Reset}`供真实Owner与无窗口测试共用。`ConfigureTouchpadViewport`及`DispatchTouchpadHitTest`约束实际初始化和来源选择。
- `CanvasPanReleaseVelocityTracker::{AddSample,Estimate,Reason,SampleCount,SampleSpanSeconds,ClearPointer,ClearAxis}`：逐指真实采样，当前唯一释放估速为可靠同向尾段线性拟合。
- `CanvasPanFlingCurve`仅保留向量摩擦闭式；`CanvasPanFlingController`负责推进/取消/限位，不再提供曲线选择枚举或旧曲线入口。
- `CanvasPanFrameOutput`额外记录raw/selected/boosted release向量、releaseReason/count/span、rejectedSamples/reversalCount/timingResetCount。 释放追加release-quality，记录key/source/profile/unit、raw-points/anchors/positive-intervals、真实锚点QPC、support-upper/valid/limited、output-limited、boost-suppressed。真实Stop入口追加触发原因；通用fling-complete-or-cancel仍不能单独证明误取消。
- `CanvasPanSourceClass{None,Touch,Mouse,Pen,PenTail}`保持0..4，高32位仍在sourceKey；`CanvasPanMotionContext`含unit/profile、两轴motionPerDip、outputKey/sourceGeneration、映射事实及空间阈值；`MakeCanvasPanMotionContext`/`ResolveCanvasPanFriction`是纯数值入口。
- `CanvasPanFrameInput::{motionContexts,admissionDecisions}`由caller持有；Down的`motionContextIndex`仅本次Step有效，核心复制选中值。`CanvasPanFrameOutput::{admissionDecisionCount,admissionFailed,releaseSourceKey,releaseMotionContext,releaseMotionVelocity,frictionParameters}`区分准入结果与释放自身标签。
- `WindowController::CanvasPanDisplayContextSnapshot()`同锁读取display+mode；`DrawingCursorEventSink::{NotifyPenFeedbackContact,PublishPenCursorSampleForContact,ClearPenCursorSampleForContact}`旁路真实RTS token；窗口`NavigationPenFeedbackToken`/`SetNavigationPenFeedbackEpoch`/`ApplyNavigationPenDecision`应用权威反馈；`ReadPenCursorSample(sample,bool* suppressed=nullptr)`一次取配对样本与抑制状态。
- `SetCanvasPanDiagnosticsEnabled(bool)` / `CanvasPanDiagnosticsEnabled()` / `LogCanvasPan(...)`；配置`Experimental.Inkeys3.ConsoleOutput.CanvasPan`（bool，false，NoUpload）。

## 3. Contracts

### 输入与所有权

- `IsNavigationDevice`在Down/Move/Close/容量检查的同一入口接受Touch、MouseLeft、Pen（倒置仍是Pen），MouseRight不参与。只放开Controller准入而遗漏此predicate会导致Pen永久held；绘制latest语义不变。导航固定2048事件环从producer Down开始保留回调级样本；Move和Up不再覆盖尚未消费的导航Move。原绘制latest语义保持。producer不分配历史内存、不做日志IO；consumer锁外reserve，锁内仅有界复制。
- Down成功入原队列及发布导航Down在同一navigation截面；每事件记录当时成功入队Down水位。consumer只提取Down已由唯一consumer出队的连续前缀，先完成静态路由并登记Pending，再由同一Step的有序Down最终准入。不能在drain后用latest读到更晚Up并越过未准入Down。
- 锁序为Down navigation→ingress、Move/Close record writer→navigation；consumer只持navigation复制，不持其执行业务或回收。Close导航终态可能先于ConsumerOwned可见，Recycle必须保持既有ClosingDiscarded→producer唯一释放合同。
- 溢出清旧历史并推进epoch，旧导航手势取消、无fling，残留触点吞到终态；完整新epoch Down可开始新手势，不继承旧fling。程序性Stop保存publication截止，未登记的旧Down同样不可复活。
- 本轮RTS仍每回调一个批末点，时间为callback arrival QPC；不宣称批内硬件保真，不生成伪240Hz时间。相同轨迹在绘制帧内可以合并呈现，但不能合并丢掉估速历史。
- 异步多指只更新真实上报那一指的估速；其他指latest-known位置仅供focus几何。加/减指重置focus基线不产生跳变；相反方向抬指清剩余释放历史。
- Pen/PenTail分类在真实Down按倒置元数据锁存，与笔尾橡皮开关无关；合法panManaged Pen不计入suppressed，持续Pen mailbox contact不每帧Stop自身。晚到跨类接触按实际生命周期重叠仲裁，同QPC仍重叠；不因publication/QPC逆序吞不重叠手势。
- `CanvasPanDynamics::Step`用与原稳定排序相同的QPC/phase比较器检查输入；已排序span直接只读消费，仅乱序才复制并stable_sort。同QPC/phase保持原输入次序；不得跳过乱序兼容或修改终态/拓扑顺序来省排序。静态收益仅指正常有序输入去掉临时分配、复制和重复排序，不承诺未经测量的时延/功耗改善。
- 普通Move保持呈现节拍；Down/终态/控制wake的独立generation允许提前结束活动等待，覆盖工作阶段已到达的事件；idle阻塞语义保持。
- `WindowController::PublishMouseCursorSample`在`CanvasPanMode()`且`valid/inContact`均未变化时，仅更新latest mailbox，不调用`RequestDrawingCursorRender`；移动模式使用系统箭头，逐位置控制wake会抢占tile恢复并打断`WaitForFrameDeadline`。Down/Up状态边沿、clear/leave/cancel、owner/suppression和模式变化维持既有唤醒，非Pan应用光标保持重绘。不得在下游忽略全部control命令来补救。

### 几何、释放与连续滑动

- 公共输入/输出仍为系统DIP并与像素互转；内部按MotionContext使用mm或DIP，首次径向扣该profile的slop。手按住时旧惯性不推动画布。Down立刻取消旧动画；拖动模式所有来源的运动均不因笔尖/笔尾hover停止，真实允许接管的Down取消旧coast，持续接触继续跟手。活动Touch/native Running持有时的后来Pen按互斥保护跟踪到终态，不混focus。
- 产品释放取最近同向50ms尾段线性拟合；保留窗口左侧一个真实锚点以兼容低报点率，标准充分估速分支至少3点且跨度20ms；Touch另有ShortSupported/ResidualOnly/DistanceBounded分级决策。raw与Touch progress每指各≤64；不得恢复旧LSQ2的20点/100ms规则替代产品释放合同。
- 反向沿原可靠方向累计达到context.reversalDistance才确认（间接基准1DIP），零点/细碎噪声不反复重置；确认即切断旧方向和boost。新尾段不足且无合法Touch组候选/有限退化时释放0，不借旧方向复活。合法超速不清历史、方向或续甩资格，不能用profile输出cap证明输入时间无效。
- ScreenPen Physical/Fallback从最新真实Move向前取间隔≥2ms锚点拟合；Touch Physical/Fallback改用真实位置进展点及运动年龄，重复raw仍保留停止观察，详见[释放质量规范](canvas-pan-release-quality.md)。Indirect保留原raw拟合；充分估速仍需真实多段；Touch短强证据/组决策与有限退化按释放质量规范，不统一用个人20ms归零，不补Up速度或伪造时间。
- 已移除输出cap判坏时间因果。前轮实机Touch末坐标平台曾被双段支持压成1–3mm短尾，现已改为集中释放决策；Ready仍不代表体验验收，重复位置的物理原因未定。见[复测](../../tasks/archive/2026-11/11-13-whiteboard-pan-scale-device/research/device-feedback-summary-2026-11-14.md)。
- 拟合方向与最新可靠方向冲突时以DirectionConflict拒绝释放，raw值仍可诊断；已准入Up即使时间异常也必须闭合并取消惯性，不能留下永久held。
- 释放selected速度还受最新可靠同向段的速度上限约束（真实点累计至少2ms与context.reversalDistance），防减速时50ms拟合残留旧快速度；raw诊断保留，不能用单个量化点强制限速。
- 原raw新鲜度门仍≤30ms全速、30–70ms衰减、≥70ms为0；Touch平台/确认停止另由纯决策处理，不把motion-age直接套同一减速门；取消或最终决策无可用seed时不fling。不得恢复250DIP/s硬台阶。
- 自由运动沿释放向量统一闭式减速：`ds/dt=−s/tau−c`；`T=tau*log1p(s/(c*tau))`、`D=tau*s−c*tau*T`。间接基准仍tau=.30秒、c=1000DIP/s²，其它profile按下节选定并锁存。不得逐轴平方改变斜向方向，不恢复Slipping。
- 连甩使用当前残余在新方向上的正投影，boost加法后最终模长≤本profile上限（间接基准12000DIP/s）；非Touch保留50ms合格滚动续期窗口（不是整个Down→Up必须≤50ms）；Touch终端平台由Decision统一裁决至确认停止边界，不无限续租。反向、弱证据、程序Stop、取消、超时或来源变化清候选，不重新累加历史初速。
- inputReleaseTime用于时效；动画以首次消费Up的呈现帧为起点，首帧不追补fling但照常提交真实Move位移。ClearAxis同步booster当前残余并归一化剩余方向，避免下一Down恢复已限位轴。之后按同一动画时钟推进；不保证消除渲染长帧自身的视觉停顿。

### 释放质量与Touch专用证据

输出cap不判坏输入；通用双段质量支持、Touch进展/重复观察分工及验收见[释放质量规范](canvas-pan-release-quality.md)。修改Tracker/释放/boost时须读该文；Touch新分工已实现，Pen/PenTail/Indirect保持前轮路径；硬件验收以任务记录为准。

### 尺度、设备响应与唯一有序准入

- 真实来源与显示输出一起判毫米资格：Display业务physicalAvailable、logicalOutputKnown、精确monitor/完整rect匹配、有限正两轴密度；motionPerDip各轴[.01,10]mm/DIP。复制/未知映射/无物理回退DIP；External/Unknown Pen不借显示器毫米。橡皮开发override、尺寸/灵敏度不改变惯性；不新增硬件枚举或手动标尺。
- 五profile初值见下表，速度/加速度按对应动作单位。时间窗口不缩放。公共contentDelta/velocity、focus/span、释放/booster诊断均是真系统DIP；每个ScrollUpdate先按当时context逆变换再累计，异向span逐指还原后求值，内部velocity不能从已转DIP的Snapshot回填。`VelocityTracker()`调试入口明确为运动域。

| Profile | 单位 | slop / 反向距离 | 最小释放 | boost新旧 / 滚动 | 模长cap | tau / c基础 |
| --- | --- | --- | ---: | --- | ---: | --- |
| IndirectDip | DIP | 15 / 1 | 30 | 350 / 150 | 12000 | .30 / 1000 |
| TouchPhysical | mm | 2.5 / .25 | 10 | 70 / 30 | 2400 | .30 / 200 |
| ScreenPenPhysical | mm | 1.5 / .15 | 15 | 100 / 45 | 1800 | .20 / 350 |
| TouchFallback | DIP | 15 / 1 | 30 | 350 / 150 | 12000 | .30 / 1000 |
| ScreenPenFallback | DIP | 8 / .75 | 60 | 500 / 225 | 9000 | .20 / 1750 |

- TouchPhysical：`w=S((长边mm−320)/880)`，Laptop取min(w,.25)、LargeScreen取max(w,.25)；`a=w*S((可靠boost前手速−600)/900)`，tau=.30+.12a、c=200(1−.25a)。ScreenPen不套触摸增强；笔尾跟随实际笔关系且不改变开关。
- TouchFallback：逻辑完整输出的`G=1/clamp(min(longDip/1920,shortDip/1080),.5,4)`，未知输出G=1；LargeScreen的`a=.5*S((手速DIP/s*G−750)/1500)`，Laptop为0；tau=.30+.12a、c=1000(1−.25a)。G只选响应，不乘几何、真实初速、残余或输出，不标mm。S为夹0..1的smoothstep。
- 每条curve按boost前可靠手速选参并锁存，不在coast中降档。等有效v0、同单位的单条自由曲线额外距离/时长≤40%（物理）/20%（回退）；不同旧残余的连续手势不受此比率约束。cap后理论单次停时<1.6s，不是排队/呈现/整个交互的墙钟保证。独立轻放、仍过boost门的低速接力、主动减速和反向分别验收。
- Register只Pending；静态路由保留非白板/设备/native历史资格，动态class/metric最终权限仅在Dynamics现有稳定QPC/phase Down分支。Pending/Suppressed不算owner/held/活动；HasContacts仍跟踪物理终态。同QPC Down先于Up仍重叠。拒收不能改sourceKey/残余，真实Touch期间拒收Pen锁到终态；兼容第二指继承首指度量，mm批次映射未知/冲突的附加指拒收，DIP批次不半途升级。
- caller决策缓冲须覆盖本帧全部Down，不是同时持有的16个；不足消费前失败，前端Stop/吞旧指。正常有序span无复制；异常终态即使排序在同key Down之前也按身份可靠取消闭合，不造速度。重复/倒退frame是no-op，Controller在Drain前延后消费，不能误当admissionFailed去Stop或提前回收排队Up。
- 真输出/DPI/正在用的物理资格/密度变更清旧运动；Pending毫米候选也必须以其Down source重验，不只检查Active。DIP→可信及纯模式偏好留到下一手势；无害revision、别屏和橡皮调测不误停。新producer sourceGeneration隔离旧残余，帧内index/每次手速选参变化不作为新来源。
- RTS每Down进程内唯一token随binding直到Up/Cancel/重置/失败发布/OutOfRange/Shutdown。窗口精确唯一查询{raw tcid,cid,sourceGeneration,原样DownQPC}，碰撞/缺失/表满保守不纠正但不丢core输入；旧token/epoch/key/handle不能清新笔。Pointer cursor/range不证明同一次Down，RTS裁决不全局清Pointer-local latch。
- 身份选择与mailbox Publish/Clear在同一短锁截面；实际光标/橡皮通过组合读取取配对抑制状态。UI/触觉/wake在锁外，普通Move不分配、不新增control wake；旧sink默认转发不改变latest样本格式。

### 配置与已停用的触控板 DM 前端

- 当前运行合同是`kTouchpadDirectManipulationQuarantined=true`：`TouchpadNavigationOwner::Initialize`在写窗口、COM初始化、CreateViewport和Activate之前返回`false`，诊断记录`action=quarantined reason=rts-dm-hwnd-conflict`；没有已初始化owner需要退休。下列DM/Owner细则只描述保留的停用实现及其无窗口模型，不能作为重新启用或系统共存证明。

- 设置→绘制→通用有“白板拖动惯性”总开关，下方内联鼠标/触控板/触摸/笔/笔（笔尾）；默认仅鼠标和笔尾无惯性。总关收起但保留子值，允许子项全关；启动及用户Changed经Host同代快照应用，仅变化发布control wake，异步保存到main.json。
- 关闭惯性只裁决最后释放/boost，不丢真实Move/Up；非末指Up仍参与方向仲裁。热禁停止对应coast并清残余，held继续平移，重开不复活旧曲线；纯kernel样本默认true不能代替产品五类默认。Mouse手感矩阵须显式开启Mouse惯性。
- DM仅识别真实PT_TOUCHPAD双指：Init固定配置`MANUALUPDATE | DISABLEPIXELSNAPPING | INPUT | EXPLICITHITTEST`，前两项不等于输入可见性/显式命中；整个DM_POINTERHITTEST均由应用处理并返回0，仅明确PT_TOUCHPAD可SetContact，非目标或失败不能落默认DM路由。COM/回调在HWND owner，manual Update只取浮点累计content transform；不接DComp呈现树、不复制普通wheel或屏幕Touch，不另起渲染线程/timer。client像素identity边界不重复乘DPI，READY先结最后变换再重置identity，重复READY不回推画布。
- `SetContact`成功只证明关联，不能发布Running或挡其它设备。真实handler同时注册viewport及`IDirectManipulationInteractionEventHandler`；BEGIN/END控制待识别泵，确认Status Running才持有，Content先到只缓存。未确认motion的初始/重复READY不能退休新候选，未识别由真实END闭合；确认后的READY照常结尾，END不截断实际INERTIA。失败/取消/旧epoch不能借S_OK返回尾部复活。
- 固定DM配置仅Init应用，不根据bridge.Active猜系统可重配；ActivateConfiguration在系统输入处理中可能失败。真实BEGIN可抓停旧自有coast，但待识别不建立Running历史或阻挡Mouse/Touch/Pen。
- DM始终配置TRANSLATION_INERTIA以允许held期间热开启，实际开关在阶段/输出边界门控；INERTIA切换前按旧Running读取当前content，禁用时保留真实Running末段并停止系统尾流。consumer同时过滤INERTIA及其READY尾差，不再起自有fling。
- 64条阶段队列：同阶段累计值可合并，边沿/终态不可覆盖，溢出取消epoch；64段Running历史按真实begin/end和admissionRevision回答过去Down是否重叠，历史淘汰截止前保守拒收。接手取消按takeoverQpc裁掉未来旧区间、保留实际过去区间，不能只用latest Ready或清全部历史。
- sample锁存手势begin admissionRevision，owner发布和consumer消费/接手保尾段都比对当前代次，gate关再开不能让已结束的旧READY移动新页。真实interaction待识别/Running/Inertia经NeedsUpdate计入同一活动帧；互斥仍只看确认Running区间，普通累计更新及Pen hover纯位置不control-wake，owner pump合并异步投递而非递归。
- StopTouchpadNavigation须等待owner真实退休。RemoveEventHandler/Abandon/Deactivate失败保留COM/apartment/partial-init责任，通过Host既有FailUnprovenProducerStop处理；超时不等于成功。最外层退栈后最后通知，通知持独立DuplicateHandle，成功flag不能让Host关闭最后Signal所借的原句柄。旧attach消息身份/epoch均不能复活。
- 原新增能力目标Win10/11 PTP已被本轮暂停决定取代；所有平台均不启用这套DM前端，原Mouse/Touch/Pen/PenTail输入保持。未来替代方案须先用真实接触证明RTS共存，并至少评估Win10；Win11 Pointer/IC、Win10 DM输入所有权切换及降级wheel的边界见当前任务`research/touchpad-future-options.md`。不得硬导入预发布API、猜wheel来源或虚构抬指/硬件时间，候选记录不等于当前支持。

### 产品诊断

- 设置→软件调试→控制台输出有独立“白板平移与惯性诊断”，持久化、默认关闭、按consoleToggle约定重启生效。启动读取配置并初始化共用console/DiagnosticOutput，再启用gate。
- 不依赖`DRAW3_RTS_DIAGNOSTICS`；关闭或无sink时不取时/格式化。启用记录序号、QPC、线程、timestamp来源和频率；输入/状态/释放/停止日志带样本、速度及原因。空Idle帧不刷屏，终止转换可见。
- 复用异步有界输出及已有drop/truncation统计，不同步写console、不为平移新增输出线程；日志丢失不冒充事件丢失。日志函数保持GetLastError。
- 导航活动/viewport恢复期间追加`render`记录：sample/frame QPC、page/viewport、tile recovered/pending与yield、visible-clear、recovery/before-composite/composite/work/frame/present毫秒数、fallback-signature/attempt/ok、present-attempt/ok、成功submit间隔和sample-to-submit年龄。`PresentFrame(..., int64_t* presentReturnQpc=nullptr)`仅在gate启用时交出真实Present返回QPC；此软件返回点先于observer/ready，不能当作DWM光学显示。fallback签名不等同资源有效，false不区分缺资源/无交集/提交失败，且日志不改变失败处理。关闭gate不额外取诊断时钟；空Idle不刷。
- 不把设置“画布调试”的可视化覆盖层与此console gate混为一谈。

## 4. Validation & Error Matrix

| 条件 | 行为 |
| --- | --- |
| Move后Up同帧消费 | 先消费真实Move，再终态估速，不丢最后反向 |
| 新Down未出队、旧指Up已发布 | 连续水位阻止Up越过Down |
| 历史overflow/Stop | 旧批次不可由Move复活；新epoch完整Down可恢复 |
| 可靠短反向但尾段不足 | 清旧候选，释放0 |
| 相同输入、不同消费fps | 真实样本与释放速度不被呈现采样改变；离散首尾帧位移单列 |
| 无可靠时间/异常短间隔 | 明确拒绝/退化，不伪造采样时刻 |
| ScreenPen成簇到达、跨段可靠 | 至少3个≥2ms真实锚点/20ms，保留末点，允许正常起滑 |
| 合法输入超过profile输出cap | 保留历史与支持资格，只在合适输出端限幅 |
| 反向新尾段或独立运动支持不足 | 不复活旧方向或不可信释放；质量限制不清真实历史 |
| 五类输入轮换 | 不混focus、不跨来源继承boost；实际消费来源决定热停哪类 |
| Pen/PenTail真实入环 | Down/Move/Up全部到达；持续contact不自我Stop，终态只回收一次 |
| hover / 实际Down | 拖动模式hover均不急停；允许接管的Down停旧coast |
| 禁惯性 / 热禁再开 | 跟手与真实终态保留；无尾流/旧boost复活 |
| Native Ready后晚消费过去Down | 按Running历史区间判重叠，不能用latest bool提前放行 |
| Gate关再开且旧Ready排队 | begin admissionRevision不匹配，旧净位移不应用 |
| owner停止失败/嵌套COM/通知 | 保留责任、异步普通pump、最外层最后独立句柄确认 |
| SetContact S_OK但系统未Running | 不建held，四类接触仍可准入，BEGIN只开泵/停旧coast |
| 关联后初始/重复READY先到 | 不提前关闭候选；后续BEGIN→Running仍可识别 |
| BEGIN但未识别→END | 真实END释放泵和候选，不用静默超时猜Up |
| 当前PTP/DM初始化或hit-test | quarantine在任何COM/Activate前返回不可用；不建立native运动，原RTS接触保持；viewport选项不再被当作共存保证 |
| Pan内高频Mouse纯位置流 | mailbox保持最新，无额外cursor control标记/generation，不提前打断活动deadline |
| Mouse状态边沿/退出Pan | 仍及时唤醒；非Pan应用光标恢复既有位置重绘 |
| 关闭诊断 | 无CanvasPan记录，不增加高频格式化 |
| 开启诊断 | 设置/启动gate连通；真实sink可读，输出丢弃可诊断 |
| 同帧毫米Touch Up→DIP Mouse Down | 按真实有序Down切context，每段恢复系统DIP，不混旧residual |
| Pending毫米候选随后EDID失效 | Step前取消旧批，终态可回收，不以旧毫米资格准入 |
| 重复/倒退frame含排队终态 | 零位移且不Drain，下一有效frame仅消费一次并闭合 |
| RTS/Pointer并发发布或旧token清除 | 身份/样本/抑制配对，旧确认不清新笔，双publisher回归无错通道 |

## 5. Good / Base / Bad Cases

- Good：真实Move序列→本指尾段→可靠释放→方向保持的摩擦曲线；新接触先停，反向不带回旧速度。
- Base：绘制mailbox/页面视口与存储不变；移动模式笔准入/hover按本合同更新；可信直接输入用mm，失效/间接输入用明确DIP回退；未知硬件timer诚实标注。
- Bad：保留RTS末点后又在adapter按整帧合一、把Up当高速Move补速度、用未交付Down之后的latest推进内核、只修公式测试并把真实传输旁路掉。

## 6. Tests Required

- ARM64原生MSBuild构建`InkeysRepo.sln Debug|ARM64`，同次PowerShell规范Path/PATH并禁node reuse，至少5分钟预算。
- Headless `--no-window --canvas-pan-only`及完整`--no-window`：覆盖产品尾段/方向/摩擦/boost/帧率与有序/乱序输入等价；不为已移除旧曲线保留参考测试。
- `--draw3-control-fence-test` / `--draw3-canvas-pan-feel-test`必须经过真实ContactInput→CanvasPanInput；覆盖末反向+Up、跨帧Down、overflow、短反拖、hitch后立即释放、stop与设备轮换。
- Mouse唤醒回归通过真实`PublishHiddenTestMouseCursor`→生产publisher→Coordinator接线；1000次burst及持续位置流检查mailbox、pending/control wake与frame deadline（Sleep(1)不代表实测1kHz），另验Down/Up/clear/模式变化及非Pan cursor render。纯动力学/MouseInputState测试不能替代此接线。
- `--draw3-diagnostic-output-test`验证生产gate启用/关闭、文件sink字节与LastError；config测试验证默认、往返、copy、ReadMini、非法bool、reset与NoUpload；i18n check覆盖20语。
- Pen/尾真实生产入环与生命周期、五类默认/同帧混源release门控、逆序非重叠/热禁真实末源、native历史/interval gap/同QPC/旧admission/接手裁剪及held热开必须经过生产probe；真实Event DuplicateHandle与retirement失败重试使用同一生产routine。Debug大型Rig用堆持有，不通过改工程栈或少测来绕过溢出。
- 设置`--no-window --setting-general-only`及真实Config元数据覆盖六bool默认/往返/非法/ReadMini/Reset/NoUpload、父关保留子项/单击ID/显隐禁交互/窄宽布局；二十语言全部校验。
- 当前前端回归必须由`RunTouchpadIngressProductionProbe`断言DM quarantine仍启用，并保留停用实现的setup/关联/interaction/状态测试及四类真实Coordinator接触回归；这些只防止隔离被无意撤销，不证明PTP可用。未来重新启用前必须执行真实RTS/DM注册与接触探针；独立HWND初始化、直接PublishHiddenTestTouchpadNavigation或只断言选项位均不够。
- 新增真实resolver/ContactInput→Pending→有序决策→窗口token路径回归：各轴/DPI/旋转、>16顺序Down、坏终态/重复逆frame、排队物理失效、双publisher配对及ABA。不能只测公式或空样本的否定断言。
- 屏幕笔到达簇回归重放2026-11-14用户日志的五个完整手势（不补造截断key75）；覆盖毫米/回退profile的非零可靠释放、滑行实际推进、末端异常跳跃、同簇反向、全段压缩、stale Up及原Indirect/Touch行为。
- 时序/质量回归：共同10MHz时钟的240/960Hz与cap两侧、19ms空档后的末尾簇、Touch两单位、低速量化、单末点/静止后jump/重复平台、持续加速与减速、反向/stale/取消；真实非零旧coast→质量限幅必须boosted=selected，纯cap限幅仍保持续甩资格。旧“超cap即清窗”异常断言按新合同改，普通Indirect基准单独比较。
- GUI/真机测试需要用户明确授权或用户手动执行；自动测试不等于手感验收。实际运行结果写当前task验证记录，规范不冒称PASS。

## 7. Wrong vs Correct

Wrong：Down单独出队，随后读各触点latest，按本帧时间排序就认为全局有序。
Correct：统一保留producer事件，按已准入Down水位消费连续前缀，Stop/overflow使用独立导航失效边界。

Wrong：旧startingVelocity3000、当前400、新甩1000仍释放4000；(3000,1500)按轴平方走成4:1。
Correct：同向残余投影最多得到1400（再按门限/上限裁决），自由曲线保持选定释放方向。

Wrong：仅看到callback→consume约1ms便认定画布已经跟手；只测真实接触Move，遗漏独立WM_MOUSE cursor路径。
Correct：分别检查cursor control wake、tile恢复、合成和Present节拍；Present返回仅是软件呈现入口的结果，光学延迟与RTS回调前延迟仍需现场证据。

Wrong：只放开Pen准入却未进producer导航环；用latest Running/队列publication来源解释过去Down或当前coast；Stop先通知后访问self。
Correct：同一producer predicate贯通Pen历史；按实际时间区间/真值sourceKey/admissionRevision裁决；COM责任退休且最外层完成后用独立句柄最后通知。

Wrong：把MANUALUPDATE当输入隔离，把SetContact S_OK当Running，或只有桥接器注入测试就声称设备链已通。
Correct：当前在任何DM COM/Activate之前执行quarantine，保留RTS接触；未来只有在真实注册与接触探针证明共存后才能启用替代前端，不能把flags、SetContact或无窗口模型当作系统级修复。

## 8. 已完成交付与后续任务入口

2026-11-13用户确认当前机惯性合适，旧参考模型清理与有序输入优化已完成；完整构建及七项无窗口回归、2240条场景和整份探针输出等价检查见[归档收尾记录](../../tasks/archive/2026-11/11-08-chromium-touch-fling/cleanup-validation-2026-11-13.md)。两个拖动任务均已completed；归档不表示其它硬件/系统/光学性能已全部实测。尺度适配已在独立任务[11-13-whiteboard-pan-scale-device](../../tasks/archive/2026-11/11-13-whiteboard-pan-scale-device/prd.md)获批实现，验证状态见其[记录](../../tasks/archive/2026-11/11-13-whiteboard-pan-scale-device/validation.md)；旧scale-stage草案“精确保留本机距离”和“笔不平移”已失效。触控板及新输入后端仍按[未来路线](../../tasks/archive/2026-11/11-08-chromium-touch-fling/research/input-backend-future-plan-2026-11-13.md)延期，不解除quarantine。

Wrong：按Register/publication先选整帧单位，或重复frame走admissionFailed清零；Pointer继承lastKnownSource就当同一次RTS Down。
Correct：有序Down唯一准入、逐段canonical DIP；重复帧在Drain前no-op；真实token精确身份确认，无法桥接的Pointer保守独立。
