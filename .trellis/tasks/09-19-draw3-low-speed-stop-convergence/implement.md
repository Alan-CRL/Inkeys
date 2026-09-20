# Draw3 低速停笔预测收敛修复实施计划

## 当前提交收尾（用户复测后的范围修正）

- 高速大曲率转弯末端拐动未解决；用户观察物理轨迹偏内、建模墨迹偏外，Up补到真实终点造成拐动。当前没有选定修复方案，暂不继续实现，保留开启任务。
- 撤销新增cubic重建与其专属断言，避免保留无已验证收益的曲率/半径干预；保留显示时间锁、pre-Up边界/失败回退保护、原有收敛/续画、有限宽度AA及有界诊断。
- 下文为前轮历史实施/验证记录，不能据此声称末钩已修复。移除补偿后重新构建与检查，再按用户本次明确授权提交一个commit；不push、不归档。

### 本次范围调整验证

- 两侧删除 `CorrectPenTerminalHook`、cubic重建及专属用例/修正标志；`CapturePenTerminalTrace` 只采样最多8个模型/接纳位置，不修改点、半径或输入。保留物理Up锁、terminalFirstPoint与失败回退保护，专项保留 `TestPhysicalUpTipTime` 和 `TestTerminalFallbackBoundary`。
- 移除后完整 `inkStrokeModelerTest.sln Debug|ARM64` 与 `InkeysRepo.sln Debug|ARM64` 构建退出0，日志 `TestResults/defer-hook-native-build.log`、`defer-hook-product-build.log`。
- `inkStrokeModelerTestTests.exe --release-tail-tests-only` 退出0；真实等待进程退出的 `Inkeys.exe --draw3-hidden-test` 退出0，`defer-hook-hidden.stderr.log` 为PASS且无FAIL，覆盖既有停笔/续画和抬笔生命周期。
- AA及GPU测试源码本次未改，沿用前轮5184组硬件读回与完整控制台通过结果，不重复无变化矩阵。独立审查确认镜像、原编码/换行及保留的端点安全；末钩仍未解决，不将这些通过结果当成其验收。

## 当前执行：Release Tail And Thin Coverage

1. 以 d4456c21 干净工作区为基线；先补物理 Up 时间/侧向末钩和薄线像素退化证据。
2. 并行实现 CPU 抬笔冻结/终态曲线与 PS 薄线有限宽度覆盖；两侧保持同构，源文件原编码换行保留。
3. 添加实际 D3D 离屏回读测试和隐藏产品设备/延迟/续接测试。测试源码所有权分离，构建由主会话统一协调避免同时改同一测试文件。
4. 完整两套 solution Debug|ARM64（ARM64 原生 MSBuild、PATH 规范化），验证四个 shader 资源链；运行控制台、headless --no-window、hidden 和离屏GPU验收。
5. 独立 trellis-check 审查全部差异和既有停笔/续画回归，更新规范与验收。按用户要求不提交、不归档；可见 GUI 和实体手感未执行时明确列出。

### 前轮 Release Tail / Thin Coverage 自动化记录（末钩尝试现已撤回）

- 前轮实现：首次物理Up锁显示时间/可变尾段边界，候选/完成/Stored统一使用，成功续接解除；曾尝试受限cubic替换新增终态后缀（现已撤回）；回退也保护Up前锚点。隐藏诊断保存有界原始Move/Up和模型/接纳尾段。
- 薄线：两侧PS用真实SV_Position像素中心和有限宽度双边覆盖；局部0.5–1px半径混合旧AA，两端均>=1px直接走旧路径；无GPU点布局、库或模型参数改动。
- 审查修复：防止失败回退改写pre-Up点；deferred Up无输出不提前消耗曲线处理资格；隐藏测试必须观察真正awaitingReconnect候选，不能重复比较完成快照。
- `inkStrokeModelerTest.sln Debug|ARM64` 最终构建退出0，日志 `TestResults/release-thin-native-build-4.log`；完整 `ARM64/Debug/inkStrokeModelerTestTests.exe` 退出0，日志 `release-thin-console-verified.log`。既有停笔/续画回归以及新物理Up、曲线/弧长插值/尖角/预算测试全部通过。
- 实际GPU：Qualcomm Adreno X1-85，5184组width/angle/XY-phase，failures=0；包含真实MRT/双源合成、数值oracle、分段、MAX幂等、L0/L1、变半径与端帽。旧/新0.05px对称线零覆盖截面为112/0。
- 近似边界明确：1.5px过渡区某相位旧/新/oracle有效宽度为1.94598/1.71765/1.71663；严格验证规定blend和不劣于旧路径，而不是任意放宽误差。纯薄线分段仍保持严格量化界；粗线旧AA图元分段差异保持原样。
- `InkeysRepo.sln Debug|ARM64` 最终退出0，日志 `TestResults/release-thin-product-build-final.log`。初次两侧均有四个shader编译成功及后续资源链接证据；源码均用现有solution/MSBuild链路编译。
- `InkeysHeadlessTests.exe --no-window` 退出0。`Inkeys.exe --draw3-hidden-test` 通过Start-Process -Wait取得实际退出0，日志 `release-thin-hidden-verified.stderr.log` 为PASS且无FAIL。覆盖Mouse/Touch/Pen硬件压感/Pen无压力、快速/停稳/延迟Up、真实候选等待、同一Touch笔画成功续接解锁后继续Move。
- 隐藏续接fixture不再用原始速度推测预测落点：实测预测误差拒绝13px外推点，改用正常同位重新接触后Move12px，沿原预测判定匹配；未改变续接策略。
- 沙箱内既有存储测试出现I/O失败，正常权限重跑通过；未为环境问题改源。裸GUI子系统命令的空输出/即时返回不计为成功，以等待真实进程退出并捕获stderr为准。
- 构建输出日志保留在忽略的TestResults；生成的四个未跟踪宿主cso与被构建改写的PptCOM.dll由主会话定点清理/恢复，不提交二进制副产物。
- 可见GUI、实体设备手感和D3D Debug Layer未验证。本轮不commit/push，不结束或归档任务。

