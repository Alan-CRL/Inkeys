# Research: 完整console日志中的Touch短尾、真实滑行与停止原因

- Query: 用户隔离测试反馈屏幕笔正常，Touch惯性时有时无，似乎用笔后更明显（用户也明确可能主观）；进一步区分释放拒绝、极短尾滑、实际取消和跨设备影响。
- Scope: internal；只读固定console快照、当前质量A实现与任务规范。仅写本研究前缀md/json/py，不改产品/测试/spec，不运行构建或GUI，不进行Git操作。
- Date: 2026-11-14（Asia/Shanghai）。
- Snapshot: `Build/Logs/pan-device-feedback-2026-11-14/console.snapshot.log`，28479321 bytes、133090行；SHA256 `540b3817922dd1832aadafc97f53c61d6e7e2e0412f36e15d896b581e54ff94a`。原日志未改。
- Current policy: 本次是已移除输出cap判坏时间、Touch/ScreenPen均用独立锚点及质量A的构建；不能套用早先“Touch仍有相邻短dt超cap清窗”的旧调查结论。

## Findings

### 1. 大多数“无惯性”更接近非常短的有效尾滑，而非这份日志中的大量拒绝

可见Touch释放121次：118次Ready、2次few-samples、1次slow-or-stationary。**118次Ready之后全部观察到非零coast delta，并至少有一次coast render的present-ok=1**。这不是只看Ready标签的结论：脚本逐次跟踪释放帧、后续无held的pan-step、render、下次Down/停止并累计真实输出像素。

有release-profile的Ready共113次（5条缺失），其中38次理论自由尾距小于3mm。38次中的33次能直接在最后三个独立锚点中复算出至少一个相邻区间的输入位置完全相同。很多动作确实只滑约1–2mm、持续约100–130ms，用户感受为几乎无惯性是合理的；不能因Ready比例高就宣称Touch体验正常。

可见Touch质量记录114条，其中51条support-limited=1且boost-suppressed=1；这表示新的质量支持界实际压低了速度，并禁止该次继承旧残余。旧的RecentSpeedLimit可能也有下降，但在下述案例中，新支持界才是更强的限制。

所有这些Touch上下文均使用profile=1、mm、同一两轴标尺(.19444445,.19791666)motion/DIP和(.5,.5)DIP/px；未看到用Pen后Touch改成另一profile的证据。不能把“时有时无”解释成EDID回退或设备类型随机切换。

### 2. 已把短尾追到质量A对末端重复坐标的具体计算

质量A从真实末尾取A/B/C，分别计算AB、BC沿raw方向的速度支持上界，取较小者。只要一个区间没有位移，该区间上界大约是 `Touch反向空间预算0.25mm / 实际dt`。8–11ms区间会得到约23–31mm/s；不是原始拟合速度真的只有这么低。

| Touch key / release行 | raw物理速度 | 最终手速 | 新support upper | 理论尾距 / 时间 | 真实后续输出 |
| --- | ---: | ---: | ---: | --- | --- |
| 5 / 17561（首次用Pen前） | 260.083mm/s | 29.525mm/s | 29.525620 | 1.654441mm / 120.054ms | 累计1.654349mm，15个非零coast step，按理论终点完成 |
| 74 / 37846 | 213.520mm/s | 23.588mm/s | 23.588203 | 1.108420mm / 99.467ms | 已见1.105904mm，最后极小delta的诊断行缺失；约100.733ms结束 |
| 90 / 41042 | 154.604mm/s | 24.055mm/s | 24.055285 | 1.148242mm / 101.139ms | 累计1.147684mm，约108.762ms结束 |
| 108 / 44666 | 374.368mm/s | 23.615mm/s | 23.615511 | 1.110733mm / 99.565ms | 先滑0.855164mm；约52.596ms被新Touch key109 Down抓停 |

这些不是发生了旧compressed-time拒绝，timing-resets均为0；也没有30–70ms age衰减触发：Ready的实际Move→Up age范围仅4.552–12.855ms。

**key74 / key90是末端平台：** key74在(1166.891,1088.958)连续6个Move完全相同，首尾覆盖41.3984ms（行37811–37836），然后Up。A=QPC111454022867、B=111454128869、C=111454194013，AB=10.6002ms、BC=6.5144ms，位置都相同；复算AB支持上界23.5882025，与日志23.588203吻合。key90同样最后6个Move重复43.2883ms。质量规则据此抑制持续快滑，符合其防止旧快速度穿透停手的设计；但不能仅凭位置平台确定用户真的主动停住，还是设备/驱动在抬手前继续报告旧坐标。

**key108更能说明体验冲突：**

```text
A: QPC111542517736  pos=(1218.520,1650.898) px  line44641
B: QPC111542623620  pos=(1218.520,1650.898) px  line44646
C: QPC111542687859  pos=(1234.847,1920.832) px  line44656
```

