# UI3 B2-P2 有限candidate/ready实际代码独立复审

日期：2026-10-01（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。本reviewer只读R2 frozen/P1实码报告、八source、baseline实际差异、共享observer/RenderLoop/Main/Interaction及新断言；没有改源/工程/spec/账本、没有Build/EXE/GUI/Git/递归，只写本报告。有其它writer，修补仅由root指定唯一writer执行。

## 当前结论

**GREEN（仅B2-P2实码、共享helper数值及真实caller编译门）。** 已独立核真实candidate消费、角色与完整commit/ready路径；确认的PNG遗漏、B222非法fixture、production helper未全限定三项均由root最小修补。合法前提下因果RED、最终完整Solution Debug|ARM64 Build0与正确Root/Build strict Headless自然0均已直接读取。无另一个确认的本批代码阻断。先前两个非法fixture失败与C3861完整编译失败保留；没有执行真实Bar GUI caller，不能将编译门称为运行场景完成。

本批不含实际SVG producer/F installer/owned输入/真实scene，不能给有限完成、真实ULW/帧P95/CPU/GPU/Win7/HC-H2 PASS。

| 初审冻结八源 | SHA-256 |
| --- | --- |
| `Bar.PresentationProbe.h` | `D56C44D1A821FC3521ADFB33DA2C8DC3EB2F272C53FD448FF07EF4A88FA8905C` |
| `Bar.PresentationProbe.cpp` | `9483172BE5130A18B8757A2A40519ABB3A07279AF973FAF90EAA5941DE1686C2` |
| `Bar.RenderLoop.cpp` | `FF59BA0501FCDC231A97243BDCBFFAF62B7D7830C6411102DAF23A576AC3E423` |
| `Bar.Main.cppm` | `9B11FDD65AD61E4BDA78C7F256542B024292E304E095C8C2B65F9E273FAB76F3` |
| `Bar.Main.cpp` | `5AA66C10E860DCF9698765ECBC22037F57724C7F48B492A6C1312B590AC29E8C` |
| `Bar.Interaction.cpp` | `8F7B1A76CB457BAAC6B257BA1CC693DB5F64B99C9266C15DA78A8E9B95CC9684` |
| `Bar.Button.cpp` | `AC5654997414BBC9D9C6E21116F0B5BD4F5415AA0ECA2FB2205ABD40C7ED4F47` |
| `render_scheduler_tests.cpp` | `10BC18E357D6B6D62520338B13756C5F482F7A4E245E8DDEB298AE9A2542F5D1` |

均在初审匹配implementation当时GREEN表；后续PNG fix身份见下表，不能复用旧三源hash。R2为439行B32BF41F…1097、P1前提保留。

## F066最终冻结源码与合法红绿门

截至本次最终source增量，以下三源已停止写入且实际SHA256匹配root final表；其余五源保持上表初审hash。

| F066最终GREEN源码 | SHA-256 |
| --- | --- |
| `Bar.PresentationProbe.h` | `AD1F8485C3AB5D039AF6D2E5561D5A33189EC8D74A838EF0E80FF1FCCC918B93` |
| `Bar.RenderLoop.cpp` | `F59F9A59BEC13C773C3E4CB9A91AC902EE1555F584790D13806D783AEA8228A8` |
| `render_scheduler_tests.cpp` | `16327A081799E2780DBBDE6C00B10BA69158AED2EFEC91A2A70E503B2B2E3ACA` |

最终映射严格为 `colorSelectionWheel && relevant ? AttributePreview : Unspecified`；caller5707–5711按真实PNG enum、nonnull observer和真实当前/目标visibility提供这两个bool，全限定调用。ChangeState/ChangeValue/ChangePct仍通过原AdvanceAnimation一次推进，在推进后只旁观active/IsSame；color/content未扩资源family，Draw/dirty/Request/原质量不改。测试2353使用合法flags66，并将SnapshotAccepted、consumedMoving、consumedStationary三bool都纳入B222前提。三文件实际UTF-8无BOM/CRLF均保持。

