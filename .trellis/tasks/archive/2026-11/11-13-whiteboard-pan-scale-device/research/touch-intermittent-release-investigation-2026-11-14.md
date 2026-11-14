# Research: 纯手指触摸偶发无惯性的证据边界与条件风险

- Query: 用户反馈屏幕笔已有惯性但偶发被拒绝，纯手指触摸也似乎出现；新日志有截断，深入核实Touch的实际失败分支。
- Scope: internal；只读新附件、当前生产Touch释放/仲裁代码及现有诊断；仅在本任务research中写报告与独立计算脚本/JSON。
- Date: 2026-11-14（Asia/Shanghai）。
- User fact: 用户后续明确，出现触摸问题时笔已经远离，仅手指操作。本报告不把触摸问题归因于笔靠近/悬停，也不否定用户实际遇到的问题。

## Findings

### 1. 新片段没有覆盖可分析的Touch生命周期

附件：`C:/Users/alan-/.codex/attachments/c5d97310-dfa6-440a-a4d7-dca0cb31e3c8/已粘贴的文本.txt`；848437 bytes、4371行、SHA256 `30999aa99ddac1a2db2ba4dc43da8c844fb89e66094fa19c133e55492e839ea7`。首行从字段中间开始；不能据此恢复片段以前的Down/Move/Up。

| 证据 | 片段内数量 | 可得结论 |
| --- | ---: | --- |
| CanvasPan `class=1`（Touch）输入 | 0 | 没有可重放的Touch导航Down/Move/Up |
| `touch-begin` / `touch-end` / `touch-position` | 0 / 0 / 0 | 没有可见Touch光标/接触生命周期 |
| `touchCount>0`、`suppressed=1`或`touchSuppressed=1` | 0 | 不能以当前片段证明活跃Touch或Touch-Pen互斥 |
| RTS `deviceKnown=1 device=0` | 0 | 没有已解码的Touch输入诊断 |
| `rts-batch`（当前对非Pen逐Packets输出） | 0 | 没有Touch逐回调计数/时间证据 |
| `pan-reject device=0` | 0 | 没有Touch因mode/owner/native而拒收的明确记录 |
| Mouse source `device=4`（IMDT_TOUCH） | 0 | 没有本片段新的Touch兼容Mouse来源 |

片段内出现的6条 `rts-input ... deviceKnown=0 device=0`（行138/387、660/916、1202/1470）是三次Pen Down/Up进入回调、尚无decoder时的占位。`RecordInputDiagnostic`在没有decoder时输出device=0；必须同时看deviceKnown，不能仅因枚举Touch=0就把它算作手指。

本片段的三次明确释放都是ScreenPenPhysical/mm（行393、924、1478），具体Pen失败/成功由并行研究报告负责，不冒充Touch证据。行3708和4263的 `navigation-stop reason=leave-move-mode`只证明后来离开移动模式，不能回填触摸释放失败。

### 2. `touchKnown=1`是旧缓存，且不是触摸仍按住

共有171条 `mouse-filter touchKnown=1`，全部是同一个缓存：`touchX=1267 touchY=1180 touchTick=6330734 touchId=0 primary=0 via=compat-mouse`。第一条完整缓存记录在行6，最后在行1623。附件第一条完整Cursor记录的tick=6334187（行2），已比缓存触摸时刻晚3453ms；缓存仅指向片段以前的触摸兼容位置。

行1623–1627：新真实Mouse消息到达，`sourceReject=0`，owner从Pen转Mouse，`accepted-mouse source=2 promoted=0`。行1640下一个Mouse过滤记录已变为touchKnown=0。此变化由WindowControl接受Mouse后显式清缓存（当前源码:3004）引起，不是自动超时“过期”。

因此，本报告的结论是“**这份被截断的片段没有那次Touch手势的证据**”，不是“用户没有做Touch”或“Touch不会出问题”。用户的纯手指反馈仍成立，需要覆盖该次手势的日志才能定实际原因。

### 3. Touch仍保留相邻短到达间隔清窗，存在同类条件风险

当前 `CanvasPanReleaseVelocityTracker::Configure`（CanvasNavigation.cpp:274–279）只为ScreenPenPhysical/ScreenPenFallback开启screenPenArrivalTiming。TouchPhysical与TouchFallback仍执行原分支：

```text
if adjacent_dt < 2ms and distance >
   profile_velocity_cap * (adjacent_dt + 1/QPC_frequency) + float_coordinate_error:
    clear this pointer's velocity history and direction
    timingRejected = true
```

