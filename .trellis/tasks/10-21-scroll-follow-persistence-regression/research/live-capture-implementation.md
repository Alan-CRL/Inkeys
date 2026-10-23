# 正式 FreezeCapture 实屏夹具与本轮停止记录（2026-10-22）

> 最新状态：再次人工解锁后，2026-10-22 正常正式生命周期矩阵已实际 **PASS / exit0 / 41.388s**：20轮共享原Mag pair、5秒静止、立即取消、真实在途Read撤销、原Freeze互斥/Reveal/Conceal后再次capture全部通过。静止实测约6.818fps，详文末最终续测。下文解锁前17NearBlack、两次安全停止、未构建修正等段落均保留为历史，不覆盖当前实际结果；产品系统ACK/可见跟随仍由主会话后续验证。

基线 `feature/move / 10abcac3`。本任务 trellis-implement 直接实施，没有派发子代理、commit 或 push。用户本轮允许创建专用窗口、原生消息操作和截图，明确禁止 Computer Use；未调用 Computer Use/CUA。17:39 +08:00 源码冻结，最后仅运行严格坏 argv 与静态检查；所有可见测试已停止。后续交独立 check，不结束父任务。

**当前不能确认正式连续捕获成功，也不能认定是 PrintWindow 或 Mag 跨线程更新故障。** 一次真实正式服务运行收到了 17 次 NearBlack、0 个有效 F0；当时只验证了蓝色中心和图案已绘制，没有完整验证专用源在屏幕上可见。随后用于区分读回线程、owner GDI flush 与 owner 重绘的实验，在到达捕获前被系统锁屏 backstop 全屏遮挡，因此这些对照没有实际执行。安全界面不能绕过，本轮在此停止。

## 改动与生产边界

只改以下三个源文件：

| 文件 | 内容 |
|---|---|
| `Inkeys/IdtMagnification.cpp` | 新 `RunFreezeCaptureLiveTest(bool diagnosticsOnly)`；复用现有专用图案、原 WindowService、原 Freeze Mag pair、原 MagnifierThread 与正式 FreezeCapture Start/Stop/GetSnapshot；增加宏内 raw Read 驻留及 owner 诊断消息、只读桌面/窗口/安全层可见性检查 |
| `Inkeys/IdtMagnification.h` | 声明专测入口，默认 diagnosticsOnly=false |
| `Inkeys/IdtMain.cpp` | 最早全 argv 严格扫描加入 `--freeze-capture-live-test` 和 `--freeze-capture-live-probe`；harness 关闭同样早退 2；坏参数在配置、Display、DPI 与 HWND 初始化前返回 |

`git diff --numstat` 分别为 `502/0`、`3/0`、`4/2`。新增部分是专用实测夹具及诊断；没有调整工程、SDK、依赖、保存、Tracker、Draw3、正式 UI 流程或任何导航/渲染参数。原 `MagInitialize`、窗口创建、readyPromise、owner 消息循环、`MagUninitialize` 与实际 producer 线程设计保持原样。没有第二套 Mag/WGC，也没有设置捕获排除属性；未运行旧 owner 定时读回夹具来替代正式服务验收。

