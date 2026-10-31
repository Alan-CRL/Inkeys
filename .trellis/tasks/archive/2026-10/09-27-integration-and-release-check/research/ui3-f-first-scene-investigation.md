# UI3 F 首轮 MainFold capture-off 失败只读调查

Active task：`.trellis/tasks/09-27-integration-and-release-check`。日期：2026-10-01。Writer：`ui3_fixture_source_impl2`。唯一新增本文件；其它源码、旧报告、Root文档、产物均只读，未 Build/run/Git/GUI/Computer Use 或递归。

## 当前结论

**已确认：第一 Main Up 被真实业务接受，但在固定下一 due 前未得到 CompletedRevision；随后正常 Close/alljoin/封口，结果 Failed。尚不能由现有产物确定唯一 layout/SVG 原因。** 本轮不是线程崩溃、输入未投递或强杀后的假成功。

## 2026-10-02 Root 针对性复验（新增证据）

最新完整 Debug ARM64 候选 `Inkeys.exe` SHA `4E31A8C9BAF87E26C5645BC1F7911F07E6603385767D973E7D82B4655AF56DF0`，命令为 `--ui3-presentation-benchmark --scene main-fold --round 1 --capture on --capacity 4096`，结果仍为 exit90。第一目标 2/2 verified；第二目标 `settled=1,pending=0`、9/10 verified，唯一未验证 tag `131084 (0x2000C)`，reason `Overwrite`；后215目标未开始。

新增 capture-only `svg-bindings.csv` 使用 **绑定时**快照，不再从运行结束时可能变化的注册表重建映射。它确认 `0x2000C` 对象是 `source=more` 的 `More` SVG；中间 `0x2000B` 空洞来自绑定时重复扩展对象消耗 ordinal，不是目标 tag 未绑定。该目标 expected bounds `[2940,890,3012,962]`，后续已知写入 bounds `[2904,959,3048,1013]`，垂直相交 3px；因此严格 Overwrite 仍是正确的拒证，当前没有足够证据实施资格豁免。

证据目录：`TestResults/release-hardening/ui3-finite-8fb2b653c4e34e4b84b0789fb27b9712/r1/s1/{finite-targets.csv,svg-bindings.csv,button-layout.csv,summary.json}`。有界 button-layout 显示 More 的当前按钮/图标矩形，但不能确定后续 overwrite writer 的像素语义，故不授予豁免。本增量不改变完成定义、不增加绘制帧、不关闭光影或降低质量；完整216目标、第二 scene、Release 三轮和 HC/H2 对照仍未验证。

需纠正派发中的一个枚举解释：冻结 `Bar.Presentation.Source.h:23` 的 `source_failure=13` 是 **Deadline**，Target 是14。`WaitGoal` 到固定下一 due 仍无完成后调用 Deadline；`Fail` 用首错 CAS，之后 `WriteAllOutputs` 的 Target 检查不会覆盖13。这是接受后未完成的期限结局，不是业务拒绝。

最后一行 `surface=0/requiredSvg=0/proofMask=0` **不能证明从未绘制**。`BeginFrame` 清空 candidate；帧在 `SettleCandidate` 之前早退时，RAII Abort把本帧空 surface/resources 写回同一个未完成行。Completed行受保护，未完成行没有“最后有意义candidate”保护。现有 CSV保留的是最后 attempt36 的未完成摘要。

## 冻结证据与来源

Root提供已在 `SAFETY_CLEAR_FIRST_F_CAPTURE_OFF` 下实际运行：

```text
--ui3-presentation-benchmark --scene main-fold --round 1 --capture off --capacity 0
parent 30540 / child 13540；ack1 / natural exit90 / stage7 / result2
```

本 writer未重新运行。只读产物根：

`TestResults/release-hardening/ui3-finite-12d6edaca4423242977c5b080bdd18da/r1/s1`

实际copied PE SHA256：`9AA0A04D6402F69422A7B348CD44238AE912ABBCDDE97665DC5A28D4277B9D31`，属于ECDB版本。当前Test.cpp为未新build的 `108E2B29…3324D20`；该增量仅 clocks-on raw integrity门，off分支相同，不能借本次off结果称108已构建/运行。

