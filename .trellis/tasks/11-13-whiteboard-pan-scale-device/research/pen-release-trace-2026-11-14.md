# Research: 屏幕笔释放日志与到达簇护栏

- Query: 屏幕笔几乎没有惯性、数位板正常；依据用户 2026-11-14 粘贴日志确认实际失败分支，并给出最小修复边界与回归数据。
- Scope: internal；只读用户日志、当前 CanvasNavigation / RTS / Controller 代码。仅写当前任务 research 文件。
- Date: 2026-11-14（Asia/Shanghai）。
- 基准: 当前已提交 c2f21bd0；本报告以调查时源码行号为准，后续最小补丁可能使行号移动。

## Findings

### 1. 本次证据已把实际失败定位到释放前的时间护栏

输入日志为 `C:/Users/alan-/.codex/attachments/aa5aaa08-dd27-4c7e-a5ec-13db52d9334d/已粘贴的文本.txt`，850707 bytes、4026 行。本报告不修改原附件。共 344 条导航 input 记录、7 次 `released=1`：6 次屏幕笔 profile=3/mm 全部 `reason=compressed-time`，selected/boosted 为 0、target-distance=0；另一次 profile=0/系统 DIP 的 class=2（Mouse）为 disabled。没有任何屏幕笔非零惯性起步证据，因而此前“晚到 Pointer 取消已经开始的惯性”条件链不是解释这六次失败所必需的原因。

| key | 释放日志行 | release seq | reason | 存活估速点 / span ms | 最后 Move 到 Up ms |
| --- | ---: | ---: | --- | --- | ---: |
| 75 | 28 | 9509 | compressed-time | 2 / 5.333 | 3.001 |
| 76 | 650 | 9684 | compressed-time | 2 / 5.201 | 3.218 |
| 77 | 1294 | 9864 | compressed-time | 2 / 6.577 | 7.246 |
| 78 | 2016 | 10094 | compressed-time | 2 / 21.508 | .957 |
| 79 | 2700 | 10324 | compressed-time | 2 / 23.621 | .733 |
| 80 | 3301 | 10518 | compressed-time | 2 / 10.989 | 3.260 |
| 81 | 3440 | 10553 | disabled | 1 / 0 | 51.598 |

每次屏幕笔后的 `release-profile`（行29、651、1295、2017、2701、3302）均为 source=14546769408、profile=3、unit=mm、tau=.2、c=350、target-distance=0、duration-s=0。`timing-resets` 是本次 Step 的统计，Up 帧显示 0 不表示此前没有重置；例如行19–20/seq9503–9504 已显示前一帧 timing-resets=2。

明确的上下文（行179、792、1378、2067、2752）均为 trusted-physical、mapped=1、DIP/px=(.5,.5)、motion/mm per DIP=(.19444445,.19791666)。日志声明的这个尺度可用于本次内部数值回放；它不等同外部尺量认证。

这些释放时的 Move age 均小于 8 ms，没有命中 30–70 ms stale 衰减；没有必要把现象解释为“只是短尾”或“抬笔延迟”。当前可靠估速还没通过，摩擦曲线未启动。

### 2. 直接触发点是把相邻回调的压缩到达间隔用于毫米速度异常判断

- `Draw3.RealtimeStylus.cpp:1482–1484` 的 Packets 只解码最后一个 packet，并把回调内 `QueryQpc()` 传给 DecodeSnapshot；`:1057` 赋给 snapshot.qpc。它不是硬件采样时钟。
- `Draw3.DrawingController.cpp` 导航 input 日志明确标 `timestamp=callback-arrival`；本附件每条真实 Pen 导航输入都沿此语义。
- `Draw3.CanvasNavigation.cpp:261–262` Configure 将 profile 的 `velocityLimit` 同时用于相邻时间可靠性护栏。
- 同文件 `:280–296`：相邻 dt<2 ms，且段位移超过 `velocityLimit*(dt+1/QPCfreq)+float量化误差`，立即清整段历史并置 timingRejected。
- `:326–337,395–401`：重置之后必须重新至少3点且20 ms 才能解除 timingRejected；当前屏幕笔常常每8 ms 左右收到一对相距不足1 ms 的回调，第二个点再次清窗，始终积不到可靠尾段。
- `:995–1051`：raw 估计无效则 selected=0，因此 detector.Up 不会启动 fling。其后 tau/c 参数即使改长，也无法让这些释放产生惯性。

数字例子（附件行201–202）：

```text
sampleQpc=41347061593  posPx=(868.082,1486.639)
sampleQpc=41347062181  posPx=(871.710,1481.348)
```

按日志 delay-ms 与 QPC 差值一致推得约 10 MHz，间隔 .0588 ms；按上下文换成 mm，位移约 .6313 mm；1800 mm/s 只允许约 .1060 mm，加 float 误差也远小于实际位移，必然清窗。这里真实鼠标/笔位移仍然用于跟手，只是估速证据不断被丢掉。

各完整接触按原护栏离线计算的 destructive resets：key76=29，77=26，78=36，79=33，80=30。key75 只有附件截断尾部，不能当完整手势进行起滑期望断言。

### 3. 有多毫秒真实尾段证据，不能一概判作完全不可恢复的时间压缩

配套 [JSON](pen-release-trace-2026-11-14.json) 已导出全部344条原始导航记录（原附件行号、phase、QPC、px、class），没有补点或重写时间。QPC frequency=10 MHz 是从 delay-ms 一致性推得的回放假设，附件没有单独 frequency 记录，应在 fixture 中明确这一来源。

