# 实施计划

2026-10-28用户已明确批准按本规划实现。执行中逐项记录证据与取舍；优先离屏，允许必要GUI窗口但不使用Computer Use，不commit/push。

S0先执行；S1–S4按其结果选择和排序，不默认全部修改，也不要求SVG主导时仍先做容量/协调优化。

实施处置（2026-10-28）：细项原计划保留用于核对，实际已执行/覆盖缺口以 [最终验证](verification/final-verification.md) 和 [实施记录](verification/implementation-notes.md) 为准。S0增加真实Scheduler六向Bar/PageControl接缝，但未覆盖native Freeze/Window/Office全链或SVG独立分段；完整组合保留三倍率三轮、每轮由30cycle缩为5cycle，成本与理由已记录。S1依据真实增长反例收敛到保留原横向安全预约、稳定完整曲线宽度范围与实际纵向在途包络。共享Scheduler粗粒度睡眠是S0新增且独立A/B确认的根因，已复用既有无自旋timer修复。S2/S3及SVG产品实现因缺少对应本场景主导证据而未改，不将这些条件项勾为完成。Debug正确性与性能改善有证据；隔离Release最终绝对预算也已验证，热态24/54组通过，R4仍未全部达标，后续研究由主会话协调。


补充：S4现已完成独立SVG数值计量的红绿/透明性和100%+200%短采样，见[独立计量报告](verification/svg-numeric-timing.md)。该诊断版本重新完整Debug构建；前一批Release/完整性能矩阵及Headless保持其冻结版本证据，不外推为新二进制重测。SVG CPU非当前主体成本，未改产品SVG资源算法；R4继续未完成。

## 1. 基线与可证伪实验（S0）

- [ ] 记录当前分支/HEAD、dirty paths、目标源码编码/BOM/换行、机器/电源/倍率/光效/EXE 哈希；保留当前 workspace failure recovery 行为。
- [ ] 按 `research/ui3-contract-reading-map.md` 补读原规范完整相关章节；validate已提示两份大规范注入截断，不能将校验通过等同全文已加载。
- [ ] 复用 `Bar.MultiTarget.Test.cpp` 的六向及快速反向轨迹。正确性固定时间 Step 与性能真实 Scheduler 运行分开；后者加入该场景真实可见 PageControl 与 Freeze 生产回调，仅最终 HWND sink 替换。
- [ ] 测 request→背景/窗口/Draw3 ready→Committed→Bar 消费的响应延迟；另测动画活动时 callback/batch/实际成功 commit 间隔。测试有界记录 completion/revision 关联，不在产品热路径逐帧写盘。
- [ ] 启用现有 raw 详细采样门与 SVG 分段计时；必要的补充指标仅为固定数值。无采样时保持无新增计时/格式化/分配。
- [ ] 六方向各三轮、每轮30次，100%/150%/200%，分开冷/热。冷运行 fresh fixture/资源、跨新宽度峰值；热运行历史容量足够。保存原始 metadata/batches/callbacks 和按方向汇总。
- [ ] 确认 raw sealed、零掉样、身份/epoch有效、真实 commit stamp/末态完整、展开主栏可见。记录 target resize次数、cache reset、SVG解析/栅格/上传、mask和full damage、背景owner等待。旧 centering CSV 无组内resize是反证，不能忽略。
- [ ] 出具排序：是开始晚、Bar自身、Freeze/分页串行、资源冷重建、owner等待，还是基准producer节奏。未证实的候选不默认全部实施。

输出目录建议 `TestResults/ui3-workspace-transition/<baseline-or-candidate>/`，遵循现有忽略规则；小型结论/哈希/命令/退出码写本任务 `verification/`，不提交大批采样文件。

## 2. 稳定容量包络（S1，先证实实际额外增长）

- [ ] 为生产 `BarUiValueClass` 与容量求值接缝建立红测：首次新最大宽度、同一 Back 段、正/负/非零起点、各曲线、中间关键帧、50%前后加入、反向/retarget、禁动画与倍率变化。
- [ ] 断言稳定段容量请求不随当前回弹采样二次放大，包络含实际所有采样；资源足够不重建，必要增长后同段不再增长；不削弱实际几何边界。
- [ ] 复用 `BarUiGetCurveExtrema` / `ResolveBarWindowAnimationRange` 的现有能力最小替换 `AnimationAbsMaximum`，不改动画曲线、duration、末态或公共按钮外观。
- [ ] 真实离屏 renderer 验证首次增长后的资源重建计数/缓存行为及 full replacement；比较前后像素、ULW tuple、命中、反向/失败/epoch恢复。
- [ ] 独立性能对照。如果仅冷态获益，在结果中明确；热态无resize的卡顿仍须继续定位。

主要文件：`Bar.RenderLoop.cpp`；仅在现有范围 helper 缺少必要能力时小改 `Bar.WindowGeometry.h` / 动画接缝；回归落现有 headless/centering/offscreen 测试。默认不改 `Bar.Rendering.cpp` 的设备缓存生命周期。

## 3. 白板共享渲染阻塞（S2，须 S0 证实）