| 产物 | SHA256 |
| --- | --- |
| summary.json | D65554A66F94D9BBCBAE8C6DA1B30A8DD6E71B835481DA6610EC8075550E4246 |
| meta.json | 92E293C6F581A110360F517D7E08E9D45D195595A9542E86B3DE4684B3F982F1 |
| finite-targets.csv | 0756568C7C07E93BDA52699E03D43FD29E60FFD1C51393C32782D33C8A706B03 |
| source-events.csv | 053638098CB4F95559ABA1B6A68D7290E25EC03B35291E5785A8F8230580D138 |
| svg-cold.json | 98EA604D73A0635B74775AFBFBFBF260735C02E2AE8E793EDB4581330E547D2C |
| svg-warm.json | CD6DB234F09349F6F921F6437684FDE34E1DC2320ECCFFAA29E9AC68A5074AA8 |

已读两个JSON、两SVG JSON完整内容；通过Import-Csv读取全部216 finite行/433 Source行并核完整状态分组。根仅六个离线文件，无raw、BGRA或hash；这与off且未进入最终功能阶段一致，不拿文件缺失冒称C读回失败。

### 实际输入、业务和环境

- Source行0 Down/行1 Up：同step1，seq1/2，实际flags31/47（Posted/Received/Enqueued/Consumed以及Down/Commit许可）；2/2/2、无Failed行。其余430行未触碰，末行432为SkippedNoContact256；没有发假Cancel或下一步。
- 两行绑定同实际 commit15/attempt15/epoch1/surface1/mapping30，屏幕点825,1743，touch-screen marker-32766，零modifier。第一消息513/第二514，Up沿同Down坐标。至少初始bootstrap15个成功事务的真实anchor已取得。
- Publication seen/retained/accepted均1，reject/ambiguous/nochange/invalid/dropped均0。唯一step1 accepted_status1/run5630512759620889040/source2/revision1/publication2；最终terminal7 ResourceUnverified。
- Initial flags64→accepted/final65：真实展开→折叠Main目标。Pen1/penMode0/color4351/widthbits1077936128/toolRevision1/dpi192/displaySerial2/configZoombits4607182418800017408/validMask255保持；没有工具、宽色、DPI/display/aux污染的正证据。
- profile true/true Edge/Dynamic、animation true/speed1、zh-CN、WARP backend0/FL45312；clock period实际1/1000000000。off所有性能时刻null/clock_reads0，不能从0推0ms。
- contact清；Source/Interact/Window/Scheduler join及Display drain为true，stageSealed7；状态与真实清理链一致。没有C checkpoint，MeasuredEnd空，216全部未验证。
- fixed564234B、raw0、destination16777216B+readable同量、total34118666B；本次未触发capacity/功能预算拒绝。

### 实际资源数据的边界

`svg-cold`为owned_initialization：parse43，无parse failure，clock_reads0。`svg-warm`准确命名render_all：lookup hit242/miss71、create attempt/success71/failure0、parse77/failure0、raster71/failure0、upload71/failure0、draw313/rejected875、invalidation61、ready10/logical bytes306624/unknownReady0/invalid0。

这证明至少此run执行过真实parse/raster/upload/DrawBitmap；统计混合bootstrap和首goal，**不能把313次都归首goal，也不能把875当875次API失败**。`BarUIRendering::Svg` 对enable=false、零尺寸、零透明度等正常早退也调用ObserveRejected；FinishDrawing只将expectedVisible且真实failure标记的槽算失败。cacheReady/known lookup也不是完整目标像素证明。

## 真实完成调用链与最后空行成因