正式服务仍在原 MagnifierThread 上串行 Update/PrintWindow(child, dc, 2)/调用线程 GdiFlush。只在 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` 内增加计时及 raw 观察调用；未启用 probe 时指针为空，观察器立即返回。owner 的额外消息处理也只在此宏内，而且须显式 probe CLI、原 worker 的 PW 已经返回并持有借用时才工作，不开放产品 owner RPC/API。

测试窗口采用当前原 WindowService 的真实角色 owner 链。第一次夹具缺失 Drawpad owner，Bar Show 成功但置顶失败；此后仅在新夹具补全隐藏的 Freeze/DrawpadPresentation/Drawpad 占位角色，不改生产 WindowService。原 Mag Host/Child 仍通过原 callbacks 创建。

## 夹具实际契约及未执行的诊断

- `--freeze-capture-live-test` 不读取用户配置、文档，不执行普通启动；专用 root 为 `Build/ScrollFollow/live-<pid>-<tick>`，逐组件拒绝 reparse，目录 create-new、日志 CREATE_NEW。自建全屏 9×7 变化图案和同进程蓝色 Bar 覆盖层；source 不在原过滤列表中，Bar 在其中。
- 测试先检查线程/input desktop 名称、窗口状态，再验证屏幕专用内容。最终版本要求 54 个覆盖层外图案采样点和 9 个蓝色点全部符合 GT，另要求 `(64,64,256,192)` 专用屏幕 ROI 每个像素正确后才允许保存此 ROI、启动正式服务。
- 正式 callback 对 63 个网格中心匹配已知 phase，记录 session/sequence、owner/worker/callback TID、采样与读回时间、painted/matched、旧帧、质量、统计；相同有效静止帧允许接受。每个 phase 另验证中心 256×192 ROI 的每个像素（容差 3）才保存；不把 63 点采样称为全屏逐像素验证。首次 accepted F0 和 unmatched 必须保留原始记录。
- 原计划以 4 个 phase、每 phase 至少 3 帧且取得验证 ROI 作最小门槛；本轮没有取得任何 accepted callback，因此未通过该门槛，未输出 phase PNG。
- `--freeze-capture-live-probe` 仅诊断，最终主动返回失败而不冒充正式门槛通过。raw hook 仅在原 worker PW 真返回后停留，记录原始 worker DIB 的质量与 PW+GdiFlush 时间。先 owner **仅 GdiFlush** 并原地复查同一 worker DIB；再 owner **仅 PrintWindow + owner GdiFlush** 到另一个 DIB，不先改 Source/transform/重绘；之后分别 owner Redraw、owner Source+Redraw 再读取。该顺序用于区分调用线程 GDI 批次、读回线程与更新线程。
- 驻留只由本 CLI 显式开启，事件等待 3 秒；owner 读回持借用 mutex，退出先放行/真实 join，再释放 callback context、DIB 与窗口。单次同步 PW 尚未返回时不强杀线程或提前释放。专用进程另有 90 秒最终失败截止；该截止不证明生产 PW 有界，也不计入正常性能。
- **本轮所有 probe 运行都在屏幕可见性门槛前停止，raw worker hook、owner-only Flush、owner-only PW、owner Redraw、owner Source+Redraw 均无实测结果。** 没有使用 scaling callback，原正式运行 `MagGetImageScalingCallback` 查询为空。

## 逐次实际运行

下表的 `live-*-test.log/.json/-stderr.log` 均位于 `Build/ScrollFollow/`；各 stdout 首行同时给出私有目录，目录内 `result.log` 保留原始记录。所有运行 exit1，专用窗口正常自动关闭，最后确认没有遗留 Inkeys/MSBuild 进程。

| 日志前缀 | PID / 私有目录 | 耗时 | 实际到达位置与结论 |
|---|---|---:|---|
| `live-first-test` | 65200 / `live-65200-207684468` | 1.655 s | 原 Mag 初始化/创建成功；Bar Show 成功、Topmost 失败，缺夹具 owner 链；未运行正式 Start |
| `live-owner-chain-test` | 16424 / `live-16424-207981343` | 5.986 s | 修正夹具 owner 后 Show/Topmost 成功；正式 Start 返回 Started/session1，17 次 rejected NearBlack，accepted0；原线程正常退出和原 owner Uninitialize 成功 |
| `live-worker-owner-probe-test` | 77424 / `live-77424-208604890` | 4.716 s | 蓝色中心屏幕门失败；未 Start，未 probe |
| `live-isolated-probe-test` | 82020 / `live-82020-208913578` | 4.589 s | 自身 source/Bar visible/topmost、位置及相对 Z 顺序正确；中心命中外部窗口，未 Start/probe |
| `live-normal-launch-probe-test` | 6448 / `live-6448-209028906` | 3.446 s | 同二进制改为 `Start-Process -WindowStyle Normal`，仍屏幕门失败；未 Start/probe |
| `live-cloak-state-probe-test` | 76896 / `live-76896-209259718` | 3.909 s | 自身 source/Bar/host/child 的 DWMWA_CLOAKED 全为 S_OK/value0，仍不可见；未 Start/probe |
| `live-own-foreground-probe-test` | 21900 / `live-21900-209637187` | 8.867 s | 补外部窗口只读身份与覆盖层外 GT，确认 LockScreenBackstopFrame 全屏覆盖，0/54 图案、0/9 蓝色点；仅自身 source 的 Foreground 返回0，Raise/自身组 Topmost 返回1，未移动外部窗口；未 Start/probe |

正式 `live-owner-chain-test` 的具体线程与窗口状态：

- MagInitialize/Host/Child/初始 identity transform/color effect 在 owner **TID63032**；正式 Filter/Source/Invalidate/Redraw 在原 producer **TID37736**。原设计存在这两个线程，测试没有移动其中任何操作。
- 初始化 API、Filter/Source/Invalidate/Redraw 的布尔结果均成功，原 source 不过滤、Bar 过滤，主屏 `2880×1920`，Mag source/child 高度按原实现为 `1919`，alpha 初始0；未注册 scaling callback。
- 最后观测 snapshot `quality=2 (NearBlack), attempts=17, accepted=0, rejected=17, api=0`。该旧版日志没有独立记录 raw mean/variance/black fraction 或各次 PW 耗时，不能补造为零或推算性能。因为没有 accepted callback，不能得出 matched phase、旧帧延迟或 F0 正确性结论。
- 该版只检查蓝色中心和 painted1，没有 63 点屏幕 GT；后续发现了锁屏覆盖，但不能倒推此覆盖在原17次读回期间必定存在。**17 NearBlack 是真实有限观测，不能证明 PrintWindow 本身、GDI flush 或 Mag 跨线程更新是根因。**
- Stop/worker join/原 owner Uninitialize 成功，pair 被销毁；GDI `11→17`、USER `4→4`。这不是20次资源稳定性验证，也不据单次进程计数宣称无泄漏。

## 当前安全层证据与停止

`live-own-foreground-probe-test.log`：

- 中心外部命中 `HWND=0x7E1046 / PID=13716 / class=LockScreenBackstopFrame / basename=explorer.exe`，Topmost1，bounds `(0,0,2880,1920)`。只读 class、进程 basename 与 bounds，没有读取窗口标题或保存其图像。
- 线程 desktop=`Default`，input desktop=`Default`，两者名称查询成功；自身窗口 cloak0。这些值不能证明未锁屏，也不能作为越过安全层的理由。
- 自身 source visible1/topmost1，bounds全屏；Bar visible1/topmost1，bounds `(960,640,1920,1280)`，owner链正确且 source 位于自身原根之后；屏幕源采样 0/54、蓝色覆盖层 0/9，中心像素 `0x0036111F`。没有可确认的专用内容屏幕 ROI。
- 只尝试提升本次 source 并恢复本组顺序；SetForegroundWindow=0、source Raise=1、group Raise=1，锁层未变化。没有最小化/隐藏/移动外部窗口、切换虚拟桌面、解除锁屏或更改系统安全设置。
- 得知安全 class 后立即停止所有 GUI/截图。最后仅补精确 `LockScreenBackstopFrame` class 的早退，因为 explorer.exe 本身不能按进程名判定安全层；该最终分支已编译，按停止要求未再用正常 live flag 实测。

本轮七个私有目录均无 PNG。屏幕 GT 通过前不保存未知内容；没有把日志失败伪装为截图成功。

## 构建、严格 argv、格式及 hash

使用现有 ignored `Build/ScrollFollow/build-motion-tracker.ps1`，按 vswhere 选择 ARM64 原生 MSBuild，在同一 PowerShell 中规范 Path/PATH、禁 node reuse，完整 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时10分钟。每次串行，无并发产品/构建，无改 SDK/依赖/链接参数，无本轮 ILK fault。

最终命令 `& Build/ScrollFollow/build-motion-tracker.ps1 -Round live-final-safety`：**exit0，13.674 s**。日志 `motion-tracker-build-live-final-safety.log/-console.log/-stderr.log`。本轮其它构建均 exit0；最近仅原 hashlib++ 三条 C4267，没有新增夹具源码 warning。

已检查 IdtMain 最早 whole-argv prefix 拒绝逻辑，再运行 `Build/ScrollFollow/run-live-bad-argv.ps1`：10组后缀 `=bad`、错位、额外参数、重复、两flag组合，全部 **exit2 / stdout0 / stderr0**，单次100–1216 ms。结果位于 `Build/ScrollFollow/live-bad-argv-209809453/result.json`。没有运行正常 live flag 或启动 Display/配置/窗口。

`git diff --check -- Inkeys/IdtMagnification.cpp Inkeys/IdtMagnification.h Inkeys/IdtMain.cpp` exit0。三源文件均保持 UTF-8 BOM/工作区 CRLF；Main 保留原无末尾换行，另外两文件保留有末尾换行。静态数据 `Build/ScrollFollow/live-final-static.json`。Git blob 为LF与工作区CRLF的差异由既有Git文本策略管理，没有全文件换行diff。

| 文件 | SHA256（最终冻结） |
|---|---|
| `Inkeys/IdtMagnification.cpp` | `c41dbdbff0ddb0861cb003344ebe2b7b6a8230e28723e1c22e96310585dde151` |
| `Inkeys/IdtMagnification.h` | `2902e83ef2be8ef81eacb44e975f975dd5b60db68cf50a1a2d921e47a7513197` |
| `Inkeys/IdtMain.cpp` | `120a4f0ce121a3f6aaa2924160541dbb59782b2fc36ab7cd7961d8a6eb874eb1` |
| `Build/ARM64/Debug/Inkeys.exe` | `c84e89214dc670a143d66cb6e0fafd7d7abf63fa88078081fe261ddb38fab758` |

最终 hash 对应补完安全 class 早退后的二进制；17次 NearBlack 来自此前 `live-owner-chain` 构建，该运行未单独留存其二进制hash，不能将最终hash当作旧运行hash。常规harness-off路径已静态确认声明/实现fallback返回2、guard一致，本轮未做Release/harness-off重编。x64/PE由主会话决定是否需要针对新增测试补查。算法/持久化/长headless没有变化，没有重复既有长矩阵。

## 后续门槛

先由独立check审查夹具/严格早退/借用与清理、实际证据和安全停止。在有可确认的非锁屏测试环境时，重新从最小正式 F0/静止/变化/排除开始，先取得屏幕专用内容GT；必要时再运行已编译的owner-only flush/PW/update对照来定位原因，仍保留原生产Mag线程设计。

本轮未完成：正式有效F0和连续phase、旧帧/延迟的实际记录、20次Start/Stop、Freeze前后互斥、初始化失败/停止取消与重复资源统计、实际FPS/耗时分布、真实Business/StateMonitoring双窗口ACK/Draw3可见跟随、Edge/Chrome/Word/PDF、触控/笔、旧Windows和WOW64。必要第一门槛未过，不能开始产品可见整链，也不能把此前隐藏ACK seam或旧owner夹具成功视为本轮正式路径成功。所有这些项目继续保留未完成。


## 解锁后正式PASS、生命周期扩展与再次安全停止

用户明确手动解锁后，checker 独占执行了无probe正常正式入口：`Build/ScrollFollow/live-check-unlocked-formal.log/.json` 及私有 `live-2484-210352218/result.log`，exit0 / 6.364s。屏幕GT63/63、蓝9/9、屏幕ROI逐像素正确；正式session1的seq1–12、phase1–4各3帧均正确，unmatched/old/wrongThread/API失败均0，alpha0下持续更新，正常pair销毁。owner TID22192、worker/callback TID79832。该证据来自checker，不是本实现者重新运行。其EXE SHA256 `A80DDEF767A37989FD2B005D878EE0D3A92A1333C6C9D11210F5156C210E50FE`，Mag.cpp SHA256 `297E96C2531E3DCDC17D5DDC59ADD70C965781BE4D6DE8B672C232220C249AC3`。解锁前17NearBlack不再作为当前正式取帧失败的结论，也不能反向认定其原因。

主会话将source/build/GUI完整交还implementer后，仅在正常夹具扩展：

- 删除未执行且已无必要的owner-only live probe、LiveReadProbe、raw Read hook/计时、private WndProc消息、借用同步和diagnosticsOnly分支；正式Backend Read/原WndProc恢复无新增观察hook。测试函数改无参。已撤销probe参数只在最早argv扫描硬拒绝2，防旧命令落入普通GUI，没有可运行入口。
- 保留checker加入的最前安全门、循环安全检查、像素前安全停止、最小屏幕几何与atomic日志统计；不回退其它作者修改。
- 正常4phase后增加至少5秒/20帧静止采样，按真实Read与timestamp差记录mean/p50/p95/max、实际FPS与skipped，不把20Hz目标当性能。
- 计划在同进程/原Mag pair上20个完整capture轮次，各验证递增新session、accepted seq从1连续、Busy仍指原session、至少2个当前phase的63点GT、Stop/drain/隐藏完成后150ms没有迟到callback，并记录各轮GDI/USER；第1轮后要求后19轮资源数一致。
- 使用原Freeze::Toggle/状态observer/原producer进行两次Reveal/Conceal：先Freeze active拒启，再在实际capture有效帧后Toggle触发撤销与排空、alpha255 Reveal、active拒启，Conceal后重新capture至少3帧。没有直接调用Mag或伪造完成。
- 增加Start后立即Stop且无F0、无hook观察实际Read inFlight后Stop并验证没有超出撤销时已准入结果的新发布；另检查空callback InvalidArgument不占用。

此扩展只修改相同三源文件，没有改生产Capture、线程、阈值或算法。`git diff --check`（实施前构建边界）exit0，未发现probe实体残留。完整 `build-motion-tracker.ps1 -Round live-lifecycle-first`，原配置Debug ARM64 solution，**exit0 / 23.820s**；日志 `Build/ScrollFollow/motion-tracker-build-live-lifecycle-first*`。只有原Setting/Main/hashlib警告，无新增源码错误，无ILK故障。

随即运行 `run-live-capture.ps1 -Round lifecycle-first -Flag --freeze-capture-live-test -WindowStyle Normal`：PID53668，**exit4 / 1.204s / stdout0 / stderr70**，stderr精确为 `blocked=security-interface-before-initialization`。日志 `Build/ScrollFollow/live-lifecycle-first-test.json/.log/-stderr.log`。最前安全门在Display/窗口/屏幕读回之前停止，因此没有创建此轮私有root/GUI，没有执行20轮、静止长采样、取消或Freeze矩阵，不能填写其结果或FPS。该安全门结果不否定checker此前12帧正式PASS。

收到停止指示后未重试正常flag、未执行坏argv、未编译或操作GUI；主会话询问人工解锁，checker仅可离线审查新diff/CLI。原解锁前坏argv/哈希是历史，不能当成新源码已经执行的CLI验证。当前冻结：

| 文件 | SHA256 |
|---|---|
| `Inkeys/IdtMagnification.cpp` | `6F604C7057F298C5EB8845792501C5D94CD7805231C81393368502B4A9ACBA5E` |
| `Inkeys/IdtMagnification.h` | `400517F5D7274BE9E54639F75ABDD42F411F4EE82FC524207AB4035A1DB82338` |
| `Inkeys/IdtMain.cpp` | `BA4A26C66487843BE77533900B45AEC9159272BE55042AA9D219705D369877A2` |
| `Build/ARM64/Debug/Inkeys.exe` | `2D5A1E20A7DAB146D583DD14FD78AD79776F7F138956345575BCC3207CC5AC3B` |

等待用户再次人工解锁、主会话明确交回执行权后，可直接运行已有二进制，无需先build。后续必须分别记录本矩阵真实执行结果和checker已有首门槛PASS，不用未运行计划代替验收。


## read-stop验收竞态的静态修正（未构建/未运行）

独立checker离线指出：一次观察 `inFlight && attempts>0 && accepted==0` 后到Stop之间，Read仍可能先提交accepted1；旧断言允许accepted1/callback1仍报告read_stop_checked。该结果只能证明曾观察在途，不能证明Stop撤销了在途Read。

本实现者重新核对原Session实现：`lateResults`仅在phase Read的backend调用真实返回、随后在状态锁下发现发布已撤销或windowAccess已关闭时增加；原生产实现未改。只最小修改live夹具此块：

- 每轮在前一session已drain后新Start，最多3轮；每session单独记录累计cancelledCallbacks的baseline，race-missed的已准入回调不会污染后续轮次。
- Stop之后accepted已大于0时记录 `outcome=race-missed / observed_inflight=1 / actual_read_revoke=0`，不计为撤销在途成功；该session先真实drain，再重试。
- 仅Stop后accepted0、drain后accepted0、本session callback0、lateResults至少1且Stop reason为Requested，才记 `actual_read_revoke=1`。accepted0但没有late-read凭据明确失败；3轮全miss则 `read-stop-uncovered-after-race-retries` 失败，不能进入matrix成功摘要。
- 原Freeze测试仍只验证原owner/coordinator、alpha及显隐/占用状态，不是Freeze屏幕像素或系统产品ACK通过。

静态 `git diff --check -- Inkeys/IdtMagnification.cpp` exit0，保留UTF-8 BOM/CRLF/EOF；源码即刻冻结。当前Mag.cpp SHA256 `512a650ea9007c9cc5080b51cd0d4a9af4f02c04e05c6d60b13d29758f147dd0`。本次按主会话要求没有build、CLI或GUI，已有二进制仍为上一节哈希、**不含本次修正**。其他坏argv由checker独立执行，不重复；后续统一串行构建与实测由主会话安排，不将本段静态推理标为已执行命中。


## 最终人工解锁后的正式生命周期矩阵：实际PASS

用户再次明确“已解锁，继续测试”后，主会话交回本实现者capture GUI/CLI；source/build保持冻结。直接运行现有 `run-live-capture.ps1 -Round lifecycle-unlocked -Flag --freeze-capture-live-test -WindowStyle Normal`，没有build、productflag、Computer Use或外部窗口操作。该二进制已由主会话完整solution `product-live-include-order` 构建，exit0 / 24.609s，本实现者没有重复编译。

运行 **exit0 / 41.388s / PID26916**；stdout78161B、stderr2504B（原DrawpadSurface自身窗口销毁诊断）。日志：

- `Build/ScrollFollow/live-lifecycle-unlocked-test.log/.json/-stderr.log`。
- 私有root `Build/ScrollFollow/live-26916-212248453/result.log`。
- 5个仅专用内容的已验证ROI：`screen-dedicated-roi.png`、`phase-1-roi.png`到`phase-4-roi.png`，分别约714–715B、256×192；没有保存用户桌面未知区域。

屏幕原始GT63/63、排除蓝层9/9、专用屏幕ROI逐像素正确。原Mag owner **TID25632**，正式producer/callback **TID50524**；同一原pair贯穿全部session。无scaling callback、无WGC/捕获排除属性。session1首个F0 `painted1/matched1`、seq1，约976.534ms到达，Read71.670ms，host/child visible1、host alpha0。四phase各至少3帧正确，之后同一phase4继续静止至少5秒；全程91个有效callback，unmatched0、old0、wrongThread0、sequenceErrors0、API失败0。

| 项目 | 真实结果 |
|---|---|
| 20轮重复启动/停止 | session2–21，每轮至少2个当前phase的63点GT；新session逐次增加，accepted seq从1连续；每次额外Start返回Busy且仍为当前session；每次原producer排空/隐藏完成后150ms没有迟到callback |
| 20轮资源 | 每轮Stop/drain后GDI均108、USER均61，20轮完全相同，resource_changes0；每轮约1.208–1.325s |
| 立即Stop取消 | session22，attempts0/accepted0/callback0，publishing/occupied/inFlight均0，未产生F0，Stop reason Requested |
| 真正在途Read撤销 | session23，首试命中；accepted_at_revoke0、accepted_final0、本session callback0、lateResults1、reason Requested，actual_read_revoke1；没有把观察在途后先提交的race计成成功 |
| Freeze预置互斥 | 原Toggle revision1 Reveal、alpha255，Capture Start返回FreezeActive(3)；revision2 Conceal，原host/child均隐藏 |
| capture→Freeze | session24已取得2个真实phase3有效帧后原Toggle；revision3到Reveal前capture已撤销/排空，reason FreezeActive(2)，alpha255，active拒启，150ms无迟到；revision4 Conceal |
| Freeze后重新捕获 | session25，真实phase2的seq1–3和GT均正确；再Stop/drain/隐藏/150ms无迟到 |
| 无效入参 | 空callback Start返回InvalidArgument(4)，没有占用 |
| 释放 | 正常Stop/join/原owner MagUninitialize成功，original_pair_destroyed1，测试自动关闭 |

Freeze证据只到原状态observer/producer/coordinator和alpha/显隐/排空，**没有验证Freeze屏幕显示像素或真实Business/系统产品ACK**。

静止采样窗口内34帧的首尾跨度4.840406s（等待条件为从起点至少5秒，首尾有效帧跨度略短）；33个实际间隔，实际FPS **6.818**。不是20Hz目标达成：

| 观测 | 数量 | 平均 | p50 | p95 | 最大 |
|---|---:|---:|---:|---:|---:|
| 正式Read耗时 | 34 | 67.448ms | 61.794ms | 97.556ms | 102.241ms |
| 有效帧timestamp间隔 | 33 | 146.679ms | 147.331ms | 174.406ms | 178.860ms |

初始session含四phase与静止的snapshot累计skipped87、rejected0；skipped不是单独5秒窗口计数。实际整条采样包含Update/等待/Read/分析和回调开销，不能用Read平均值倒算截图FPS。未调参数提高数字。

最后进程GDI从11到84、USER从4到34，这是pair销毁后的进程级读数，**不等于回到初始，也不据此宣称整个进程零泄漏**；20轮相同停止位置的资源计数稳定是本次已验证的范围。初始/结束数包括主会话已接入product fixture后的进程静态/首次初始化资源，具体剩余对象未在本轮逐个归因。

实际运行EXE SHA256 `F84BF2037B033876AB3EF8BAC2DC7A67D4F24CEB4319A416A6B1DBA80E1BA8C6`。运行后只读源hash（已包含主会话product接线，并非此前512A单独块版本）：

- Mag.cpp `C8B1651708F2EF9ECE15B577912195BF4C39ED2CDC6E768F1E1490E774C2F16A`。
- Mag.h `662EFC46CA1DFFB090BA0F275FFC1C44A9F7EC107F375411A47198A35091D8EC`。
- Main.cpp `0BF398A0C3A31A7F0DE1C0C8D1340C3169942ECA81694115872DD74DAB8C639A`。

实际PASS日志/hash已先立即交还主会话GUI/CLI，再写本续篇。无待运行进程，无额外测试；后续产品整链、浏览器专用内容、真实触控/笔/旧系统及缺失应用继续由主会话统筹。生产初始化失败路径未在本轮注入故障；不能把InvalidArgument当作MagInitialize失败覆盖。


## 最终运行的离线逐行与PNG复核

保持源码/build/GUI冻结，仅解析已存在的本次 `result.log`、stderr及5张PNG。完整结果和91行数字保存在私有root `offline-verification.json`；原result.log SHA256 `eee319b5ac1c16c30ea16f5989b70075e849ab175c7fed141151ed3094614d66`。

- 91个callback逐行核验：session1共46帧（前12为四phase各3帧，后34为静止phase4）；session2–21各2帧，当前phase严格按轮次匹配；session24有2帧phase3，session25有3帧phase2。每session seq从1逐次连续、accepted=seq、sample时间严格增加；每行painted=matched，owner25632与worker/callback50524一致且不同，host/child visible1、alpha0、quality0、rejected/API0。这是原始callback GT统计的逐行一致性审计，未保留91张全帧图像，不把日志审计称为91帧全屏逐像素复核。
- 20轮完整解析，round1–20/session2–21/预期phase均对齐；每轮warm GDI108、USER61、resource_changes0。最终进程11→84/4→34单独保留，不作初次零泄漏结论。
- 真ReadStop仅一个attempt，实际撤销1、at-revoke/final accepted0、callback0、lateResults1；immediate-stop attempts0/accepted0/late0。Freeze两次revision1–4原状态/alpha/占用记录精确对齐；未新增Freeze像素或productACK证据。
- 静止34个Read及33个间隔从三位小数日志重算：平均67.448088ms、146.678970ms，FPS6.8176099，与原汇总舍入相符。p50取 `sorted[(n-1)//2]`、p95取 `sorted[(n-1)*95//100]`，是lower order statistic，不是插值/nearest-rank；不要将该p95与旧报告不同口径直接比较。用户说明优先采用约67ms/6.8fps。
- stderr **2504B** 全部为 `[DrawpadSurface]` 生命周期日志，共12行，非空stderr但没有混入其它错误；完整SHA256 `0f285b5f6c2520f888fb1655d7fd5dca1378a90085677cf4e560956445144214`。

每张PNG解码为RGBA，对全部49,152个像素按原 `PatternColor` 与真实source坐标反算比较，**每张exactMismatchPixels0、maximumRGBAError0、alpha255**。主屏2880×1920；屏幕ROI起点(64,64)，phase ROI起点(1312,864)。这5个ROI恰好各位于单个网格色块内部，所以各自是单一RGBA颜色；它们只证明该ROI的精确像素，跨区域/排除与phase推进仍依赖原63点GT，不夸大为全屏逐像素图案验收。

| PNG | 实测/期望RGBA | SHA256 |
|---|---|---|
| screen-dedicated-roi | (79,63,93,255) | `d89208c0f8bb4faf40b12ae4fb2807e9c1e405913d0bc3958486daedefa5a1f0` |
| phase-1-roi | (176,194,32,255) | `9ec299391564d536744be8a4cdefc25f48ca19a3c8d40f268c2f2ee26e9f804c` |
| phase-2-roi | (223,33,93,255) | `71976c84624d8f14f0c50c046b8b6c1c94ddcf52b6c301a08a71f8d89a4eaa9f` |
| phase-3-roi | (78,64,154,255) | `4fa70032cfd5cf71d3cff4f5c58999bc741f57824abc046cd3838c8e691c6375` |
| phase-4-roi | (125,95,215,255) | `f64f2f13d3da3a94843afed97538dce8d382dd794bd64819906810bab0c26a1f` |

以上纯离线核验exit0；没有重新执行live/CLI、打开窗口或修改图像。报告及原始统计更新完毕，capture执行权已交还主会话，保持冻结。
