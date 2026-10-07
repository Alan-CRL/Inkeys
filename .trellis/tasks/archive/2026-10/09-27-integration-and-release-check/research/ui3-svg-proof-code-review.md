# UI3 B3 SVG proof RED 实码与范围安全独立复审

日期：2026-10-01（Asia/Shanghai）。Active task：`.trellis/tasks/09-27-integration-and-release-check`。Review：`cleanup_ui3_design_check`，trellis-check。本轮仅写本报告；产品/测试/工程/spec/账本只读，没有自修、递归、Build、EXE、GUI、Computer Use 或 Git 操作。其它writer存在；root唯一构建/运行槽和B3唯一writer保持。

## 结论与身份

- **CLEAR_RED_SCOPE / RED_VALID**：本冻结七源可作为现有 `--bar-eraser-offscreen-test` 的保守B3红测检查点。新块不创建HWND或启动真实Bar输入/业务；当前paint桩确实拒认证，不以bitmap ready或DrawBitmap次数给完整目标PASS。实际完整Build0和精确两项RED已直接读到。
- **NEEDS_REVISION（完整B3 GREEN）**：台账资源失败摘要丢失、initial publication0的实际DPI未带入缓存证明、后绘制SVG没有使其它已绘制SVG的覆盖证明失效，三项已向root提交并由root接受。当前保守桩避免了false positive，不表示接口与真正paint认证已完成。
- **F / 真GUI / 场景 / 性能未批准或未验证**：没有F installer/source生命周期或真实两scene/ULW输入闭环；新auth helper未登记于此次构建。此RED安全范围不授权新GUI F，不升级SVG/API次数为像素证明、GPU或整scene性能结果。

依据：真实AGENTS、task prd/design/implement/check.jsonl、已载native UI/diagnostics/C++/resources/encoding规范及当前相关章节；R2完整439行（尤其§5/§10）合同 SHA256 `B32BF41F5EF31F9B8DF6B0488D0F8A6074839800A8CF9B1A7C8E7213054D1097`，B3 GREEN_DESIGN仍选择NoRetainedVerified保守paint。B2-P2/F066独立报告 SHA256 `220E4CAB708FF17CAE6B0F4A71BBC066BED13E407797734B84ECEB5068994D15`及其AD1F/F59F/16327身份已核，不将旧绿色转记成B3通过。

B3实施报告冻结 SHA256 `A6F41F9E602CB18441468A80715CD718236531B8E76AB8843FFD722AB7511641`；本reviewer检查真实符号和调用链，不以实施者叙述代替源码。以下七源两次实际SHA256均匹配，后续GREEN修改不能复用这些RED指纹。

| RED冻结源（均在 `Inkeys/Inkeys/UI/Bar/`） | SHA256 |
| --- | --- |
| `Bar.PresentationProbe.h` | `5E9D3B266934A8AF035968DFB2A367DCB9EAE9D1223A900F4F676D112B1F9749` |
| `Bar.PresentationProbe.cpp` | `FF098371E5502B6B47443283C3313A84DFBDBC0342B7304FE5329861566DF39F` |
| `Bar.UI.cppm` | `55E4A2369884F7F151AFD6196B56D83F9E0D84BF2233D59A2FA02653A7A51205` |
| `Bar.UI.cpp` | `24525B2E3A425E8A62B348F7907E8B9E620098C90A0BECCC9199F809A0007702` |
| `Bar.Rendering.cpp` | `8A69FCD6E2CFCD9CF5747B890AD22C01D274E67C3E32E10459AEFE40ADEABE2C` |
| `Bar.RenderLoop.cpp` | `DD64B44553097DB0C323DED3D3436045B38DFDC176DA0D2EB6D657531E9CAF1F` |
| `Bar.EraserAttribute.Test.cpp` | `6A37845003BAE429497EB984554785E580CEF8EF7E7F7483F071138CBE3775B4` |