| 实际位置 | 对本轮的含义 |
| --- | --- |
| Test.cpp:242 WaitGoal / :298 limit | pair0期限为表下一Down的1000ms；Up+20ms后只复核exact accepted tuple与CompletedRevision，没有改节拍/补发 |
| Test.cpp:112 Fail | 首错CAS；Deadline13后续不会被收尾Target14覆盖 |
| Probe.cpp:913 BeginFrame | 每帧candidate/resource/roles重置；source单goal的observer slot按同run/revision复用 |
| RenderLoop.cpp:13350–13374 | 原Wake前SnapshotAccepted/BeginFrame/BeginSvg；FiniteFrameExit在所有早退调用AbortFrame(ResourceUnverified) |
| RenderLoop.cpp:13623 MarkConsumed | 真Submit后读同一tool/display/DPI/signature，核真实业务目标；接受记录有效不自动说明每帧consumed有效 |
| RenderLoop.cpp:5765–5795 / :6100–6115 | 真实role推进/资源Require；MainFold需要六个layout roles，Feedback/Lighting不作为layout阻塞角色 |
| RenderLoop.cpp:8172 ShouldPresent / :13276 Idle | 无原呈现需求时直接Idle，未执行Settle/drawing/四API；RAII Abort仍可覆盖该未完成目标行 |
| RenderLoop.cpp:10122–10141 | 仅真正Draw准备点锁实际surface/viewport/DPI/alpha并SettleCandidate/BeginBackingWrite |
| RenderLoop.cpp:12837 | 绘制后FinishDrawing→FinalizeResources，仅补同一次candidate资源，不重新Settle |
| RenderLoop.cpp:13228 / Probe.cpp:1091–1157 | 全GetDC/ULW/ReleaseDC/EndDraw成功才Complete(true)；same candidate/current goal/settled/identity/SVG全证且NoteCompletedGoal真成功才Completed |
| Probe.cpp:1070–1089 / :1160 | StoreOutcome对未Completed的同一行覆盖candidate数值；Abort之前candidate可能还没有surface/resources |
| Probe.cpp:1206 | Seal只将Pending转Stopped，不替换ResourceUnverified为真完成 |

observer_resource_unverified_frames=6只是StoreOutcome(ResourceUnverified)次数，既可来自committed但资源未证，也可来自Abort。末attempt36、surface0、epoch1、failureFlags0、pendingRoles0，符合“BeginFrame后尚无有效Draw candidate就早退”的写法；具体是无需求Idle、准备前其它早退或布局相关路径，现包没有分帧判别数据。

## 排除、候选与未确认项

| 分类 | 判断 | 证据/限制 |
| --- | --- | --- |
| 已确认排除 | 输入未到队列、没有真Up/Main业务接受、整run注册失败 | 2/2/2、Down/Commit flags、accepted1、flags64→65、bootstrap实际15提交 |
| 已确认排除 | 真绘制完全缺席、SVG API创建全部失败 | render_all actual raster/upload/draw非零、create/parse/raster/upload failure均0；未分goal |
| 已确认排除 | 第一gesture被Cancel、下一请求supersede、More/Settings误动作 | Cancel skipped无接触、下一430行0、ambiguous/reject/nochange0；仅一goal |
| 可排除的具体代码假设 | 普通fixed按钮被复制，导致全部SVG未绑定 | Button.cpp:1138 AppendFixedButtons直接使用registration的同一shared对象，Load:1260..1299替换同一列表；只有Divider clone，Scene.cpp:467不绘制Divider SVG。不能据此排除其它单tag Unbound/Semantic |
| 强候选，未动态确认 | Unknown写的全槽overwritten/Hidden lineage阻断 | Probe.cpp:281 UnknownWrite给所有required槽overwritten+hiddenLineageUnknown。Scene.cpp:456–486是Shape→SVG→Word，Word真实DrawTextW位于Rendering.cpp:2987会Unknown；主栏后Main Superellipse、logo1、logoInk等也有真实顺序。Hidden required没有Draw来清overwritten，FinishDrawing先判Overwrite再判Hidden；折叠将旧visible按钮变Hidden时这类保守规则可长期拒证。现包缺具体tag/reason，不能定唯一根因 |
| 强候选，未动态确认 | 动画终点被标pending，而下一自然帧无原需求、Settle前Idle | RenderLoop.cpp:5778明确contentAdvanced连finished=true都保守pending，等待自然下一帧；PrepareLightingAndDemand只按原needRendering/lighting/retry等加需求，有限probe不新增请求。末行空candidate与这一后续Idle现象相容，但没有逐帧roles/settled/shouldPresent证明 |
| 其它未排除 | 合法partial clip/full-visible coverage、hidden旧bounds、semantic/value/color/DPI/epoch/尺寸、required0、sameCandidate/identity/current mismatch、实际四API失败/defer | FinishDrawing与Complete的真实拒证分支均存在；最后空摘要不足区分。无本run逐帧API/资源tuple，不凭计数或Bootstrap成功排除 |

