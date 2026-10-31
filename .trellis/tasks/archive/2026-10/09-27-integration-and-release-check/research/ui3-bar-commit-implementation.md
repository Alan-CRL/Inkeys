# UI3 U04-B1 Bar commit implementation

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。唯一 writer：`ui3_bar_commit_impl`。当前阶段 **B06 默认关闭门控 GREEN PATCH_READY；绿色动态验证待 root**。root 独占构建/测试/GUI，本 worker 不运行任何 EXE。

## B06 GREEN：默认 detailed capture 门控（当前优先）

本批严格只修改 `RenderPipeline.cpp::StampBarCommit/FrameStageTimer` 和 `Bar.RenderLoop.cpp` 新 attempt 诊断字段三处，其它源码保持 B06 RED hash；不修改 Main/Helper/Window/Draw3、classify算法、schema2、旧六stage或异常sink，不扩大到B2/B3/F。

- Stamp 的首个 guard 核 `diagnostics && detailedCaptureEnabled`，再核 committed/首次stamp；显式传clock参数也不能启用捕获。false门不读steady_clock/测试clock、不写真戳数值。
- FrameStageTimer仅对 `stage>=WakeAndSnapshot`（已追加七stage）且detail=false设置内部diagnostics_=nullptr，随后沿原nullptr不读钟路径；Stop/析构保持原幂等。原六index0..5无此门，异常sink仍可计阶段。
- Bar本来增加的 `presentAttemptFrameSerial` 诊断复制在同一detail门内；状态里的原attempt计数、Tick/raw dt、失败/退避、ULW及请求均保持原调用。源bit仅由Scheduler当前真实rawActive赋值；普通安装异常sink并不授予B1详细捕获。

真实RED由root执行、本worker已只读核raw：`ui3-b06-red-debug-arm64-build.log/.status.txt`，完整 `InkeysRepo.sln Debug|ARM64`，2026-09-30 18:00:14开始、1:49.19完成，0 Error(s)/140 Warning(s)，exit=0；`ui3-b06-red-debug-arm64-headless.stdout.log/.stderr.log/.status.txt`，严格 `--no-window` 自然exit=1、PID32752，仅B06失败/FAILED count=1，layouts216 failures=0，旧B/R无新增失败。这证明旧读取反例准确，**不证明修补后的绿色已通过**。

root根据该RED派发GREEN_IMPLEMENT。本worker完成上述最小门控后，仅执行静态diffcheck exit0、严格UTF-8/无BOM/全CRLF及原完整测试主体保留检查；没有运行build/test/EXE/GUI。root待C-P2同批源码冻结后串行新Solution Build、strictHeadless及独立实际review；不得在其它writer半写期间构建。B06红源片段另存忽略目录 `TestResults/release-hardening/ui3-b06-red-source/`。

| 文件 | B06 GREEN SHA-256 |
| --- | --- |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm` | `56cca2891f9dda89698111eacc9987ed6d935740ce7063fa1373023367b767b8` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | `7fef230234cfe8915a5d1ca7b84af91903bfaba5a98c09257f94663119474b83` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h` | `f1941ecf267b8ea76805f1ddadd35218dc54b8fd32a848717cf263f35816067b` |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | `123e495edde81819716ae199b0a21b54fe7805105b259a9a36cd9ecda95027f1` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `84dcdd858b3e537d0c09ace773cae52b5cd5d41fa76e8ceb318a052e20212caa` |

## B06 RED：异常 sink 与 detailed capture 独立门（历史）

独立实际复审指出此前默认关闭断言不足。已只读核实际 Main:1764 普通日志初始化安装 SetDiagnosticsSink，Scheduler:750–825 按 diagnosticsActive||rawActive 给出旧非 null FrameDiagnostics。因此仅 R01 无 sink 测试不能证明普通产品不开 B1 新计时；这是本轮确认的默认合同偏差，不修改 Main 或关闭旧异常诊断来绕过。

