# UI3 SVG proof H1 独立增量代码复审

Active task: `.trellis/tasks/09-27-integration-and-release-check`

日期：2026-10-01。独立 reviewer 仅只读实码、规范、原 review/implementation 与 Root 验证产物；唯一写本报告。不是源码 writer，未修改产品/测试/spec/工程/账本/其他报告，未 build/run/Git/GUI/递归。

## 当前判决

**SCOPED_STATIC_GREEN；H1 现有离屏实际门 GREEN。** 原 `ui3-svg-proof-code-review.md` 末 P1-H1 的早先 full Clear 追认后来 unknown write 问题已由 writer 最小修补，实码顺序与 H100/H101/H102 真实 D2D 反例一致。无本增量待修阻断。

Root 新完整 Debug|ARM64 构建0与离屏 failures0已直接读取；以下只覆盖 H1 与既有离屏/辅助回归。原 BBC6…18A 报告及所有 RED 证据保持不动。Root 已确认的 F067 design `0386…` 批准继续保留，本报告不重新审查或撤销该记录，也不据此增加 F 接口、source/helper 或 GUI 权限。

## 实际冻结身份与最小 delta

| 文件（Bar目录） | bytes | 当前 SHA256 |
| --- | ---: | --- |
| Bar.PresentationProbe.cpp | 64632 | `EB99F2A10C13B337408382C567C33FA8E898514FC53B2F3EC8BB2C4D3C2FF4B9` |
| Bar.PresentationProbe.h | 25061 | `E2DE55BE5A73C7CB7AC5DCFCFA3488D3E52F54B89D984D79AB8A72A4BAE570F7` |
| Bar.EraserAttribute.Test.cpp | 105211 | `12731E2341AC776FA706AF820C672787567D7A70755435F5530899C1DD9B975F` |
| Bar.UI.cppm | 15345 | `55E4A2369884F7F151AFD6196B56D83F9E0D84BF2233D59A2FA02653A7A51205` |
| Bar.UI.cpp | 37676 | `10B29A341D99C731E2533134B1C53AAA71927B93182918BFBEEDBE8F4707CF27` |
| Bar.Rendering.cpp | 119586 | `8A69FCD6E2CFCD9CF5747B890AD22C01D274E67C3E32E10459AEFE40ADEABE2C` |
| Bar.RenderLoop.cpp | 632036 | `AB1E5B8B5CEDC58F90961EEBB1B3632E2F93A71912DA5A59DB718A71CA89474F` |

原独立 review 当前 SHA256 仍是 `BBC6FCCFF8E23F8235998EDD284D5771E031E700E63A810B11E87DFCFD1BB18A`；实施报告当前 `60CE9D2B6423FEC5347D5BFDBD60C9F4E8EFD67E11C5CA8BE9B8EEAA44ABCD3D`。

本 reviewer 在内存中只移除 `ObserveUnknownWrite` 的新中文注释与 `fullBackingCleared_ = false`，以及 `ObserveDraw` 新增的 else 调用/见证更新行，所得完整 cpp 精确恢复64421bytes、SHA256 `FD276C005491B4BB00DB28FDB4766B118C525273B1AC1A2202E4C9E4DE3201EE`。没有写入该重建结果。由此确认本轮代码变化只有所述三条语句和中文注释；其余台账、DPI、timer与 classifier字节未动。当前 cpp bare LF=0、无BOM，测试仍保留原UTF-8 BOM/CRLF。

## Findings (fixed)：P1-H1 由 writer 修复，本 reviewer 核对

### 高优先级 H1：成功事务不能抹掉后来 unknown write 的谱系