Unknown机制本身是已冻结保守合同，**没有证据批准直接去掉UnknownWrite、把Hidden视作不需要证明、接受retained，或让Completed绕SVG**。Word/Shape与SVG有真实不相交时，可将“精确写域不会覆盖”作为未来最小资格修补候选，须先拿本goal真实失败原因并用actual D2D/同序反例证明；不能凭视觉猜不相交。

## 最小下一诊断与复现顺序（待Root，未执行）

1. 先新完整Solution编入108 raw gate与Root其它已冻结源，完成core/SourceSpecific independent safety；本次许可只capture-off，不能自动升级为on许可。Root获对应运行门后同一MainFold、round1、true/true、fresh child运行on，第一轮capacity4096即可保住约首goal短失败的全部raw；仍先按真实stride+4MiB+32MiB+统计预算核合法，不能靠建议数值推分配成功。

```text
Inkeys.exe --ui3-presentation-benchmark --scene main-fold --round 1 --capture on --capacity 4096
```

2. 重点读完整raw前缀：accepted前后的attempt/epoch/generation/动画推进/四API尝试与成功、resource/GetDC/ULW/ReleaseDC/EndDraw结果、Retry/deferred/Idle、target/capacity/source/viewport及commit stamp。核最后attempt36类callback是否无呈现、是否有成功事务但有限目标未完成；区别“缺最终自然帧”和“最后成功帧资源未证”。on没有B3 per-tag原因字段，不能承诺只靠raw就定唯一SVG原因；保留第一次off分母与新on全部失败。

3. 若raw仍不能分layout/resource，最小新增诊断方案是**auth fixture opt-in、render owner内**保留每goal“最后有意义candidate”的纯数值摘要：only在实际Settle/Finalize/Complete有合法surface与frame tuple时复制；另记最后早退stage而不覆盖meaningful摘要。字段只需existing run/step/source/revision/pub/epoch/surface/attempt/viewport/dpi、consumed/settled、pending/mismatch/lifecycle、resource required/verified/failed/unverified/first tag/reason，以及Complete的sameCandidate/currentGoal/identity/layoutCurrent/committed判断位。无新增时钟、格式化、I/O、主动GPU操作；默认无observer/opt-in不记录。

   只复制owner自己已锁存的candidate/判别位，不读活Source或其它线程plain对象；固定每goal上限512、owner前分配并计入actual fixed≤4MiB与共同64MiB。全部owner/control真join后一次输出。可先用现observer计数frames/layoutSettled/commits/unverified的封口输出补信息，不将它们冒具体失败原因。该方案是提案，本writer未写字段/代码/接口，不创建泛化遥测或新registry。

4. 具体修补由新证据选择：若终点pending且最后成功帧本来已满足全部角色，应在真实Advance producer精确区分“本帧发生改变”与“advance后仍未settled”，用同生产动画实际终点/反向/中点测试验证观察资格；不得强迫额外帧。若确定某tag Overwrite/Hidden lineage来自可证明不相交的非SVG写，针对实际写域提供有限资格，保留unknown/交叠/失败/deferred反例；不得改Draw顺序、关光影或放宽Completed。若是API/identity/semantic/coverage问题，先精确取该tuple/原因，不随机更改资源或节拍。

不存在本轮可授权的“延长1000ms等待/补Request/fullDirty/Flush/多画/重置baseline”修补。窗口/渲染正常收束只证明失败安全结束，不证明性能目标已通过。两scene216目标、完整BGRA/on-off、三Release轮及其余family/Win7仍未验证。

## 只读源码身份与交付限制

