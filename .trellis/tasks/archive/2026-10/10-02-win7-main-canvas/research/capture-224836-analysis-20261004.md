# Research: Win7 20261003-224836-752 完整采集分析

- Query: 本次 Win7 鼠标、RTS、controller、激光/橡皮与 ULW 原始源像素是否能定位不出墨？GPU 自检是否实际运行？
- Scope: internal；原始采集只读、生产源码只读，仅写本报告。
- Date: 2026-10-04

## Findings

### 1. 文件与测试身份

采集目录 `G:/Inkeys/Inkeys-Win7-Gpu-Probe-x64-Release/Inkeys-Win7-Gpu-Probe-x64-Release/20261003-224836-752/`，已完整读取四个文件。以下行号各自从 1 开始：

| 文件 | 字节 / 行 | SHA-256 | 描述 |
| --- | --- | --- | --- |
| `identity.txt` | 351 / 8 | `677AC16D1A4A3CBD1D923C79E857C0F9281E58518525C45FC078F3077C8B89DA` | EXE 身份、Win7 SP1、PowerShell 2.0、采集结果 |
| `console.stdout.txt` | 3030735 / 16245 | `49D062EAF3BF55B053457F5DB2EDE30CB9F81225FBB2826659861E89F7F78B4E` | 完整 Draw3 输入、Cursor GPU、ULW 像素日志 |
| `console.stderr.txt` | 91757 / 309 | `9B2CCFD107E82E9CF163EE354F7B9567C476378F15177315E8A7AA6DE5F5E586` | Surface 状态与 controller 输入/橡皮快照 |
| `idt1791038917360.log` | 39323 / 27 | `33CA9D55DF15FC0CADAB2ACBC20DC13AE8F44239569BEFE675E6A7519E80C6BE` | 启动、门控、退出与 UI3 诊断 |

`identity.txt:2` 的 EXE SHA 为 `06CD58C7E1E1F01042D489902457C18FF3E32DAF843197386AE7BB7B283DF8AE`，与已交付 GPU probe 候选一致。`:3-5` 为 Windows NT 6.1.7601 SP1、启动 `2026-10-03T22:48:37.0798662+08:00`、PS2；`:7-8` 正常应用退出 0，结束 `22:50:01.4086679+08:00`。stdout:1 标识 `draw3-Oct 3 2026T16:15:40 arch=x64 msvc=1944`，本身不包含 Git SHA。

**独立 GPU 自检没有启动：** identity:6 为 `pixel-test-error=无法使用指定的命名参数解析参数集。`，目录不存在 `pixel-test.stdout.txt` / `pixel-test.stderr.txt`，没有任何 `[Draw3PixelTest]` 结果。不能把此次完整应用测试当成 Clear / 正式 Draw 对照已执行，也不能把应用 `exit=0` 当成自检通过。

### 2. 输入已被接收和消费

- stdout 中 `mouse event` 共 **2508** 条，全为 `accepted=1 reason=accepted-mouse`；首末分别 stdout:57 / :16224。此处仅证明 Mouse Cursor 样本接受，不能单独证明绘制 contact。
- `rts-input` 共 **122** 条：Down arrival 25、Down published 25、Up arrival 25、Up published 25、Packets published 22（Packets 有每秒节流）。所有 published 记录 `deviceKnown=1 device=2 decoded=1 published=1`，对应 MouseLeft。没有失败 reason、diagnostic overflow/drop 记录；arrival 的 published=0 是尚未发布阶段，不是拒收。
- 首个 RTS Down 为 stdout:311-313，tick **56405905**，位置 **654,869**；首个 Up 为 stdout:738-739。对应 stderr:15 的 controller `event=begin inputContact=1 tool=0/0 device=MouseLeft source=Mouse contactGen=1`，位置 **654.009,868.989**，有独立消费证据。
- stderr 共 95 条 `[EraserInput]` 快照；有非零代次 **1..24** 的消费证据。按首次出现汇总：Pen0 代次1..4（stderr:15/24/34/43）、HardPen1 代次5..10（:57/61/69/84/96/101）、Highlighter2 代次11..14（:114/121/129/136）、Laser4 代次15..17（:144/152/161）、SolidLine5 代次18..20（:178/185/192）、Eraser3 代次21..24（:207/231/249/262）。各记录均 MouseLeft / Mouse，并有合法 canvas 坐标。
- controller 的 `event=begin` 实际有21条，其中一条是 gen0 hover/preview，不能机械等同21次物理 Down。没有观察到 gen25；快照的活动/预览边沿及250ms节流不是逐 Down 审计（`Draw3.Host.cpp:959-965`）。不能从缺一条快照宣称一个短橡皮接触丢失；最后5个 RTS Down/Up 均已发布。stderr:270 又出现 gen1 的橡皮 runtime，也不能当成之前 Pen gen1 继续。
- stdout:16236-16237 为 clear 前 auto-save capture/commit：19 strokes、34739 bytes。这支持存在 document 操作，不能替代 GPU 像素证据。

