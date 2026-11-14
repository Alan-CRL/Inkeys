# 白板释放质量与Touch运动证据

## 1. Scope / Trigger

适用于CanvasNavigation的释放估速/支持、Touch末平台、motion age、boost与呈现速度修改。主输入/所有权/单位/曲线合同见[canvas-pan.md](canvas-pan.md)。Touch新分工已实现，结果以任务验证记录为准；Pen/PenTail/Indirect前轮路径保持。

## 2. Signatures

- 保留`LastSampleQpc`及raw样本含义，终态合法性/水位/延迟不换时钟。
- raw与Touch progress各固定≤64点，总存储约增加24KB/Tracker；不是两个流共用64个槽。
- `rawReleaseVelocity`对Touch可来自contact/group候选，不保证等于local-contact的raw-points拟合；以touch-decision候选来源为准。raw-points/span仍是原始观察统计，非Touch原raw语义保持。
- `LastMotionQpc(key)` / `MotionSampleCount(key)` / `HasStationaryObservation(key)`：Touch读取进展时钟/点数/显式重复观察，其它profile回原raw含义；`LastSampleQpc`始终保留raw含义。`History::progressSamples/progressCount`固定64点，无逐Move分配。
- `CanvasPanFrameOutput::{releaseMotionSampleCount,releaseLastRawQpc,releaseLastMotionQpc,releaseMotionAgeLimited}`并列原始/运动时间，`release-quality`追加motion-points/raw-last-qpc/motion-last-qpc/age-limited，不覆盖输入延迟时钟。

## 3. Contracts

- `ReleaseSupport(...) -> CanvasPanReleaseSupport{anchorCount,positiveIntervalCount,firstQpc,lastQpc,supportFirstQpc,supportLastQpc,speedUpperBound,valid}`；FrameOutput附key/support与质量/输出/boost标志。非法终态TimingRejected记invalid-time，InsufficientSupport记unsupported-tail；正常限幅不计timingReset。
- 最新独立锚点A/B/C、raw方向u，各段upper=`(max(0,dot(delta,u))+reversalDistance+floatError)/(dt−1/frequency)`，取AB/BC最小。floatError为两端坐标模长×float epsilon；dt≤tick、非有限或不足两正向区间时不给非零支持。reversalDistance是既有工程空间容差，非硬件量化认证或绝对速度界。
- age前基准为min(rawSpeed,既有RecentSpeedLimit,profileOutputCap)，再应用supportUpper；只有支持界实际压低这个cap后基准才标support-limited并取消本次旧residual boost。cap-only不否定输入或boost。releaseOutputLimited/output-limited专指boost前支持后的手速被profilecap裁剪，不含旧residual相加后才触发的最终cap；后者需结合boosted与当前cap判断，不能由output-limited=0推断所有阶段未限幅。FlingBooster后仍有最终向量cap；不叠历史初速，不逐轴裁剪。
- 共用基础支持界；Snapshot呈现估计与Touch释放决策分别保持其时间职责，预取/blur有界，held实际contentDelta仍1:1。固定≤64点scratch、O(n)扫描，不增per-Move heap或全点对拟合。
- 代价：一个新运动区间可能保守0，持续加速需多区间确认。单jump不足两正向区间不能声称可靠速度；已停住contact不得借距离降级复活；已有运动后的jump/平台仍可能有noiseBudget/dt尾流，2ms预算约Pen75/Touch125mm/s，不保证精确停笔。B变体因新增6/448量化误拒未采用。
- 算法、对照和验收见[执行计划](../../tasks/archive/2026-11/11-13-whiteboard-pan-scale-device/overspeed-history-plan-2026-11-14.md)及[质量研究](../../tasks/archive/2026-11/11-13-whiteboard-pan-scale-device/research/overspeed-quality-design-2026-11-14.md)，不由规范宣称硬件PASS。

### Touch集中释放决策

- 纯值API：`ResolveTouchRelease(TouchReleaseFacts)->TouchReleaseDecision`。mode为NotEvaluated/Stop/Measured/ShortSupported/ResidualOnly/DistanceBounded；Facts分开raw新鲜度、motion/scroll年龄、平台/确认停止、候选、方向、确认位移与当前残余。Decision交出handVelocity/velocitySeed、参数来源、evidence key/qpc、distanceBudget、addedResidual与boostSuppressed。
- TouchPhysical/Fallback仍保留每指raw和真实进展历史；重复位置不造新运动，raw40ms断报与motion70ms停住分开。Snapshot在尚未实际Scrolling或显式重复时只呈现0；释放/短证据保留，正常滚动的空frame不任意置0，非Touch不改。
- Touch最后Up不刷新先前raw新鲜度。raw新鲜、已有支持运动、平台未到确认停止边界时，不再将约50ms motion-age机械减半或直接禁续甩；输入真正陈旧仍用raw30–70ms门。motion/确认停止>=70ms退出，不能凭新点给已停旧候选续命。非Touch保持原算法。
- Touch标准证据仍3锚/20ms；ShortSupported可低于20ms，但须3个真实>=2ms独立锚、两个邻接段互相dot>0，且各段沿拟合方向位移超过既有空间噪声+float误差。不可把0.32ms两点算作可信巨速，不全局20改10。
- 组候选仅真实非零ScrollUpdate更新；成熟个人向量按当前focus人数组合，静止/未知指计0，不取最快个人。拓扑Down/Up不造运动或续租。新的弱慢scroll只能限制旧候选，不能提高/无限保持last-good；真实反向、断报、取消和换域退役候选/预算。相反个人Up清新腿时用该真实Up作意向时钟，不把Up添为速度样本。 真实非零scroll按既有微秒/至少1ms/最低滚速规则撤销shadow残余、低置信距离资格与旧确认预算；0delta平台不触发，已有可靠慢候选仍可按当前支持输出。混有减速前位移的帧拒绝预算ACK，不能借大Move预算绕过减速。
- Measured/ShortSupported按受支持手速选响应，只加合法当前残余正投影并最终向量封顶。Touch平台续接资格与最终应用由同一Decision处理，避免另撞旧50ms截止；零位移重复不无限续租。没有残余与主动禁止残余是不同状态。
- ResidualOnly仅用合法同向当前残余；DistanceBounded用确认新腿净位移的正投影预算（再受D(cap)限制），按基础摩擦D(v)单调有界逆解并留float余量。两者均为最终seed，禁止再boost及由它们自触发大屏快甩档，不冒称测得手速。tap、未形成scroll、取消/长停、无可靠证据或无可用预算时可Stop。