当前 Source.h `75BBFB73…05A4A0`；Test.cpp `108E2B29…3324D20`；Probe.cpp `0538C544…5B45A8`；RenderLoop.cpp `520190BD…382EC4`；UI.cpp `10B29A34…7CF27`；Rendering.cpp `8A69FCD6…ABE2C`。这些读到的源身份与copied PE区别保存，不称HF。

检查仅产物完整读取/全部CSV状态分组/SHA256及生产调用链静态核对。Root实际首轮结果作为提供的运行证据引用，本writer未运行任何EXE、测试、构建、GUI/Git或修改源码。只新增本报告；本结论明确区分已确认超时链、最后摘要信息丢失、强候选与待诊断原因。

## 2026-10-01 capture-on 最后有效候选诊断增量（PATCH_READY，未构建/运行）

Root提供新事实：完整 InkeysRepo.sln Debug|ARM64 9cce16b对应产物 SHA256 32D9E52DF6486892ACB3308BB5CAF8364913E2370B526EDBEE16B683559E798C 构建 exit0；fresh严格Headless14648/0、offscreen12264/0。随后首 MainFold round1/capture-on/capacity4096，parent13224/child15636，ack1/Sealed/自然exit90。只读raw根 TestResults/release-hardening/ui3-finite-ec3fde93df1ace4f8435be9d6302bf7f/r1/s1 保留callback35/35、零drop/invalid，整run32实际Bar提交、无API失败，最后callback35 Idle未present；首goal accepted1/Completed0，后215未开始。以上为Root运行证据，本writer没有重新运行；raw仍缺同goal完整candidate，未据此修改Unknown/Hidden/pending。

本增量仅改 Bar.PresentationProbe.h/.cpp、Bar.Presentation.Test.cpp、Bar.RenderLoop.cpp。验真FixtureState仅capture-on在原4MiB/共同64MiB预算检查中加入实际 512 * sizeof(Ui3FiniteGoalDiagnostic)，owner前一次分配固定prefix并显式绑定；capture-off不分配/不启用此span。render owner在实际Settle/Finalize/Complete后仅复制已有合法epoch/surface/attempt/尺寸candidate、consumed/seen/pending/mismatch/lifecycle、原resource汇总/首tag原因及Complete局部判别位。早退只记last stage/attempt/真实FrameResult/Abort状态，不能覆盖meaningful。无新clock、GPU、I/O、wake、资源或通用registry；StoreOutcome、NoteCompletedGoal、SVG判据、原返回值与绘制/四API次序均保留。

所有真正join后，现finite-targets.csv仅on追加46个诊断字段；meta追加启用及payload bytes，summary追加已有observer frames/commits/layoutSettled/completed/unverified/superseded/invalid封口计数。Complete mask bit0..8依次为called/committed/predicatesEvaluated/sameCandidate/identityValid/goalCurrent/anchorsValid/layoutCurrent/validTiming；实际结果另在meaningful_status。stage 1..10依次为Begin/Wake/Display/Submit/Advance/Lighting/Dirty/Settle/Finalize/Complete；last_frame_result按现真实FrameResult数字（0 Idle、1 Continue、2 Retry、3 DeviceLost、4 Stop），valid字段区分未记录。

| 源 | 本增量SHA256 |
| --- | --- |
| Bar.PresentationProbe.h | 6A59AA3D238445F54497D4FE9881C0E4B5D7C09A512B52C6038EAC228CC032BB |
| Bar.PresentationProbe.cpp | 90B42FAAD131F88C9A6E716BD1440AC956A310FEA0B7E86825F537252F11C28D |
| Bar.Presentation.Test.cpp | 422AEBDD2FF085DEE39CDCC36C5C7782D3B7446C894271834ADBE3952C7B5DDB |
| Bar.RenderLoop.cpp | 4F642B068F6909E98C08B0BA097818E0B2202C89BBA8C0F5929E1C206DA0B8E7 |

