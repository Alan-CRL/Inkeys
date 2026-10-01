# UI3 B2-P2 finite candidate implementation

日期：2026-10-01，Asia/Shanghai。Active task：`.trellis/tasks/09-27-integration-and-release-check`。Writer：`ui3_bar_commit_impl`。当前 **B2-P2 GREEN PATCH_READY；生产caller完整Solution/动态绿验待root**。root独占build/run/GUI/工程；本worker没有运行EXE、没有递归派发、未改Root Main/Helper/Window/Draw3/RenderPipeline/UI/Rendering/SVG/spec/账本。

## GREEN当前实现

- shared observer已实现稳定snapshot消费、完整有限role seen/pending、真实caller签名/epoch/attempt/surface/viewport/batch验证。Consume要求原同even publication；Complete只用锁存candidate和当前稳定semantic goal，NoBusinessWrite仅serial变化可保持已consumed目标，新goalSuperseded、odd/unknown unverified，不重读business倒填。供给candidate也要与内部候选一致，Caller committed/tick不能替代settled/role/proof。
- 原BarUiAdvanceAnimation只调用一次；仅private observer存在时读after IsSame及原result.active。原shape/superellipse/svg/word/button遍历顺手归固定roles，MainRoot/脉冲/DrawRoot/默认Preview闭合、真实四spring/phase/displaycenter/DPI和域内SVG/word内容过渡均在原点观察；不另扫图/注册全动画。hover/press/fill/frameLight独立Feedback/Lighting，业务needRendering/changed/active逻辑保留。protected content timeline未读/改，原AdvanceContentTransition结束帧true保守pending，等原自然后继帧。
- Wake前stable token保存复用，Submit pulse判别不重新Snapshot；实际tool snapshot只在probe时用同一versioned getter，raw0原样拒fixture证明，不造1。Main readonly signature提取的P1所有字段/normalize/Wake顺序保持。Root/drawBatch只原timeline Restart增，不按callback。初始布局/publication serial0另外处理，不生成measured目标。
- 原CompleteAttempt/四API/B1 stamp/timer保持，成功全几何发布后写纯commit/anchor信息，timed要求真实B1 detailed gate+hasStamp+同attempt/epoch；raw off不读新阶段clock、不配置raw，ready时间null由timingValid=false表示。失败不推进完成，资源/epoch/surface失效清ready anchor/layout资格；普通pointer absent没有新heap/clock/资源请求或渲染Request。
- 生产资源输入始终producerPresent=false；哪怕当前布局settled和真实ULW返回成功，outcome仍ResourceUnverified。只有typed finite资源producer存在、required>0且count/identity完全匹配的shared helper组合输入才可CompletedLayoutAndSvg。本P2没有B3 bitmap/paint producer；positive输入仅CPU测试，不升级真实SVG/ULW验收。
- 512 observer prefix/drop和结果分母有界，goal重复frame不冒充新目标；未知publication恢复时回绑既有slot，不丢后续完成。Frame early returns Abort，不补渲染请求；Observer plain own rows由render唯一writer。completion只P1 atomic receipt，all owners真实join后Absorb才写普通publication ledger，NoChange引用仍不产新0ms commit。当前source没有F的all-owner出口/installer，Absorb仅测试/后续F调用，不在Unregister冒称Interaction已join。
- 真实同步Unregister保证Bar render callback已静止后，Seal只observer own rows/ready；不会在这里写P1 plain rows或撤backing。ready由render/静止后seal唯一publisher，22个atomic words逐字段编码（不bit_cast含bool/padding的整个Ready）；odd→前置release fence→relaxed字段→even release，reader一次copy/fence/复核。Register/InteractionReady仅独立monotonic atomic flags，同object生命周期。

## 实际RED证据（only新project output）

本worker已只读原始文件、未运行EXE：