## Implementation

1. 在测试宿主与产品各自的 `InkPrediction` / `StrokeGeometry` 增加同构的 modeled-tip 收敛判定：finite 检查、raw endpoint 误差、`Result::velocity × frame interval` 门槛。
2. 扩展 `UpdateIdleFreezeState`，把 `modelSettled` 作为必要条件；保留 live-tip 停笔时长和三帧 L0 position/radius 稳定门槛。
3. 在两侧 `DrawingController` 的帧首 raw snapshot 消费后，为未结束、非 reconnect 的普通笔 runtime（产品 Pen/HardPen，测试宿主 Pen）增加 stationary model advance：
   - 使用最近接受进入模型路径的 snapshot position/stylus state 与单调 frame logical time；
   - `inputSpeed=-1`，不写真实速度相关字段；
   - 仅在 model 未 settled 且 stroke 未 frozen 时执行；
   - stationary `Update` 明确失败后按 contact 锁存，避免逐帧重试和日志；下一份成功真实输入或 reconnect 解除锁存；
   - 添加简短中文注释说明不能用重复 prediction 代替模型追赶。
4. 在渲染更新后重新计算 model convergence，并传入 freeze 判定；确认 resize/reconnect/Up 分支不执行 synthetic advance。
5. 若模型级测试表明 stationary 输出近重复点仍明显超预算，只在 idle-only append 路径增加 position/radius 容差合并；不要修改真实 snapshot 的 append 行为。

## Tests

1. 在 `inkStrokeModelerTestTests/contact_input_tests.cpp` 用真实 `StrokeModeler` 构造低速轨迹：证明只重复 `Predict` 时内部 modeled endpoint 不前进，而 stationary Update 后会追到 raw endpoint。
2. 断言收敛前 endpoint error/velocity gate 阻止冻结，收敛后三帧才冻结。
3. 断言 frozen 后继续模拟长按不会增加 modeled/real/L0 点数。
4. stationary settle 后送入下一份真实 Move，断言时间单调、Update 成功，预测/可见 endpoint 不出现与实际位移不成比例的甩出。
5. 同坐标 Up 后最终 endpoint/radius 相对停稳 L0 不超过现有视觉容差。
6. 覆盖 prediction disabled/空 prediction、非 finite result 以及两个独立 ActiveStroke 的收敛隔离。
7. 静态/现有测试确认 Eraser、Highlighter、Laser、Shape 未进入新增路径。

## Validation