静态检查已确认四源原UTF-8无BOM/CRLF/bareLF0、实际diff仅上述诊断接缝、46列/46分隔符且列名唯一、启用/分配/预算/输出均沿原owner寿命。没有build/run/Git/GUI/Computer Use、递归agent、新测试或新报告树。此刻停写，交Root完整构建/core回归及对应安全门后同一首场景取真实candidate与末Idle因果；不沿用旧PE的测试PASS，不预填GREEN。完整216/两scene/全BGRA/on-off/Release三轮/Win7仍未验证。

## 2026-10-01 首goal实际Overwrite归因与精确资格补丁（PATCH_READY，未新构建/运行）

Root新完整Debug|ARM64产物 A34A782F77F4C065D2B135FCD06DF2FCA9E25843FF806FDBC84FFAC4F21E4F8D，build0/85.83s；fresh Headless24160/0、offscreen21372/0、START01 CLI192/0。新实际main-fold on4096 parent11736/child11664自然90，raw根 TestResults/release-hardening/ui3-finite-e8c4d99a04f2a142a080715949e19367/r1/s1。本writer直接读首finite行及raw末五callback：lastMeaningful attempt35/epoch1/surface7/run5078551040306086798/source2/rev1/pub2/rootbatch2，198x198/dpi192，consumed/settled/stable均1，pending/mismatch/lifecycle0、seen63，Complete mask511；required10/verified1/failed0/unverified9，首tag65536/reason9 Overwrite。末36是stage7/Idle/consumed1/pending0/Abort。实际backing3790x1892，末present source1796,847/198x198；不能把该Clear称full backing。Root提供observer36frames/33commits/layoutSettled3/completed0；原成功API/零drop/invalid事实保留。

实际65536是logo1。RenderLoop按Superellipse→logo1→logoInk绘制，Pen下Frame94着色层pct=1、同中心同尺寸；两内嵌资源有实际路径重叠。原整体dest矩形相交一律Overwrite阻断了既定主logo设计层叠。Root因此明确授权精确expected composition：仅真实producer声明的本帧logo1→logoInk、固定两tag/真实对象、已提交底层/未提交上层、相同dest/transform以及两侧既有资源/quality/epoch/surface/DPI/opacity/full coverage全部通过。声明单次消费，不清此前Unknown/overwritten；无声明/倒序/错误对象/旧或错误bitmap/transform/partial/未知重放继续拒证，原B337不降。

本补丁仅Probe.h/.cpp、Rendering.cpp、RenderLoop.cpp、既有EraserAttribute.Test.cpp。共享原visible判据供两层检查；normal Draw/四API/strict canonical保持。Rendering仅已查明不读取旧SVG的Shape/Superellipse/CLIP Word与自身A8几何光mask，在实际域/变换/描边边界/AA及clip下记录纯写，visible相交仍拒，invalid域回原Unknown。oldBounds改为实际mapped且clip裁后的完整写域，带epoch/surface；Hidden的outside资格仅同身份、已知无未知/failed谱系、真实完整viewport数值/尺寸/inside-backing校验和旧域完全不相交，不删required、不追认Retained、不假称旧像素已从backing清空。证明仍staged，只有原四API全成功才canonical完成。

新增B350–B361直接执行真实D2D和同生产probe/renderer：内嵌logo正常顺序及全BGRA等价正例，未声明/倒序/unknown前后/错误cache对象/变换/partial/错误producer对象负例；真实Shape→SVG→CLIP Word→Superellipse不相交/覆盖反例；实际外域旧像素保留而viewport中不存在、mapped旧域在viewport内、unknown向viewport重放及失败/deferred后不得域外追认。原H100/H101/H102和B337保留；新增offscreen旧实现RED未运行，本轮因果RED是上面的真实MainFold，不冒称新增用例已绿。

| 补丁源 | SHA256 |
| --- | --- |
| Bar.PresentationProbe.h | AEF1B97D84739607D2764D809C253D5CF83822ACD9E34F310818A8B693C99BAF |
| Bar.PresentationProbe.cpp | E3FB10973D54604CFC7806F77AAFB2B0755818E9D8240E336AB5815441083A22 |
| Bar.Rendering.cpp | F935D15A4E0371A7E38601C49AF83734347833333A324C0485BC2E17B005CD1E |
| Bar.RenderLoop.cpp | 2FE611C2190C950E61BC8DF8D2AE3F1735FD8FF960E8FAE624BD2465A455F4D3 |
| Bar.EraserAttribute.Test.cpp | 542EC52F2DDC3DE268AA0EA25F0EE5FC03C841EBE1FFA27890351C22F44EAEC0 |