全部实际bare-LF=0；前六UTF-8无BOM，Eraser测试保留原UTF-8 BOM。没有操作Git或声称已独立隔离七源完整Git diff；这里核的是实际源码接缝与已审B2基线，root最后总diff/HF复审仍必需。

## Findings (fixed)

本reviewer没有源修补。下列接缝在当前RED实码已存在，核对通过于所列范围：

1. **绘制后资源finalize位置已闭合。** `Bar.RenderLoop.cpp`10031原Settle仍在BeginDraw前；12739实际PopFrameDirtyClip后12741调用 `FinalizeResources(candidate, FinishDrawing())`。`Bar.PresentationProbe.cpp`865–880只核已锁存run/step/revision/epoch/surface/attempt/dims/root-draw batch/pending/mismatch/settled，再ApplyResourceProof；无Settle重跑、业务重读、读钟或layoutSettled重复增加。`CompleteAttempt`仍在原四API/决策成功和完整几何发布后确认；B304实际PASS。
2. **默认null及capture-off钟门有效于本源。** `SvgObservationScope`72–78只有nonnull probe才安装TLS并恢复前scope；StageTimer354–365只有scope/probe/clocksEnabled齐全才读钟。生产RenderFrame13236–13239只从已安装finite observer取得B3 pointer，clocks位沿 `diagnostics->detailedCaptureEnabled`，不凭clock参数绕B06。普通无probe无新计量clock、表分配、资源创建或Request/Flush；新数值成员与少量null检查仍存在，尚不能声称CPU开销等价已测。
3. **真实缓存与API计数接缝已接。** UI.cpp540/552/567对应原loadFromData/renderToBitmap/CreateBitmap；原cacheBitmap和cW/cH/cColor成功提交后590–597才发布typed ready。自然失败留原返回/fallback形状与stage failure，不伪造成功。CalcWH639、content transition335按同scope计parse，初始化计数独立。Rendering.cpp2784原needUpdate/质量阈值保留，最后2851单次DrawBitmap；读回不在四个资源timer内。
4. **旧未知资源不追认。** UI.cpp384–409核owned初始化发生在同一对象、无旧bitmap、owner serial与实际cacheBitmap指针；Bind-before-owned-init/拷贝/旧bitmap不会凭tag取得semanticKnown。ApplyContentDirect456、真实contentCommitted374、原ChangeString.ApplyTar后的SVG owner marker5433分别递增真实value；SetTar不是value提交。epoch重建仍沿Rendering131/168/180原ResetCache链及两类icon，不增新设备。
5. **固定容器与无retained形状。** Probe.h214/323固定256 slots，Bind111–140线性有限、先检查tag family/key并拒tag或对象冲突/257th，不用巨大tag作为数组下标。Fits预算378–387先检查加法/范围后除法，包含当前probe/publication/observer/对象metadata及source预留；B307默认32768与巨量size反例PASS。`CapacityEvictionApplicable=false`，per-object单bitmap的replacement/reset不能写成capacity eviction。

## Findings (not fixed)

### P1-R1：资源失败摘要在finite台账丢失

- **File/symbol：** Probe.h `Ui3FiniteTargetRecord`52–62；Probe.cpp `ApplyResourceProof`854–863、`StoreOutcome`918–929、`AbsorbAfterOwnersStopped`721–729。
- **实际链：** FinishDrawing返回required/failed/unverified/firstTag/reason，Apply完整带进candidate；StoreOutcome只写requiredSvg/verifiedSvg，连现有row.firstUnverifiedSvgTag都未赋值，row也没有failed/unverified/firstReason。Absorb只能搬旧字段。真实ResourceUnverified结束后，离线台账因此不能保留本目标缺图/失败原因与细分分母。
- **最小改动：** 在内部纯数值record增加failedSvg/unverifiedSvg/firstUnverifiedReason，StoreOutcome与停后Absorb同样复制tag和这些字段；no-change保留原goal引用且不造新commit/time。mapping/cap失败或unboundRequired也须有明确unknown/invalid原因，不能只留reason0。
- **验收：** 用真实SVG full/partial/missing/parse failure summary经过late finalize→Complete→所有owner停止后的Absorb，断言required=verified+failed+unverified及首tag/reason均保留；failed/unverified不因成功ULW或prefix满而静默消失。记录sizeof变化并复核64MiB预算。
- **未修原因：** root/B3 writer独占源与内部接口，本reviewer只写报告。当前RED仍安全拒完成。

