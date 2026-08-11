# UI3 动画架构、性能、代码质量与安全阶段性验收

## Goal

在当前 Inkeys3-UI3 + Draw2 兼容过渡期，对 UI3 Bar 完成一次可测量、可回滚的阶段性收口：先建立 headless baseline，规范现有动画 runtime 与线程所有权，再按证据消除无意义的逐帧工作，随后拆分巨型结构、完成 `Buttom -> Button` 命名收口，并对近期 UI3 代码进行 C++ / concurrency / Direct2D / Direct3D / COM / Win32 正确性审计。

最终目标是在不降低帧率、不关闭动画或光影、不降低视觉质量的前提下，使 UI3 正常动画显著轻于当前实现，完整动态光影尽可能接近普通光影成本；同机主观体验应明显优于 Inkeys2。用户后续已授权受控启动 UI3、点击真实主栏交互并输出固定样本数据；每次测量必须等待完整文件落盘后直接终止进程。主观流畅度与视觉一致性仍由用户在最终 checklist 上人工验收。

## Background And Confirmed Facts

- 审计起点为 `feature/animation` 上的 `a4201c992bfb722ff2ca50c783e243bd1899612c`；实施时必须记录新的实际 HEAD。
- `Bar.Main.cpp` 当前 17,521 行；`Rendering()` 8,874 行；`Interact()` 3,468 行。三项规模估算均由当前源码确认。
- curve、timeline、keyframe、Value/Pct/Color/State 及 SVG/Word content transition 已存在于 `Bar.UI.*`。本任务扩展/整理现有体系，不创建第三套动画 framework。
- `Rendering()` 仍同时承担 target/layout、通用动画推进、五类 widget 全量扫描、lighting、dirty 计算、D2D draw、GDI/ULW present 和 pacing。
- UI3 已有 idle wait、render-once、sustain、shared device epoch、整帧租约、dirty clip/ULW dirty rect、A8/Gaussian mask、gradient/solid brush、SVG/PNG、superellipse、Fine Dial text layout 等 cache。
- drawing activity 当前只触发第三鼠标光的一次 Dormant 检查；第一主光、颜色过渡和普通 UI 动画没有 drawing gate。本任务不得重做 Draw2 调度或恢复整段绘图 quiet。
- `07-26-ui3-lighting-render-performance` 和 `08-03-ui3-render-cache-optimization` 已落地核心 cache/dirty/device 能力，但前者的重复 benchmark、自动测试和部分兼容验证没有完成。
- 从 08-03 cache 提交到当前 HEAD 又有 27 个产品提交修改 Bar core；近期新增 More、几何、粗细 Slider/Fine Dial、颜色选择器等状态需要纳入回归和安全审计。
- 详细源码与历史证据见 `research/current-source-architecture.md`、`research/performance-runtime-baseline.md` 和 `research/git-history-and-task-overlap.md`。

## Scope

### In Scope

- `Inkeys/Inkeys/UI/Bar/` 的 animation、layout/target、rendering、lighting、dirty/present、wake/pacing、interaction、initialization 和 cache hot path。
- 为验证当前 UI3 行为而需要的最小 `IdtD2DPreparation.*`、`IdtDrawpad.cpp`、Win32/D2D/D3D/COM 边界审计；Draw2 只作兼容和 regression protection。
- 与拆分 module/文件和 `Buttom -> Button` 全量改名直接相关的 `Inkeys.vcxproj`、import、符号和引用更新。
- 临时、可删除的 QPC 聚合 instrumentation 与 headless benchmark/test harness。
- 受控 Debug ARM64 UI3 runtime 测量，包括静置帧和主栏展开/收起、模式切换、属性面板开关、粗细 Slider/Fine Dial/预览切换、快速反向、动态光影等生产状态场景；优先使用可重复的临时 Debug-only 场景驱动。
- 最近约一个月 UI3 相关 Git 变更；发现更早且与当前工作直接相关的明确 bug 时可以最小修复。

### Out Of Scope