### 实际位移确认

`TouchMotionTag{group,leg,epoch,metric}`与`TouchMotionFeedback{tag,frameQpc,scrollQpc,present,eligible}`贯穿；`ConfirmAppliedMotion(feedback,actualContentDeltaDip)`仅接受当前匹配tag、当前Step且未确认的frame、有限位移和仍held的组。serial不因Stop立即复用。

Controller在真实ApplyToCanvas/SetViewport成功后，以限位后`-viewportDelta`转换系统DIP确认；不拿requested delta作actual，不等待光学/Present ACK。只累计此前确认的净位移，同帧未确认、混组/混腿/非Touch混合帧不借。实际输入scrollQpc保持，较晚frame确认不续租停止时钟。ClearAxis/限位、反向新腿、Stop/cancel/域变化清/折减对应预算；确认停止后旧contact的单jump不恢复低置信资格。

### 诊断与证据边界

- `touch-decision`记录mode/reason/response、contact-focus/group-focus/prior-current-residual/applied-net-distance来源、证据时间、raw-age/platform、预算和group/leg/epoch/metric。
- `release-quality`明确support-scope=local-contact和support-evaluated；弱末指未评估的0不等同组没移动。多指实际候选以touch-decision为准。同帧后续坏终态必须清release专属Decision/tag/age，不串用前一正常释放；帧级motionFeedback单独保留归属。
- RTS仅在现有gate记录当前实际GUID/metrics、timer/serial未请求/是否返回/未验证，以及终态原始值；desired列表、pressure/tilt、producer与正式arrival时钟不改。未返回未请求属性不等于设备不支持。未知硬件时间不用于导航。
- 当前修复不预测Up前未交付的位置、不在held时自行惯性、不倒回曲线起点补位移。短平台与真实短暂停手不可完美区分；明确停住约0.1秒仍需无惯性。距离降级只提供有限尾滑，不保证所有极短数据都等同完整快甩。
- 历史native对照与本轮验收见[修复计划](../../tasks/archive/2026-11/11-13-whiteboard-pan-scale-device/touch-release-repair-plan-2026-11-14.md)和任务validation；旧7b虽然短拖可滑，却有过大/错向，禁止整套回滚。

## 4. Validation / Error Matrix

| 条件 | 结果 |
| --- | --- |
| 可靠超profilecap | 保留历史，合适出口向量限幅 |
| 已确认停住+孤立末点 | 不制造顶格/旧boost，不借旧预算 |
| Touch短平台/重复raw | 不造新运动；raw新鲜且未确认停住时可保留支持候选 |
| raw断报≥40ms、真实反向、取消/epoch | 原安全栅栏，同步清对应证据 |
| Touch长停≥70ms后新点 | 新腿重建，不回旧快速度 |
| 显式重复/仅frame无样本 | 前者呈现0；后者正常低报点估计不中断 |
| 短支持/距离降级 | 明确模式/单位/预算，不伪测手速或二次加残余 |
| 混组/迟到/重复ACK、限位 | 不借未应用位移，不跨标签复用预算 |

## 5. Good / Base / Bad

Good：raw观察、位置进展、Up时效和呈现分工明确。Base：非Touch路径保持，Touch几何1:1与曲线参数不改。Bad：用输出cap判坏时间、让重复点一直刷新运动新鲜度、无条件丢停手证据、Snapshot清排队Up或补造硬件时间。

## 6. Tests Required

- 共同QPC多报点率/cap两侧、质量受限与纯cap的真实非零旧residual接力；完整新旧时序保护。
- 完整代表Touch74/108短尾与107/109正常长尾，核selected/age/尾距，不只非零；其余缺日志全量只标离线近似。
- 平台raw新鲜与纯断报分开；70ms停止、长停新jump、反向、多指候选与弱末指新慢拖、量化/低报点率、Up消费hitch与Snapshot不清排队证据。旧motion-age50ms必减半不再作为Touch黄金。
- key4/25/26、64及47/48完整真实回放；3点短强证据/90度非同向；距离逆解实际尾距<=确认预算；真实Controller Apply确认、限位/混组/反向/Stop/迟到ACK；成熟主指+弱末指focus500、新腿Up意向时钟和同帧坏终态metadata。
- 显式stationary呈现0且释放保留，普通无新raw帧估计不中断；非Touch黄金/分支证据，主solution ARM64 Debug与适用完整无窗口套件。

## 7. Wrong vs Correct

Wrong：把Touch末两重复点的noise/dt当物理手速，或把去重后的40ms空档当raw断报。

Correct：raw观察、运动证据、组候选和当前残余分开；Touch纯决策一次选择可信速度/有限退化/停止，最终seed只执行一次。