RED 最小新增：`FrameDiagnostics::detailedCaptureEnabled=false` 纯数值位；Scheduler 只在当前 callback 创建样本时从实际 rawActive 赋值。B01/B02/B03 直接 fixture 显式 enabled=true，ObserveBarCommit/testClock 不自动赋予许可；B04/B05 由真实 raw recorder 赋予 source bit。**RED 尚未改新7timer/Stamp旧读取guard**。

B06 真实 Scheduler + SetDiagnosticsSink + ConfigureRawCapture(0) + production CompleteAttempt：旧 TLS 必须非 null，detailed flag=false；Stamp 的测试 clock 调用数应0、新戳/数值应0，每个新增 stage 都包2ms等待但累计应0；原 Draw timer包完整父段且>0、旧diag正常、无raw report、join后TLS null。等待失败仍 Stop/join。该测试不会因为实际scope极短测得0而假通过，也没有用clock参数绕过 capture gate。预计本批只新B06红，B01–B05/旧R保持原有状态，实际结果由root完整构建和严格no-window确认。

GREEN 最小方向：FrameStageTimer只对新增stage检查detail bit，原六stage照旧；Stamp必须显式detail门，clock参数不能授权；Bar新增attempt诊断字段同门，raw校验戳与source一致。原schema2/数组预算/旧proxy/sink/调度/ULW均保持。当前尚未 GREEN，不把新增位已经写出称为门控修复通过。

本 worker 自有五文件 diffcheck exit0；UTF-8无BOM/仅CRLF；原完整R测试主体逐字保留。未运行编译/测试/EXE，root独占验证。修补前字节备份在忽略目录 `TestResults/release-hardening/ui3-b1-before-detail-gate/`。B06 RED SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm` | `56cca2891f9dda89698111eacc9987ed6d935740ce7063fa1373023367b767b8` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | `97c06d90059adf7a46f175f7f15690dc94678e0739153b4d7c90131f8fac20ca` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h` | `f1941ecf267b8ea76805f1ddadd35218dc54b8fd32a848717cf263f35816067b` |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | `12031dabb27d37fe2417ec2ab6c89f916639682b63c0de966a91e011461e4a74` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `84dcdd858b3e537d0c09ace773cae52b5cd5d41fa76e8ceb318a052e20212caa` |

## 首次 B1 GREEN 行为（B06 修补前历史）

- `StampBarCommit` 只在非 null 诊断、完整事务 committed 且本回调尚未有戳时读一次时钟，保留首次 tick/attempt/epoch。生产 Bar 插点在 CompleteAttempt 后、原 presentCommitted/成功快照/StartupPreview 通知前。无诊断、四 API 任一失败、deferred 和重复调用不读钟。
- `ClassifyBarCommitStamp` 核 Bar client、barSampled、完整成功 flags/API、非0一致 epoch/attempt、callback 时间因果及前真戳顺序；新 status/分母不改旧 validTime 和 callback-end proxy 的含义。缺失成功戳为 Unverified，矛盾戳 Invalid，不补 endTicks、不 clamp。其它 client 不能增 trueBarCommits。
- Scheduler 的真链与旧 proxy 独立。只有有效真戳推进 run 内 barSuccessSerial；同代活动失败可保留上次有效成功，Idle/epoch/重注册切断；缺失/非法成功戳也切断以免假定其两侧连续。满容量后继续累计 true/unverified/invalid 及既有 seen/retained/dropped。Start 只在新 capture run 重置该成功序号。
- 原五调用各旁挂一个 timer；DirtyAndPrepare 在 CalculateDirtyAndDrawPresent 取 TLS 后开始、原 Draw timer前 Stop，Resources 仅围原 EnsureDeviceResources。早退由原 FrameStageTimer 析构 Stop 一次。没有移动 Tick、资源/dirty/quality/request、原六阶段/锁/四 API 或 pacing；nullptr timer仍不读钟。
- 本轮仅把新测试 local `registered` 改为 `barRegistered`，消除 RED 编译报告的 C4456；原154行 registered 与其它警告未修改。

## 最小边界与冻结接口（含 RED 历史）