- 无关窗口、非任务输入、主观视觉结论或未经固定采样边界的 GUI profiler 数据。
- Draw2 调度/绘制算法重构、Draw3 实现或新增 Draw2 coupling。
- 重做 drawing-aware third-light Dormant 合同。
- `08-01-render-pipeline-refactor` 所属的 UI2 弃用、多 HWND 拆分、Setting/PPT/白板统一 device 或公共串行 scheduler。
- `07-17-investigate-unified-ui-d3d11-pipeline` 所属的 Setting 迁移、CSO 或普通窗口化；当前 Setting 已是独立 DX11 + embedded CSO。
- UI 视觉重设计、新功能、新配置/持久化格式和新第三方依赖。
- commit、push、PR 或 task archive；除非后续用户指令另行明确授权并满足 Trellis 对应确认门。

## Requirements

### R0 - Trellis lifecycle and uninterrupted execution

- 本 task 当前保持 `planning`。只有用户在看到本轮最终规划摘要后的后续消息明确批准完整实施，才运行 `task.py start`。
- “Work Phase 0-5”是本 task 的工作阶段，不是 Trellis lifecycle Phase 编号。`task.py start` 后在 Trellis Execute 中按 Work Phase 0 -> 1 -> benchmark -> 2 -> 3 -> benchmark/validation -> 4 -> remove temporary diagnostics -> 5 -> final validation 连续执行。
- Work Phase 之间不得等待人工确认。非阻塞不确定项采用最保守、可逆、可维护的判断并继续；只有无法安全读写仓库、无法构建任何相关目标或缺少实施所必需外部依赖等真实 blocker 才停止。
- GUI runtime 项应在当前环境可可靠定位窗口时自动执行；无法可靠定位或复现时标记 `NOT VERIFIED` 并继续，不得伪造 PASS。主观视觉项始终标记 `MANUAL`。
- 每个 Work Phase 后必须：完整 build；运行可用 headless tests/benchmarks；检查 diff；更新 task execution ledger；记录 completed/findings/measurements/decisions/remaining risks。

### R1 - Work Phase 0: baseline, instrumentation and headless measurements

- 记录 git status、实际 HEAD、分支、构建配置、warning 和 existing failure；不修改与本任务无关的 baseline failure。
- 按根 `AGENTS.md` 使用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，超时至少 5 分钟；不得单独把 `Inkeys.vcxproj` 当作完整构建门。
- 建立临时、默认可完全删除的低开销 diagnostics。优先 QPC、固定容量 numeric samples、聚合计数和 benchmark 结束汇总；禁止 hot path per-frame `printf/std::cout/logger`。
- timing boundary 根据真实控制流划分，至少覆盖可测的 wake/frame、target/layout、animation advancement、lighting preparation、dirty preparation、D2D work、GetDC/ULW/EndDraw 和 sleep/spin；不能 headless 测得的阶段必须明确标为 manual/integration-only。
- headless benchmark 至少覆盖 curve/timeline/value advancement、target no-op、interruption/keyframe、与真实规模相符的 active/inactive widget scan、cache-key lookup、wait/spin 和 wake/shutdown；可行时用现有 WARP 创建设备外的离屏 D2D target，但不得伪造 ULW/DWM 数据。
- 所有结果记录样本量、warm-up、计时器、median/P95、run-to-run noise、CPU/thread cycles 和限制。没有证据的猜测不得进入优化结论。
- 真实 UI 动态场景必须记录输入方式、按钮/状态转换、跳过帧数、采样帧数和输出文件；真实点击与 Debug-only 场景驱动的数据必须明确区分。第三光源需在相关面板展开后使用可重复的圆周指针轨迹走真实输入/lighting 路径，并与普通光影同口径对照。

### R2 - Work Phase 1: animation architecture cleanup