静态已核原编码/CRLF、声明定义及本机SDK GetWidenedBounds签名；内存移除新Rendering观察接缝可精确恢复旧8A69FCD6…ABE2C全文hash，实际绘制/API次序未改。Main保持28B03510…FE4B1、诊断Test保持422AEBDD…B5DDB，无新宽诊断/registry/clock/wake/平台/源码树。未build/run/Git mutation/GUI或递归。当前停写交Root新完整build/offscreen/独立增量复核及fresh同scene，首goal是否Completed、全部216/像素/两scene/Release三轮/Win7仍按新实际结果判。

### 2026-10-01 独立must-fix：域外成功后保留backing谱系

Reviewer确认原CompleteAttempt把所有HiddenExpected都清oldBoundsKnown/possiblyVisible，而B357的outside实际仍有72,72旧alpha且clearCoversOldBounds=false；后续viewport扩回可漏Require。本次仅Probe.cpp+原Eraser测试修正：outside保留旧mapped/write bounds、身份和possiblyVisible，只有真实Clear覆盖旧域时撤销；原资源失效重置路径不变。B357后直接新增B362，不repaint：同epoch/surface，源viewport从0..32扩至非零source48,48/48x48，原72,72像素仍非零，必须required1/verified0/unverified1且拒HiddenExpected。H101/B337及其它反例保留。

新Probe.cpp SHA256 A0A2B77FB78F001BA59E48F70FCA939FC05BA6D52D8E9BA0EB83437423C6FB74；Eraser测试 3B69CF6CAF1B45F830A72D801961B5F19852DBC6DF00F8345DF355F95887A2A0。其它UI3/诊断/Main保持前表hash。静态原编码/CRLF及局部diff已核；没有build/run/Gitmutation/GUI/递归。Root进行中的旧AEF/E3构建不覆盖此序列；修后新构建/实际B362与首goal仍待Root，当前再次PATCH_READY并停写。

### 2026-10-01 EC37 offscreen两项失败的夹具前提修正与有界日志

Root提供新完整Debug0/33.54s，PE EC37E7D59FCC55F6AF5DD37231077A40816C832D353A2DA89284894B0D87148F，实际A0A2/3B69编入；freshHeadless7464/0，offscreen31304自然1，仅B352 grouped与B358失败，其它新增/旧/H101/B337/B362通过。已直接读复制的resume-20261001-ui3-lineage-fix-core.offscreen-results.log；它没有variant数据，不能反推是哪一分项。

B358初始flags0与displaySerial1违背生产FiniteSceneStateSupported的bit64和IsUi3FiniteSignatureValid的非零even合同，原Snapshot/MarkConsumed返回值又未检查。本次只改测试为flags64→65/displaySerial2，增加合法accepted/consumed/settled前提，保原proof与failed不Completed并强化要求Pending返回。B352 variant6调用已active的frameDirtyClip入口，Rendering257–260明确直接return，故没有真实nested clip；改为现B312同样的真实PushAxisAlignedClip及实际matrix的原ObserveClipPush/Pop，保partial必须拒证。旧grouped失败尚不能仅据源码归入6，因此增加测试owner固定8条B352实际required/verified/failed/unverified/首tag/reason/ink语义quality/coverage/createDelta，以及1条B358 tuple/Complete结果日志；没有正式宽诊断或放松negative。

唯一源码EraserAttribute.Test.cpp新SHA256 52ED0E7F9E9CEF666DDA35D44E9BF1CB2E7D65DA97E3A86EB55A70642EEEF645；其它UI3生产/诊断/Main保持上轮hash。原编码/BOM/CRLF、旧H101/B337/B362及每一失败断言已核，未build/run/Gitmutation/GUI/递归。当前PATCH_READY并停写，等待Root同主Solution incremental及同offscreen实际结果，不预填green。
