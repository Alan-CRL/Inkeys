# Research: TouchTerminal版的快速定住、短尾与释放交接

- Query: 用户反馈TouchTerminal版比上轮改善，但抬手后仍会快速定住或只走很小距离；单指、多指均可能，屏幕笔没有此问题。不是“碰撞/顶住”物理反馈，本报告按速度交接与短尾/零释放分析。
- Scope: internal；只读固定新日志、当前Touch独立进展证据和motion-age实现。仅写本前缀md/json/py，不改产品/tests/spec，不构建/GUI/Git/commit。
- Date: 2026-11-14（Asia/Shanghai）。
- Snapshot: `Build/Logs/pan-touch-feedback-2026-11-14/console.snapshot.log`，14737778 bytes、65855行，SHA256 `ac5a7c2ab6564f44fa027b06979baf40f8a9fb6b8534c49fc431511d7cfafd15`，复制期间稳定。原文件未改。
- Version: 当前代码含Touch MotionSamples/LastMotionQpc、重复raw不刷新motion时间、30–70ms motion-age衰减及aged boost抑制；不是之前的零段noise/dt主导版本，也不是早先cap清历史版本。

## Findings

### 1. 必须分开三类现象；Ready不能证明衔接自然

可见Touch释放81次：76 Ready、4 few-samples、1 short-span。76 Ready全都有后续非零coast，并至少有一帧coast render的present-ok=1；但这不能反驳用户“快速定住/惯性感很弱”的反馈。

本次最明确的三个观察是：

1. 多数Ready在OS Up之前有约40–50ms没有位置进展，之后selected又因motion-age被大幅衰减，形成“最后一段跟手运动→停顿→较低速自由滑行”的不连续交接。此前只追求非零没有覆盖这一体验。
2. 真正的5次零coast中，2次有明显几何移动却因独立点数/时间门未通过。特别是双指47/48整批已拖动19.21mm，但最后指48个人跨度只有12.409ms，最终不给惯性。不能统称正常tap。
3. 极短尾key187/.432mm与key31/3.717mm属于三指批，但末端实际运动已很慢；它们不能仅凭整批移动距离大就判定最后指丢了此前高速。单指key30/14.844mm则主要是末段速度限制与约50ms age继续相乘。

### 2. Ready的主要速度损失发生在释放选速，而非coast首几帧突然多刹一次

76 Ready中74条有quality：55条明确age-limited=1，18条support-limited=1；另2条quality缺失，但release age仍可读。按release age计算，57/76大于30ms。motion age中位49.3695ms，selected/raw物理速度比例中位约0.50963。

这对应当前公式：

```text
ageGate = clamp((70ms - (UpQpc - LastMotionQpc)) / 40ms, 0, 1)
selected = min(rawSpeed, RecentSpeedLimit, supportUpper, profileCap) * ageGate
Touch age > 30ms 时还禁用旧residual boost
```

原始最后raw和最后motion时间差中位40.92705ms；有52次Ready的最后非零held画面到释放帧间隔至少30ms。很多普通快滑的最后位置被重复上报5次左右，raw仍及时到达，但不再刷新motionQPC。于是“避免重复点当新运动”的修复确实生效，同时把这段终态平台导入了旧30–70ms年龄门，约50ms平台会把释放速度腰斩。

| key / release行 | raw物理速度 | support/tail后的主要限制 | motion age / gate | selected | 真实后续输出 |
| --- | ---: | --- | --- | ---: | --- |
| 5 / 16317 | 677.595mm/s | supportUpper607.838，tail不限制 | 49.915ms / .502125 | 305.207mm/s | 57.766mm，437.0ms后新Touch Down |
| 9 / 17251 | 1169.244mm/s | support/tail均不限制 | 49.576ms / .5106 | 597.016mm/s | 53.658mm，113.0ms后新Touch Down |
| 26 / 21669 | 1062.488mm/s | supportUpper677.653，tail不限制 | 50.017ms / .499575 | 338.531mm/s | 38.198mm，151.6ms后新Touch Down |
| 30 / 23197 | 318.328mm/s | 原RecentSpeedLimit先压到约225.73mm/s，support不限制 | 49.971ms / .500725 | 113.027mm/s | 14.844mm，317.9ms正常收尾 |
| 122 / 35954 | 925.503mm/s | supportUpper681.110，tail不限制 | 50.151ms / .496225 | 337.993mm/s | 24.067mm，87.2ms后新Touch Down |

**单指key26的完整交接：**