### P1-R2：initial publication0缓存proof记录dpi0

- **File/symbol：** RenderLoop13242–13249初始accepted零值；Probe.cpp807–829 `MarkConsumed`把合法实际signature放在consumedSignature_；RenderLoop10042从candidate.accepted.signature取dpi；UI.cpp471–475锁存plannedProof。
- **实际链：** 没有accepted目标的initial publication0仍可通过真实Submit/Advance初始布局，accepted.signature却仍默认零。初帧正常CacheBitmap因此把dpi0记入ready proof。随后真正接受的目标dpi非零，原needUpdate=false合法reuse不会为了计量重建该缓存；一个只用人为target.dpi96的offscreen正例没有覆盖此production caller。
- **最小改动：** 在真实Wake锁存targetDisplay.dpi或同Submit已通过验证的consumed signature中，将实际DPI一次固定到初始候选/纯帧值供BeginBackingWrite使用。不要默认96、从dpiZoom反推、读最新业务倒填，或为证明额外重建缓存；initial仍无测量step/revision，不能伪造Accepted。
- **验收：** publication0→实际owned初始化/CacheBitmap→纯commit initialStable→合法目标及同bitmap真实reuse，typed DPI/epoch/surface正确且不增加create/upload；再变真实DPI/epoch应保留原重建/拒旧证明。capture-off同样能用纯身份，不加计量clock。
- **未修原因：** 是当前B3 production caller与纯候选字段决定，需唯一writer最小接线；保守RED不受影响。

### P1-R3：后绘制SVG没有使其它槽的覆盖证明失效

- **File/symbol：** Probe.cpp `ObserveDraw`266–286、`ObserveUnknownWrite`260–265、`ObserveClear`251–259、待实现的 `FinishDrawing`297–321。
- **实际链：** 普通Shape/PNG/Word及直接target写已旁挂UnknownWrite；Clear会标已提交槽overwritten。ObserveDraw只设置当前slot.submitted并把其overwritten置false，没有处理这次实际SVG Draw对先前其它required/submitted槽的覆盖。以后只在FinishDrawing按full clip认证，将可能让先画A在后画B重叠后仍得到完整证明。
- **最小改动：** 在真实SVG写点按已锁存dest/effective transform/viewport/clip求与其它staged bounds的交集；无法证明时保守使其它已绘制槽未知。保留原API次数和顺序，不新增绘制/Flush/Request，不能用画了B就认为A仍有完整像素。
- **验收：** 同真实target A full draw→B实际重叠draw，A必须Unverified；可证不相交时不误当全图覆盖。随后Clear、unknown write、partial/empty clip、opacity/变换变化都沿同lineage验证。failed/deferred attempt可能在旧bounds外写过新像素；Complete(false)不得继续用旧成功bounds认证后续Hidden，须失效或保守覆盖所有可能区域。
- **未修原因：** GREEN paint谱系逻辑仍是桩，由B3唯一writer实现。当前verified恒0，所以本RED没有因此false positive。

## RED safety / 原顺序与接口核对

