# UI3 U04-B1 真Bar提交时刻实际代码独立复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。只读五源PATCH_READY、真实Bar四API/CompleteAttempt、Scheduler/TLS/collector、无窗口测试及保留baseline实际no-index diff；未改产品/工程/spec/账本，未运行编译/EXE/GUI。只写本报告。

## 当前最终结论

**B06门控修补与root最新组合复验GREEN，见末节当前五源身份与原始status；下节为修补前历史发现。** 仅关闭B1软件/default-sink合同，真实Bar/B2/B3/F与完整性能未因此通过。

## 初审结论（修补前历史）

**NEEDS_REVISION：新增B1细计量未在普通产品默认关闭。** root已接受为明确合同偏离，要求唯一writer修复；新冻结源码/完整构建/真实sink-only关闭测试及原套件复验后才能给B1 GREEN。当前软件stamp、分母、生命周期和分段没有发现第二个阻断。

| 修补前五源 | 核对SHA-256 |
| --- | --- |
| `RenderPipeline.cppm` | `3318D1498F4A68E877CDFC6107E591EA5C13E9163B09BD92E903801A3BF8F166` |
| `RenderPipeline.cpp` | `74BFC352B609A2127217C62CAB281103BB0426570950E9041B09D1C6F5CDE1F1` |
| `RenderPipeline.Diagnostics.h` | `F1941ECF267B8EA76805F1DDADD35218DC54B8FD32A848717CF263F35816067B` |
| `Bar.RenderLoop.cpp` | `12031DABB27D37FE2417EC2AB6C89F916639682B63C0DE966A91E011461E4A74` |
| `render_scheduler_tests.cpp` | `8B218717223847696B55C72E94CE32599512540DC2FDEB1070D39C9B903CFBFC` |

## Findings (fixed)

本 reviewer无产品修补。root先前已修本批新测试local registered的C4456，未改原注册变量/其它警告；本次baseline diff核旧R01–R14主体未变。默认off问题仍在下文，不能以类型/测试通过当已关闭。

## Findings (not fixed)

### P1-B01：产品默认sink使新clock/timer默认执行

实际 `IdtMain.cpp` 1764附近无条件创建并安装UI3 DiagnosticsSink；Scheduler约749按 `diagnosticActive || rawActive` 建FrameDiagnostics/TLS。故普通产品raw capture关闭时仍有非null diagnostics。新StampBarCommit和七职责timer仅检查该指针，仍在默认产品读取新steady_clock/采新段；HealthySummariesEnabled=false只关健康日志，不关闭这些采集。

R01默认Scheduler没有sink，只证明sink和raw都无时TLS为空，不能证明普通产品的默认off。原六阶段诊断本来启用，不要求把它们或异常sink关闭。

最小修订由root已冻结：Scheduler在每帧旁挂明确detailed/raw-enabled数值位或等价TLS accessor，Stamp和新七timer只有该位开启才读钟；旧六timer/原异常诊断保持。补真的sink-only callback case，给确定性clock和实际计时scope延迟，核新增stamp clock零读取/新段零，同时原诊断工作。raw-on仍单次stamp和分段，容量用新sizeof重验；最后五源重新冻结再完整编译/strict测试。修补前本报告不发GREEN。

### P2：尚无真实Bar/完整性能证据

这里新增Headless共用生产CompleteAttempt和Stamp，但API结果由fixture给定，未执行真实GetDC/ULW/ReleaseDC/EndDraw。Scheduler callback真假戳/epoch/idle/overflow测试不是实际Bar成功帧，也不确认动画目标或SVG内容已落到该帧。B2/B3/F、CPU/GPU/像素/三轮Release、HC/H2/Win7仍各有门。默认off修好也不能扩大结论。

## 其它实际路径核对