AB完全静止10.5884ms，得到(.25mm+float误差)/(dt−1tick)=23.6155108mm/s。BC相隔6.4239ms，实际位置差约(1.58735,26.71222)mm，沿raw方向26.75729mm，含空间预算的支持上界约4204.26mm/s。但质量A取两区间最小值，最终仍为23.6155mm/s并禁boost；旧tail-limit高达21049.145DIP/s，tail-limited=0，排除了“原末段限制已经把它压低”的解释。

该手势前面的重复点已经出现在真实输入日志中：行44626/44631/44636/44641/44646连续保持同坐标，每次对应 `rts-batch reported=1 retained=1 published=1`（行44620/44630/44635/44640/44645）。持有期间4帧delta=0、present-attempt=0，最后C到达才如实移动(16.33,269.93)px并Present（行44657–44659）。**这不是held限速把持续输入裁成0；几何仍是1:1，输入点本身没有变。** 最后C接近屏幕下边缘，仅凭当前回调时间也不能认定这26.76mm是实际手指在6.42ms内的物理速度。

对照紧邻动作，key107理论213.28308mm、key108只有1.11073mm、key109又215.32077mm；这类大幅差异足以产生“时有时无”的感受。它们并非参数随机变化，而是末段输入形态与当前支持规则不同。无法据此断言C是异常跳点或真实快动作；这是当前质量规则的保守代价在真实Touch上的具体实例，应作为下一步设计/回归的关键数据。

### 3. 三次真正无coast的释放都有可追踪分支

#### key64：没有实际拖动的短接触

行35972 Down、35999唯一Move、36009 Up全部位于(1807.068,798.690)px。末窗只有2点、12.589ms，Up age16.444ms；行36014为few-samples，selected=0。它不属于一段被清掉的快速拖动。该条quality日志缺失，但同帧真实终态与profile可互相校对。

#### key120：低于现有最小释放速度

行47711 raw约9.211mm/s，原RecentSpeedLimit约45.021DIP/s后，selected约8.769mm/s，低于TouchPhysical最小释放10mm/s；support-limited=0。最后6个Move重复约41.1259ms，属于实际估计很慢/停住后的释放；不能归因于新支持界、Pen靠近或坏时间。

#### key171：双指相反方向抬指的既有保护

双指171/172原本共同拖动。行59130显示171确认反向（reversals=1），样本降为2；之后行59147再次更新171。行59148先处理172 Up，行59150剩余171的历史清为0，后续行59169只增加1个Move，行59179最后171 Up才输出few-samples。

以本日志真实输入在172 Up QPC111695896311前，用既有离线质量研究函数复算：171 raw方向约(-7.38168,-15.38106)mm/s，3锚点/45.2183ms；172 raw约(17.57671,14.00906)mm/s，selected约8.82455mm/s，方向相反。当前Dynamics对非最后一指Up检测selected与其余有效速度点积为负时，明确清全部release history并reset booster（CanvasNavigation.cpp:1106–1115）。日志的样本清0与此吻合。它是已有多指防止旧方向复活的规则，不是Pen遗留状态或旧cap清窗。

### 4. Ready后实际停止原因：本次没找到Touch被Pen mailbox错误急停的实证

118次Ready的后续终止：

- 29次到 `fling-complete-or-cancel`。28次有理论profile可比；已见累计距离与理论几乎一致，结束时刻在理论终点后的约0.309–8.205ms，符合下一呈现帧收尾。剩余key66的profile行缺失，只记泛化停止，不作解析终点认证。
- 86次有下一次真实CanvasPan input Down，先停旧动画再跟手，属于既定正确中断。
- 3次相应CanvasPan input Down行因诊断contention缺失，但仍有RTS Touch Down published、pan-register和accepted，以及随后held=1：key85→86（行39965/39968/39970/39974/39976）、key92→93（41484/41486/41488/41492/41494）、key128→129（50333/50335/50337/50341/50343）。因此这3次也有真实新Touch接管的交叉证据。

合计89次被后续真实Down抓停、29次泛化结束，其中可比的28次符合理论收尾；没有Ready后立刻完全无输出的Touch。当前日志未见 `navigation-stop reason=pen-mailbox-contact`，也未见这些Touch周期出现leave-mode、epoch或显示热停打断。诊断有丢弃，不能把“未见”升级成所有可能跨API取消从未发生；但它不是本次短尾的必要解释。

present-ok是应用软件提交成功，不等于光学可见性测量；本报告不声称已验证用户屏幕视觉/触感。

### 5. 用Pen前后没有显示整体短尾率变差；笔尾之后没有Touch样本

首次普通Pen Down在行25415（key38）。描述性对比只取Ready且有profile的Touch，避免将3次非Ready的0尾距混入：