- 将 `Rendering()` 中 `Finish* / Change* / ApplyAnimationCurve` 等通用推进能力收敛到现有 animation infrastructure 或职责明确的相邻 partition；不得另起独立体系。
- 明确并记录 persistent widget/interaction target、cross-thread request 与 render-thread runtime 的所有权。先证明实际读写线程，再决定哪些字段保留 `IdtAtomic`、哪些可以由 rendering thread 独占。
- 保持 `SetTar` same-target no-op、50% batch join、timeline remaining duration、interruption、single middle keyframe、global speed、animation-disabled 和 `animateWhenDisabled` 语义。
- 系统搜索 `elapsed/progress/duration/start/target/animating/transition/curve/easing/lerp`。能减少重复 timing/easing/state 或降低成本的 private animation 才迁移；lighting、Fine Dial physics、SVG/Word content transition 等专用状态可以保留。
- 保持 duration、easing、overshoot、middle keyframe、rapid repeat click、open/close、hover、press/release 和 color transition 的视觉意图；不顺便重新设计 UI。
- Phase 1 完成后重跑 Phase 0 headless benchmark；行为不等价或性能退化的架构改动必须修正或回滚。

### R3 - Work Phase 2: evidence-driven performance investigation

- 第一优先级测量一个局部动画帧实际执行的完整 target/layout 和派生计算，按 Main Button、Main Bar、Draw Attribute、Geometry Attribute、More Panel、Color Picker、Thickness、Lighting、Debug 等 domain 归因。
- 测量 57 个 render-owned value、五个 widget map、按钮/More snapshot 的 full scan 与 atomic traffic；只有 full scan 显著时才选择 active registry、dirty generation、changed-widget list 或 per-domain active flag。
- 审计交互线程、渲染线程和窗口线程的全部 animation/shared-state 读写；不得为了减少 atomic 先破坏多字段事务一致性或引入 data race。
- 测量 enum/map lookup；容器不是预设瓶颈。只有收益超过测量噪声且复杂度合理时才采用 cached reference 或 enum-indexed storage。
- 审计 `UpdateRendering`、`AtomicWaitClass`、`renderOnceFlag`、`sustainFlag` 的 request coalescing、producer storm、consumer clear-window、Cosmetic skip、missed wake、unnecessary wake、idle 和 shutdown。
- 对现有 lighting/cache 记录 hit/miss/create/evict、key stability、geometry/scale invalidation、mask generation spike、Raw Input wake 与 dirty growth；稳态不得重复创建已缓存资源。
- 分段测量 `HighPrecisionWait()` 的 sleep、spin、overshoot 和 thread cycles；不得未经测量删除或以降低 FPS 规避。
- Phase 2 形成按收益、风险和证据排序的 Phase 3 change set。无显著成本的 hypothesis 明确记为 rejected，不得为“理论更快”实施。

### R4 - Work Phase 3: UI3 performance optimization

- 固定优先级：P0 eliminate unnecessary work；P1 reduce full-frame calculation；P2 animation runtime efficiency；P3 lighting/cache；P4 D2D/rendering；P5 frame scheduling。
- 实现并证明：No change -> no recalculation；No active animation -> no animation traversal；No visual change -> no redraw；Unchanged resource -> no rebuild。
- 一个局部动画不得无理由重新执行所有无关 domain 的 target/layout。优先选择最简单可靠的 domain dirty flag、revision/generation、cached layout、event-driven target update 或 active-domain tracking。
- active tracking 如被采用，必须覆盖 target update 登记、interruption、completion removal、device/state reset、ownership 和非 owning lifetime；若 full scan 实测便宜则不引入 registry。
- 减少重复 state snapshot、atomic load/store、duplicate `SetTar`、interpolation、text-layout/resource creation 和过大 dirty region，但不以难维护的 obscure micro-optimization 换取少量指令。
- lighting 优化只能建立在现有 cache 上，重点改善 hit rate、key、invalidation、mask/geometry reuse、dirty region 和 cursor update coalescing；不得缓存快速变化的最终合成帧。
- frame pacing 最后处理。目标是稳定 60 FPS-class 动画与低 CPU，不追求 cosmetic UI 的无意义亚毫秒 busy-spin，也不得简单降低 FPS。
- 每个优化单元都记录 before/after；收益不超过噪声、增加明显复杂度或 benchmark 变差时必须撤销。

### R5 - Work Phase 4: structure, naming and cleanup