- Main1005–1006现有exact `--bar-eraser-offscreen-test`直接return，在配置/单实例/普通GUI初始化前结束。RunEraserAttributeOffscreenTest88–100沿原COM/shared WARP/D2D/embedded fonts和Build/eraser-b输出。新增638–756只用独立96x96 BarUIRendering、local probe/SVG/finite helper，未新建HWND、MouseHook、SendInput、SetCursorPos、系统配置或Office/Host producer。旧Eraser测试原有行为不被本delta安全结论扩大成新F私有bootstrap批准。
- probe先构造后SVG；owned-init与每次draw的TLS scope都在局部关闭，有限publication/observer销毁顺序正确，raw key不被反向解引用。新增读回先验证target尺寸再Map像素；没有新线程/暂停门或跨owner读取活台账。后续F必须init→render happens-before，真正source/Interaction/Render/Window join后撤，不能仅running=false。
- RenderLoop10047在BeginDraw可能写backing前推进mutation/invalidation，真实Clear10058在API前观察。全局frame dirty clip由Rendering235/256旁挂；真实preview nested clip10591/10602、10807亦对应Push/Pop。scope比对实际context，mask专用DC不会作为主backing；只认证轴缩放/平移矩形clip，旋转clip或未知栈为unknown。实际Svg GetTransform取旋转后的有效matrix，最终tarPct/windowAlpha/viewport已记录。
- 其它场景的Layer、opacity、未知绘制不能因首family数字存在就视为已覆盖。Current FinishDrawing没有实现full/hidden认证，对所有required均failed或unverified；不存在RetainedVerified路径。Current frameInvalid_/mappingInvalid_、counter overflow、bitmap语义/尺寸/DPI/epoch匹配等须在GREEN认证中实际拒证，不能只收集字段不参与判定。
- UI.cppm/普通Probe头均在global module fragment引用；新增caller全限定命名空间，Probe.cpp保持普通独立单元无module import。root完整Solution0已编译这些真实caller；Headless可继续链接同一helper，没有第二套“正确算法”。原GetDC/ULW/ReleaseDC/EndDraw与B1 Stamp和成功几何发布没有为B3移动。
- ownerSerial/type约束不代替线程所有权。固定tag family检查与线性mapping避免下标越界，但GREEN还要实际enum/registered ordinal范围、未知旧cache、重复绑定/冲突/257th、所有value与epoch/Reset路径、自然parse与受限raster/upload失败、完整BGRA等价验证。不得把这些尚无断言的范围算本RED通过。

## 真实验证与RED归因

本reviewer只读root产物，没有运行命令：

| 产物/范围 | 直接读取结果 |
| --- | --- |
| `c3b-index-probes-b3-red-debug-arm64-build.status.txt` / 同名log | 完整InkeysRepo.sln Debug|ARM64，ARM64 MSBuild，exit0，159 warnings / 0 errors |
| `ui3-b3-red-debug-arm64-offscreen.status.txt` | 正确 `Build/ARM64/Debug/Inkeys.exe`，pid16476，自然exit1 |
| 同名 `.results.log`（root复制原Build/eraser-b/offscreen-results.log） | 恰B302与B303两FAIL，总failures2；没有B300/B301/旧Eraser前提失败 |
| typed真实首帧输出 | firstRequired1 / firstVerified0 / coverage1 / parse1 / raster1 / upload1 / readyBytes4096 |
| offscreen stderr | PageControlScene failures0；stdout为空 |

B300真实设备/96x96、同owned-init/tag/no-old-cache、真实CreateBitmap/Svg/EndDraw及非空BGRA都成立；B303 bridge合法MainFold flags65→64、stable Snapshot/MarkConsumed/layout前提成立。B301真实first creation计数和初始化parse分段成立，故此轮是有归因能力的paint桩RED，与前轮非法B222不同。

B302/B303要求同真实CacheBitmap/Svg/full clip和late finalize形成已认证paint并完成候选；当前FinishDrawing全保守，两个失败符合冻结桩。B304不重复layout，B305真正partial覆盖拒full，B306既有bitmap真实reuse与clock gate/同一像素，B307sizeof预算都没有失败。**B306仅单像素smoke，不是整张BGRA等价、CPU等价或三轮性能。** 未读取本轮Headless/parked/PptCOM产物时不预记其PASS；它们即使通过也不代替B3 paint负例及F调用链。

## Verification / 交接