Guard在同文件:309–333；TouchPhysical cap=2400mm/s（:183–185），TouchFallback沿默认cap=12000系统DIP/s（CanvasNavigation.cppm:43）。这两个cap也是最终释放/boost限速，但“输出速度超过cap”与“回调到达时间不可靠”并非同一个事实。

RTS Packets对Touch也只保留批末点，以 `QueryQpc()` 写入callback-arrival时间（RealtimeStylus.cpp:1450–1514，关键:1483与1504），并没有Touch独立硬件采样时钟。若多个已有位置点在很短的回调间隔内补到达，Touch的相邻pair仍可能被清窗；最近一次ScreenPen修正没有改变这个Touch路径。

这是一条当前源码可证明的**条件性缺口**，不是这次纯手指实测失败的已证实分支。不能因前轮Pen确实发生过成簇到达，就断言所有Touch拒绝都出自它。

### 4. 独立合成数字检查：相同总体运动，仅到达节奏变化就可使Touch清窗

配套脚本忠实复刻Touch的标量单调运动分支：相邻短dt guard、40ms gap、50ms尾窗及左锚点、容量64、3点/20ms可靠资格。没有执行产品C++，没有读取/生成真实Touch硬件时间；这些是明确标注的合成条件示例。

频率10MHz；20个位置依次增加，成对回调的间隔是0.1ms，每对之间约8.233ms；总时长75.097ms。对照组保留同样端点/总时长和点数，仅均匀到达。Up距最后Move统一2ms，排除stale条件。

| Profile | 每个相邻位置增量 | 总体平均速度 | 成对到达结果 | 均匀到达结果 |
| --- | ---: | ---: | --- | --- |
| TouchPhysical | 1mm | 253.006mm/s | 10次清窗；Up前只剩1点；compressed-time | 0次清窗；14点/51.382ms；Ready |
| TouchFallback | 5系统DIP | 1265.031DIP/s | 10次清窗；Up前只剩1点；compressed-time | 0次清窗；14点/51.382ms；Ready |

Physical短pair允许距离约0.24024mm，实际1mm；Fallback允许约1.2012DIP，实际5DIP，float误差不足以解释差异。总体平均速度分别低于2400/12000输出cap，但相邻到达pair的瞬时速度会远超cap。

这个例子只能说明：当前算法可能把到达节奏压缩解释为不可用时间，普通总体速度也不能避免清窗。它不能由纯位置/QPC证明原始硬件采样应当如何分布；真正的瞬移/伪时间与延迟到达仍需可靠证据区分。因此不能简单删除guard或伪造均匀时间。

### 5. 还需要区分的真实Touch拒绝条件

没有Touch Down/Move/Up，不能在这些分支之间选择：

- 时间护栏清窗后，新尾段不足3点/20ms；或合法但过短的独立手势本身达不到资格。
- 相邻回调间隔达到40ms使尾段重建；最后Move到Up≥70ms会stale清零，30–70ms逐步衰减。
- 真正末段反向累计≥TouchPhysical 0.25mm / TouchFallback 1DIP，确认后旧方向证据被切断；新方向过短会无惯性。保留全部raw点仍有必要，不能用簇合并藏起反向。
- 拟合方向冲突、末端主动减速上限、未过释放速度或起步slop、惯性开关关闭。
- 多指中不是最后一指释放：released只在最后有效持有者终态输出；某一指反向退出可清剩余释放证据，不能用合成focus假装每指都在同一时间移动。
- 模式/页/显示标尺切换、overflow/Stop的明确取消；或非导航Down的准入失败。这些必须看真实Down、pan-register/admission与stop原因，不能从最后一行Idle判断。
- 已成功起滑后被后续真实Down抓停，与“释放时selected=0”是两类现象。现有直接mailbox StopFling缺独立原因诊断，Cursor owner本身也不能证明接触。用户当前明确笔远离，不把该条件候选当本次Touch原因。

## 建议的最小判别证据与后续方向