已直接读取 `c3b-u2p2-b222-valid-red-compilefix-debug-arm64-build.status.txt` exit0及log Build succeeded/6 warnings/0 errors。该完整InkeysRepo.sln Debug|ARM64编译了真实RenderLoop/Main caller，但header当时是RED stub。正确Root/Build/ARM64/Debug Headless的 `ui3-b222-png-valid-red-debug-arm64-headless.status.txt` exit1/pid10952，stderr恰B222/FAILED count1，216layouts/旧项无其它失败。此轮才是合法fixture下的归因RED。

GREEN仅恢复共用映射，不放宽支持门/接受前提/原断言。已直接核最终 `c3b-u2p2-b222-green-debug-arm64-build.status.txt` exit0，以及log完整InkeysRepo.sln/Debug|ARM64/125 warnings/0 errors（旧转换等警告，未发现三份新增source相关warning）。`c3b-u2p2-b222-green-debug-arm64-headless.status.txt` exit0/pid26160/exe=`Build/ARM64/Debug/InkeysHeadlessTests.exe`，stderr空，stdout PASS animation correctness/216layouts failures0。B222位于实际RunRenderSchedulerTests中，animation_tests.cpp1643将其返回值计入failureCount；与合法RED相同入口，不是启动旧PE或未调用新增断言。

辅助回归只记录实际范围：`draw3-u2-p2-green-debug-arm64-parked.status.txt` exit0/pid22032，stderr旧五suite/Session/ContentProof PASS；`c3b-u2p2-b222-green-pptcom.status.txt` exit0/pid17700，stdout descriptor ownership/session-owner PASS、stderr空。它们不运行本UI3 Bar GUI，也不替代其它writer的实际设计/代码review。三源final SHA256在读日志后再次匹配，未发生source漂移。本reviewer没有执行Build/EXE/GUI。

## Findings (fixed)

本reviewer无产品修补；以下由root唯一writer完成，并经实际源码/红绿输出独立核对。

- File：`Bar.PresentationProbe.h`193 / `Bar.RenderLoop.cpp`5705。Issue（P1）：真实PNG色轮没有layout pending角色。Fix：caller共用两bool有限映射，只真实相关色轮geometry/visibility归AttributePreview，默认null不额外读visibility；B222合法红绿闭合。
- File：`render_scheduler_tests.cpp`2353–2372。Issue（P1）：flags67被真实publication拒绝，初始红测没有归因能力。Fix：合法flags66并检查Snapshot与两次MarkConsumed前提；保留原两轮失败，再取得合法因果RED/GREEN。
- File：`Bar.RenderLoop.cpp`5709。Issue（P1）：bool/bool无法ADL找到普通header中的helper，production编译C3861。Fix：全限定Inkeys::UI::Bar调用，完整compilefix/最终GREEN build通过；保留首次完整失败。

普通header/global fragment实际可见：Main.cppm新增actual IdtState.h包含，避免不完整裸声明；Probe.cpp保持普通单元，无Bar/RenderPipeline import圈。P1 readonly signature抽取没有重写业务setter，原UpdateRendering规范化→Notify/Request保留。已用实际文本比较：baseline中RunRenderSchedulerTests起点至原return之前的76856字符前缀完全保留，P1/B/B06/R断言与场景没有移除或降低期望。P2与B222仅追加在其后，新的合并cap检查另计observer。

## 已关闭项的发现与失败历史

### P1-PNG：真实属性色轮遗漏（root最小修补，静态已核）

`Bar.RenderLoop.cpp` 5704附近在整个pngMap loop前把geometry/color/visibility/content置Unspecified；仍原样Advance该PNG，却不进入observer pending。真实对象是 `BarUiPNGClass` / `BarUISetPngEnum::DrawAttributeBar_ColorSelect12Wheel`，在Initialization524建图；Submit3473/4272/4345附近取得它并设置实际x/y/w/h/pct/相关时长。这属于DrawAttribute子控件布局/visibility，和是否做PNG资源计数不同。

