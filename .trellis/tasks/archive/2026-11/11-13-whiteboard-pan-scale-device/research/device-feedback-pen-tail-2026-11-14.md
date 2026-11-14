# Research: 最新Surface测试的笔尾拖动与接触边界

- Query: 用户反馈笔尖正常、触摸间歇无惯性，笔尾拖动似有问题；进一步澄清偏向笔尾“没有完全按下/尾压力”，也怀疑抓手后画布仍相对移动。
- Scope: internal；只读当前产品与测试源码、最终隔离配置和冻结console日志。只写本任务research，不改产品/测试/spec，不构建、不运行GUI、不操作Git。
- Date: 2026-11-14（Asia/Shanghai）。
- 日志快照：`Build/Logs/pan-device-feedback-2026-11-14/console.snapshot.log`，28,479,321 bytes/133,090行，原日志只读。SHA256、完整性及本次计算输入见[JSON](device-feedback-pen-tail-2026-11-14.json)。
- 配套[脚本](device-feedback-pen-tail-2026-11-14.py)只解析日志并做离线几何复算，不是native/hardware测试。

## Findings

### 1. 结论先分三个问题

1. **收到RTS Down并越过起步slop后的软件抓手，日志没有发现速度缩放、相对漂移、错误反向或提前Stop。** 44个已进入移动链的笔尾手势均被接受、单触点持有，终态均回held=0；40个有实际移动，4个未越起步门槛。可匹配的1783个render帧及44个手势终点，viewport与真实位移/既有slop模型误差全部<0.1px。
2. **应用不另设笔尾压力门槛。** 白板移动路由只看RTS生命周期、来源/倒置身份和模式；压力虽然会解码到snapshot，导航核心没有pressure消费者。接近/轻触何时被硬件/驱动识别为Down，不能由当前日志证明。
3. **笔尾抬起后的惯性确实关闭。** 44次release全是disabled，配置PenTail=false；这解释抬手就停，不能用它解释按住时没抓手，也不说明硬件压力坏。

### 2. 笔尾真实分类与开关

44个导航笔尾key190–233，3649条可见input，全为class4、RTS cursor22/context1912558080；对应源key19092427264。每次pan-context均为profile3/mm、trusted-physical、映射成功，DIP/px=(.5,.5)、mm/DIP=(.19444445,.19791666)。活动期间没有contact=1/inverted=0样本，也没有混入其它导航触点。

源码：

- `Draw3.DrawingController.cpp:2609–2615`：Pen在真实Down根据inverted锁存PenTail/笔尖类别；不是按笔尾橡皮开关猜设备。
- 同文件`:2675–2705`：Pen/PenTail且SourceKind::IntegratedPen共享屏幕笔物理profile；profile3不是把笔尾当触摸或外置数位板。
- 同文件`:3266–3270`：每个导航样本携带`inertiaOptions_.Allows(CanvasPanKindOf(contact.panClass))`。
- `Draw3.CanvasPanSettings.h:10–29`和`Other.Config.cppm:446`：笔尾惯性独立，默认false。已准备的测试配置与分析时配置SHA相同，`Enabled=true,Pen=true,Touch=true,PenTail=false`。
- `Draw3.CanvasNavigation.cpp:1075–1104`：allowInertia=false在终态把选速设0并给Disabled，跟手Move仍正常。Disabled优先于质量/低速原因，不能据此断言全部44次在开启后必然滑行；其中35次support-valid=1，9次还存在样本/跨度等不足。

证据例：

- 首个key190：快照94782上下文、94783注册、94786 Down、94790 accepted；94835 Up，94839 disabled，94840质量支持valid=1，94841目标尾距/时长0。
- 最后key233：129799 Up，129801 disabled、129802质量记录、129803目标尾距0。

本次没有观察到把笔尾误分类为Pen尖后又切回、在同一生命周期中更换cursor/context、或尾Down被Touch互斥拒收的实例。

### 3. 44次按住阶段的实际位移核验