- [ ] 用可控的 owner 完成延迟测试 production readiness 接缝：未完成时其他 Scheduler 客户端仍可推进、同 revision 不重复投递，完整 ACK 前不得提交新背景/工作区。
- [ ] 若等待主导，将 SetBounds/Hide 的有界请求与就绪回执接入既有 Window Service/协调状态；render 不跨owner同步等待，保留单一渲染owner和真实窗口owner。
- [ ] 区分像素提交与后续 owner ACK；若像素已成功且身份/epoch仍有效，等待几何确认时不无条件再次整屏重画。失败仍保留未提交 damage/旧命中，取消或设备失效不能复用旧候选。
- [ ] 新 revision、HWND/generation变化、设备丢失、超时/取消/迟到完成、Hide失败、源补偿和重试均先红后绿。故障路径沿用当前 failure recovery，不改失败预算来隐藏慢响应。
- [ ] 背景本身成本主导而非等待时，先写清具体最小接缝及样本，再决定是否需要一次性准备/分阶段执行；不盲目把重工作丢到新线程、不提前ACK。
- [ ] 同一场景前后对照：主栏成功间隔改善，且全屏背景/页主题/owner显隐无回归。

条件文件：`Whiteboard.cpp` / `Whiteboard.BackgroundState.h`，必要的既有 Window completion接缝；不得为本问题全局改成所有 Window 命令异步。

## 4. 协调通知/重复投影（S3，按样本单独选择）

- [ ] 若背景ACK后出现轮询尾延迟，先红测“Draw3无新revision、背景完成后协调器及时被唤醒”。复用现有轻唤醒/完成通知条件，保留250ms兜底及过期token验证，不用更频繁轮询。
- [ ] 若重复 StateUpdate/表现锁确实明显，测试单次Commit的投影次数及目标幂等，再合并冗余更新。保留工具即时反馈、Recall期限/延期点击、属性关闭和锁外wake。
- [ ] 六向及后台PPT开始/结束/换文稿、白板最小化恢复、中途取消/反向、迟到ACK与失败重试全部回归；pending不冒充Committed。

条件文件：`IdtState.cpp`、`Bar.Main.cpp`、`Bar.Button.cpp` 及既有协调完成接缝。只改证实需要的通知/投影，不做三态架构重写。

## 5. 剩余热点选择（S4）

- [ ] 白板按钮SVG已确认颜色动画可触发每帧解析/栅格/上传；先用已有分段timer证实占比。若为主要热点，补充公共renderer最小资源复用设计及多色/alpha/尺寸/epoch像素矩阵后再实施。
- [ ] full replacement不直接删除。若仍为最大成本，单独证明source/origin/旧新映射和脏区等价后再提局部优化；未知时继续完整替换。
- [ ] 不恢复旧未获可靠收益的mask-slices候选，不顺便整理缓存/字体/按钮体系。复杂度扩大时更新设计并重新审阅。

## 6. 必要验证命令与门禁

先检查当前 Solution/项目和Harness开关，复用本机工具链，不改依赖/SDK/项目来绕过环境错误。按当前用户要求完整 `InkeysRepo.sln Debug|ARM64`，动态定位 ARM64原生MSBuild，超时至少5分钟（建议外层600秒）。示例：

```powershell
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nativeMsbuild = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\arm64\MSBuild.exe' | Select-Object -First 1
if (-not $nativeMsbuild) { throw '未找到 ARM64 原生 MSBuild，先报告环境问题' }
# workaround和MSBuild必须在同一个PowerShell invocation。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $nativeMsbuild InkeysRepo.sln /m /nr:false /p:Configuration=Debug /p:Platform=ARM64
if ($LASTEXITCODE -ne 0) { throw "Solution build failed: $LASTEXITCODE" }
```

下列既有入口在产品窗口初始化前返回；每个命令单独检查退出码、摘要和输出是否齐全，不把最后一行当结果。新freeze性能fixture必须先证明无HWND再纳入执行。

```powershell
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
& .\Build\ARM64\Debug\Inkeys.exe --state-mode-production-test
& .\Build\ARM64\Debug\Inkeys.exe --ui3-workspace-centering-test
& .\Build\ARM64\Debug\Inkeys.exe --bar-eraser-offscreen-test
& .\Build\ARM64\Debug\Inkeys.exe --ui3-multitarget-benchmark
# 修改公共输入/续接时才追加：
& .\Build\ARM64\Debug\Inkeys.exe --ui3-input-worker-test
```

- 预算：热态batch P95≤16.67ms、P99≤33.4ms；同时报告活动成功提交间隔和>50ms来源。冷态、响应等待、掉样/失败另列。若预算仍不达标，不宣称已修复。
- Release性能对照若Debug开销影响归因，沿用Solution的Release|ARM64及显式既有Harness开关，记录开关/哈希；不改变发布默认或用Release替代Debug正确性构建。
- `git diff --ignore-submodules --check`、文件级diff/编码/换行检查；默认子模块status权限问题不得引发配置/权限改动。
- 规范复审覆盖公共按钮单一来源、隐藏锚点/绘制顺序、0.5批次中点、dt=0、字体尾段续帧、脏区成功事务、默认WARP/光效与静止idle。
- 用户已允许必要GUI窗口，但禁止使用Computer Use；当前实施优先无窗口。真实GUI/Office/WPS/DWM/触控及主观手感尚未验收，无窗口软件结果单列，不外推系统/平台支持。