- `ui3-b2-p2-red-headless-debug-arm64-build.status.txt` exit0，是Standalone Headless Debug|ARM64 target构建，root未设SolutionDir，其实际新产物为 `InkeysHeadlessTests/Build/ARM64/Debug/InkeysHeadlessTests.exe`。它不编译本批RenderLoop/Main生产caller，**不是完整Solution PASS**。
- 从该新EXE运行的 `ui3-b2-p2-red-project-output-debug-arm64-headless.status.txt`自然exit1/PID21704，stderr恰B211–B220十FAIL/FAILED count10，stdout216 layouts failures0；B221/旧P1/B/R无新增失败。
- root先前误从根 `Build/ARM64/Debug` 旧Headless启动得到exit0的 `ui3-b2-p2-red-debug-arm64-headless.*` 已标错产物，不作为RED/GREEN；保留失败方法学记录，不删除。
- root依据真正十项RED授权当前GREEN。当前代码/root调用尚未编译，后续必须八源和其他writer冻结→新完整InkeysRepo.sln Debug|ARM64→核真实Root/Build新时间/哈希产物→strictHeadless及适用parked→独立actualdiff，不能复用project output或旧0。

## GREEN源码静态检查/身份

本worker git diff --check exit0；新旧UTF8/CRLF与Main.cppm/Interaction原BOM保持。tests逐字等RED（十红/P1/B1/B06/R期待不改）；静态对照原Tick、EnsureDeviceResources、BeginDraw/GetDC/ULW/ReleaseDC/EndDraw、CompleteAttempt、B1 Stamp及原timeline Restart调用次数全部一致，无新Request/forceDirty。role观察没有改变原advance/needRendering/资源条件。不以这些静态结果称动态PASS。

原RED八源字节另存忽略目录 `TestResults/release-hardening/ui3-b2-p2-red-source/`。当前SHA256：

| GREEN源码 | SHA256 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h` | `d56c44d1a821fc3521adfb33da2c8dc3eb2f272c53fd448ff07ef4a88fa8905c` |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.cpp` | `9483172be5130a18b8757a2a40519abb3a07279af973faf90eaa5941de1686c2` |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | `ff59ba0501fcdc231a97243bdcbffaf62b7d7830c6411102daf23a576ac3e423` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | `9b11fdd65ad61e4bda78c7f256542b024292e304e095c8c2b65f9e273fab76f3` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | `5aa66c10e860dcf9698765ecbc22037f57724c7f48b492a6c1312b590ac29e8c` |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | `8f7b1a76cb457baac6b257ba1cc693db5f64b99c9266c15da78a8e9b95cc9684` |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | `ac5654997414bbc9d9c6e21116f0b5bd4f5415aa0eca2fb2205abd40c7ed4f47` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `10bc18e357d6b6d62520338b13756c5f482f7a4e245e8ddeb298ae9a2542f5d1` |

## 已读来源与前置

439行R2 contract SHA `B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097` 及最终B2-P1独立actual-code GREEN已核，RenderLoop输入hash `123E495EDDE81819716AE199B0A21B54FE7805105B259A9A36CD9ECDA95027F1`。只读已核root status：完整 `c3a-u2-b2-green-f065-debug-arm64-build` exit0、strictHeadless exit0/PID34212、PptCOM exit0/PID7044。这是P1阶段PASS，不给新P2任何动态PASS。

## 精准seam mapping（GREEN实际role体的约束）