- **File / symbol：** `Bar.PresentationProbe.cpp:277–285 ObserveUnknownWrite`。仍先检查当前 drawing/context；随后新赋 `fullBackingCleared_=false`，再递增纯 mutation serial，并对当前 required槽置 overwritten/hiddenLineageUnknown。未知写发生后，原早先 full Clear 不再有恢复资格；原 `CompleteAttempt(true):412` 只能保留该未知谱系，不能凭成功提交清掉它。
- **tracked unknown 同源：** `ObserveDraw:288–311` 的未绑定/非required槽 `:290` 原本已调用同一 UnknownWrite；tracked SVG 的 `writeKnown=false` 现在也调用同一入口，并将当前 draw.bufferMutationSerial 更新到该次纯值 mutation。因而缺失clip、不明/无效写域不能只留当帧后写标记，却仍继承旧 full-clear资格。它额外递增的是 mutation见证，未增加 Draw/API计数、时钟或资源操作。
- **恢复顺序保留：** `ObserveClear:258–275` 仍由当前有效clip计算实际 clearBounds 与 backing containment，仅真正 full backing Clear 才把 fullBackingCleared_置真。Unknown→fullClear→成功事务可在 `CompleteAttempt:412` 恢复谱系；Unknown→partialClear 无法恢复。Unknown之后的fullClear也不会抹掉本帧的 overwritten拒证，保持当帧保守，下一帧才可凭已知小域clear完成Hidden。
- **失败/deferred 保留：** `CompleteAttempt(false):405–407` 对全部旧槽置未知与Unverified，跳过成功恢复；`:428` 关闭drawing/context。`BeginFrame:202–209` 清本帧标志但不清 hiddenLineageUnknown/oldBounds；没有因为下一帧开始而丢未知谱系。

### 实际写点与剩余分类

| 真实路径 | H1 处理结果 |
| --- | --- |
| 对当前 backing 的直接 unknown写 | `ObserveUi3SvgUnknownWrite:464` 经nullable TLS调用同一helper；`Bar.Rendering.cpp` 的 PNG/Word/Shape等既有写点、RenderLoop的既有直接写点均沿此入口。 |
| SVG未绑定或本帧非required | `ObserveDraw:290` 调用同一helper，撤销全局本帧full-clear恢复资格，并保守污染当前required证据。此处不宣称认证未required的历史槽。 |
| tracked SVG写域未知 | `:309–311` 不再绕开UnknownWrite；当前及其它required槽都失效，当前draw见证取更新后的mutation。 |
| tracked SVG写域已知 | 原clip交集及相交判断 `:309–317` 保持；已知partial不会被新else误当full证明，相交仍撤先前staged proof，可证不相交保持。 |
| context不匹配或drawing已结束 | 原guard返回，不污染另一个context；nullable观察器也不因本修补在正常模式变成非null。 |
| Hidden分类 | `FinishDrawing:347–358` 仍先拒overwrite，再要求已知旧bounds、实际clear覆盖及有效谱系；没有新增RetainedVerified。 |

`Bar.Rendering.cpp:2834–2857` 仍在同一实际 DrawBitmap 之前采集effective transform/dest/opacity并调用ObserveDraw；原D2D DrawBitmap和质量路径不变。`RenderLoop:10036–10060` 保留纯candidate.renderDpi、原BeginDraw/dirty clip/真实Clear顺序；`:12742` 原FinishDrawing进入resource finalize，`:12881–12896` 仍由实际presentation completion调用CompleteAttempt。没有强制full dirty、新Request、Flush或补帧换取证明。

## H100 / H101 / H102 真实因果回归

测试 `Bar.EraserAttribute.Test.cpp:654–735` 使用真实WARP/D2D 96×96 target、原CacheBitmap/Svg与成功EndDraw；不是重写正确classifier的纯mock。H1段 `:869–910` GREEN与RED测试hash完全一致。

