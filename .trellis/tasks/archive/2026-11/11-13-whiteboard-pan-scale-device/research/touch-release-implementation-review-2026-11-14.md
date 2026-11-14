# 独立检查：Touch释放决策与分级交接

2026-11-14。**结论：PASS（源码与自动验证；真机体验待验收）。** 静态审查、必要最小自修及最终专项均完成，未发现剩余产品/测试阻塞。未启动GUI、未commit/push/archive，也未派生代理。主会话统一完整Solution构建；本检查仅在授权下编译独立ARM64纯核探针，并在明确交接后修改CanvasNavigation.cpp及canvas测试。

## Findings (fixed)

- **短强证据的同向语义。** `ShortTouchSupported`原先只检查AB/BC各自对AC正投影，90度/轻微钝角也可通过。补充相邻两段互相dot>0，保留原真实>=2ms、三锚及信噪条件；回归包含90度、阈值内钝角及正常同向。没有把所有未达Short的轨迹一律清零，合法低置信模式仍由Decision裁决。
- **相反非末指Up后的运动腿时钟。** 既有策略清个人/组证据后，裸`BeginTouchLeg()`保留很久以前的intentQpc，可让紧接的新Move被误当70ms停住。改传该真实Up的qpc，仅作新腿意向时钟，不把Up加为运动样本。150ms长组后立即新Move/真实Apply确认回归保护，只借新腿预算。
- **释放专属metadata串用。** 同帧正常Touch释放后接坏早终态或非Touch释放，会遗留前一Decision/tag/raw-age/platform。集中清Touch专属释放字段；帧级contentDelta/motionFeedback保持。已补正常Touch→坏早终态、Touch→Pen、Touch→Touch取消精确对照。
- **影子残余绕过真实慢移。** 原代码Dispatch先让legacy booster取消慢Scroll资格，但`touch_.residualEligible`独立保留。独立native反例：旧500mm/s→新Down→1ms大Move实际7.5mm→20ms后0.3999996mm慢Scroll，legacy残余已0、个人无可靠估速，最终却ResidualOnly恢复500。当前修复在真实非零Touch Scroll上同步既有微秒量化、>=1ms及profile最低滚速语义，撤销影子残余、低置信距离资格与旧确认预算；同帧若含减速前位移则拒ACK，防旧预算借回。可靠当前慢候选仍可正常输出。平台0delta不触发取消，不借legacy超时状态破坏平台合同。
- **slop内虚构呈现速度。** Touch短强证据可在尚未形成实际Scroll时被Snapshot发布给contentVelocity，造成预取/blur的可达错误输入。Touch未detector.Scrolling时仅呈现向量0，tracker/短资格保留；非Touch数值路径不变。回归明确slop内Estimate有效但呈现0，越slop后恢复。
- **测试数值界过宽。** 60ms真实25单位catchup按既有空间预算约420.834mm/s（Fallback约2100DIP/s），断言收紧以拒绝旧1000未减速；成熟移动指加静止旧指的合法focus约500，而非任意非零<=550。

局部精确增量保存在忽略目录`Build/Logs/pan-touch-release-2026-11-14/check-local-fixes.diff`、`check-release-metadata-fix.diff`和`check-followups.diff`。最后产品收口仅涉及CanvasNavigation.cpp的Touch分支和既有canvas测试；随后仅在DrawingController现有pan-feel probe加入前后均Scrolling的采样条件及中文注释，未改Controller产品路径。各文件编码/BOM/CRLF保持；未改接口/RTS/配置或非Touch策略。

- **恒速probe的实际启动边界。** Touch slop内呈现0后，constant-100首次真实Scroll可晚于脚本120ms；旧恒速断言把合法0→100当稳定抖动，green-5的32失败全部属于该场景。仅将稳定速度断言限定前后均Scrolling，其余速度/位移/距离断言保持，不恢复虚构呈现速度。

## 已核对的端到端合同

- `ResolveTouchRelease`一次输出Stop/Measured/ShortSupported/ResidualOnly/DistanceBounded。后两类用基础响应；最终seed在Dispatch重置booster后交唯一曲线，不第二次叠残余、不凭seed触发快甩增强。
- 最后Up不刷新group.lastRawQpc，不作为运动点。真实非零Scroll更新组候选；拓扑Down/Up不伪造运动或续租。成熟个人速度按真实held人数平均，静止/未知指只作0贡献；不能取最快个人。
- raw40ms断报、raw30–70ms时效、真实progress/组scroll70ms退出及frame曲线起点分开。Snapshot不改释放候选，空frame不续租；raw新鲜的约50ms平台不再机械半速。真实确认停止、反向、cancel、Stop/epoch/换域仍退役旧证据。
- `ApplyToCanvas`只在真实SetViewport成功后，用限位后`-viewportDelta`转DIP确认。固定group/leg/epoch/metric及当前frame校验防旧ACK、重复ACK、混组/混腿、同帧尚未确认位移借用；ClearAxis清相应预算。这里是已应用viewport的ACK，不是光学/Present ACK。
- key64旧残余沿新方向投影约-229mm/s，不能ResidualOnly；DistanceBounded只借此前确认约9mm新腿实际净位移。没有确认预算的同帧短拖仍可Stop。不可把11mm/.32ms当可信手速。
- 原生测试保留实际非零旧residual：ResidualOnly平台500的最终仍500；DistanceBounded另用同向旧500、raw-age35ms/legacy仍在续接窗、基础seed高于boost门且实际尾距<=已确认预算，能抓住二次boost，不能只靠旧res相反的key64来证明。
- Pen/PenTail/Indirect不走Touch新Decision/组预算/短资格；默认开关、笔尾压力/slop和PTP quarantine不改。RTS只记录实际返回GUID/metrics及终态原值；desired、pressure/tilt、producer和callback-arrival时钟保持。