| 原实际符号/位置（以符号为准） | P2边界与角色 |
| --- | --- |
| RenderFrame→WakeAndSnapshot前 | 安装者明确的private observer存在时，一次TryReadStable锁存accepted。没有source/pub、odd/unknown不能认目标；普通raw0不造toolRevision1。 |
| WakeAndSnapshot原GetStateModeSnapshot | 仅probe存在用单次真实GetStateModeVersionedSnapshot的state替代同一帧原快照，保存实际revision；原Tick/rawdt/50msclamp/退避不移动。 |
| SubmitTargetsAndLayout Main pulse/原mainBarTimeline.Restart、drawAttributeTimeline.Restart | pulse用真实serial/触发支路；root/drawBatch仅实际Restart旁挂增量，不能每callback冒充新目标。 |
| Submit后 | 共用P1 readonly ReadFiniteSignature，仍来自本帧同一tool快照；真实Main/drawLayoutSide锁进candidate，固定环境与publication同一even/semantic校验，不在commit倒读新business。 |
| 原AdvanceAnimation/Change*及shape/superellipse/svg/word/registered-button loops | 复用原BarUiAdvanceAnimation result.active和after IsSame，顺手记录固定roles，不再扫描全图/不造animation registry。当前RED未填完整role体，GREEN按下列清单接。 |
| MainRootGeometry | MainBar x/y/w/h/rw/rh/ft/enable/pct、main timeline after态；可见registered button/divider geometry/enable及必要visibility；反馈背景hover/pct/frameLight另外排除。 |
| MainClickPulse | MainButton w/h/n和logo1/logoInk w/h/content；mainButton x/y属于DockDisplay；Draw scene不等无关pulse，新Main请求/未知pulse干扰须拒确认。 |
| DrawRootGeometry/AttributePreview | DrawAttribute root/实际children geometry/visibility、draw timeline；原独立Thickness/LaserShell/PreviewMorph/PresetNumber/extension[3]/popup/slider/fine/menu/color等进度全部相关after态。press/hover feedback不粗暴当布局pending。 |
| DockDisplay | 原四spring/phase/recovery/自动居中、displayCenterX/Y/Dpi过渡、initial placement；直接读实际post step active/settled，保留原物理参数、dt和布局。 |
| SvgSemantic | 域内实际可见/target可见SVG geometry/enable/pct/content/必要color/angle；bitmap/paint proof是B3，未接不确认。UI protected timeline不访问/不加UI accessor；原AdvanceContentTransition true表示本次发生/仍有过渡，结束帧保守pending，等待原needRendering造成的自然后继帧。 |
| Feedback/Lighting | hover fill/frameLight/mouse/primary和press feedback继续原动画/draw，只独立角色计数，不阻Layout settled。 |
| EnsureDeviceResources→Draw前 | 按实际成功generation/target尺寸变化给surfaceSerial；SettleCandidate得到当前viewport尺寸/epoch/attempt，失败/重建/隐藏失效不能借旧proof。原资源调用不加/不改。 |
| 四API→CompleteAttempt→原完整成功几何发布末尾 | B1原Stamp/detailgate/timers/业务快照原位保留；候选纯值从本帧锁存，成功后旁挂commit/anchor latch。失败不完成，当前稳定semantic goal才确认；NoBusinessWrite仅serial变可保持旧consumed goal，新goal superseded、odd/unknown unverified。 |
| 真Register/Interaction.Run | 私有ready纯值来自实际成功Register/Run，独立于无Startup tracker的Report；普通observer absent no-op。 |

## RED源码（历史冻结）

- Probe.h/.cpp增加有限Ui3PropertyRole、typed Ui3FiniteResourceProof/CommitIdentity、R2 Ui3FixtureReadyValue及固定512 Ui3FiniteObserver。SnapshotAccepted复用绿P1 reader，其他状态/roles/Settle/Complete/ready/Seal/停后Absorb目前为limited桩，产生具体断言失败；不是assert(false)或测试里复制正确算法。
- private global observer binding默认null，只在owners启动前安装、所有真实join后撤。不存在普通产品installer、新CLI/sourcepointer或fixture bootstrap。
- RenderLoop已接实际Wake前snapshot、probe-only versioned state、真实timeline Restart counters、Submit后签名消费、timeline after/lifecycle、资源成功surface观察、Draw前实际dims、完整事务后原完整几何末尾的软件身份/anchor端点。**完整属性role pending体尚未接（只有timeline/lifecycle粗seam），留待root真实RED后GREEN。** default observer null时不走新getter/clock/资源metadata操作，没有新Request/forceDirty/改变Tick。
- P1 signature builder仅抽为Main的同一readonly member，Main.UpdateRendering原StateUpdate/ThicknessDisplayUpdate后仍仅activeMutation时调用，原Notify/Request不移动；新方法也供RenderLoop probe调用。正常头放global module fragment，versioned type来自actual IdtState.h，未在global fragment写裸forward declaration。Main接受/Button业务逻辑不改。
- 真实Interaction.Run旁挂独立pure ready通知，原Startup.Report仍保留；真正Register成功旁挂notify。两者不为计量创建线程/窗口或追加工作。
- 生产MarkConsumed后始终ObserveResources({})，B3 producerPresent=false；requiredSvg默认0不是完成proof。即便最终布局settled及真实ULW commit，GREEN也只能ResourceUnverified，不能叫CompletedLayoutAndSvg。typed positive资源仅由无窗口测试显式传入，当前没有真实B3。