已有 Scheduler raw schema1 用 callback 返回时刻作为成功代理。B1 在真实 Bar 的四 API（GetDC/ULW/ReleaseDC/EndDraw）及 `BarPresentDecision::CompleteAttempt` 后、成功快照发布前旁挂确认戳；同时追加七职责阶段。原调度、动画 Tick/dt/idle/pacing、dirty/retry、资源请求和画质保持原调用语义。B2/B3/F、目标/输入/资源 proof、真实 runner 均未批准/未实施。

五个源码文件为本单元所有权；先前 U04-R 改动保留，原 R01–R14 测试主体逐字保留。对照备份位于忽略目录 `TestResults/release-hardening/ui3-b1-source-baseline/`，不覆盖其它 writer 文件。

- `StampBarCommit(FrameDiagnostics*, bool, uint64_t attemptSerial, uint64_t epoch, BarCommitClock clock=nullptr) noexcept`：默认 steady_clock，单次成功戳；可选无状态 noexcept tick 函数仅用于合同测试。RED 时函数为空；GREEN 已按上述短路读取单次时钟。
- `FrameDiagnostics` 新增 `hasBarCommitStamp/barCommitTicks/barAttemptSerial/barCommitEpoch`，另复制原名 `presentAttemptFrameSerial`（回调/退避序号）核对身份，不能将其叫 success 次数。
- `RawCallbackSample` 新增 `BarCommitStampStatus::{Absent,Unverified,Valid,Invalid}`、`hasPreviousTrueBarCommit/previousTrueBarCommitTicks/barSuccessSerial`；旧 start/end/previousActiveCommitTicks 和 validTime 口径保留。report/client 新增固定数值 `trueBarCommits/unverifiedBarCommits/invalidBarCommitStamps`。schema2；RED 时真链/计数为空，GREEN 已实现上述数值 recorder。
- `ClassifyBarCommitStamp` 是生产 recorder/测试共用纯数值校验器；RED 时暂返回 Absent；GREEN 核 client、完整成功 flags/API、epoch/attempt 一致和 start<=commit<=end、前真提交顺序；缺戳成功为 Unverified，矛盾戳 Invalid，不补 callback end、不 clamp。
- FrameStage 原六 index 0..5 保留；追加 WakeAndSnapshot/DisplayTransition/SubmitTargetsAndLayout/AdvanceAnimationsAndDeriveLayout/PrepareLightingAndDemand/DirtyAndPrepare/Resources，Count=13。formatter 同步名称和 static_assert。GREEN 已包裹实际新计时器；Resources 是 DirtyAndPrepare 的 inclusive 子段，不加总当 CPU/GPU。
- 容量仍最大65536、64MiB；分配按真实 `sizeof(RawCallbackSample)+sizeof(RawBatchSample)` 除法先验，默认32768加编译预算断言，不扩大采样中的数组。

## RED harness

B01 共用实际 CompleteAttempt，成功一次、重复戳保留原 tick/attempt/epoch。B02 GetDC/ULW/ReleaseDC/EndDraw 任一失败、CosmeticLeaseSkipped、nullptr 均无戳且不调用测试 clock。B03 共用纯数值校验器，核时间/epoch/attempt/flags/其它 client/前真戳。B04 实际 Scheduler 接共用 CompleteAttempt+stamp，核代理和真时刻区别、失败保留前一成功、独立 successSerial、idle/epoch/重注册断链。B05 固定容量2在掉样后保留 Valid/Unverified/Invalid 分母，Settings 相同数值不冒充 Bar。所有新增等待超时路径均 Stop/join；未新增永久 pause。

这些合成四 API 结果证明软件决策/计量合同，**没有执行真实 GetDC/ULW，没有真实 Bar GUI/GPU/像素性能证据**。root 已取得 RED 的 B01/B03/B04/B05 四项失败，B02 空函数负向合同成立；最终绿色结果仍须以 root 的构建与严格 `--no-window` 输出/退出码为准。

## 静态检查与身份