- **提交点**：Bar.CalculateDirtyAndDrawPresent 12630附近GetDC→ULW→ReleaseDC→EndDraw→HandleFrameEndDrawResult→CompleteAttempt；deferred已提前return且保留retry。Stamp12708位于完整决策后、presentCommitted/dirty/viewport/eraser/startup快照前。失败/null/duplicate入口短路；成功一次tick/attempt/epoch保留，software事务时刻不叫光学可见。
- **原proxy与新链**：RawRecordCallback仍按sample.endTicks推进旧commitTicks，新增trueBarCommitTicks/barSuccessSerial独立。Classify检查Bar client、完整成功API/flags、attempt一致、frame/context/stamp epoch一致、start<=stamp<=end与前真stamp顺序。Absent/Unverified/Valid/Invalid显式，缺失不补endTicks、非法原数值不clamp。另client不能计真Bar成功。
- **断链/分母**：RawMarkIdle及generation/epoch/非活动切断；同代普通失败重试保留上次真成功，未知/非法成功也切断其后链，避免静默桥接。全程seen/retained/dropped、true/unverified/invalid和各client计数在满容量后仍增长；successSerial只有效真戳增加，新capture run重置。旧validTime/proxy含义未改。
- **七新阶段**：baseline Bar diff仅新增旁挂timer/serial/stamp，未移动Tick/duration/dt/dirty/资源/锁/四API/pacing。Wake/Display/Submit/Advance/Lighting分别Stop一次；DirtyAndPrepare从Calculate入口到原Draw timer前，早退析构Stop；Resources仅围EnsureDeviceResources，属于准备父段。原六序号0–5保持，Count13/formatter 13 names有static_assert；新资源父子wall不应相加叫整帧/GPU成本。
- **固定预算/lifecycle**：raw数组仍在Configure锁外一次预分配，cap65536与64MiB按sizeof除法先验；默认32768有编译断言。collector只固定索引，热路无排序/格式化/写盘/扩容；Stop真join封口、Take一次规则保留。捕获停用数组默认空成立；细计时默认关闭需修P1-B01。
- **测试实际性**：ObserveBarCommit调用生产BarPresentDecision.CompleteAttempt，给实际四阶段结果载荷，不复制决策；B01真一次clock和duplicate，B02五失败/deferred/null无clock，B03十一矛盾 DTO和unknown/absent，B04实际Scheduler失败/epoch/idle/reregister与proxy分离，B05容量2保留7个callback分母/4真/1unknown/2invalid，sizeof账本和另client拒绝。有限等待失败都先Stop/Unregister join后判定，不留下悬空capture。旧R主体通过baseline diff确认未变。

## 现有验证（修补前）

root完整 `c-p1-ui3-b1-green-debug-arm64-build.status.txt` exit0，log0Error/7Warning、39.65s；`ui3-b1-green-debug-arm64-headless.status.txt`实际 **exit0 pid24596**，stderr空，216layouts failures0/PASS animation。B01–B05无新失败；对应旧RED只有B01/B03/B04/B05四项失败。当前通过这些实现合同，但没有sink-only默认off用例，不能覆盖P1-B01。以上原始文件均在TestResults/release-hardening，本reviewer只读未重跑。

## Verification

- Lint：未运行独立linter；只读baseline no-index差异，预期差异退出1不当lint失败/编译成功。
- TypeCheck/Build：修补前rootDebug|ARM64 PASS；待默认off新源复验，本reviewer不占构建槽。
- Tests：修补前strict no-window PASS、真实pid24596；P1缺口未被该测试覆盖。
- Spec/HF：修补后由root同步schema2/true stamp/默认off与范围；本报告当前不解除整个UI3/发布门。

## 2026-09-30 B06真正产品sink-only红证据

root补有限每帧 `detailedCaptureEnabled` 载荷和B06反例，尚未实现绿色门控。当前RED接口/source身份：module `56CCA2891F9DDA89698111EACC9987ED6D935740CE7063FA1373023367B767B8`；cpp `97C06D90059ADF7A46F175F7F15690DC94678E0739153B4D7C90131F8FAC20CA`；测试 `84DCDD858B3E537D0C09ACE773CAE52B5CD5D41FA76E8CEB318A052E20212CAA`。上表五源是最初PATCH_READY历史，不能当未来门控修补身份。