六个总role seen可以由其它对象满足，所以人工遍历1..6的B211–B220不会暴露这个对象未IsSame的遗漏。可能先得到layoutSettled/ready而该子目标还在变化；B3 absent目前仍阻止CompletedLayoutAndSvg，但不证明布局角色正确。

root已持权实现真实caller共用 constexpr `Ui3FinitePngLayoutRole(colorSelectionWheel,relevant)`，仅真实enum匹配且enable当前/目标或pct当前/目标仍可见的PNG将geometry/enable/pct归AttributePreview。`relevant`读取以nonnull observer短路，color/content保持Unspecified；原advance、Draw/resource、Tick/Request次数保持，没有PNG像素认证。新映射静态覆盖当其它AttributePreview对象已same、该颜色环仍active/unequal的反例；普通PNG或完全无关隐藏色轮不阻塞。B222合法前提、归因RED及最终GREEN数值测试现均已核，见顶部最终冻结门。

### P1-B222：初始fixture非法，首两轮不是因果RED/GREEN

`render_scheduler_tests.cpp` 2353初值 `goal.flags=67`，实际signature的bit0为Main fold、bit1为DrawAttribute展开（`Bar.Main.cpp::ReadFiniteSignature`232），并非两个合法独立fold。`Bar.PresentationProbe.cpp::FiniteSceneStateSupported`118–119拒绝 `(flags & 3)==3`；`FinishAtRenderRequest`190另拒DrawAttribute scene带Main fold。真实目标因此进入UnsupportedState，无法stable publication，随后SnapshotAccepted及两次MarkConsumed都被void忽略，stationary仍不settled。这个第一轮失败与PNG role没有因果隔离。

已直接读F066首轮RED：standalone Build0、project-output exit1/pid23084、stderr恰B222/FAILED count1；首轮映射GREEN：Build0、project-output exit1/pid34360、stderr同一B222/FAILED count1。旧216/animation数值正常。保留全部原始文件，禁止更改产品支持门、静默覆盖或计作GREEN。

root已最小改为合法 `goal.flags=66`，SnapshotAccepted与两次MarkConsumed作为显式前置断言；同一合法fixture的Unspecified stub真实pending反例RED已核，随后仅改映射为AttributePreview的GREEN Build/Headless亦已实际核对。本reviewer不写源码、不运行这些case。

第一轮静态映射身份（后续合法fixture/hash需另列）：

| F066首次映射GREEN三源（测试仍非法） | SHA-256 |
| --- | --- |
| `Bar.PresentationProbe.h` | `AD1F8485C3AB5D039AF6D2E5561D5A33189EC8D74A838EF0E80FF1FCCC918B93` |
| `Bar.RenderLoop.cpp` | `408CFEA1A5C7417E4274FD26E8A28061009756CAD627A18C686EF768E96215DC` |
| `render_scheduler_tests.cpp` | `F8F66DDF26E47A498319EFBF8353FBA792F4D6DE4E679CD57441452599026FF2` |

其余五源保持初审hash；三个首次映射hash已独立匹配，不可把这一身份绑定到将来合法fixture运行。

### P1-F066-C3861：bool helper的真实caller未全限定（root已单点修）

`Bar.RenderLoop.cpp`5709首次调用未全限定 `Ui3FinitePngLayoutRole(bool,bool)`；函数声明在 `Inkeys::UI::Bar`，该函数内仅引入Ui3PropertyRole，两个bool参数不会像RoleMask的enum参数那样提供ADL。已直接读取完整Solution `c3b-u2p2-b222-valid-red-debug-arm64-build.status.txt` exit1及首意义C3861/identifier not found。Standalone Headless只编译shared helper/tests，无法发现此production caller错误。

