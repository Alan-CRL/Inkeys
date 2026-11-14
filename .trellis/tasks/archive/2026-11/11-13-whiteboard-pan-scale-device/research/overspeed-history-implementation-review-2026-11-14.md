# 独立检查：超速保留历史与末点支持界

日期：2026-11-14。最终结论：PASS。源码静态审查、必要旧residual回归、最终隔离完整构建、五专项和完整无窗口套件均通过。本检查不构建、不启动GUI、不提交/推送/归档；未回滚此前八文件修复。主会话统一管理构建与冻结，检查只在明确解冻窗口追加既有canvas测试。

## 实现与批准合同

- tracker不再持有输出velocityLimit或以超cap清时间窗；只保留真实长gap、逐raw反向、无效/重复/倒退时间与既有生命周期保护。高速度不会单独清history/boost。
- 直接Pen/Touch以最新真实点为端点，反向取≥2ms实际独立锚点，≥3点/20ms；Indirect保留原全部raw点拟合次序。所有profile质量支持使用独立锚点。
- 最终A规则：当前尾窗相邻独立锚点沿raw方向的投影超过两端float误差，至少两个这样的正向区间才给非零支持。没有把reversalDistance当每段最小位移，也没有用共享末点的长跨段重复计数。
- 对最新A/B/C的AB与BC，支持上界分别为(max(0,projection)+既有reversalDistance+floatError)/(实际dt−一QPCtick)，取较小者。dt≤一tick或非有限时保守不足；AB不含最新C，BC不跳过零位移。工程空间预算不是新绝对速度阈值或硬件量化认证。
- Snapshot与release都组合raw、既有RecentSpeedLimit、支持界和profile输出cap，age仍按原真实最新Move计算。它们只改变估计速度，held实际contentDelta保持1:1；没有逐轴限幅。
- 支持是否额外收紧与min(raw,oldTail,profilecap)比较，随后才age衰减；支持实际限幅时取消本次旧residual boost，保留历史。只因profilecap裁剪不取消资格，booster相加后仍有最终向量cap。
- 真实反向先切旧历史/方向，支持只从新尾段取；新方向不足不回旧窗口。stale、cancel、坏终态、Stop、源/单位/epoch变化继续保护，未改后端、配置、EDID、橡皮、触控板或工程系统。

## 检查提出并落实的修正

1. cap后实际限幅分类：原候选按cap前raw/oldTail比较，可能在支持界高于最终cap时误标质量限制。已由implement按批准合同修正，混合尾段0/0、10/60、20/120、30/180、40/200、50/230的回归要求仅cap限幅，不禁boost。
2. 真实新key75回归已累计各Step reset而非只看Up，sourceKey低位改为实际3843376640；fixture说明保留截断缺失事实。
3. 旧residual防旁路原测试仅新建空内核，Reset即使失效也可能空过。本检查在主会话解冻窗口仅追加TestQualityLimitedContinuation：ScreenPenPhysical/TouchPhysical各验证真正旧coast→同源Down→质量受限/纯cap两条接力。明确旧残余超过boost门、滚动资格窗覆盖Up、真实第二触点闭合。
4. 质量受限分支要求boosted=selected且旧残余更大；纯cap分支使Up age=40ms，selected低于cap，从而真正观察旧残余加入，而非只比两个相同的饱和cap值。最终boost必须=min(selected+实际残余,profilecap)。这四条场景已由最终headless-pan专项实际运行通过，见下节对应日志。
5. 已指出旧spec矩阵残留“跨段超cap即清窗”，主会话已改为新合同。检查未私自编辑其spec。

## 诊断与资源边界

release-quality交出key/source/profile/unit、raw点与独立锚/正向区间数、真实首尾QPC、AB支持锚QPC、支持上界及valid/limited/output/boost标志。support速度属于内部motion单位，raw/selected/boosted公共速度仍系统DIP，不混标签。早终态路径清新metadata，普通非释放帧不复述旧释放。

新增释放格式化在既有CanvasPan gate和released分支内；关闭时不取诊断时钟。LogCanvasPan复用4096字节异步记录、LastError保护，未新增逐点硬件查询/heap/线程/timer。支持只用≤64点固定scratch和O(n)扫描。Stop/StopFling仅新增原因日志，不改变原停止动作；input epoch变化采用中性input-history-epoch-changed标签，不一概叫overflow。

字段范围提醒：releaseOutputLimited/output-limited表示支持后、boost前手速被profilecap限制；booster相加后最终cap仍生效，但该bool不记录后者是否触发。主会话已在规范注明pre-boost范围，不能根据此flag=0推断输出从未限幅。

## 验证证据（持续补充）

