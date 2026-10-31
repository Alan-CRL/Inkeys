# 独立规划审查（通过）

- 审查日期：2026-09-27。
- 范围：父任务及 A–G 七个子任务的 `prd.md`、`design.md`、`implement.md`、`implement.jsonl`、`check.jsonl`，父任务证据账本、矩阵与两份基线/架构研究；独立执行八个 `task.py validate` 均退出 0。未审产品改动、未运行构建或 GUI。
- 本文件仅审规划可执行性。`task.py validate` 检查上下文文件格式，不能替代需求、依赖和验证设计审查。

## 初审缺口与复审结果

### P0：生产 Draw3 规范仍要求已禁用的 DWM 回退（已修正）

初审时 `.trellis/spec/native-desktop/draw3-integration.md:74,379` 仍要求 DComp→DWM2→DWM→ULW，`native-desktop/index.md:41` 仍把 DWM 写成生产 presenter。主 agent 已把生产规范改成首发 DComp→ULW、两种 DWM 的自动/强制路径均禁用、ULW 保留 FLIP_SEQUENTIAL，并明确 H0 源码尚未对齐；独立 demo 的 `native/platform-and-resources.md` 顶部已加适用范围注。复审 `git diff --numstat` 为三个规范文件合计 8 增 4 删，`git diff --check` 无输出。**生产选择代码仍待实施，规范文字不能算功能通过**；安全兼容子任务须对自动/强制入口、DComp 首次失败的 HWND 重建和 ULW 成功呈现做回归。

### P1：子任务自身缺少明确前置依赖和阶段出口（已修正）

初审时父 `design.md` 有 Work 0→5 图，但七个子任务自己的 `prd.md`/`implement.md` 未写前置依赖与出口。主 agent 随后已在七项子任务分别补入入口/前置、出口与读写并行边界；复审可从阻塞清单移除。执行时仍须在账本记录每个出口的实际状态，不能因文档有门禁就自动视为通过。

### P1：上下文清单有效但关键生产规范覆盖偏窄（已修正）

初审时八任务 JSONL 都有真实条目，但若干关键生产规范未进入实施/检查上下文：状态检查缺 `cpp-conventions.md`、Draw3 实施缺生产 `input-and-ink.md`、UI3 检查缺呈现/诊断规范、安全检查缺 Win7 平台约束。主 agent 已扩充各 manifest；复审全部引用路径存在，八个 `task.py validate` 均退出 0。独立 demo 的 `native/runtime-and-rendering.md` 只能作对照，不能覆盖生产 `native-desktop/draw3-integration.md`；派发时仍按路径补读因长度截断的规范。

### P1：崩溃恢复验收需拆成文件完整与新进程可见（已修正）

`research/architecture-and-validation.md` 只找到 Desktop `SubmitLoad` 用于当前进程内 Clear 撤销，尚未找到启动时加载旧 Desktop UInk 的生产入口，跨进程 PPT 恢复也记为未开放。主 agent 已在 `crash-restart-recovery/prd.md` 和 `design.md` 明确分开“最后已提交文件完整且页身份正确”和“新进程可见恢复”，并规定未开放能力写不适用、不能为了验收打开新入口；复审可关闭此缺口。

## 需继续核对

1. Win7 FLIP 纠偏：用户明确报告 Win7 SP1+仅 KB2670838 的 `FLIP_SEQUENTIAL` 实测可用，保留 FLIP、不引入 bitblt 回退。父/安全/集成计划、F-007 与兼容矩阵目前已按此修正；微软通用文档与用户实测应各列来源，本轮无目标真机时仍标需要人工。实施回归要同时覆盖 Hardware FL11.0、有硬件但无 FL11.0 时的 WARP、DComp 不可用后的 ULW、两种 DWM 不可被强制选择。UI3 默认 WARP，Draw3 Hardware→WARP，须按实际各自策略记录，不把“都有设备创建代码”当作同一运行路径。
2. 已复核 `research/baseline-sources.md`：HC 的强候选是 2026-08-11 成功的 GitHub Actions run `31487748238`、提交 `82f7b7c0` 及其 ARM64 artifact `9100206152`；H2 的强候选是正式 Release `20260713a`、tag `0d9751b9` 及 ARM64 ZIP。研究正确区分了 archive SHA-256、内层 EXE 与用户实际安装包；没有后两者时，HC 不能称唯一定位、H2 也不能自动等同用户实际使用版。用户安装包与同机 Release/同效果对照是后续证据门禁，不阻塞 H0→HF 工作。
3. `audit-coverage.tsv` 已初始化 611 个候选（H0 可达 587、未合入近期 24），目前逐 diff 已审 0；远端 refs 完整性、未合入分支相关性、旧日期近期合入映射仍是 Work 0/4 任务。此统计只证明候选账本建立，不能标为审查通过。
4. 性能门槛、三轮与分位数口径已在优化前记录。正式 `performance.md` 还应在 Work 0 填具体采样入口、原始数据路径/格式和本机环境；当前生产 UI3 汇总日志不能直接提供逐帧 P95/P99，生产 Draw3 Host 尚未启用现有 demo metrics，不能拿旧 demo 成绩替代集成产品。
5. 基线研究取得本机 Windows 11 ARM64 build `26200.9457`、Snapdragon X1E80100、Adreno X1-85 驱动 `31.0.137.0`，并发现 Oray 虚拟显示设备。研究没有取得完整 KB 清单或实际 renderer/presenter；这些字段保持未验证是正确的。后续性能采样若发生在虚拟显示会话，只能代表该会话，不能外推实体屏体验。

## 当前结论

规划范围、文件所有权、子任务依赖、验收口径、未提交边界与人工门禁均已核对；初审缺口已修正。**可以执行 `task.py start`，从 Work 0 开始实施。** HC/H2 用户实际安装包、真 Win7 两类 GPU、GUI 体验、成功 Present 与光学延迟、611 个候选 commit 的逐项审查仍属待执行工作，不能因规划通过升级为验证通过或发布就绪。基线研究和当前任务文档已为这些缺口保留明确证据状态。