1. `git diff --check`，检查仅任务文件和预期源码/测试变更，确认 BOM/CRLF 无大面积 diff。
2. 运行 `inkStrokeModelerTestTests` 的无窗口测试，确认新增及现有断言通过。
3. 按仓库规则定位 Visual Studio ARM64 原生 MSBuild，在同一 PowerShell invocation 中移除重复 `PATH` 并设置 `MSBUILDDISABLENODEREUSE=1`。
4. 构建完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64`，至少允许 5 分钟；核对退出码和 shader/resource 链。
5. 按根 `AGENTS.md` 用 ARM64 原生 MSBuild 构建完整 `InkeysRepo.sln Debug|ARM64`，再运行 `InkeysHeadlessTests.exe --no-window`。
6. GUI/Computer Use 未获当前授权，不自动启动主窗口。交付时给出人工复现清单：低速停笔、停稳后原地 Up、停稳后再移动、多 contact 一动一停、prediction disabled 对照。

## Approved Follow-up Execution (2026-09-20)

1. 先补停笔老化/同位 Up、恢复前缀拒收后解锁、真实逐帧稀疏输入回归；记录当前失败证据，再实现。
2. 同步产品/宿主的显式显示时间、合成输出年龄、完成态一致与冻结条件；基础宽度状态不因视觉笔锋改变。
3. 恢复状态固定旧停点，接纳安全后缀解除门禁；覆盖空帧、90/180 度方向改变与同位新输入。
4. 压缩模型已收敛静止时段，保留真实速度/显示时间；测试 10 秒及超长停留后 Move/Up 输出预算。
5. 扩展隐藏产品链路诊断及测试；测试必须推进帧并检查真实 L0/提交状态，而不是空断言循环。
6. ARM64 原生 MSBuild，先在同一 PowerShell invocation 规范化 PATH，构建两个已有 solution Debug|ARM64；执行控制台、headless --no-window、--draw3-hidden-test。不开可见 GUI。
7. trellis-check 独立审查后更新规范及结果。本轮用户明确要求不 commit、不归档，任务继续 in_progress。

### Baseline Red Evidence

- 在 c88d8989 实现上新增 `TestStationaryTipAging`，实际调用 `RebuildL0DrawPoints` 与 `BuildCompletedPenTail`。
- 完整 `inkStrokeModelerTest.sln Debug|ARM64` 构建成功；测试出现两条预期失败：停笔一秒的 L0 端点半径未恢复 2.5px，同位完成态半径也未恢复 2.5px。
- 本证据验证显示时间问题，不把它当成中心线或完整控制器链路的验收结论。

### Follow-up Implementation And Final Verification

- 已同步两侧显示时间笔锋老化、完成态一致、固定旧停点恢复、长静止模型时间压缩；基础笔宽估算器继续使用模型时钟。终态失败回退也保持显示时间单调。
- `PenRuntimeDiagnostics` 仅在隐藏测试开启；真实控制器发布接纳末点误差、基础/显示半径、模型调用/real/L0/L1 计数及 frozen/recovering，Host 通过既有诊断锁传输。
- 回归：61 组逐帧稀疏曲线/输入间隔/帧率/方向与宽度组合、20 步半径恢复、实际十秒静止帧、一小时后 Up、模拟压感长停恢复、跨批全部拒收/空帧/安全前缀后回摆/安全后缀解锁。
- 独立 `trellis-check` 发现的终态回退时钟问题已修复；新增恢复分支测试后最后检查通过。核心 helper 两侧一致，无第三方、HLSL 或工程配置改动，原文件 BOM 状态和 CRLF 保持。
- 完整 `inkStrokeModelerTest.sln Debug|ARM64` 最终构建退出 0；`ARM64/Debug/inkStrokeModelerTestTests.exe` 最终退出 0。
- 完整 `InkeysRepo.sln Debug|ARM64` 最终构建退出 0；`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 退出 0。
- `Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test` 最终退出 0，`[Draw3Hidden] PASS`；最终 PenDwell 记录 endpointError=0、tipRadius=baseRadius=4.54855。隐藏用例验证停稳点数/模型调用不变、同向/180/90 度恢复及同位 Up。
- 构建使用当前 VS 安装的 ARM64 原生 MSBuild；同一 PowerShell invocation 先清理重复 PATH 环境并禁用 node reuse。未通过改源码或升级依赖绕过环境问题。
- 主会话清除四个本轮生成的未跟踪 shader `.cso`，并将开始时干净、被构建改写的 `Inkeys/PptCOM.dll` 恢复为 HEAD 内容；保留实际构建输出供后续使用。
- 可见 GUI、真实设备手感与 D3D Debug Layer 未验证。按用户要求本轮不 commit/push、不结束或归档任务。

## Review Gates

- 收敛条件不能退化为固定延迟或只比较 prediction 数组。
- synthetic idle sample 不得写入真实速度/压力基准。
- frozen 后点数必须恒定，不能靠长期重复点维持稳定。
- 不修改第三方 modeler、HLSL 或其他工具语义。

## Rollback Point

源码改动可按三处独立回滚：controller stationary advance、收敛 helper、freeze predicate；没有持久格式或资源迁移。

## Follow-up Plan: Endpoint Monotonicity（已于 2026-09-20 完成）