root已仅将5709全限定为 `Inkeys::UI::Bar::Ui3FinitePngLayoutRole`，当前RenderLoop SHA256 `F59F9A59BEC13C773C3E4CB9A91AC902EE1555F584790D13806D783AEA8228A8`；没有修改支持门/动画行为。合法B222源码SHA256 `16327A081799E2780DBBDE6C00B10BA69158AED2EFEC91A2A70E503B2B2E3ACA`，已实际核Snapshot/consumedMoving/consumedStationary都进入断言。该身份的完整compilefix/合法归因RED现已实际通过相应门；最终header已恢复GREEN映射且hash见顶部。最终GREEN完整重编/Headless输出亦已实际通过，见顶部。本reviewer只读，产品机械修补也由root唯一writer执行。

## Findings (not fixed)

### P2：后续B3资源finalization位置

当前caller在BeginDraw前SettleCandidate，而真实资源数据总是ObserveResources({})。未来B3在Svg DrawBitmap/clip中产生证明时，不能仅晚调用ObserveResources就假定已经锁存的candidate更新。当前方法只存resource_，最终Complete使用内部已锁存candidate。B3要另冻在真实绘制结果可用后的资源finalize/重新校验点，保持锁存的layout/身份与真实bitmap/coverage一致，无新Request/clock；当前不实现B3，也没有零required误完成。

### P2：timing与来源接线

当前正timing检查依赖真实B1 gate、consumed/settled/commit正且有序；typed接口的acceptedTicks0尚不独立拒timing。F若on时owner clock缺失，不能把0 accepted当真实源时间；应明确timing valid mask/正accepted入口并有缺时钟反例，或者只导出有证据的子段。本批正常未安装observer，未证明产品存在错误时间样本。

raw工具revision0保持，不造1；legal Pen baseline及固定initialStable来源仍必须由F真正初始化。Display来自Bar自己的even publisher、configZoom实际Normalize正值，与P1边界一致；同frame GetStateModeVersionedSnapshot替代原state getter只在private observer存在时执行。

### P2：owner/封口与范围

active observer atomic pointer不拥有对象；所有producer启动前安装、source/Interaction/Render/Window真实join后撤必须由F实现。Unregister只证明Bar callback静止，Seal不能写Interaction plain ledger；Absorb只all owners stopped后。现在没有F安装/最终出口，不能声称该lifetime已动态整链验收。完整production caller在合法RED compilefix与最终GREEN均编译0，共享helper数值GREEN已核；Release/其它架构、GUI及B3/F/性能还未通过本P2门。

## 其余实际路径核对

### publication消费与候选身份

RenderFrame在Wake前一次SnapshotAccepted，BeginFrame保存run/step/revision/epoch/attempt；ordinary absent只沿原GetStateModeSnapshot，无private heap/clock/新Request。Submit后用同一Wake的versioned tool及真实side锁存，MarkConsumed要求同原even和semantic signature，初始publication0独立处理。root/draw batch只在原两个timeline.Restart后递增，Main click pulse真实检测单列，Draw scene遇无关Main pulse标interference。

MarkConsumed拒未知/固定环境变化、unsupported gesture或missing mask，没把commit时最新business值倒填到候选。Settle要求target/surface/epoch/dims有效、所有required role seen且pending0；初始layout与measured accepted目标不混计。新goal/旧late成功分别Superseded，pureNoBusinessWrite serial变允许已消费旧semantic goal；odd/unknown不确认。

### role和原动画语义

新ObserveRole在原BarUiAdvanceAnimation一次调用之后读IsSame/原active，原changed/active/dirty处理保留。MainRoot、MainClickPulse、DrawRoot、默认Preview/Popup、SVG/Word/button content、Dock四spring/phase/display值均按原循环观察；持久hover/light/pressScale归Feedback/Lighting，原质量/渲染需求未关闭。content private timeline不跨锁窥读，只沿原AdvanceContentTransition返回true保守pending，结束帧也等自然下一帧，不补Request。PNG遗漏已按上述最小映射修补。

数值RequiredRoles防整个域未观察，未完成role不能靠真实ULW success跳过。必须逐个实际目标分类，不能拿role总mask替代对象覆盖；不扩大成全animation registry。

### 完整事务/anchor/ready

