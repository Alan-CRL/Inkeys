# 独立实施审查：白板惯性尺寸与设备适配

日期：2026-11-13；角色：trellis-check。审查批准后的prd/design/implement及check.jsonl，范围为本任务8个产品/测试文件。构建期间只读；收到主会话明确允许修复后修改，随后重新冻结。未自行启动MSBuild、应用主窗、GUI、commit/push或归档。

## Findings (fixed)

### P2：身份投影与对应样本发布不在同一事务

- 文件：`Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`（ReadPenCursorSample / PublishPenCursorSampleImpl / ClearPenCursorSampleForContact），对应`.cppm`；实际视觉消费者在`Draw3.DrawingController.cpp`。
- 证据：旧实现先在penFeedbackMutex_内写RTS/Pointer通道、token、suppression，再解锁后Publish sample。两路发布可交错为“RTS样本+Pointer身份”或相反；token-aware Clear也先校验再解锁调用无条件Clear，存在TOCTOU。停笔后错通道可能留到下一输入。单独getter还可能把不同发布截面的样本和抑制值拼接。
- 修复：身份与来源缓存、plain mailbox Publish/Clear同一短锁提交；窗口/触觉/诊断/wake都留锁外。Window边界ReadPenCursorSample新增可选suppressed输出，让实际光标及橡皮视觉消费者取同一截面；通用latest mailbox布局与语义未变。原Clear状态边沿的重绘/刷新保留。
- 回归：生产`RunCanvasPanScaleProductionProbe`新增两个实际publisher并发（RTS token与Pointer各2000次）并配对读取样本标记/抑制位；同一事务必须始终满足两通道各自的抑制结果。不是复制发布算法的helper测试。

### P2：Pending毫米候选的物理资格变化未撤销

- 文件：`Draw3.DrawingController.cpp` 的 `CanvasPanInput::ObserveDisplay`。
- 证据：原检查只在dynamics_.Active且当前单位mm时重解；Register后尚未Step的毫米候选仍Pending，若只有physicalAvailable/cm密度变化而DPI/bounds不变，会把旧候选标成Trusted准入。此项由主会话复核后交check最小修复。
- 修复：以每个Pending毫米候选真实Down source/class在next显示事实下重解，只有毫米资格/逐轴密度真正变化才走原Stop/吞旧指到终态。模式、无害revision/橡皮override及DIP→可信升级不因此重解释旧批次。
- 回归：实际Coordinator→静态Selection路由Register→ObserveDisplay→Step，五个变化分支（失效、单轴密度、模式、无害revision/override、DIP增益），验证前两者封口，其余保持锁存，所有终态仍可回收。

### 测试修正：真实Pen身份夹具未对齐

- 文件：`Draw3.DrawingController.cpp` 的尺度生产probe source工厂。
- 证据：IntegratedPen source原为context/cursor=1/1，PenFeedbackContact却为kPenTablet/3。严格ForContact校验会拒绝发布，令“合法笔纠正”可能在空presence上通过，overlap用例也不能验证预期。
- 修复：夹具IntegratedPen真实source与原始tablet/cursor对齐，保留运行时严格身份校验。新增并发回归也使用完全一致真实source/metadata。

### P2：无效帧被当作准入失败并破坏已有手势

- 文件：`Draw3.CanvasNavigation.cpp` 的Step、`Draw3.DrawingController.cpp` 的CanvasPanInput::Step/Stop/Reset。
- 证据：首轮新产物pan-feel在qpc-zero-repeat-backwards的32种组合出现128条旧断言失败；内核对非递增帧置admissionFailed，前端因而Stop旧批次。仅清flag仍会在Drain之后丢掉排队Down/Up并可能提前回收，不能作为修复。
- 修复：内核无效/非递增帧恢复Snapshot no-op；前端维护有效帧时钟，在Drain之前拒绝不推进帧，以当前状态快照返回零位移，保留真实历史到下一有效帧；Stop/Reset同步重置时钟。
- 回归：活跃且已移动Touch，队列里Touch Up+新Pen Down/Up，随后重复/逆/零帧，要求不admissionFailed、不变位移、不丢历史、不坏批次；下一有效帧一次准入并闭合所有终态。原32组合的重复/逆帧断言保留，未改弱。

### 测试更新：Stop后死触点不再冒充有效owner

- 文件：`Draw3.DrawingController.cpp` 旧control-fence断言。
- 首轮专项两处旧断言要求Suppressed死触点OwnerClass仍Touch，与已批准“只跟终态、不算有效owner/held”的新语义冲突。
- 改为OwnerClass=None且非TouchPanHeld/Active；HasContacts、BatchDead、Move无位移及Up后回收/重开批次的检查完整保留。

## 审查范围与结论