- Lint：本reviewer未运行产品lint；本报告编码/CRLF/尾随空白另行静态核。TypeCheck/Build：root当前RED完整Solution0已直接核，不是B3 GREEN。
- Tests：root现有offscreen真实RED exit1且精确仅两预期FAIL，前提有效；本reviewer未执行EXE。无新GUI/F运行许可。
- Next：只让原B3唯一writer按三项最小改动及共享真实负例转GREEN，连同完整full/hidden/unknown/failure/default-off验证另冻结hash，root串行Build/测试后再独立增量review。台账/初始DPI/后写覆盖三个门未闭合前，完整B3保持NEEDS_REVISION。
- Retained、其它family/scene、鼠标光/Hardware/Win7、真实ULW输入与Release三轮性能仍未验证；不扩大本授权或重写UI框架。

---

## 2026-10-01 GREEN_SOURCE 候选增量复审

本节追加于原RED报告，原正文完全保留；追加前SHA256为 `57380013ED466DD89156F35E284BA2B88969E1AF8C4523C81A6300CEABA65861`。仍只read/check及本report写入，无产品自修/Build/运行/Git/递归。实施报告候选SHA256 `821C016D9AF54B1B40AF97C63DC50B60CE587AC18A297CEAFF871DCD4976C480`已匹配；七源实际身份如下。

| GREEN_SOURCE候选 | SHA256 |
| --- | --- |
| `Bar.PresentationProbe.h` | `E2DE55BE5A73C7CB7AC5DCFCFA3488D3E52F54B89D984D79AB8A72A4BAE570F7` |
| `Bar.PresentationProbe.cpp` | `FD276C005491B4BB00DB28FDB4766B118C525273B1AC1A2202E4C9E4DE3201EE` |
| `Bar.UI.cppm` | `55E4A2369884F7F151AFD6196B56D83F9E0D84BF2233D59A2FA02653A7A51205` |
| `Bar.UI.cpp` | `10B29A341D99C731E2533134B1C53AAA71927B93182918BFBEEDBE8F4707CF27` |
| `Bar.Rendering.cpp` | `8A69FCD6E2CFCD9CF5747B890AD22C01D274E67C3E32E10459AEFE40ADEABE2C` |
| `Bar.RenderLoop.cpp` | `AB1E5B8B5CEDC58F90961EEBB1B3632E2F93A71912DA5A59DB718A71CA89474F` |
| `Bar.EraserAttribute.Test.cpp` | `93616F3856FE4E9DCFF2EF94EEE527D24F735B01B82BEFF15B799D9F7D5BD096` |

### 当前增量判决

**NEEDS_REVISION（P1-H1：full clear与随后unknown write的跨帧顺序丢失）。** 原R1/R2实码门已闭合，原R3的SVG→SVG相交/未知覆盖及failed/deferred旧bounds问题已有修补和真实负例；新classifier仍会因“本帧曾full clear”在成功提交时抹掉后来未知写的Hidden lineage。即使当前完整Build/全部现断言绿色，也不能把未覆盖的反例视为通过。root已即时收到精确调用链及最小建议。

当前补丁的现有offscreen入口范围安全仍CLEAR；新增fixed fault只在explicit probe且fault!=None时工作，没有新GUI或系统副作用。F ownerjoin/source/auth/bootstrap仍未由此报告通过。

### Findings (fixed)：原三项的实际修补范围