- 行21634最后真实进展到(1411.729,1468.120)px，motionQPC=155325545829；行21635的实际held增量(-1.13,65.01)px，按相邻呈现帧测得约771.48mm/s。
- 行21639/21644/21649/21654/21659又收到5次完全相同坐标，raw最后QPC=155325979911，距motion最后点43.4082ms。应用held几何为0，约45.8092ms后才进入释放帧。
- OS Up sampleQPC=155326046003（行21664），motion age50.0174ms；selected最终338.531mm/s，age及质量两段相乘。这里没有选速达到profile cap再截断，output-limited=0。
- 真正coast的头几帧并无第二次异常急刹：行21676约338.22mm/s（elapsed .2393ms），21686为326.98（8.8284ms），21688为316.40（17.137ms），21690为306.06（25.4985ms），均成功Present，符合tau=.3/c=200的正常曲线推进。
- 下一Touch key27 Down在行21743/QPC155327555377，按既定规则停止旧coast；并非被Pen mailbox取消。

单指key9也有类似衔接：最后非零held帧约1542.87mm/s（行17215），随后约44.69ms没有画布位置进展，释放时只有597.02mm/s。它的support/tail都没有额外限制，强折损主要由motion-age门造成。

**证据边界：**这里的“最后非零held速度”是实际应用帧增量/帧时间，raw/selected是算法估计，都不是手指物理测速；OS Up也不是实际手指离开屏幕的硬件时刻。日志足以证明软件交接存在停顿与速度折损，但不能仅凭它判断这40–50ms是用户主动短停、系统终态保持，还是其它设备报告规律。不能把所有年龄衰减一删就宣称正确，也不能用有61mm中位软件滑行来否定用户对衔接不自然的反馈。

### 3. 五次真正无惯性，逐指与整批分开看

| 最后指 / release行 | 本批最大held | 实际held画布路程 | 真实证据与直接分支 |
| --- | ---: | ---: | --- |
| 46 / 27188 | 1 | 0 | Down→Up5.350ms、同坐标、无Move；raw/motion各1点，few-samples |
| 48 / 27282 | 2（47/48） | 19.213mm | 最后指48有3进展锚，但个人跨度12.4089ms<20ms，short-span；不是无位移tap |
| 64 / 28965 | 1 | 9.031mm | Down+1Move只跨.3196ms，原几何移约11.54mm后扣slop；只有1个独立时间锚，few-samples |
| 68 / 29325 | 1 | 0 | raw4含2次重复，motion只有2点；唯一位置进展约1.38mm、未过2.5mm slop，few-samples |
| 174 / 42813 | 1 | 0 | Down→Up7.7831ms、同坐标，raw/motion各1点，few-samples |

**双指47/48是有意义的资格边界：** 两指Down QPC分别155374171151、155374173423（行27212/27223）；47先Up=155374296396（27256），48最后Up=155374376697（27277）。整体从最早Down到最后Up约20.5546ms，期间画布确实移动19.21mm；但47个人只有Down+1Move，48个人运动跨度只有12.409ms，最终内核只使用最后指48的候选速度，Reason短跨度而raw=0。不发生反向、质量支持限速、age fade或cap清窗。

因此，当前“个人可靠尾段≥20ms、最后指释放才起滑”的规则会在这一类快速短多指拖动上给出零惯性；同样单指64也已经有可见画布拖动却因独立时间不足归零。它们符合当前保守合同，但确实可能属于用户所说的“快速定住”。是否使用更完整的手势批证据，需要单独设计真实异步/拓扑处理，不能给最后一指补造其它指样本。

46/174已追溯前后held批：它们都是新独立单指接触，不是一个此前高速多指批的最后接替指。174的Canvas input Down行因输出丢弃缺失，但行42781 RTS Down published、42783 register、42785 accepted，以及raw/motion点QPC155523567947可交叉证明。

### 4. 三个短尾案例，不混为同一种原因

**key187：三指大批之后的真正低末速。** 本批186/187/188最多3指，held画布累计78.904mm；188在行45079先Up，186在45102 Up，187在45123最后Up。raw只19.710mm/s，age41.356ms后selected14.115mm/s；support/tail均不限制。理论.431571mm/63.381ms，真实.431605mm/66.941ms完成。最后非零共享held帧只有约9.019mm/s（45069），各指末端都出现减速/重复；不能仅看批次总路程就认定最终仍在快速运动。

**key31：三指长批退出后仅3.717mm理论尾。** 本批31/32/33最多3指，held累计338.467mm、约1.055s；33先Up（25002），31最后Up（25068），32终态日志缺失。末指raw50.968mm/s、age32.936ms，selected47.229mm/s，support/tail均不限制。最后非零共享帧约30.258mm/s（25030），因此同样缺少“最后整体仍快却被末指抹掉”的证据。理论3.717mm/174.184ms，实际先滑2.625mm，82.10ms后新Touch Down停止。