生产四API/CompleteAttempt/B1 stamp仍原顺序；finite Complete在成功的dirty/viewport/mapping/geometry等原快照发布末尾。identity用实际epoch/surface/attempt/presentedSize、真实presented Main screen center及Draw按钮的实际inh几何经当前两轴mapping/direct translation计算中心；mapping serial与alpha条件提供纯值anchor。该软件identity不叫屏幕光学可见或硬件Touch。

ready 22word逐字段编码，Signature9word无当前padding，bool/尺寸32bit合并数值，不bit_cast整个Ready。odd→前置release fence→word relaxed→even，TryRead一次copy/acquire fence/复核；Register/Interaction onlyatomic flags。epoch/surface/target lost及unsupported清anchor/layout资格，resize/Hidden沿实际identity条件；capoff不依赖StartupTracker/B1clock，timing=false/time0内部表示，离线须null。invalid/未知geometry不能成为ready。

### 无SVG与停后ledger

生产在MarkConsumed后无条件ObserveResources({})，producerPresent=false/required0；Settle的svgProofComplete要求真实typed producer且required>0、verified/failed/unverified与revision/epoch/surface/attempt匹配。即使layout settled和ULW成功，返回ResourceUnverified。CPU组合positive只能证明该判定，不能称真实SVG producer完成。

observer own prefix由render唯一writer，512前缀满后仍goalsSeen/drop；完成同goal不按每callback重计。P1 completion只atomic receipt；实际同步Unregister后Seal只observer数据/ready，不写P1数组。AbsorbAfterOwnersStopped按run/revision/semantic signature吸收，nochange引用仍不造新commit/timing，plain ledger只最后归单owner。early return通过Abort留unverified，不造新帧。

## 测试与动态门的真实范围

B211–B220直接共享production Ui3FiniteObserver+P1 publication，组合的resource与API结果为固定CPU数值：全部角色/active/same、feedback排除、最后失败→成功、pure reject/新goal、无B3、off ready/初始独立ready、缺role/旧epoch-surface、512+3drop。B221默认observer absent。没有复制完整Bar/正确算法；原P1/B/R保留。它们未执行normal RenderLoop/Main。

已直接读取RED新project output：`ui3-b2-p2-red-project-output-debug-arm64-headless.status.txt` exit1/pid21704，stderr恰B211–B220十FAIL。Standalone Headless project build0只编译测试/shared helper，root误启动Root/Build旧PE0的文件无效，不能当绿。已直接读取正确Green project-output status，exit0/pid14416/exe为 `InkeysHeadlessTests/Build/ARM64/Debug/InkeysHeadlessTests.exe`；stderr空，stdout末尾PASS animation correctness/216layouts无失败。它只证明初审共享helper数值，不覆盖后加PNG caller/错误B222或生产caller编译。

新 `ui3-b2-p2-green-headless-debug-arm64-build.status.txt`exit0只是同standalone project；完整InkeysRepo.sln/normal caller首次valid-red C3861失败与修后完整compilefix0均已直接读到，最终映射GREEN完整Build0/strict Headless0亦已直接读到。其它writer源由root串行冻结，不请求并行构建或修改其文件。

## Verification

- Lint：本reviewer未运行产品lint；本报告UTF-8无BOM/CRLF与尾随空白检查通过。TypeCheck/Build：root合法RED完整compilefix0、最终GREEN完整Solution Debug|ARM64 0（真实caller已编译）均已直接读取。
- Tests：P2原RED十失败/正确project-output共享helper数值GREEN已核；F066首两轮非法fixture失败保留，合法fixture归因RED仅B222已核。最终映射GREEN strict Headless pid26160自然0/全部新旧断言及216通过；parked/PptCOM仅辅助回归0。
- GUI/真实Bar/有限完成/性能：未运行/未通过本批。保守unverified、后续B3/F及跨架构门保留。
- 仅本report写入；最终冻结source/完整caller编译/共享数值红绿GREEN，无另一本批确认代码阻断。不能将本门升级为真实scene/性能PASS，B3/F实际接线与安全/生命周期另审。