1. 先在 `contact_input_tests.cpp` 增加只记录轨迹、不改产品逻辑的模型级用例，量化低/中/高速瞬停与高速即时 Up 的每个 Result：endpoint distance、approach-axis projection、首次最近点后的反向距离、terminal radius 和总点数。
2. 为 `RuntimeStroke`/`ActiveStroke` 增加最小 endpoint-settling 状态和复用 scratch；真实 Move 仍走现有 `modeledResults -> realPoints`，stationary/terminal Update 改走 scratch。
3. 实现纯 helper 对 scratch 做 endpoint admission：距离单调、stop-plane 上限、精确 raw endpoint 去重；产品和测试宿主保持同构。
4. settling 时禁止 Kalman future extension 进入 L0；visible pinned 后只更新内部 convergence snapshot，不增长任何几何点列。恢复 Move 的首批输出继续通过恢复走廊，确认向新 endpoint 单调前进后回到 Tracking。
5. `kUp` 前记录 pre-Up accepted tip，整批 terminal scratch 门禁后才构造完成 centerline；Stored Stroke、即时 raster 和重放共用 sanitized 数据。
6. 调整 SoftPen completed taper：有效移动笔画的 raw Up endpoint 使用 fully-developed taper floor，并向前取得足够真实上下文；保留 click/short stroke 与 HardPen 语义。
7. 扩展测试到 30/60/120/240 FPS、Kalman/StrokeEnd/Disabled、MouseLeft/MouseRight、Pen/HardPen、同位 Up/前移 Up/曲线急停、pinned 后恢复 Move 和 10 秒点数恒定。
8. 实现后再执行既有 ARM64 两套 solution build、控制台测试、headless/hidden 集成；真实 GUI 由用户授权后按速度矩阵验收。

该 follow-up 的产品与测试源码已按上述计划完成；具体落地范围和验证结果见下方
“Endpoint Monotonicity Implementation Results”。本节保留为实施追溯，而非待办项。

## Validation Results (2026-09-19)

- `inkStrokeModelerTest.sln /t:Build /m:1 Debug|ARM64`：通过。
- `ARM64/Debug/inkStrokeModelerTestTests.exe`：通过，包含低速停笔、prediction 开/关、finite gate、长停点数恒定、恢复 Move 与 Up 收敛回归。
- `InkeysRepo.sln /t:Build /m:1 Debug|ARM64`：通过；仅有既有第三方数值转换 warning。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：通过。
- `Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test`：通过，覆盖产品 Host、RTS、绘制线程和 presenter 隐藏集成。
- `git diff --check`、Trellis context validate、源码 UTF-8 BOM + CRLF 检查：通过。
- 可见 GUI 与真实设备人工复现未执行（仓库规则禁止默认启动交互式窗口）；保留为维护者验收项。

## Endpoint Monotonicity Implementation Results (2026-09-20)

- `ActiveStroke` 已增加复用 `modelScratch`、最新内部 Result 快照和 endpoint admission 状态；正常 Tracking 的累计 `modeledResults` 路径保持不变。
- 单个目标帧内的 raw sample 空洞继续保留 Kalman Tracking；sample age 超过一个目标帧后才进入 endpoint settling，避免高渲染率下 prediction 常态闪断。
- 测试宿主 Pen 与产品 Pen/HardPen 的 stationary、physical Up、停笔后恢复首批和 reconnect 恢复首批已接入同构门禁。settling/pinned 期间不再把 Kalman future extension 或新的稳定前缀提交送入可见层；模型仍可用有界 scratch 后台收敛。
- endpoint helper 会拒绝首个越界、距离不再改善或回摆候选及其后缀，只保留安全前缀并至多钉住一个精确 raw endpoint；已钉住时后续 scratch 仅更新内部收敛快照，不增长中心线。
- physical Up 的整批模型输出进入 terminal scratch，完成态 raster 与 Stored Stroke 继续共用净化后的 `realPoints`。SoftPen 有效移动完成态使用 fully-developed taper floor；click/极短划与 HardPen 保持原语义。
- 新增纯轨迹门禁、整批 Up、未收敛恢复、精确 endpoint 去重、交替 raw/empty 帧、30/60/120/240 FPS × 低/中/高速 × Kalman/StrokeEnd/Disabled、internal settled `<=200ms`、10 秒点数恒定及 SoftPen/HardPen/click 边界测试。
- `inkStrokeModelerTest.sln Debug|ARM64`、`ARM64/Debug/inkStrokeModelerTestTests.exe`、`InkeysRepo.sln Debug|ARM64`、`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`、`Build/ARM64/Debug/Inkeys.exe --draw3-hidden-test` 均通过；编译 warning 仍为既有第三方编码/数值转换 warning。
- 可见 GUI 与真实鼠标/触控笔速度矩阵未执行；按仓库规则保留为维护者人工验收项。本轮未创建 commit，也未结束或归档任务。