B06实际ConfigureRawCapture(0)+SetDiagnosticsSink+真实Scheduler callback，取得旧TLS，调用生产CompleteAttempt和带确定性clock的Stamp；遍历七新FrameStage逐段Sleep2ms，保留旧Draw父timer。Stop/join后核enabled=false、clock读0、stamp/新段0而旧Draw>0、无raw report、TLS清空。不是无sink的R01，也不是仅检查字段默认0的镜像测试。

已读 `ui3-b06-red-debug-arm64-build.status.txt` exit0；`ui3-b06-red-debug-arm64-headless.status.txt` **exit1 pid32752**。stderr准确只有B06 failed和FAILED count1，stdout216layouts failures0；原B/R没有新增失败。该红证据直接验证P1-B01，当前B1继续 **NEEDS_REVISION**，root已决定修复，不接受新增默认成本。需唯一writer完成门控、重新冻结五源、完整构建和相关strict绿后增量复审；当前不发B1 GREEN。本reviewer未执行构建/测试。

## 2026-09-30 B06最终门控增量与复验

**B1增量实际代码GREEN，P1-B01在默认异常sink场景关闭。** 当前五源已逐项核实际guard/调用点，与implementation B06 GREEN表匹配：

| 文件 | 当前SHA-256 |
| --- | --- |
| `RenderPipeline.cppm` | `56CCA2891F9DDA89698111EACC9987ED6D935740CE7063FA1373023367B767B8` |
| `RenderPipeline.cpp` | `7FEF230234CFE8915A5D1CA7B84AF91903BFABA5A98C09257F94663119474B83` |
| `RenderPipeline.Diagnostics.h` | `F1941ECF267B8EA76805F1DDADD35218DC54B8FD32A848717CF263F35816067B` |
| `Bar.RenderLoop.cpp` | `123E495EDDE81819716AE199B0A21B54FE7805105B259A9A36CD9ECDA95027F1` |
| `render_scheduler_tests.cpp` | `84DCDD858B3E537D0C09ACE773CAE52B5CD5D41FA76E8CEB318A052E20212CAA` |

Scheduler在实际sample.emplace之后只以当前rawActive填写detailedCaptureEnabled；diagnosticActive不授权。Stamp287附近首先检查detail位，再检查committed/首次戳，即使clock参数非null也不读。FrameStageTimer300附近只将七个新stage的disabled指针归null，进入既有无钟Stop路径；原0–5阶段保持。Bar WakeAndSnapshot986附近新attempt字段同门，原状态attempt计数/Tick/退避未变。未关闭Main的异常sink，未改schema/classifier/proxy/64MiB/业务调用顺序，未扩大B2/B3/F。

B06仍是真Scheduler+sink-only+cap0：旧TLS存在、旧Draw在确定2ms子段等待后>0，新7段全0、stamp测试clock读取0/数值0、no report/停止后TLS null。原直接B01–03显式detail=true，真实raw B04–05由Scheduler授权；测试clock本身不写位。最小修补改变的是三处门控，不用修改测试期望掩盖偏离。

已读取 `c-p2-b06-green-debug-arm64-build.status.txt` exit0及log0Error/136Warning、1:11.00；`c-p2-b06-green-debug-arm64-headless.status.txt` **exit0 pid32328**、stderr空、216layouts failures0/PASS animation（B06和旧B/R无失败）。同冻结组合parked status exit0/pid15540，旧五及Metrics/M16 PASS；PptCOM status exit0/pid12112。执行者均root，reviewer未重建/运行。B06红exit1/pid32752保留，不能替换成无sink测试。

结论只关闭默认product sink的新细计时开销与B1软件合同门。此strict no-window没有真实Bar ULW/动画目标/SVG完成/性能三轮；未把组合编译通过升级C-P2真实故障/UInk验收。本次未运行Git或修改任何源/工程/spec/账本，只更新此报告。