- 在性能路径稳定后实质拆分 `Bar.Main.cpp`、`Rendering()` 和 `Interact()`；目标是职责和所有权清晰，不是机械减少行数。
- 默认边界包括 animation runtime、layout/target transitions、rendering/resources、lighting、interaction/input、initialization/coordinator；最终文件/partition 数量由依赖图决定，不制造 circular import。
- `Rendering()` 最终应呈现清晰阶段，例如 snapshot、dirty-domain update、target update、animation advancement、lighting、dirty calculation、draw/present；名称遵循现有项目风格。
- `Interact()` 按 base bar、draw attribute/thickness/color picker、geometry、keyboard/pointer helpers 分解，并保持消息、capture、hover suppression、Fine Dial 和关闭语义。
- 全量完成 `Buttom -> Button`：文件、module/import、class、set、preset enum、member、comment、项目项和全部引用；不得保留半套旧名。`Bar.Bottom.cppm` 是另一现有文件，改名时必须先画清两者职责。
- 清理 stale animation memo、dead code、duplicated helper、obsolete region/comment、无用 include/import，并改善 RAII、const/constexpr 与 ownership；只处理本任务涉及范围。

### R6 - Remove temporary performance facilities

- Phase 4 后强制删除本次调查加入的 QPC scope、temporary counters/histogram、benchmark-only product hook、console/CSV output、debug macro 和仅为一次调查服务的 harness。
- 只有已成为长期 regression benchmark 的 headless test 才可保留，并且必须与产品 hot path 分离、Release 零开销、维护价值明确、最终报告说明原因。
- 删除后重新完整 build/test，并证明产品 Release 路径没有调查负担。

### R7 - Work Phase 5: safety and correctness audit

- 用当时真实 `git log/diff/blame` 审计当前日期向前约一个月的 UI3 commits 和本任务最终 diff；更早的明确相关 bug 可以最小修复。
- C++：UB、lifetime、dangling、iterator invalidation、null、bounds、signed/unsigned、narrowing、uninitialized state、resource ownership。
- Concurrency：`IdtAtomic` 多字段事务、render/interaction/window ownership、mutex/lock ordering、missed wake、Cosmetic skip/pending present、shutdown、detached work 和 stale state。
- D2D/D3D/COM：generation/device loss、resource invalidation、effect input lifetime、context ownership、D2D/GDI interop、HRESULT/BOOL、failed-resource fallback。
- Win32：HWND/message/Raw Input/timer/capture/hook/callback/thread lifetime 与 shutdown。
- 重点验证 research 中记录的 Cosmetic frame 最后一帧、idle shutdown wake、present failure old-bounds、GetDC/ULW recovery、text-format null 和 AtomicWait handoff 风险；只有证据成立才修复。
- 使用现有 compiler warnings 和仓库/工具链允许的 MSVC static analysis；只修真实问题，不机械追求 0 warning，也不处理无关 baseline warning。

### R8 - Compatibility, evidence and reporting

- UI3 功能和视觉意图不减少；不得新增 Draw2 coupling，不妨碍未来 Draw3 替换。
- idle 仍真实休眠；interaction 能及时唤醒；request 可合并但不丢失；animation active 才持续推进。
- 自动验证必须区分 `PASS / FAIL / NOT VERIFIED / MANUAL`，保留命令、退出码、样本数据与现有失败。
- Phase 0、Phase 1、Phase 3 和最终结果形成可比较 measurement table；任何数字都必须来自保存的 headless output，不得依据主观印象。
- 最终提供用户手工 checklist：Inkeys2 同机对比、normal lighting、full dynamic lighting、idle、rapid interactions、展开/收起/换边、Draw2 drawing、DPI/zoom、device failure/Win7 等无法自动执行项目。
- 自动执行完成后若只剩 GUI/manual 项，不得虚报 task 全部验收通过；报告为“工程阶段完成，manual acceptance pending”，保持 task 可继续，等待用户反馈。

### R9 - Prohibited shortcuts

不得：降低 FPS；默认关闭动画或第三光；降低效果质量；删除功能；盲目增加 worker thread 或 D2D multithreading；用更多 CPU 核换流畅；增加 busy spin；无测量重写全部容器；重做现有 cache；缓存最终动态光影帧；把 UI3 卡顿转嫁给 Draw2；为抽象纯度统一专用状态；启动 GUI 或 desktop automation。