因此，**本次鼠标复现不是因为 Win7 缺少新 Pointer/Pen API，或 RTS 没有收到 contact 才不出墨。** 真实数位笔/Touch 未测试，不能扩大到所有硬件输入。

### 3. ULW 接受了全透明的原始读回源

全部 **39** 条 `ulw-pixels` 都是 `api=success error=0`，且 `sourceAlphaNonzero=0 sourceRgbNonzero=0 sourceMaxBgra=(0,0,0,0)`。其中 Primary Drawpad **37** 条，Selection DrawpadPresentation **2** 条。

37条 Primary 的最终DIB都只有 alpha=1 命中底层：`finalDibAlphaNonzero=pixels`、RGB全零、最大 BGRA `(0,0,0,1)`；两条 Selection 最终DIB全透明。因此应用阻挡桌面输入与屏幕无笔迹可同时发生，不能将 alpha=1 命中底层当成墨迹。

**橡皮同帧分界非常明确：**

| stdout 行 | tick | 证据 |
| --- | --- | --- |
| 14607-14610 | 56460693 | Mouse 样本819,955接受，owner=2；系统光标 hidden=1 |
| 14611 | 56460693 | cursor-gpu upload-success，Map 已完成，hr=0，resources均1，removed=0 |
| 14612-14613 | 56460693 | before-draw / after-draw-issued，实际 viewport与目标/绑定全部匹配 |
| 14614 | 56460693 | dirty `(801,937,837,973)`，1296源像素全零，ULW成功，最终DIB仅alpha1 |
| 14615-14617 | 56460693 | Eraser tool3、primary1；白色32×32、opacity0.5、fill1、位置819,955；present success1、visuals1 |

绘制线程的队列日志稍后flush，故视觉日志排在 ULW 输出后；应按同 tick/dirty/位置关联，不按文本相邻先后推断实际绘制顺序。

**本次激光笔也确实被请求：** tool4 frame 共90条，`laser-tip source=primary` 共 **68** 条。首次 stdout:8370-8373，tick **56441489**，位置1007,830、radius2.5、opacity1、color `0xff1000ff`，dirty `(997,820,1017,840)` 的400源像素全零，同时 `present success=1 laserTips=1`。另一个精确对应 stdout:8755-8758，tick **56442643**，红色笔尖1289,862、dirty `(1279,852,1325,872)`，920源像素仍全零。不是只采到启动空白帧，也不是没有选过 Laser。

### 4. Cursor GPU 状态排除了部分旧假设，尚未证明 shader 产出

15条 cursor-gpu 是 **5组各3条**：upload-success / before-draw / after-draw-issued。tick为56460693、56461707、56462721、56463735、56464749；对应 stdout:14611-14613、14898-14900、15220-15222、15420-15422、16036-16038。所有 hr、removed 均0，required resources均1。

**只有10条 `bindings=1` 做了实际查询：** 都 `vpCount=1 vp=(0,0,1920,1080,0,1)`，constants1920×1080；RTV资源与backbuffer均 `0000000004DA8750`，canonical identity `targetMatch=1`；VS/PS、VS/PS b0、VS t0的bound及match均1。没有 sampled Map失败、missing-resources、invalid-appearance、设备移除、viewport/目标/绑定不匹配。

另外5条 upload-success 的 `bindings=0 vpCount=0 targetMatch=0 bound=0 match=0` 明确表示此阶段**没有查询绑定**；不能误报缺失viewport或shader。`hr=0`是成功上传或围绕 Draw 的诊断常量，不是 Draw API 返回值；Draw无HRESULT，`after-draw-issued`只证明命令发出。

最小剩余范围是**实际绘制管线/生产混合或shader执行/写入，到 GPU→staging 拷贝读回之间**。日志的source是 staging Map 后、CPU命中底层前的源字节，并非GPU原纹理的直接统计。缺失Clear/readback与正式Draw/readback自检，仍不能定出唯一GPU原因；USER32 dirty ULW不是首要解释，因为输入给它的数据已透明。