1. **先补到失败Touch的完整实际链**：至少保留它的Down/`pan-context`/`pan-register`、class1的Move和Up、release/release-profile及之后数帧。最好紧邻保留一次成功Touch作对照，并保留CanvasPan enabled行中的频率/build。不得给截断片段补造Down。
2. 仅分析惯性时可短时单独保留现有CanvasPan诊断，减少Cursor/RawMouse数千条记录挤掉前面的Touch。若没有pan-register/class1，再短录Cursor以核对RTS `Down arrival/published`、`touch-begin/end`；不要把Cursor gate关闭后的缺少这些日志当作输入未到达。
3. 若实际release=compressed-time，需要输出/重放具体被拒绝pair或跨段锚点（key/profile/unit、前后QPC/位置、dt、distance、允许界、清窗前后count/span及真实最新点）。当前timing-resets是每Step计数，不标具体样本；只看Up帧timing-resets=0可能漏掉此前已经清窗。
4. 若release=Ready而接下来突然Idle，需独立记录StopFling/Stop的入口与触发者，而不是统一 `fling-complete-or-cancel`；同时保留最后Pen有效接触时间、实际新Down、clamp与模式切换，才能区分解析终点和非预期取消。此项是最小诊断建议，本轮未实现。
5. 若Touch实际证实成簇误拒绝，应重新审查“到达可靠性”和“最终输出cap”的分工，可复用真实跨时间锚点证据，但不能机械套用当前仍在调查的ScreenPen方案。保留物理/回退不同单位、每指独立历史、最新真实末点、逐raw反向与Up年龄、真异常跳跃及全段压缩拒绝；不改EDID/后端或造硬件时间。

后续验证矩阵应覆盖TouchPhysical/TouchFallback、桌面/大屏、各向异性、单/双指异步上下、普通/成簇/整段压缩/超大跳跃/主动减速/短反向/stale、同向连续/反向接力、已起滑后的Down中断。先保留旧Touch连续手势正确性基线，再做真实失效轨迹回放；不能把当前合成例当真人手感验收。

## Files Found / Code Patterns

路径均相对 `D:/Project/Inkeys/Repo/Inkeys/`：

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp:170,274,309,447,465,1053`：profile cap、ScreenPen-only到达处理、短dt清窗、可靠资格、拟合与释放；`cppm:28,35–43`是时间窗口/容量默认值。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2675`：Touch实际映射/物理资格决定Physical/Fallback；`:3040,3065,3132`记录历史gap、真实导航input和准入；`:3300,3318`记录mode/native拒收；`:3114`明确生产移动模式hover不刹停；`:16542`附近的mailbox分支与`:16559`起释放诊断。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1225,1254,1344,1483,1504,1655`：RTS Down/Up、Touch生命周期通知、最后点/回调时钟与deviceKnown语义。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1604,1620,1645,2547,2852,2908,3004`：Touch计数/抑制、位置缓存与兼容Mouse来源、接受真实Mouse后清缓存。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Diagnostics.cpp:719,817,837`：独立Cursor/CanvasPan gate、有界输出、enabled频率与日志序号；诊断丢弃/粘贴截断不等于输入丢失。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cppm:27`：RTS InputDeviceType固定Touch=0/Pen=1；不能混用Mouse源API枚举。
- `Inkeys/IdtMain.cpp:3416–3433` 与 `Inkeys/Inkeys/Other/Other.Config.cppm:507–508`：现有Cursor/CanvasPan两个独立控制台gate，本轮没有改配置。

## External References

无。结论来自本地当前源码、用户明确反馈和附件；没有引入新的Windows API行为假设或修改输入后端。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：逐指真实尾段、反向/stale/Up闭合与诊断合同；最近到达簇修正明确仅ScreenPen。
- 本任务 `prd.md` R4/R6/R8/R9 与 `design.md`：保护Touch连续手势、单位闭环，Surface不代表所有设备最佳；精确根因和舒适性仍需要真实证据。
- 配套 `touch-intermittent-release-investigation-2026-11-14.py/.json`：可复算的日志计数、缓存行号与合成护栏数字；均未执行产品C++。

## Caveats / Not Found

- 实际Touch拒绝的Down/Move/Up、释放原因、当前profile、手指数、开关和其后是否被取消均未出现在本片段；不能计算真实拒绝率，不能否定用户现场体验。
- 用户明确笔远离，不能用笔靠近或旧Pointer候选解释这次纯手指现象；普通hover生产合同也不刹停。
- 没有改产品/测试/spec、没有Git操作、没有构建/GUI或硬件测量。脚本只做本地离线计数和标量条件计算；不能把Ready判定模拟等同完整生产coast已经执行。
- 阅读当前源码的CanvasNavigation.cpp SHA256为 `39a5c7f80f135a4106d99603169f7b111f8f842689f172fc7139b0af8c672041`，与前轮最终机器证据一致；后续代码改动可能使上述行号移动。