## Findings (not fixed)

没有待产品修正的静态阻塞。以下是有意边界，不能由自动PASS消除：

- 新鲜重复坐标无法完美区分系统末端平台与用户真实短暂停手；小于70ms仍可能保留明显续滑。需用户带意图的真机轻放/短停/明确0.1秒停住复测，不能称算法已证明最优。
- DistanceBounded是已批准的距离降级，不是测得手速。真实ACK不足、混组或同帧未知时可保守0；不承诺所有已移动都必有惯性。
- TIMER_TICK/SERIAL_NUMBER当前未请求、未验证。未返回不能推断硬件不支持；无可信单位/回卷/映射证据不接新时钟或双producer。没有证据将平台归为系统API故障。
- 没有GUI/真实大屏/全部输入设备手感验收。Pen既有正常反馈与数值兼容，不证明Touch体验；历史旧7b曾有大滑距/错向，不能整套回滚。

## Verification

- **Lint/格式：PASS。** `git diff --ignore-submodules=all --check`通过；自修两个源/测试保留UTF-8 BOM和全CRLF。无独立通用linter配置需要新增。
- **TypeCheck/编译：PASS。** 主会话使用现有InkeysRepo.sln Debug|ARM64、原生ARM64 MSBuild，最终green-6完整Solution exit0、14.328s。11个既有warning来自ink_stroke_modeler转换及原DrawingController nodiscard行；本次自修未新增warning。不单构工程、不改工具链。
- **最后收口前完整套件：PASS。** headless-all-final exit0、444.156s、末行`PASS animation correctness`；源码前后和日志/产物SHA独立匹配。其核心为4eb6c0bc…，测试8f7b2b3f…。
- **最终专项：PASS。** headless-pan1.047s、control-fence1.984s、pan-feel5.375s、pen-width.234s、diagnostic-output1.953s，均exit0。逐份结果的sourceBefore/After、当前9源、日志SHA与EXE SHA均独立匹配；headless63cb071c…、产品b266c29b…。最终核心da3939dc…、测试b1270087…；Controller f7084e6f…只改probe条件/注释。与green-4全套仅这3文件的`check-followups.diff`和`check-feel-scroll-boundary.diff`不同，另外6源及接口字节保持。最终新增Touch路径、真实Controller Apply及呈现估计由五专项复验，复用无关模块完整套件结果；**最终字节未再跑整个WARP/设置长套件**。依据见[机器审查证据](touch-release-final-review-evidence-2026-11-14.json)。
- **真实/历史native：已独立核验。** 六版ARM64模块编译/链接和有边界数据的case运行均0，源码/日志/EXE哈希匹配。key64/48从4b起硬零；旧7b的64约956mm且26错向，不能整体回滚。纯核版本与真实Controller Apply probe各证明不同边界；缺失边界/ordinal数据不补造。`repair-final`历史导出为green-4快照，最后收口后不冒称与当前核心逐字一致。
- **新增缺口native反证：PASS复现。** `check-shadow-residual-probe.result.json`记录精确green-4模块两条CL命令和运行exit0；慢0.4/20ms错误恢复500，0delta/快2mm/门槛0.6mm对照保留500。随后修复由最终生产模块回归验证，探针不是修复后结果。

## 最终pan-feel差异分类

比较前轮TouchTerminal的`pan-feel-green-1.log`（SHA da0d359d…）与最终`pan-feel-delivery.log`（SHA 5e2a7587…），2240条场景/INFO记录加1条summary。最终PASS failures=0、rows=2112（summary不把INFO计入正式case）。不能称整份字节一致。

- 646条记录变化；max_dv_dip_s变化615条（478降/137升）。这是短资格、真实Scroll前呈现0、启动/拓扑focus统计的有意变化，不能都归舍入或声称稳态完全未变。恒速96条变化均仅maxdv，release完全一致。constant-100在首次Scroll从0到约100，现稳态断言只比较连续Scrolling帧；启动/脚本变速仍记录maxdv。accelerate-lift最大dv增量260.658、topology-2约246.826，不伴随几何跳变或释放坍塌。
- 32个jitter-rest的maxdv、实际delta、release现在全部0，排除了此前slop内短估计带来的虚构呈现速度。
- release变化48条，全部stagger-2/3/4/5-0多指组均值/float次序微差，最大0.027954DIP/s（约1.9e-5相对）；滑距45条最大0.007243DIP，理论余距48条最大0.006989DIP，release_ratio最大0.000018。没有非零release变0，也没有普通constant的release变化。
- max_delta_px、stop_ms、residual_px、locked_error_px、nonfinite、failures字段均未变化。最大dv是包含启动/变速/拓扑的全程统计，不能据此单独证明稳定滚动抖动；保持原稳定速度/跟手误差/距离断言并已通过。
- 单独green-4到最终仅253条maxdv变化，release/尾距等字段全部字节相同；最后两项产品收口没有改变这份标准矩阵的既有释放结果。新慢Scroll/同向残余Distance等缺口另由最终native核心回归证明。

可复查证据：忽略目录`Build/Logs/pan-touch-release-2026-11-14/check-pan-feel-final.py`、`check-pan-feel-final.json`、`check-pan-feel-followups.json`、`check-delivery-verification.json`。不能用Touch主矩阵代替Pen/PenTail/Indirect的独立黄金/源分支证据，也不将其当真人手感验收。