## 7. 回退及交付

- [ ] 每个候选独立保留证据和回退点，未获稳定收益/像素或状态回归则仅撤回该候选；不改其它任务和本分支已有修复。
- [x] 在任务内记录当前首要耗时链、前后分位、未解风险、构建/测试退出码与实际覆盖；主会话已同步timer、容量和诊断合同，剩余性能验收仍进行中。
- [ ] 未获用户当前任务明确授权不commit/push，不自动归档。

## 8. S5：异步窗口所有权实施（用户已批准）

先读 `research/async-window-ownership-design.md` 和 `research/native-pagecontrol-unlimited.md`，并补读规范中render同步等待、PageControl direct-move、Whiteboard ACK及workspace Prepare/Complete的完整相关章节。保留当前未提交的 `PageControl.cpp` 性能计时/驱动改动，它们是A/B基线，不得回退或覆盖其它agent/用户改动。

### P1：PageControl candidate/receipt

- [x] 建立并执行上述边界验证：mailbox 和背景内容失效有生产 helper 的确定性红→绿；幂等、Show/promote、owner 延迟、native 失败、pair 回滚及完整 ACK 门禁通过生产屏外故障注入。未把 API 缺失编译失败或静态同步调用检查冒充执行红测。
- [x] 定义最小固定candidate/receipt及revision发布合同，复用现有Window overlay owner和mailbox/恢复能力；不把desired当真实完成，不新增第二套窗口管理器。
- [x] 保留RenderSurface动画/布局/PresentScene/ULW，将同步SetBounds/Show/Hide改为非阻塞发布与回执消费。完整匹配回执前保留damage、旧命中和UI-ready；pending用Retry而非跨owner等待。
- [x] 普通Show/Hide/SetBounds幂等，Z-order只在Pointer Down promote、workspace/topology/band/owner变化或恢复时执行。不得把逐帧RefreshOverlayGroup简单异步化。
- [x] 用 `INKEYS_PAGE_CONTROL_TRANSITION_PERF=1 --page-control-hidden-test` 做同口径A/B，保存改动前后EXE哈希、frames/summary/metadata；补充队列深度、coalesced/stale命令和candidate→receipt延迟，不能只测UI callback。

### P2：Whiteboard Freeze复用

- [x] RenderBackground的Hide/SetBounds/Show改为同一candidate/receipt；BackgroundPrepared/ inactive ACK使用真实回执，不由UI/协调器直接查询HWND冒充ready。
- [x] 首背景、最终显示、最小化恢复、退出源补偿、取消和旧generation回归；像素已成功而native pending时不无条件重复整屏重画，失效epoch/候选不得复用。

### P3：direct-move与promote

- [x] PageControl pair direct-move改为latest-wins窗口候选和pair mask回执，第二窗失败仍真实回滚；快速反向不播放旧位置。
- [x] Pointer Down PromotePptWindow非阻塞化，保留PPT内部顺序及Bar正下方合同。
- [x] Bar直移沿用现有原子translation/几何屏障迁移直接SetWindowPos，逐像素/命中验证旧位图不被搬到新位置。

### P4：协调器与规范收尾

- [x] StateMonitoring Prepare/Complete改为事件/回执驱动，保留250ms兜底、失败预算、Draw3首帧/fence、PPT session/target identity及pending/Committed。
- [x] 更新rendering-and-ui、whiteboard、canvas-freeze-boundaries、ppt native-session及诊断规范中旧的同步等待/UI直接窗口操作合同。
- [ ] 六向、翻页、任务栏自动升起、拖动/吸附、DPI/显示器、故障恢复、快速反向全回归；完整Solution Debug|ARM64及相关hidden/offscreen/state/headless通过。真实Office/WPS/DWM单列，不从屏外FPS外推。
- [x] 每阶段独立记录证据和回退点；P1/P2未验证前不大范围推进P3/P4。未经用户明确授权不commit/push/归档。

P1/P2 独立门禁已通过，完整命令、真实退出码、EXE 哈希与三轮 A/B 见 [S5 P1/P2 验证](verification/s5-p1p2-validation.md)。P3/P4 实现与本轮自动化回归也已完成，见 [S5 最终验证](verification/s5-final-verification.md)。完整现场覆盖与 R4 帧预算仍未闭环，不将代码/自动化通过写成全任务完成。

- [x] 最终完整 solution、hidden（含真实 pair/Bar/Whiteboard/Workspace 异步）、state、offscreen、bottom-dock、centering、input-worker、headless 退出码均为 0；交替三轮原生 A/B 完整。
- [ ] 真实 Office/WPS、任务栏与物理 DPI/显示器/触控端到端验收；R4 全帧预算。两版共享渲染短测均仅 1/54 组达到目标，保持记录而不调整阈值。