- 五个profile与proposal-v1阈值、tau/c、w/G公式一致；fallback gain仅选快甩参数；在boost之后执行profile模长cap。数学界只对相同有效初速的一条自由曲线，不承诺连续手势总距或墙钟时间。
- mm/DIP只在批次内部；每事件先逆变换再累加，focus/span逐指恢复，held速度不从canonical Snapshot二次逆变换；raw/selected/boosted和tail scalar的公共单位明确，release有独立来源/上下文。
- Pending不计有效owner/held，唯一有序Down裁决；同QPC Down先于Up保留，拒收身份不混focus，真实拒Pen屏障保留直到终态。容量预检覆盖本批Down数而非16；旧纯DIP默认入口保持。
- 异常终态包含同帧排在Down前的坏时间情形，按身份取消闭合且不伪造速度；源generation首Down隔离旧残余。现有尾段/Up年龄/反向/水位/overflow/Stop规则继续覆盖。
- RTS全进程单调token随binding闭合，精确原样DownQPC查询，不以lastKnown来源或接近时间证明跨API同笔；Pointer-local抑制不被RTS旧ack全局清掉。可选元数据失败仅降低反馈纠正能力，真实core输入保持。
- 热路径使用已缓存显示/来源及预留缓冲，未新增逐Move分配、硬件枚举或普通位置control wake；共享显示/橡皮/Config/DM隔离/工程与第三方没有额外改动。
- 修改后静态检查未发现其它确定的阻塞缺陷。此结论仍依赖主会话对冻结源码和新产物的运行结果，不用旧EXE作为PASS证据。

## Findings (not fixed)

没有留待产品决策的已证实运动/准入缺陷。

P3已在最终收尾关闭：ResolveCanvasPanMotion新增source-conflict/source-not-direct-touch诊断标签，十个独立失效夹具均断言精确reason；两处固定DIP旧注释改为本批运动域。相对已全量通过的R3快照只有这两文件的诊断字符串选择、fixture断言及注释变化；resolver去掉reason选择后的整个函数文本一致，其它六源文件字节一致。差分证据：`Build/Logs/pan-scale-device-2026-11-13/closeout-vs-r3.diff`。没有留下已证实源码缺陷。

以下是明确保留的能力/验证边界：Pointer cursor/range不能证明同一次跨API Down，故无法关联的Pointer-local latch不由RTS权威结果清除；可信EDID仍受现有基础尺寸/图像区域能力限制；Touchpad继续隔离。真实Surface/大屏/笔舒适性、硬件抖动与OS输入时序未由本审查运行验证，不宣称参数最优。

## Verification

- 静态lint/格式：`git diff --ignore-submodules=all --check`通过；修改保留原BOM与CRLF（DrawingController原本无BOM）。
- TypeCheck/编译：主会话协调完整`InkeysRepo.sln Debug|ARM64`、ARM64原生MSBuild、`/m:1 /nr:false`。`build-2`在独立OutDir成功，35.172s，0 errors，23个现有警告，冻结源码未变；证据在`Build/Logs/pan-scale-device-2026-11-13/build-2.result.json`和`.log`。
- 环境：`build-1`主程序编译后旧默认输出的增量链接器内部崩溃LNK1000，未据此修改项目/SDK/源码；不能把它说成C++源码错误。
- 首轮新产物专项：headless-pan、diagnostic-output、config-boundary通过；control-fence仅旧死触点owner断言失败，pan-feel仅重复/逆/零帧旧断言失败，已按上文修复。修复后的build-3完整solution PASS（24.875s，源码冻结一致）；五项新产物专项headless-pan/control-fence/pan-feel/diagnostic-output/config-boundary全PASS，pan-feel整份708090字节与开工及上次已验证输出同SHA256，实际2240场景。setting-general-r3 PASS（18.313s）、完整headless-all-r3 PASS（325.265s），合计七项无窗口全部通过，均sourceUnchangedDuringRun=true；本审查未自行运行产品或GUI。

## 规范同步复核

已只读核对native-desktop/canvas-pan.md、input-and-ink.md及native/runtime-and-rendering.md本任务新增合同，参数、单位/有序准入、Pending事实失效、时钟延后消费、RTS身份旁路/配对发布及Pointer保守边界与当前代码一致。未发现新增规范冲突。

## 最终收尾冻结

全量R3通过后，仅按主会话明确授权关闭上述P3诊断与注释。已检查八源文件相对R3快照差分、原BOM/CRLF及git diff --check；无参数、准入、估速或运动逻辑变化。主会话将对最终收尾产物执行build-4及相关专项，不无理由重跑同运动逻辑的325秒全量。最终build-4/专项结果由主会话验证记录补入。

## 主会话最终验证补充

最终收尾build-4完整solution PASS（23.687s），五项最终产物专项全PASS；主会话备份本次失败生成的默认ILK后按原项目配置build-5-default PASS（15.875s，0errors/0warnings），默认目录headless-pan/control-fence/pan-feel三专项均PASS。默认pan-feel仍为原2240场景/708090字节，同基线SHA256。完整R3 Headless为325.265s PASS，与最终源码差异仅前述reason、对应fixture和注释，无运动逻辑变化；未做GUI或硬件体验。全部命令及冻结源码/产物哈希见validation.md和research/implementation-validation-results.json。