1. **H100前提：** 真SVG seed full draw成功且cache存在，oldBounds来自当前16..48绘制加原AA边界14..50。隐藏该SVG后，`unknownReplayFrame(false)` 先调用原观察clear与真实D2D Clear，再经UnknownWrite标记后真正DrawBitmap到64..80外域；FinishDrawing仍unverified、实际EndDraw成功并Complete(true)。读回72,72非零alpha；下一帧真实Clear0..52后外域像素仍非零。每个前提各自有会计失败，不能用未执行H101充PASS。
2. **H101拒证：** 在上述实际前提全部成立时，断言 hostileHidden.verified==0 且 use不是HiddenExpected。H1 RED真实只此断言失败；新GREEN没有改变前提、色、区域或期望。
3. **H102反向序：** 重新seed，然后先实际Unknown DrawBitmap，再最后真实full Clear并成功完成；下一帧小Clear应verified1，72,72 alpha为0。它确保修补没有把正确恢复永久封死。
4. 既有 B319–321 failed/deferred/partial、B336/B337相交/不相交、B325整96×96 BGRA on/off、B326 default-null、B331台账与B332–335初始DPI/reuse断言仍原hash，未弱化。

## 已直接读取的 Root 动态证据

以下均由 Root 执行，本 reviewer 没有运行EXE。前缀位于 `TestResults/release-hardening/`。

| 产物 / 进程 | 实际结果与范围 |
| --- | --- |
| ui3-b3-h1-red-debug-arm64-build.status.txt | 完整主Solution Debug|ARM64 exit0，原H1生产行为未修。 |
| 同RED offscreen.status.txt / results.log；pid16204 | 自然exit1；raw精确仅H101 FAIL、failures1，H100外域前提和H102正确反向序未失败。 |
| ui3-b3-h1-green-debug-arm64-build.status.txt / build.log | 完整InkeysRepo.sln Debug|ARM64 exit0，4warnings/0errors，25.80秒。build/test分开取证。 |
| 同GREEN offscreen.status.txt / results.log；pid18304 | `--bar-eraser-offscreen-test` 自然exit0、failures0；结合未改测试与各H100前提，H101拒证及H102恢复均真实通过。 |
| 同GREEN headless.status.txt / stdout；pid21724 | `--no-window` 自然exit0，216layouts/failures0、animation correctness PASS，stderr空。 |
| 同GREEN parked.status.txt；pid30344 | `--draw3-parked-desktop-exit-test` 自然exit0，旧退出/存储辅助回归；stderr624B，未冒称它是C3 fresh reader或保存全矩阵证明。 |
| 同GREEN pptcom.status.txt / stdout；pid13644 | 自然exit0，descriptor ownership与session/owner contracts PASS，stderr空；不代表真实Office GUI。 |

当前直接散列 `Build/ARM64/Debug/Inkeys.exe` 为 `C84BEC789B602D72F4D5B36CEA09FFA997A47CDCD41D0DBA5CBFBBBDA179763B`，与Root给出的本轮PE一致。离屏输出实际sizeof仍为record248/candidate248/producer107208/publication127448/observer127904；firstRequired1/firstVerified1/coverage1，parse/raster/upload各1、readyBytes4096。H1没有布局/ABI变动，不由这些数值推导F完整runner预算或性能收益。

## Findings (not fixed) / 未验证边界

本增量没有待修代码问题；未复审和未验证的范围保持原门禁：完整F auth/bootstrap/source/owner真join、全主栏与两scene的实际ULW输入/目标证据、其它family/Retained语义、Release三轮CPU/GPU/光学指标、HC-H2及Win7。现有UnknownWrite保守拒证可以降低候选证据覆盖率；不得修改正常绘制、扩大dirty或绕过required分母来换取F通过。

H1源问题闭合不等于原整个任务完成，不提供新的F GUI运行许可证，也不升级既有C3/UInk/真实RTS callback静止边界。F067既有设计批准按Root记录保留，后续实际source增量仍分别审查。

## Verification

- 静态：PASS于本H1窄增量；实际guard/unknown四类入口、Clear先后序、partial/failure/deferred、正常写点、冻结身份与内存精确旧hash恢复均已核。
- Lint：未执行独立lint（本dispatch禁止run）；report与cpp编码/EOL已静态核。
- TypeCheck / Build：Root完整Debug|ARM64 PASS（0errors），本reviewer未build。
- Tests：Root本轮H1 RED1→GREEN0有效，既有offscreen、Headless、parked、PptCOM均退出0；只在上述实测范围记录GREEN。