- **R1 CLOSED于实码/现断言。** h52–62新增record failedSvg/unverifiedSvg/firstReason。cpp1002–1019 StoreOutcome复制所有资源数值与firstTag/reason；788–821 Absorb在NoChange分支前复制同goal资源摘要及失败flags，NoChange保留reusedRevision、timingValid=false、不造finalCommitTicks。B331用实际full/partial/未重画/自然parse/实际upload失败结果经同production publication/observer/Seal/Absorb验证，非复制正确台账算法；整个函数无新publisher并发写plain rows。
- **R2 CLOSED于实际caller与CPU+真实cache组合范围。** cpp898–921在合法actual signature和consumed成功后写candidate.renderDpi。RenderLoop13504–13512用同Wake的versioned tool/实际Submit signature，并与原ApplyDisplayTransition保存的activeDisplayDpi比对；不一致清required validMask，不从zoom反推。10043只用纯candidate.renderDpi，initial publication0也非零，不修改accepted step/revision或默认96。B332/B333 publication0→真CacheBitmap→初始pure commit→合法goal→同bitmap reuse，create/upload不增；B334/B335用显式DPI/原frameZoom真实raster政策及DPI不匹配拒证。它没有启动OS显示切换或normal Bar HWND，source真实性仍靠上述实际caller静态核。
- **原R3直接SVG覆盖 CLOSED于当前写点。** cpp284–317对真实effective dest/transform/clip写域，与其它required/submitted槽求交，相交/unknown撤先前staged proof；unbound/unrequired SVG走UnknownWrite。B336/B337确实在同D2D target画A再画B，分别不相交/相交，无第二套正确判定。cpp400–425 Complete(false)将Hidden lineage未知；成功帧旧bounds未清则保守union。B319–321真D2D已画新位置后显式commit=false，再局部旧bounds Clear看60,60像素仍有alpha并拒Hidden，最后真full backing Clear才恢复。此为模拟ULW failed/deferred语义，不是实机ULW故障。
- **CompletedRevision接口符合所述边界。** h111只有现completedRevision_的acquire load；NoteCompletedGoal仍经真实candidate+资源+软件事务确认才写receipt。B303前0/完成后对应revision，B331 partial/missing/API失败不发布。getter不读活plain ledger，也不证明F source/Interaction/Window生命周期已真实join。

### Findings (not fixed)：P1-H1，早先full clear错误覆盖后来unknown write

**File/symbol：** Probe.cpp `ObserveClear`258–275、`ObserveUnknownWrite`277–282、`FinishDrawing`352–355、`CompleteAttempt`400–410。

当前 `fullBackingCleared_ |= Contains(clearBounds, backing)` 表示本帧任意时刻曾清全图。后来UnknownWrite会令slot.overwritten/hiddenLineageUnknown=true，却没有撤销这个full-clear恢复资格；Complete(true)409只看ever bool，再把所有slot.hiddenLineageUnknown清false。下一BeginFrame又清overwritten，因此未知新区域不再被保护。现B316只在当帧拒证，B319–321只覆盖commit=false；二者不能覆盖成功事务后的顺序反例。

具体真实API反例（不需新GUI）：

1. A已真实full draw并成功，oldBounds已知。
2. 次帧隐藏A；先真full Clear，再用标为UnknownWrite的真实API在A旧bounds外画回同一bitmap/旧语义。此帧资源应Unverified，但EndDraw/软件事务可成功；当前Complete(true)会因早先full clear清掉unknown lineage。
3. 再次帧仅真Clear原小bounds并提交成功，未知新位置仍有非零alpha。当前classifier允许HiddenExpected，因为oldBounds cleared而unknown标记已被抹掉；这是错误认证。

**最小建议：** 用最后未知写/完整清除的顺序维持恢复资格。UnknownWrite应撤销先前full-clear恢复资格，只有真正位于其后的full backing Clear才能重建；不明写域的SVG同样必须传播Unknown lineage，而不只是当帧overwritten。若采用mutation serial比较，可固定纯数值，不增clock/资源/Request/dirty/等待。成功提交本身不能使未知写变已知。

**必需验收：** 新真实两帧/三帧offscreen负例按上面的Clear→unknown draw→successful completion→小Clear顺序，读取旧bounds外残留像素并断言Hidden仍未证；再真full backing Clear在最后unknown write之后才允许恢复。保留反向正确序（unknown→最后fullClear）与现B319–321、B336/337。只在同共享production probe/API入口加反例，不在test复制正确lineage算法。实际normal RenderLoop保持原顺序和API次数。