已独立核对日志SHA、sourceBefore/sourceAfter冻结及退出码：build-red完整ARM64 solution exit0/6.704s；headless-pan-red exit1/1.625s，日志确实包含新key75和多profile超cap清零失败。随后build-green-1 exit0/23.047s、headless-pan-green-1 exit0/1.531s；不是只新增测试却未证明其能抓住旧问题。

追加旧residual测试后，green2编译无源码错误，默认输出链接首错LNK1104不能打开Build/ARM64/Debug/Inkeys.exe。主会话没有关闭用户进程或改源码规避，改用同Solution/Debug ARM64/原ARM64 MSBuild的命令行隔离OutDir继续验证。最终build-green-3-isolated完整solution通过（exit0/18.641s，0warning/0error），采用命令行OutDir=Build/ARM64/Debug/PanOverspeedValidation，未改工程或编译配置。最终五专项均exit0：headless-pan-final 1.688s、control-fence-final 1.531s、pan-feel-final 3.718s、pen-width-final .218s、diagnostic-output-final 1.969s。已独立核对其sourceBefore/sourceAfter与最终构建、当前八份文件hash完全一致、实际EXE路径均为隔离新产物、EXE与日志SHA均匹配。完整headless-all-final exit0/382.750s，末行PASS animation correctness；其sourceBefore/sourceAfter与最终构建及当前八文件完全一致，实际Headless EXE与pan专项为同一SHA256（17a20bc67eb5123923f806833ece2fcacc03168cbb7525cb934b548110306d1c），完整日志SHA已独立校验。没有仅凭末行或复用旧产物认定通过。

所有写入保留原UTF-8/BOM/CRLF；DrawingController继续无BOM。追加测试后git diff --ignore-submodules=all --check通过。最终八文件均已复核UTF-8可解码、BOM与HEAD一致、纯CRLF，源码hash匹配最终构建/专项；无大面积格式变更。

## pan-feel与此前基线的独立分类

旧SHA为c3c3e6975bac0b8998c98a291a72f71e823bf708b8a1e01701238df01e67d9df，新输出SHA已变化，不再声称字节等价。独立比较明细保存在Build/Logs/pan-overspeed-history-2026-11-14/pan-feel-comparison.json。

2241行中2240行带scenario（含128条INFO），602行变化；未改变scenario/fps/scale/delivery身份。max_delta_px、stop_ms、residual_px、locked_error_px、nonfinite、failures字段逐字节不变，无任何原先非零release变为0。没有发现跟手位移、停止残余或错误计数回归。

- 525行max_dv估计速度瞬态变化，其中512行差值>.01：480条减小，32条topology-2至5增加（最大增加426.70DIP/s）。这不是全为舍入，也不能称常规估计速度完全不变；源码变化与字段证据将其归入时间/运动资格及支持界对估计速度的影响；这是对峰值成因的推断，未对每个峰值逐帧定位，不能据此宣称真实帧更平滑。held几何与最终释放没有对应异常。瞬态预取/blur速度的真实感受仍应校准。
- 64条constant记录变化中56条只有max_dv；另外8条还有极小的release/滑距浮点差异。全体非accelerate场景release最大差0.001343DIP/s、glide最大差0.000976DIP、理论剩余距离最大差0.000336DIP；并非严格逐字节同，但没有物质性释放变化。
- 实质释放变化仅8条accelerate-lift：30fps/frame的四档scale由约2749.998降为2655.016DIP/s，相应尾距约616.279→590.632DIP。此例加速度7500DIP/s²，前一独立33.3ms区间平均速2625，加现有1DIP空间预算/33.3ms约30，支持上界约2655，与新值一致，是批准质量确认的代价。
- 240fps/subframe的四档scale由约2808.594变为2812.500DIP/s（+约0.14%），尾距增加约1.059DIP；TouchFallback由全部高频点改为≥2ms独立锚点LSQ，窗口相位不同，这是样本选择变化，不是曲线参数/boost无界叠加。30fps释放比例约.885、240fps约.9375，原验收界均保留。
- 12条pause_restart INFO速度仅有≤0.000126DIP/s的差异。其余release差异属上述小量浮点/选锚相位，没有新增突然零释放。

## 已知代价与未验证范围

最少两个正向区间意味着仅一次真实运动区间可能保守不起滑；真实持续加速需后续区间支持，低报告率可能更明显。零限制段直接判0的B变体未采用，因为离线量化矩阵产生6/448新增误拒。

整个静止尾窗+单jump及其后同位置只有一个正向区间，应为0；但已有真实运动后jump/平台仍可能保留空间预算内尾流。约2ms时Pen预算可约75mm/s、Touch约125mm/s，不能统称不可见或完美停笔。这是批准并明示的质量取舍，不作为未授权重调理由。

Surface/Touch真实轻放、加速和快停手感仍需用户设备验收。当前Touch故障附件缺真实class1生命周期，不能以合成PASS声称其现场根因已全部解决。