从最后真实 Move 向前选择相隔至少2 ms 的真实点（保留50 ms窗口左侧锚点），独立线性拟合如下。4 ms 选择在本次尾段得到同一组点；8 ms 也保留4–5点、超过44 ms，给出相近方向/速度。这里仅说明存在足够的真实时间跨度，不是已经运行生产 C++ 的结果。

| key | 2 ms真实锚点数 | span ms | 拟合 speed mm/s | 8 ms真实锚点 speed mm/s |
| --- | ---: | ---: | ---: | ---: |
| 76 | 7 | 56.240 | 1029.25 | 1011.55 |
| 77 | 8 | 52.139 | 1190.80 | 1168.69 |
| 78 | 6 | 51.622 | 879.28 | 842.45 |
| 79 | 6 | 54.276 | 769.42 | 717.36 |
| 80 | 8 | 56.636 | 509.16 | 483.21 |

因此所有完整的五次屏幕笔手势都能以多个真实锚点支持正常的非零释放，且低于1800 mm/s输出cap。key75片段只有2个相隔5.33 ms的真实锚点，仍应保守拒绝，不为凑PASS补造Down/旧点。

### 4. 最小修复建议与护栏

建议把改动限定为 ScreenPenPhysical / ScreenPenFallback 的回调到达簇估速：

1. 短相邻间隔超cap时，向前寻找 current-anchor≥2 ms 的最近真实点，按真实跨段距离、真实跨段时间和原量化误差复核；仍超界才按原机制清窗。没有跨段锚点时，保留几何/真实点但维持 timingRejected，不能起滑。
2. 可靠释放与拟合改用从最新真实点反向选取的 ≥2 ms 独立锚点，至少3点、20 ms。保留最新真实末点，不丢尾部减速或抬笔前位移；不平均/补写时间戳，不把 Up 当 Move。
3. 真实反向仍逐条 raw Move 处理；到达簇筛选只决定估速证据，不能把末段反拖藏在“合并”里。stale、异常Up可靠闭合、取消、有限值、最终速度cap及boost规则保留。
4. Mouse/外置板 IndirectDip 原算法保持；Touch 不顺手扩围。原三点0ms/0、20ms/30、21ms/60 DIP压缩案例仍必须无惯性，不能因刚好有3个raw点而把它当3个独立时间簇。

对候选跨段复核算法做了仅Python的真实输入数字检查（未运行生产代码）：key76/78/79/80不再有 destructive reset；key77仍在行1132相对于1113有1次复核超界，真实7.7648 ms内移动14.8341 mm，平均1910.43 mm/s超过1800。它在Up之前已有足够新证据恢复，五个完整手势末段均仍满足锚点数量与跨度。不能宣称这个护栏只会拒绝假时间，真正高于配置cap的快速运动也可能触发。

简单将dt替换为max(dt,2ms)不能修复本次全部动作：短pair位移最大5.154 mm，大于1800*.002=3.6 mm；keys76–79仍会分别有13、15、10、18次reset。仅抬高profile cap或删除护栏缺少明确正确性边界，也会改变实际手感上限。

建议最小回归：将JSON中keys76–80的完整Down/Move/Up按原始QPC差值与各轴转换进入真实CanvasPan入口，断言held按时闭合、五次释放均非零且正确方向/cap；key75作为截断证据只记录，不编造起点。另保留整段<2ms压缩、原三点压缩、极大单点突跳、真实末反向（包括同簇内反向）、停顿后Up、默认IndirectDip不变与960Hz合法点。只证明曲线/公式不能替代实际入口回放。

## Files Found

路径根为 `D:/Project/Inkeys/Repo/Inkeys`。

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp/.cppm`：相邻时间护栏、50ms逐指历史、反向、可靠释放、最终速度cap。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp`：Packets最后真实点及callback-arrival QPC语义。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：来源/profile解析、真实输入诊断及释放输出。
- `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp:643–710,1095–1112`：压缩、短反向、量化及960Hz物理速度cap既有回归。
- 本目录 `pen-inertia-user-feedback-2026-11-13.md`：上一轮尚无硬件日志时的条件性假设；本次日志已提供更直接的分支证据。

## External References

无；本题由当前本地源码及用户日志直接回答，不新增API或外部行为假设。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：真实点、没有硬件时钟不造采样时刻、时间/反向护栏、五profile及诊断。
- 本任务 `design.md`：屏幕笔短尾与间接设备分工；普通hover不刹停，Surface不代表所有设备最优。

## Caveats / Not Found

- 本轮研究没有执行C++、GUI、产品或硬件回放；Python只用于日志计数与独立数值检查。最终实现/构建/生产入口回归由implement/check代理负责。
- 日志没有本次外置数位板的完整Pen轨迹，key81是class2 Mouse，不能拿它证明真实数位板采用哪个backend。屏幕笔和数位板均可产生Pen，但前者有直接屏幕关系，后者是间接映射；真实profile选择不同，不代表两者必须同手感。
- callback-arrival不能恢复硬件采样时刻，以上不能证明硬件自身失常。没有硬件时钟，某些中等幅度突跳与合法延迟点在观察上无法完全区分；应保守保留跨段/独立锚点/最终cap及反向保障，不能宣称识别所有时间压缩。
- 仍无证据证明晚到Pointer候选不存在；只是它不是当前六次零释放的必要原因，最小修复无需同时扩大到跨API身份/时钟改造。