本 worker 已执行自有五文件 `git diff --check`，exit0；UTF-8 严格解码、无 BOM、仅 CRLF 与原始源码一致；小 diff 对照保留旧 U04-R 和全部原测试主体。没有运行 build/type-check/lint/Headless/GUI，动态门由 root 串行执行。RED 当前 SHA-256：

| 文件 | SHA-256 |
| --- | --- |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm` | `3318d1498f4a68e877cdfc6107e591ea5c13e9163b09bd92e903801a3bf8f166` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | `ce5cf932330950b3fbf64658b95402092df0d272049cd0d19dd65f346b9b76da` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h` | `57ce65491da2969e11c5a0320d5fe34a17ba4c3c965ddabd2134387022e5ee84` |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | `5623d1bcb2cf967b5940a9d4ecb727a8a1a11a854a8bc5312981438a73286457` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `84e2a258f67f82b845ce893b841aa80f3a0f8c28ce03f2d923047820574d569e` |

## 动态证据

本 worker 只读下面的原始日志和 status；命令执行者为 root。没有把只读核实称作本 worker 独立运行。

- RED：完整 `InkeysRepo.sln`、`Debug|ARM64`，2026-09-30 16:48:15 开始，2:01.23 完成，Build succeeded、0 Error(s)/152 Warning(s)，status `exit=0`。原始 `TestResults/release-hardening/c-p1-ui3-b1-red-debug-arm64-build.log/.status.txt`。唯一与新增 B1 代码有关的 warning 是 tests:1608 新 registered 遮蔽154行旧 local，GREEN已局部改名，实际消除仍待重建确认。
- RED：`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，root 报告并由 status 核 `exit=1 pid=30080`；`ui3-b1-red-debug-arm64-headless.stdout.log/.stderr.log/.status.txt`。stderr 恰为 B01/B03/B04/B05 + FAILED count=4，stdout 仍含 `[EraserAttribute] layouts=216 failures=0`；旧 R/其它测试无新增失败。
- root 根据上述真实 RED 已派发 `GREEN_IMPLEMENT B1`。本 worker 的绿色代码已完成，未执行绿色 build/Headless/EXE/GUI，等待 root 串行构建、严格 no-window 和独立实际 diff review。

## 首次 GREEN 冻结身份（B06 修补前历史）

自有五文件 `git diff --check` 再次 exit0；严格 UTF-8、无 BOM、全 CRLF，原完整测试主体仍逐字存在。DTO与 RED相同；固定数组仍受65536/64MiB、按sizeof除法先验及默认32768编译预算断言约束。RED完整构建已通过该预算断言，GREEN实际 allocatedBytes由报告记录、B05按sizeof核对。原始 RED 源字节另存忽略目录 `TestResults/release-hardening/ui3-b1-source-red/`。

| 文件 | GREEN SHA-256 |
| --- | --- |


| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cppm` | `3318d1498f4a68e877cdfc6107e591ea5c13e9163b09bd92e903801a3bf8f166` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.cpp` | `74bfc352b609a2127217c62cab281103bb0426570950e9041b09d1c6f5cde1f1` |
| `Inkeys/Inkeys/UI/RenderPipeline/RenderPipeline.Diagnostics.h` | `f1941ecf267b8ea76805f1ddadd35218dc54b8fd32a848717cf263f35816067b` |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | `12031dabb27d37fe2417ec2ab6c89f916639682b63c0de966a91e011461e4a74` |
| `InkeysHeadlessTests/render_scheduler_tests.cpp` | `8b218717223847696b55c72e94ce32599512540dc2fdeb1070d39c9b903cfbfc` |

## 未覆盖项

真实 Bar runner、B2 动画目标完成、B3 SVG/cache/path proof、F auth/source/lifetime、像素等价、GPU/线程 CPU/三轮 Release、HC/H2 同机、Win7 SP1 仅 KB2670838 都仍未验证；不因本单元软件戳而关闭 E04 或发布门。E01/E02/E03/已绿 U1 和父 spec/账本由原 owner 保持。
