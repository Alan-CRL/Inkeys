# 白板移动模式：可靠输入、释放与诊断

## 1. Scope / Trigger

适用于Draw3白板Selection移动模式的MouseLeft/Touch。2026-11-08用户批准以708自由滑行作为手感基准，取代7b阶段1逐字复刻Chrome的产品动力学；Chromium组件仅保留参考测试。设备物理尺度、桌面/大屏profile、笔拖动和缩放延后。本轮不改变绘制latest mailbox、页面存储、Freeze/背景及tile策略。

## 2. Signatures

- `ContactNavigationEvent { ContactHandle handle; ContactSnapshot snapshot; uint64_t ordinal, downWatermark; }`；`ContactNavigationHistoryStatus { epoch,published,dropped,pending }`。
- `ContactInputCoordinator::{NavigationHistoryStatus,DrainNavigationHistory,WaitForFrameDeadline}`；每个record的`NavigationEpoch/NavigationSequence`在Down锁存。
- `CanvasPanInput::{Register,Step,Stop,Reset}`消费导航流；`CanvasPanDynamics`仍是唯一产品手势/惯性真值。
- `CanvasPanContactSample::sourceKey`携带class/context身份，在实际Down消费边界清跨来源boost，不能只在Register时提前清理。
- `CanvasPanReleaseVelocityTracker::{AddSample,Estimate,Reason,SampleCount,SampleSpanSeconds,ClearPointer,ClearAxis}`：逐指真实采样，产品释放与参考共享LSQ2分离。
- `CanvasPanFlingCurveKind::Friction`，`CanvasPanFlingController`复用推进/取消/限位接口。
- `CanvasPanFrameOutput`额外记录raw/selected/boosted release向量、releaseReason/count/span、rejectedSamples/reversalCount/timingResetCount。
- `SetCanvasPanDiagnosticsEnabled(bool)` / `CanvasPanDiagnosticsEnabled()` / `LogCanvasPan(...)`；配置`Experimental.Inkeys3.ConsoleOutput.CanvasPan`（bool，false，NoUpload）。

## 3. Contracts

### 输入与所有权

- 导航固定2048事件环从producer Down开始保留回调级样本；Move和Up不再覆盖尚未消费的导航Move。原绘制latest语义保持。producer不分配历史内存、不做日志IO；consumer锁外reserve，锁内仅有界复制。
- Down成功入原队列及发布导航Down在同一navigation截面；每事件记录当时成功入队Down水位。consumer只提取Down已由唯一consumer出队的连续前缀，先同步完成准入，再Step。不能在drain后用latest读到更晚Up并越过未准入Down。
- 锁序为Down navigation→ingress、Move/Close record writer→navigation；consumer只持navigation复制，不持其执行业务或回收。Close导航终态可能先于ConsumerOwned可见，Recycle必须保持既有ClosingDiscarded→producer唯一释放合同。
- 溢出清旧历史并推进epoch，旧导航手势取消、无fling，残留触点吞到终态；完整新epoch Down可开始新手势，不继承旧fling。程序性Stop保存publication截止，未登记的旧Down同样不可复活。
- 本轮RTS仍每回调一个批末点，时间为callback arrival QPC；不宣称批内硬件保真，不生成伪240Hz时间。相同轨迹在绘制帧内可以合并呈现，但不能合并丢掉估速历史。
- 异步多指只更新真实上报那一指的估速；其他指latest-known位置仅供focus几何。加/减指重置focus基线不产生跳变；相反方向抬指清剩余释放历史。
- 普通Move保持呈现节拍；Down/终态/控制wake的独立generation允许提前结束活动等待，覆盖工作阶段已到达的事件；idle阻塞语义保持。
- `WindowController::PublishMouseCursorSample`在`CanvasPanMode()`且`valid/inContact`均未变化时，仅更新latest mailbox，不调用`RequestDrawingCursorRender`；移动模式使用系统箭头，逐位置控制wake会抢占tile恢复并打断`WaitForFrameDeadline`。Down/Up状态边沿、clear/leave/cancel、owner/suppression和模式变化维持既有唤醒，非Pan应用光标保持重绘。不得在下游忽略全部control命令来补救。

### 几何、释放与连续滑动