### 5. 启动与窗口证据仍正常

stdout:7-11：Hardware `0x887a0004` 后 WARP成功、FL11_0；:16-21 waitable `0x887a0001` 后普通 FLIP_SEQUENTIAL启用，1920×1080、B8G8R8A8、2 buffers、flags0。

stderr:11 显隐应用成功：主窗valid1、visible1、topmost1、layered1、transparent0、bounds1920×1080、error0；辅助窗:12 hidden/transparent1。退出前/切回selection的隐藏记录不证明书写期间隐藏。IDT:19-20 在22:49:47有selection-output gate等待及30ms后ready，不能据短暂切换门控解释此前所有透明采样。

### Code patterns / files found

- `Draw3.ContactInput.cppm:26`：MouseLeft=2，RTS诊断设备值的定义。
- `Draw3.WindowControl.cppm:29`：tool0 Pen、1 HardPen、2 Highlighter、3 Eraser、4 Laser、5 SolidLine；不能将HardPen1误称荧光笔。
- `Draw3.RealtimeStylus.cpp:1529`：RecordInputDiagnostic区分arrival/published；`:1544` Packets每秒限频。
- `Draw3.DrawingController.cpp:9949` / `:9972`：frame和laser-tip视觉请求；`:10069` runtime handle generation用于controller诊断，`:10095` frameSeconds用mouseVisualSeconds。
- `Draw3.Host.cpp:959-965`：输入/预览活动边沿及250ms快照节流，非逐contact完整审计。
- `Draw3.RendererPrimitives.cpp:245`：首个/每秒GPU诊断；`:281` 仅inspectBindings阶段查询；`:343` ink Map；`:354` constants Map；`:389-392` Draw前后发出命令；`:376-388`生产目标与shader绑定。
- `Draw3.Renderer.cpp:311-314`：GetBuffer(0)取得backbuffer并由同一texture创建RTV。
- `Draw3.TransparentPresentation.cpp:513-544`：GPU脏区Copy到staging、Map及源/DIB统计；`:573-575` ULW后记录API结果。统计发生在Copy/Map后的CPU字节，不能替代GPU原texture证明。
- `research/Collect-Win7.ps1`：此次交付版probe启动同时使用WindowStyle Hidden与stdout/stderr重定向；PS2参数集失败发生于此，正常应用采集随后继续。本报告只确认日志错误与未启动，不修改脚本；具体兼容修复由独立实施/检查负责。

### Related specs / external references

已读 `.trellis/workflow.md`、本任务prd/design/implement、`.trellis/spec/native/index.md`、`native-desktop/index.md`、`native-desktop/input-and-ink.md`、`shaders/index.md`。重点合同为 Input and Ink 的输入归一化、Cursor诊断及“Win7 GPU透明读回分界诊断”：API成功不能替代像素，混合对照不能直接当修复，本机WARP不能冒充Win7验收。

没有新增外部API支持结论；本报告依据本次完整原始采集与产品源码。Pointer API的既有支持讨论不是本次Mouse contact失败证据。日志与源码事实不需要重新下载外部资料。

## Caveats / Not Found

- 本次已经足够排除鼠标输入/RTS完全缺席、 sampled Cursor Map失败与viewport/RTV/VS/PS/CB/SRV绑定缺失；仍缺同一Win7上的独立Clear/Draw/混合对照及pipeline stats。
- GPU运行期诊断只采了5个可见橡皮请求，不覆盖每帧、激光计算粒子、所有笔画上传。不能把sampled正常扩大为所有绘制必然正常。
- 未记录原shader输入缓冲区/CB实际读回内容、正式blend/raster/depth状态的动态核验，也没有证明所有PS invocation/输出。资源绑定identity匹配不等于内容有效。
- stderr frameSeconds使用另一时间来源，不能未经换算把它乘1000与stdout GetTickCount64 tick直接比较。跨流以位置、tool、generation、顺序关联；同stdout GPU/像素/视觉可按tick精确关联。
- 日志无输入诊断drop/overflow记录，不等于无限完整逐Packets审计；Packets及controller快照有明确节流。
- 现场G:文件未更改；未启动EXE、GUI、自检或构建，未修改任何源码/脚本/spec、未做Git操作。
