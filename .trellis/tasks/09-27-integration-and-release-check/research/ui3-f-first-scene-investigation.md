# UI3 F 首轮 MainFold capture-off 失败只读调查

Active task：`.trellis/tasks/09-27-integration-and-release-check`。日期：2026-10-01。Writer：`ui3_fixture_source_impl2`。唯一新增本文件；其它源码、旧报告、Root文档、产物均只读，未 Build/run/Git/GUI/Computer Use 或递归。

## 当前结论

**已确认：第一 Main Up 被真实业务接受，但在固定下一 due 前未得到 CompletedRevision；随后正常 Close/alljoin/封口，结果 Failed。尚不能由现有产物确定唯一 layout/SVG 原因。** 本轮不是线程崩溃、输入未投递或强杀后的假成功。

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
