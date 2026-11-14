# Research: Touch末端平台的进展历史与运动年龄

- Query: 为Touch单独修复重复坐标末段导致1–3mm短尾，保留Pen/Indirect现有行为，并明确停手、单跳点和catch-up的取舍。
- Scope: internal；当前源码/本机固定日志和离线Python反事实计算。研究只写本目录，不改产品/tests/spec、不构建/运行GUI/Git/commit。
- Date: 2026-11-14（Asia/Shanghai）。

## Findings

### 1. 选定候选的可执行定义

Main已确认下列TouchPhysical/TouchFallback专用方案；其它profile不进入该路径。它是对Touch漫游的工程响应政策，不是恢复真实硬件测量时钟。

1. 原始导航点仍全部进入既有几何、反向、raw回调gap、生命周期和诊断。按住位移继续1:1，不对raw点删改/补时。
2. Tracker另用固定容量64的**位置进展历史**：当前位置与最近进展位置的欧氏差大于两端可计算float误差界时才追加，保留该回调原始QPC；重复/误差内坐标不推进进展末时间，也不以新重复QPC裁掉旧运动窗。进展历史自身保持原50ms窗口+左侧一个真实锚点及容量界，仍取≥2ms独立锚点、至少3点/20ms与至少2个正向真实区间。
3. Touch的raw LSQ、RecentSpeedLimit和现有AB/BC空间支持界以该进展历史计算。平台后C与最后实际进展B之间的桥接区间是真实到达时差；例如key108的B→C为40.3477ms，不能因它超过40ms便清窗，因为raw回调一直持续。**raw无回调gap40ms与motion无进展是两件事。**
4. 原始raw gap≥40ms、真实反向、Clear/Stop、epoch/来源/度量变化仍同时切断raw及进展证据。ClearAxis必须同步两套坐标，避免限位后旧轴速度复活。
5. Touch释放和Snapshot估计的年龄取`Up或frameQpc−lastProgressQpc`。沿用现有≤30ms全速、30–70ms线性衰减、≥70ms归零；重复坐标是明确的“没有位置进展”证据，不再每次把运动年龄刷回0。Down/终态合法性仍按raw contact lastQpc判断，不能拿较旧motionQpc放行倒序Up。raw最新sample用于呈现延迟和队列因果，不能改成motion时间。
6. 当位置已有≥70ms不进展而又出现新C，应退役旧进展证据；可用真实previous raw snapshot作新锚，再追加C。只一个新运动区间不释放，避免长停后单jump刷新运动年龄、复活旧速度。不是把previous点的时间挪到当前。
7. 原质量支持实际限制仍禁旧residual。Main另确认Touch运动年龄>30ms、已进入衰减时也禁旧residual boost，避免原50ms续甩资格窗在30–50ms内把衰减补回。仅正常profile输出cap限幅仍不等于坏输入；最终向量cap保留。

采用双固定有界数组或等价有界表示即可；不需要新后端、线程、硬件枚举或逐Move堆分配。不要只对旧raw窗做临时去重：旧窗已被重复点的最新QPC裁掉时，丢失的运动证据无法重建。

### 2. 真实日志离线反事实

输入固定快照：`Build/Logs/pan-device-feedback-2026-11-14/console.snapshot.log`，SHA256 `540b3817922dd1832aadafc97f53c61d6e7e2e0412f36e15d896b581e54ff94a`。采样时间按日志显式10MHz QPC，像素按本机上下文两轴映射为mm；位置仅保留日志精度。候选与旧算法均单接触离线复算、无boost、基础Touch friction tau=.30/c=200。

| key | 边界完整性 | selected mm/s 旧→候选 | 理论尾距mm 旧→候选 | 候选motion age ms |
| --- | --- | --- | --- | ---: |
| 5 | 缺Down | 29.526 → 471.577 | 1.654 → 102.206 | 49.356 |
| 74 | 完整Down/Up | 23.588 → 238.315 | 1.108 → 42.626 | 49.840 |
| 90 | 缺Up | 24.055 → 307.142 | 1.148 → 59.537 | 49.736 |
| 107 | 完整Down/Up | 875.764 → 875.764 | 213.283 → 213.283 | 8.412 |
| 108 | 完整Down/Up | 23.616 → 388.584 | 1.111 → 80.364 | 8.286 |
| 109 | 完整Down/Up | 883.028 → 883.028 | 215.323 → 215.323 | 8.183 |
| 110 | 完整Down/Up | 29.662 → 0.000 | 1.668 → 0.000 | 49.648 |
| 120 | 完整Down/Up | 8.770 → 14.980 | 0.000 → 0.482 | 49.525 |

- key74/108及正常长尾107/109有完整Down/Up输入，可作native固定回放；key5缺Down、90缺Up，后者只由旧释放age反推末事件相对时间作敏感性分析，不能补造为精确生产fixture。
- key108旧AB同坐标10.5884ms给出约23.6；候选以实际进展B→末端C的40.3477ms支撑，不再用平台内部间隔作速度上限。新选速388.58仍被前段独立支持限制，并非无条件采信最后26.76mm跳动或放到cap。
- 正常107/109的selected和尾距均不变，说明不是整体加长曲线或调高cap。
- 在121次日志释放中只有120次有可用的按key输入序列；还有Down/Up/中间ordinal缺失与多指互相抬起清历史，孤立回放不模拟其完整全局状态。因此离线旧短尾40例**不是**日志实测38例的精确重放，不能据此声称全部120动作都改善。
- 同一120个孤立模型的短尾从40降到5，非零→零只有110：该实际尾段已真实反向，随后只有3个位置进展点、16.73ms，重复平台不再替新方向凑20ms，所以保守不足，不回旧方向。慢例120从0变为约.48mm小尾，不应算作漫游大幅改善。