| 项目 | 结果 |
| --- | --- |
| 已准入导航笔尾手势 | 44/44 accepted |
| 按住最大触点数 | 全部1 |
| 真实Up后held | 全部0 |
| 按住期间Stop/viewport-clamp | 0 |
| 按住期间Touch抑制/selection=0 | 0 |
| 输入位移越slop并实际移动 | 40 |
| 未越slop、不移动 | 4：197/199/200/222 |
| 可匹配真实最新sample的render帧 | 1783 |
| 匹配帧viewport误差>0.1px | 0 |
| 全部44手势累计viewport误差>0.1px | 0 |

几何复算沿当前`CanvasPanGestureDetector`分支：系统DIP换成每轴mm；首次越1.51mm判定后径向扣1.5mm，随后真实位置差1:1输出，.02mm scroll epsilon暂存微小变化，终态不拿Up位置补Move。对应源码`CanvasNavigation.cpp:501–575`及profile初值`:174–190`。本机每轴mm/px约.0972222/.0989583，1.5mm约15.2–15.4屏幕像素。

viewport为实际渲染记录中的业务位置；比较它与输入模型，避免只加日志delta造成误判。15个手势缺少少量input ordinal文本，部分pan-step也被输出器丢行。只加可见pan-step时，key207/211/215/228/229表面会少约9–77px；相同帧段的真实viewport补证与模型吻合，所以这些差值是缺日志，不能当实际少移动。

精度限定：

- key221的Down input日志缺失；RTS真实Down和pan-register都在，模型仅使用pan-register的0.1px精度起点，终点误差约0.0418px。
- key225缺ordinal9264，恰在首次越slop附近，影响扣除方向的近似；最大误差约0.0867px。
- 其余42条手势累计误差最大约0.00126px，符合打印精度/离线double与生产float差异。
- 1783逐帧比较仅取最新sampleQpc可匹配的帧；没有给缺失sample补造精确位置，也不把软件viewport数学关系当成光学延迟测量。

长按反复拖动也有证据：key211约1.704s、212约1.147s、228约1.747s、229约1.657s，均无中途Stop、触点替换或额外缩放；逐帧/终点viewport符合相同规则。这不支持“已经成功按下且超过死区后，应用仍持续按小于1的增益追赶手”的解释。

### 4. “已按下却还没抓手”可由当前起步死区解释的四例

| key | Down–Up回调时间ms | Move数 | 相对Down最大位移mm | 应用结果 |
| ---: | ---: | ---: | ---: | --- |
| 197 | 22.428 | 1 | .529 | 未越1.51mm，不移 |
| 199 | 53.611 | 9 | 1.337 | 未越1.51mm，不移 |
| 200 | 20.849 | 1 | .594 | 未越1.51mm，不移 |
| 222 | 23.443 | 3 | 1.015 | 未越1.51mm，不移 |

可核对行：key197为97006–97038，key199为98261–98312，key200为98357–98399，key222为114942–114978。短接触已被应用识别并接收，缺的不是代码中的压力资格，而是尚未达到移动启动距离。这种slop与笔尖共用，不是笔尾专有门槛。不能仅凭这些短生命周期断言用户原本想点击还是想拖动。

起步扣除还会留下约1.5mm的固定初始偏移：一旦开始拖动，物体相对原始Down手位并非完全重合，但后续位移增益为1。需要区分这种一次性扣除，与持续滑差/增益不足，以及在Down之前仍为hover的情况。

### 5. 压力、接触时间与hover：当前能证实的边界

- `RealtimeStylus.cpp:994–1009`会解码pressure（缺属性时未知）；`:1293–1319`在RTS StylusDown中设置inverted并直接发布Down；没有应用自定义的笔尾压力阈值。
- `ContactInput.cpp:628–650`仅在终态位置非有限时沿用最后有效位置闭合；正常有效Up直接发布。`CanvasNavigation`两个源码没有pressure消费者。
- `DrawingController.cpp:3302–3331,16464`在Move/Selection先走导航路由并返回，之后的`:15244–15252`倒置橡皮工具覆盖不参与已管理导航。
- 控制台没有任何pressure数值记录（大小写不敏感的pressure行计数0）。`properties=6`只能说明数量，不能猜其中的GUID、尾压力数值或物理按下状态。