未修原因：产品源/B3接口归root指定唯一writer，本reviewer只追加report。此项只改变观察资格；不应靠改正常绘制或强制全脏来换取PASS。

### classifier/default-null/新增测试实际核对

- FinishDrawing327–398现在实际按semanticKnown/valueRevision/color mask+RGB、epoch/surface/DPI、有限requested/pixel尺寸、quality、final opacity/windowAlpha、coverage及后写标记判定；仅当前DrawnVerified或已知旧bounds的当前Clear HiddenExpected，仍无RetainedVerified。纯NoDraw/partial/empty/unknown全部保持未证；但H1的跨帧unknown次序必须修补。
- UI.cppm/Rendering.cpp与RED身份相同；原nullable scope、timer门、真实DrawBitmap/needUpdate/质量fallback等接缝保持。UI.cpp只给explicit probe默认None的两类fault：实际raster后拒其result（synthetic consumer fault）与固定小bitmap非法alpha交真正CreateBitmap得HRESULT（injected参数、真实API失败）。自然坏SVG parse为真实parse失败。三者不能写成自然OOM/driver failure。
- 新B310/311/312有实际empty/absent/nested clip和原NoRetained/no-redraw；B313真实rotation/translation，B314 half opacity，B315 windowAlpha0；B316 actual unknown write后本帧拒证。当前clip reader一次数值scope，不新查GPU/增读钟。
- B325 ReadSvgProofPixels确实Map完整96x96目标、按真实pitch逐行复制，on/off整张BGRA逐字相等且期望长度非零；不是原B306单像素冒充全图。读回不在资源timer内，不能升级CPU/GPU性能等价。
- B326实际null scope draw并检查clockReads/drawSubmit不增加；normal RenderFrame clocks位仍沿B1详细位。scope/TLS/probe/typed cache局部寿命保持，没有新frame Request、fullDirty、Flush、动画/输入规则更改。初始化/Render所有者的handoff与真join依旧留给F actual diff。
- B338创建独立WARP/D2D epoch并明确Reset局部SVG原缓存再换context；B339真正无observer旧bitmap绑定/绘制仍Unknown；B340/341真实256对象/合法257th、重复/冲突/unknown ordinal/巨大tag先拒而非索引。新增svg enum尾值31有static_assert。预算按新sizeof重新计算；F后续所有source sidecar仍要计真实sizeof，不能把目前预留视为最终runner预算通过。

### 已直接读取的候选动态证据

| 当前候选产物 | 直接读取结果与范围 |
| --- | --- |
| `c3b-io-diagnostics-b3-green-debug-arm64-build.status.txt` / log | 完整Solution Debug|ARM64 exit0，155 warnings/0 errors |
| `ui3-b3-green-candidate-debug-arm64-offscreen.status.txt` / results | pid24948自然exit0，现有全部断言failures0；firstRequired1/firstVerified1/coverage1/parse-raster-upload各1/ready4096 |
| same offscreen sizeof | record248/candidate248/producer107208/publication127448/observer127904 |
| `c3b-io-diagnostics-b3-green-debug-arm64-headless.*` | pid31860自然0，stderr空，216layouts/animation PASS |
| 同前缀 parked | pid29972自然0，旧五/Session/ContentProof PASS，仅辅助回归 |
| 同前缀 PptCOM | pid1784自然0，descriptor/session-owner PASS，仅辅助回归 |

这些是当前候选真实绿色，**不关闭H1未覆盖的源问题**。原RED 16476自然1/恰B302+B303/总2及573800原报告完整保留。无本reviewer运行记录，无新F GUI许可；真实Bar/两scene/ULW输入、三轮Release、CPU/GPU/光学、Win7/HC-H2仍未验证。

### 增量交接

R1/R2和原直接SVG覆盖已实际核对；H1由原B3 writer做最小修补+同真实API归因负例，再冻结新hash由root串行验收。当前完整B3维持NEEDS_REVISION。报告仍只有本文件追加；其它源与测试只读。