### 3. 合成对照与明确体验代价

[脚本](touch-terminal-policy-2026-11-14.py)及[JSON](touch-terminal-policy-2026-11-14.json)保留实际运行的离线断言与所有变体数据，不是native/硬件结果。

- 30/60/120/240/480/960Hz下，持续同向20/100/1000/2500mm/s路径在没有重复末平台时维持相同选速/封顶。Fallback另有18组60/1000/15000 DIP/s常速和25/40/70/100ms停顿，仍按原cap12000/c1000，而非假毫米。
- 1000mm/s运动最后进展在32ms、重复上报至56ms、Up57ms：运动年龄25ms，候选保留1000mm/s，旧双段只有约31.25mm/s。它是Touch漫游选择，不能推断用户真的持续移动。
- **真实停止40ms并不保证立即停止惯性**：同样1000基础运动后Up在最后进展41ms，原30–70年龄门保留725mm/s，基础曲线尾距约171.22mm。这是显著尾距，Main已明确接受沿原年龄语义；必须向用户说明短暂停指仍可能续滑，不称微小或完美停笔。运动年龄71/101ms时均为0。
- 静止历史+单jump、之后同坐标重复的各个2.1/10/70/100ms案例均没有两份真实运动支持，选速0。已有300mm/s真实运动再单jump，候选最多约325mm/s且禁boost；之后同位重复不让jump变成新的支持锚，因此不会因重复上报不断抬高，但在70ms前仍可保留明显约64mm尾距，属于有界而非完美异常识别。
- 真正加速/减速维持原质量/末段上界；真实反向切新尾段后不足20ms仍0，足够尾段方向正确。raw无回调40ms仍重置，不借进展历史跨来源/缺输入复活。
- 60ms平台后的新C会推进50ms进展窗，只剩旧左锚+当前C两点，支持不足0；没有C时仍可能保有按age衰减的旧速约250。Main已明确接受这一非单调边界为长停后新运动证据不足，不为所有<70ms平台延长窗口/补造锚点。大/小C都列在JSON，不说任何末端catch-up都能恢复。
- 672组量化（6报告率×7速度×4量化步长×4相位）：实际速度≥15mm/s没有新增“旧>Touch最低10、候选≤10”的零释放。恰10mm/s边界有10组翻到≤10，主要反映运动点间隔/拟合门限，不宣称全部量化样本都相同；其中60Hz/.25mm的候选约7.5，须保留为边界而非用宽epsilon掩盖。

### 4. 生产验证建议

1. 完整key74/108：以物理参数及对应DIP回退重放，检查速度/尾距明显高于旧1mm，真实coast推进；不要只断言Ready。key107/109作正常长尾不应被整体放大对照。
2. 新诊断并列raw latest QPC、last-progress QPC、motion age、进展点数/独立锚点数、重复跨度、age/quality各自抑制boost的原因。原始input序列和raw点必须还在，不能诊断只显示去重后的世界。
3. 明确25/40/70/100ms真实停指语义、长停后单C、反向后重复点、原raw40ms gap、ClearAxis/Stop/源切换、多指相反抬起与样本容量溢出；特别验证旧residual不能绕过motion-age衰减。
4. Pen/Indirect的源码分支和既有本机26次正常Pen逻辑保持，不把Touch策略传播成公共旧golden重写。本研究未执行native，需实际现有headless/生产probe/完整solution验证。

## Files Found / Code Patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp`：`PanIndependentReleaseEvidence`、`AddSample`、`Reason`、`Estimate`、`RecentSpeedLimit`、`ReleaseSupport`（研究起点约149、283–504行）；原raw窗按每次新sample裁剪，平台末零区间主导支持界。具体行号随本轮实现移动，函数名为准。
- 同文件`CanvasPanDynamics::Snapshot/Step`：组合raw/tail/support/cap及年龄；原terminal时间合法性用raw contact，不能混成进展时钟。
- `Draw3.CanvasNavigation.cppm:28–42`：既有40ms raw gap、50ms窗、64点、3点/20ms、30–70ms年龄与2ms独立间隔。
- `research/device-feedback-touch-2026-11-14.{md,json,py}`：121手势的实测coast/Present/短尾与原始行号。
- `research/device-feedback-quality-review-2026-11-14.md`：真实单packet重复、Decode无旧XY回退及448旧实验范围。

## External References

无。以当前本地源码、固定快照及离线数学计算为依据；未新增API语义或硬件能力假设。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：真实点/水位、反向/stale/boost、单位和诊断合同。
- `.trellis/spec/native-desktop/cpp-conventions.md`：最小改动、共享状态、BOM/CRLF/中文关键注释。

## Caveats / Not Found

- 全部数字都是离线反事实，并非native/GUI/物理手指速度；缺失输入、坐标日志舍入、多指外部状态未伪造。
- 无法从重复坐标唯一分辨真实停指和设备交付平台；候选选择Touch漫游优先、以运动年龄退场，不能承诺所有短尾都应变长或所有平台立即0。
- 本轮候选没有改变摩擦参数、输出cap、触控板隔离或后端，也未研究笔尾压力/起步门槛。