日志能看到笔尾移动时`contact=0,inverted=1`，直到RTS报告Down才变contact=1。例如94765–94778有hover移动，94779 Down arrival，94780 contact=1，94781 Down published，94786消费。已收到RTS Down之后，43条有精确Down input的应用消费延迟中位.468ms、最大.762ms；当前不支持“Down到应用后很久还没抓手”的解释。**不能由hover轨迹判定用户那时是否已经物理压到屏幕，因而不能断言硬件没压实、压力传感器坏或驱动有错。**

尾端最后Move到RTS Up间隔中位21.300ms、范围17.665–24.869ms，笔尖26次对照中位2.985ms。它是两类输入的回调时序差异，不是独立的压力或接触检测延迟测量。软件Present返回口径下笔尾可见提交间隔中位16.218ms、笔尖8.380ms，sample-to-submit中位约4.311/4.192ms；手势内容不同、没有硬件/光学时钟，不能直接归因尾硬件只有某固定报告率或把Present返回称实际显示。

### 6. 另有两次笔尾接触是在绘制态

原始RTS cid22共46次Downpublished，其中44次进入上述导航。其余两次：

- 快照124065 Downpublished，124069附近system状态`selection=0 tool=3`。
- 快照125485 Downpublished，125489先`selection=0 tool=0`，125501为`selection=0 tool=3`。

`WindowControl.cppm:43–49`的tool3是橡皮；`DrawingController`在非Selection时才继续普通工具/倒置橡皮路径。这两次没有pan注册符合当时模式，不能记作移动模式Down丢失，也不能假设用户当时仍想拖动。

## 最小后续判别建议（未实现）

若继续验证用户偏向的“尾压力/没压实”解释，应对比稳定重按与轻按时同一动作的RTS Down/Packets/Up、inverted、contact，以及可用的原始/归一化pressure与属性能力；压力缺失必须显式标记，不能把未知当0。若物理仍在压住却日志回到contact=0，需要同步物理证据才能归因硬件/驱动边界；现有日志只有上游已作出的接触判断。

与此同时可单独验证是否只是1.5mm起步死区不合笔尾操作预期，不能靠开启PenTail惯性解释或修复按住阶段。当前source没发现收到Down、越slop后仍持续失去抓手的分支；不建议在缺压力数据时盲目修改压力门槛或改按住速度增益。

## Files Found / Code Patterns

- `Draw3.CanvasPanSettings.h:10–29`：笔尾惯性独立子开关与默认false。
- `Other.Config.cppm:446`：PenTail持久化默认false、NoUpload。
- `Draw3.RealtimeStylus.cpp:994–1009,1293–1319,1338–1408,1437–1445`：压力解码、倒置身份、真实RTS Down/Up及hover发布。
- `Draw3.ContactInput.cpp:628–650`：终态有限位置与闭合。
- `Draw3.DrawingController.cpp:2609–2615,2675–2705,3266–3270,3302–3331`：分类、来源/profile、allowInertia与选择路由。
- `Draw3.CanvasNavigation.cpp:174–190,501–575,1075–1104`：物理slop、1:1几何、终态惯性门控。
- `Draw3.WindowControl.cppm:43–49`：工具枚举。
- 隔离测试`Inkeys/Config/main.json`：启动准备副本与分析时SHA一致。

## External References

无。结论仅依赖本地源码、真实日志和明确标注的离线几何复算；未引入外部API推断。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：真实生命周期、起步slop、按住1:1、独立笔尾惯性偏好、质量/停止诊断。
- `.trellis/spec/native-desktop/input-and-ink.md`：RTS与接触归属、绘制latest和导航历史边界。
- 本任务`device-test-overspeed-2026-11-14.md`及`overspeed-history-plan-2026-11-14.md`：测试目标与当前质量规则。

## Caveats / Not Found

- 没有实际压力数值、原始packet GUID快照、同步物理按压/离屏视频，不能认定压力传感器或驱动故障。
- 44次disabled不能单独代表按住阶段错误；反过来，viewport跟手数学正确也不能证明物理接触判定一定及时。
- 控制台有输出竞争丢行。模型对缺失点只做明确近似，不把缺日志算丢输入；逐帧仅核验可匹配最新真实sample的1783帧。
- 1.5mm一次性偏移和某些短接触未越门槛确可见，但不知道用户主观“抓不住”对应哪一次具体动作。
- 本代理未修改产品、测试、spec、配置，未构建、运行GUI或执行Git。