## Acceptance Criteria

### Animation and ownership

- [x] 公共 finish/progress/curve/interpolation/state-replacement 逻辑位于现有 animation subsystem 的清晰边界，不再以大组 private lambdas 留在 `Rendering()`。
- [x] persistent target、cross-thread request 与 render-thread runtime 有可审计 ownership；减少 atomic 仅发生在证明线程独占后。
- [x] same-target、50% join、keyframe、interruption、speed/disabled、hover/press/color 等语义由 headless test 或静态 contract 覆盖。
- [x] 专用 lighting/Fine Dial/SVG/Word 状态仅在真正减少重复或风险时统一。

### Performance

- [x] Phase 0 baseline 与 Phase 1/3/final headless measurement 可重复，包含 median/P95/noise 和 CPU/thread-cycle 数据。
- [x] 局部动画执行的无关 target/layout domain、full scan、atomic traffic 或 resource work 有可量化减少；保留的改动均超过测量噪声。
- [x] idle wait、wake/coalescing、Cosmetic skip、shutdown 和 request handoff 通过 headless stress/contract test；无持续 idle render。
- [x] steady-state cache miss/resource creation 符合现有合同，动画终点不形成集中创建 spike。
- [x] full dynamic lighting 不通过降质/禁用作弊；最终人工 checklist 能直接比较 normal/full-lighting 与 Inkeys2。
- [x] 相关 headless benchmark 无 regression；无法自动验证的 ULW/DWM/视觉项目明确为 MANUAL。

### Structure and cleanup

- [x] `Bar.Main.cpp`、`Rendering()`、`Interact()` 完成职责性拆分，coordinator、animation/layout、rendering、lighting、interaction、initialization 的所有权清晰且无循环依赖。
- [x] `Buttom` 在第一方 UI3 Bar 文件、module、类型、enum、member、comment、project item 和引用中完整更正为 `Button`，并确认 `Bar.Bottom.cppm` 的独立职责未误改。
- [x] stale memo、dead/duplicate helper 和本任务涉及的资源/lifetime/format 问题完成最小清理。
- [x] 临时 instrumentation 和一次性 benchmark hook 已删除；若保留长期 benchmark，Release 零开销且理由已记录。

### Safety and compatibility

- [x] 最近约一个月 UI3 commits 与最终 diff 完成 C++、concurrency、D2D/D3D/COM、Win32 审计；每项 finding 有证据、处置和 remaining risk。
- [x] generation、device loss、GetDC/ULW/EndDraw、cache invalidation、Raw Input、timer/capture、detached thread 和 shutdown 路径达到现有可测范围。
- [x] Draw2 当前兼容、activity notification 和第一/第三光合同不回退；不新增 Draw2 coupling，不占用 Draw3 架构空间。
- [x] 完整 `InkeysRepo.sln` `Debug | ARM64` 使用 ARM64 host MSBuild 在每阶段通过到 baseline 可达范围；headless tests、`git diff --check` 和 task context validation 通过。
- [x] 最终产品 diff 不包含未要求的 Setting/PPT/multi-HWND/UI2/Draw3 重构。

## Risks And Deferred Manual Acceptance

- 当前环境可采集真实 UI3 layered-window 的阶段耗时和动态输入场景，但仍无法自动证明用户主观丝滑度、Inkeys2 同机优劣、完整光影视觉或设备/Win7 组合。Work Phase 0-5 仍必须连续完成，最终由用户按 checklist 验收。
- active registry、domain dirty、atomic reduction 和 waitable timer 都是候选，不是预先批准的必选方案；测量可能证明应保持当前实现。
- 巨型 module 拆分和全量命名修复 blast radius 大，必须在性能逻辑稳定后分批构建并保留明确 rollback points。
- 当前两个线程 detached，且多字段动画状态跨线程；所有权重构若无法在一次 task 内以测试证明安全，应选择保守同步边界并记录剩余风险，不得留下半迁移状态。