- 几何仍在统一DIP边界与像素互转；15DIP slop及首次径向扣slop保留。手按住时旧惯性不推动画布。Down立刻取消旧动画；笔hover在Coasting停止，落笔及页面/模式控制停止规则保持。
- 产品释放取最近同向50ms尾段线性拟合；保留窗口左侧一个真实锚点以兼容低报点率，至少3点且跨度20ms。每指容量64；参考LSQ2的20点/100ms规则不能替代产品释放合同。
- 反向沿原可靠方向累计达到1DIP才确认，零点/细碎噪声不反复重置；确认即切断旧方向和boost。新尾段不够时释放0，不回退旧窗口。小于2ms且段位移超过12000DIP/s允许距离（包含1个QPC tick与两端float坐标量化误差界）作为可检测异常重置证据，几何仍跟手；仅arrival无法识别所有时间压缩。
- 拟合方向与最新可靠方向冲突时以DirectionConflict拒绝释放，raw值仍可诊断；已准入Up即使时间异常也必须闭合并取消惯性，不能留下永久held。
- 释放selected速度还受最新可靠同向段的速度上限约束（真实点累计至少2ms与1DIP），防减速时50ms拟合残留旧快速度；raw诊断保留，不能用单个量化点强制限速。
- 最后有效样本距Up≤30ms保持速度，30–70ms线性衰减，≥70ms为0；取消/无有效速度不fling。不得恢复250DIP/s硬台阶。
- 自由运动沿释放向量统一闭式减速：s=|v|，ds/dt=−s/.30−1000；总距`.30*s−90*log1p(s/300)`，停止时长`.30*log1p(s/300)`。不得逐轴平方改变斜向方向，不恢复Slipping。
- 连甩使用当前残余在新方向上的正投影，最终模长≤12000DIP/s；保留50ms合格滚动续期窗口（不是整个Down→Up必须≤50ms）。反向、弱证据、笔、取消、超时或来源变化清候选，不重新累加历史初速。
- inputReleaseTime用于时效；动画以首次消费Up的呈现帧为起点，首帧不追补fling但照常提交真实Move位移。ClearAxis同步booster当前残余并归一化剩余方向，避免下一Down恢复已限位轴。之后按同一动画时钟推进；不保证消除渲染长帧自身的视觉停顿。

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
| 鼠标与Touch轮换 | 不混focus，不跨来源继承boost |
| Pan内高频Mouse纯位置流 | mailbox保持最新，无额外cursor control标记/generation，不提前打断活动deadline |
| Mouse状态边沿/退出Pan | 仍及时唤醒；非Pan应用光标恢复既有位置重绘 |
| 关闭诊断 | 无CanvasPan记录，不增加高频格式化 |
| 开启诊断 | 设置/启动gate连通；真实sink可读，输出丢弃可诊断 |

## 5. Good / Base / Bad Cases

- Good：真实Move序列→本指尾段→可靠释放→方向保持的摩擦曲线；新接触先停，反向不带回旧速度。
- Base：绘制mailbox/笔仲裁/页面视口与存储不变；设备尺度继续DIP适配，未知硬件timer诚实标注。
- Bad：保留RTS末点后又在adapter按整帧合一、把Up当高速Move补速度、用未交付Down之后的latest推进内核、只修公式测试并把真实传输旁路掉。

## 6. Tests Required

- ARM64原生MSBuild构建`InkeysRepo.sln Debug|ARM64`，同次PowerShell规范Path/PATH并禁node reuse，至少5分钟预算。
- Headless `--no-window --canvas-pan-only`及完整`--no-window`：参考组件黄金值与产品尾段/方向/摩擦/boost/帧率测试分开。
- `--draw3-control-fence-test` / `--draw3-canvas-pan-feel-test`必须经过真实ContactInput→CanvasPanInput；覆盖末反向+Up、跨帧Down、overflow、短反拖、hitch后立即释放、stop与设备轮换。
- Mouse唤醒回归通过真实`PublishHiddenTestMouseCursor`→生产publisher→Coordinator接线；1000次burst及持续位置流检查mailbox、pending/control wake与frame deadline（Sleep(1)不代表实测1kHz），另验Down/Up/clear/模式变化及非Pan cursor render。纯动力学/MouseInputState测试不能替代此接线。
- `--draw3-diagnostic-output-test`验证生产gate启用/关闭、文件sink字节与LastError；config测试验证默认、往返、copy、ReadMini、非法bool、reset与NoUpload；i18n check覆盖20语。
- GUI/真机测试需要用户明确授权或用户手动执行；自动测试不等于手感验收。实际运行结果写当前task验证记录，规范不冒称PASS。

## 7. Wrong vs Correct

Wrong：Down单独出队，随后读各触点latest，按本帧时间排序就认为全局有序。
Correct：统一保留producer事件，按已准入Down水位消费连续前缀，Stop/overflow使用独立导航失效边界。

Wrong：旧startingVelocity3000、当前400、新甩1000仍释放4000；(3000,1500)按轴平方走成4:1。
Correct：同向残余投影最多得到1400（再按门限/上限裁决），自由曲线保持选定释放方向。

Wrong：仅看到callback→consume约1ms便认定画布已经跟手；只测真实接触Move，遗漏独立WM_MOUSE cursor路径。
Correct：分别检查cursor control wake、tile恢复、合成和Present节拍；Present返回仅是软件呈现入口的结果，光学延迟与RTS回调前延迟仍需现场证据。