| 阶段 | Ready Touch | 有profile | 正尾距<3mm | 理论距离中位数 |
| --- | ---: | ---: | ---: | ---: |
| 首次Pen前 | 32 | 32 | 11（34.4%） | 183.50284mm |
| 首次Pen后 | 86 | 81 | 27（33.3%） | 197.9513mm |

Pen后的5条profile缺失，不当作零距离。key5在首次使用Pen之前已经出现同样的重复末点+新支持限速短尾，故不需要先使用Pen才会发生。此表不是受控实验，动作速度、停止方式、单/双指和续甩顺序不同；它不能证明没有更细条件的跨设备缺陷，也不否定用户现场感受。

本快照最后Touch释放在行59179，首次PenTail Down直到行94786（key190）。没有笔尾使用之后的Touch可比较，不能从本记录判断“笔尾后Touch更差”。笔尾44次disabled的根因由另一个研究主题处理，本报告不重复其配置结论。

## 数据关联与复算方法

- `.json`按每次释放保存release/quality/profile、对应真实Up、context、起滑frame、下一Down、实际coast step数量、Present成功次数、结束类型、累计mm、末尾重复点和ABC区间重建。
- key优先使用release-quality.key，source高32位确认class。只用nearest logged terminal会在队列丢日志时串key：行41042实际key90，不是89；其它修正为41421→92、43167→102、56277→155。没有quality时保留低置信标记，不补造缺失事件。
- 累计coast从释放之后的无held pan-step开始，排除释放帧可能包含的真实Move位移；新Down之前停止，泛化stop后的最后Idle delta存在则计入。像素逐轴乘DIP/px及motion/DIP换成mm；诊断delta仅保留两位小数，累计有舍入误差和日志缺项，不能用不足1%的差异推断产品漏输入。
- `.py`只做离线文本与数值分析；对双指171/172复用已有研究函数定义，没有执行其写文件入口或产品C++。

## 可供下一步评审的重点

1. 优先把真实Touch平台→末端更新→Up、平台→Up、持续运动→Up三种不同形态纳入回归；key108与相邻107/109是有意义的对照。不要仅修到所有release标签变Ready。
2. 不应直接删除质量保护、忽略最新重复点或把最后跳点全当快速手势，否则会恢复旧快速度/异常暴增。需要明确如何在有输入坐标平台时保留合适的放手感觉，同时仍拒绝静止后孤立尖峰；这是可量化的质量规则/手感取舍。
3. 若要判断平台是主动停手还是设备报告行为，需要同一次动作的实际手部/指尖运动或可靠原始packet证据；当前input和兼容Mouse都只看到停点后再跳，不能恢复不存在的硬件时钟。
4. 若后续仍怀疑用Pen后才异常，应保留同一类可重复动作的pre/post输入序列、同样手指数和落点，比较末端平台长度/选速/实际coast，而不是只比较整体拒绝率。

## Files Found / Code Patterns

源码路径相对 `D:/Project/Inkeys/Repo/Inkeys/`：

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:274–337`：当前无cap清窗，真实gap/逐raw反向及尾窗；`:378–445`最近末段限制和质量A支持界；`:447–490`独立资格/拟合；`:1030–1130`释放选速、真实多指Up反向清窗、最后终态起滑。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:3177`：Coasting→Idle的泛化stop；`:16548`独立Pen mailbox停止原因；后面的pan-step/release/release-quality/render输出是本次实际证据。输入/held delta保持1:1，没有将Snapshot质量限速乘入跟手几何。
- `.trellis/spec/native-desktop/canvas-pan.md`：已更新质量A、独立真实锚点、支持界、真实Down抓停和软件呈现证据边界。
- `.trellis/tasks/11-13-whiteboard-pan-scale-device/overspeed-history-plan-2026-11-14.md`：用户批准的输入超速保留历史与质量保护范围。
- 本目录 `overspeed-quality-design-2026-11-14.py`：独立A研究函数，用于双指边界数值核对；不是原生测试。

## External References

无。所有结论来自本地当前源码和固定用户日志，不引入新的系统API时序保证。

## Related Specs

- `canvas-pan.md`的质量A、可靠尾段、每指独立、多指反向与boost合同。
- 现有任务PRD R4/R6/R8/R9与超速保留历史计划：保护连续手势，不用单机曲线声明全部设备最优。

## Caveats / Not Found

- 日志内容完整复制且哈希固定，但输出器明确存在contention丢弃；缺日志不等于输入丢失，缺质量/profile不补造。
- 未找到Touch在Ready之后无任何coast或无任何成功软件提交的实例；仍有38条很短尾滑，不能据高Ready率否定手感问题。
- 当前证据能把具体短尾追到新支持界，但无法证明每个重复位置代表用户有意停手，也无法证明末端大跳是真实硬件速度。不能凭本日志给出“全部Touch正常”或“全部是驱动故障”的结论。
- 不是实际硬件重放或GUI验收；未修改产品/测试/spec、未构建/提交/归档。