## shared observer RED断言（期望未改）

B211消费/完整roles/typed资源组合；B212active/IsSame pending与持续feedback/light排除；B213最后settled失败→成功且render不写P1 plain ledger、真stop后吸收；B214NoBusinessWrite仅serial变允许已consumed旧goal；B215新goal后旧candidateSuperseded；B216无B3/required0为ResourceUnverified；B217capoff无Startup/B1时钟的纯commit/anchor ready；B218独立initialLayout identity不计measured goal/SVG完成；B219缺role/旧epoch-surface proof拒确认；B220515个accepted goals/512prefix+3drop；B221默认private observer absent。

预计准确B211–B220十项新红，B221负向及P1/B1/B06/R旧body仍绿；真实结果/编译首因须root完整Solution+strict no-window确认，当前没有执行P2。所有新数据输入/Complete bool仅用于共用production helper组合测试，不是实际SVG/ULW、动画完成时长/像素/GUI证据。旧测试期望未修改，没有复制完整Bar或observer正确算法。

## owner、预算、编码与冻结

publication ledger仍Interaction唯一writer；render future只atomic receipt，普通P1 rows只true all-owner join后通过Absorb吸收，当前Absorb桩。observer candidate/own accepted-goal prefix由render唯一owner，live跨线程ready用数字atomic word publication；22-word编码必须逐字段处理bool/padding，不bit_cast整个Ready DTO。RED尚未写word算法，不声称已有latch内存序证明。fixed对象在显式安装前构造，default无对象/数组；合并R默认32768+publication+observer在headless static_assert<=64MiB，未来B3/source/final65k预算另核。

worker静态diffcheck exit0；严格UTF8/CRLF、Main.cppm/Interaction原BOM保持。旧RunRenderSchedulerTests P1/B1/B06/R完整body逐字保留；216文件未触。原B1 Stamp/detail/timer语义由静态diff保留，尚须最后源构建/回归实际验证。源备份忽略目录 `TestResults/release-hardening/ui3-b2-p2-source-baseline/`。

| 历史RED源码 | SHA256 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h` | `b4efdc7364f6b4698287042e11e6f110ff416455246aceecb9579859edcc3ae0` |
| `Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.cpp` | `72d9329a92f6fa6a5353d8fd51e1a46185837caade7c662476d021cfe7c94387` |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | `f111375822134f80e38b278acdaf0b5ba1ecbd853f58a3038ec397de2ac1c4a7` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | `9b11fdd65ad61e4bda78c7f256542b024292e304e095c8c2b65f9e273fab76f3` |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | `5aa66c10e860dcf9698765ecbc22037f57724c7f48b492a6c1312b590ac29e8c` |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | `8f7b1a76cb457baac6b257ba1cc693db5f64b99c9266c15da78a8e9b95cc9684` |
| `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp` | `ac5654997414bbc9d9c6e21116f0b5bd4f5415aa0eca2fb2205abd40c7ed4f47` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `10bc18e357d6b6d62520338b13756c5f482f7a4e245e8ddeb298ae9a2542f5d1` |

## 未完成/未验证

完整role pending/observer组合/ready word/stop后吸收已实现；GREEN新P2尚未build/TypeCheck/Headless/GUI，root唯一slot，完整生产caller编译和actual-source review仍必要。B3真实SVG ready/bitmap/paint/full clip/hidden lineage未接，F auth/source/CLI/privatebootstrap/phase/actualownHWND/truejoin未实施；没有实际两scene/最终BGRA/三Release轮、线程CPU/GPU/HC-H2/Win7或其它family结论。不能因P1绿或P2设计GREEN将整体E04/发布标PASS。