**key30：单指长行程的末速衰减。** 整批held实际105.827mm，最后运动区间约225.73mm/s，末尾重复平台约41.38ms；raw LSQ仍318.33，但RecentSpeedLimit取较慢末段，再乘约.500725 age后只113.03。理论14.844mm，实际14.844mm自然完成。这一例确实是小尾距，但不是质量支持界、最后指切换或Pen干扰。

以上都不意味着手感已正常；它们说明需要分别处理“时间资格断崖”“末端年龄折损”“实际慢速末段”，不能用一个加长摩擦系数修好全部问题。

### 5. Ready后没有看到额外未知急停；仍不能把非零当作通过真人验收

76 Ready后均有非零coast与成功软件Present。中位已观察coast距离61.402mm，直到下一事件的中位时间约172.498ms。

- 68次明确被下一条Touch input Down接管。
- 5次Canvas input Down日志缺失，但RTS Touch Down published、pan-register和accepted都存在：14→15（18304/18306/18311）、23→24（20952/20954/20959）、29→30（22487/22489/22494）、151→153（40014/40018/40025）、172→174（42781/42783/42785）。同样是后续Touch Down，有73次接管的交叉证据，未见以Pen接管解释这些停止。
- 3次泛化结束分别是30、178、187，其实际累计距离/时间均与理论终点相符：14.844mm/317.89ms、214.994mm/827.11ms、.4316mm/66.94ms。

当前片段没有pen-mailbox-contact等Touch错误急停实证。诊断队列有contention丢弃，不能绝对排除未记录分支；但观察到的停止不需要该假设解释。软件Present成功也不等于光学或手指运动同步测试。

### 6. 后续应评审的具体行为边界

1. 将普通快甩的末端40–50ms坐标平台与明确意图停手区分；目前统一用motion-age30–70ms衰减，约50ms时自动腰斩并禁boost，是可量化的Touch释放交接问题。修正方向不能只把Ready数抬高，必须看最后运动→实际Up→coast最初数帧的速度/延迟。
2. 将单指64与双指47/48加入真实边界回放：它们已产生9/19mm实际画布运动却因个人20ms证据门归零。要讨论的是如何在有界、真实异步证据下避免可见拖动突然0，而非为最后指生成伪点/伪时间。
3. 31/187要保留为慢末段对照，防止为追求长尾重新复活早期高速；5/9/26等单指平台快甩则用于验证不会再先停再低速接力。
4. 若需区分终态系统报告与主动停手，应该对一次快甩后手指完全离开且不再接触的动作单独观察。当前73次后续Touch Down经软件链证实，但它们是否都对应用户期望的下一次接触，不能由日志替代实际动作说明。

这些是调查得出的设计输入，不是本轮已实现修复。

## 数据与源码证据

- 配套 `.json`保存81次Touch的紧凑摘要和11个关键案例，含motion/raw时间、选速损失、组held数/真实画布路程、实际coast与停止、首4个coast帧；没有复制逐帧大表。
- `.py`只从固定快照离线复算，没有执行产品C++。key13的释放pan-step被丢弃，脚本用真实terminal的frameQPC关联后续coast，不补造输入；2条quality缺失明确保留None。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:275–402`：当前Touch进展存储、MotionSamples、LastMotionQpc与HasStationaryObservation；`:447–560`：运动样本下的末段限制、质量支持、资格与拟合；`:1120–1205`：motion-age衰减、aged boost抑制与最后指释放。
- 同文件`:868–901`：Snapshot对显式重复Touch原始观察给静止呈现速度，但不改held几何。当前held contentDelta仍1:1，停段坐标已在输入中重复，不能说是人为held限速。
- `Draw3.DrawingController.cpp`现有input/release-quality/pan-step/render及真实Stop原因：本报告使用快照行号为主要可复核证据。
- 相关合同：本任务 `touch-terminal-fix-2026-11-14.md`、`.trellis/spec/native-desktop/canvas-pan.md`。修复前重复点导致零段noise/dt的旧调查不能替代本轮motion-age因果分析。

## External References

无。结论只基于本地当前源码、用户澄清和固定日志，不引入新的Windows硬件时钟/终态行为保证。

## Caveats / Not Found

- 日志能确认强motion-age折损、真实短证据零释放以及实际coast，但不知道实际手指离开时间、末端平台是否主动停手。因此不能保证仅改变年龄阈值就能让所有手感合适。
- 用户说的是快速定住/路程小，不将其解释成碰撞。单指64与双指48的可见拖动后零释放，已明确区别于无移动46/174。
- 没有找到多指31/187最后仍高速却仅因任意选择末指而丢失整体速度的直接证明；不以总路程代替末速。个人资格不足的48是另一种明确边界。
- 未改源码/tests/spec/配置，未构建/GUI/提交/归档。该报告不把Ready/Present或既有自动测试通过等同真人体验通过。
