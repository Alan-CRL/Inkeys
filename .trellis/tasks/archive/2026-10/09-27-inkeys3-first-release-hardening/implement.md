# 实施路线与门禁

## Work 0 — 冻结事实与规划

- [ ] 核对 refs、submodule、活动任务；记录 H0/HC/H2、OS/GPU/显示/电源/构建与效果配置，不可比则保持未验证。
- [ ] 审历史 UI3/Draw3、PPT、窗口、UInk 任务和生产代码，建动作→模块→状态→结果回归表。
- [ ] 写性能场景、原始数据格式、三轮以上重复、warm-up、分位数有效样本量、噪声和门槛；分冷启动、首次、稳态、长期。
- [ ] 冻结 commit 集合、分支清单和 H0 审计终点，早期风险预检。
- [ ] 冻结 Win7 SP1 仅 KB2670838 的矩阵：FL11.0 硬件有/无、HARDWARE/WARP、DComp 不可用→ULW；两种 DWM 模式不得被正式路径选择。
- [ ] 七个子任务各有三份规划文档与有效 JSONL context；`task.py validate` 全通过；独立 reviewer 审范围、所有权、依赖和验证，之后启动当前可交付的子任务。

## Work 1 — 规范与统一入口

- [ ] 绘职责依赖和状态写读表；核对现有统一方法与所有调用点。
- [ ] 把旁路/重复更新改为同一语义入口，保留 revision 条件与线程所有权；按职责做必要最小拆分与中文关键注释。
- [ ] 建迁移表和生产逻辑测试；相关验证、独立 review；完整 Debug|ARM64 Solution 构建。

## Work 2 — UI3

- [ ] 用生产 diagnostics 确认计时口径与输入到成功呈现阶段，记录 scheduler、首次 cache、光影、SVG/path 成本。
- [ ] 分项形成假设，最小优化无效工作、dirty/cache 或明确瓶颈，维持效果；确定性时钟/失败呈现测试与三轮以上前后对照。
- [ ] 独立 review、修正复验、完整 Solution 构建与适用 headless 测试；真机视觉/ULW 未授权则留人工门禁。

## Work 3 — Draw3

- [ ] 单独建立生产输入、队列、modeler、prediction、几何、上传、绘制、Present、history/save 成本分布及资源趋势。
- [ ] 以保留样本和语义为前提做最小优化，覆盖慢/快/折返/抬笔/取消/切工具/多接触、橡皮/PPT/长期文档。
- [ ] 独立 review、相关测试和完整 Solution 构建；软件计时不冒充光学延迟。

## Work 4 — 审计、安全与恢复

- [ ] 每个 commit 读取实际 diff，风险高项追调用链和失败路径；记录 merge/import/未合入及最终 patch 映射。
- [ ] 区分 confirmed/hypothesis/already fixed/not applicable；确认严重问题先给回归再最小修复，独立复审与复验。
- [ ] 依官方 API 合同审 Win7 feature level、swap effect、导入表和禁用 DWM；确认错误优先修复，不靠额外补丁或系统升级。
- [ ] 画崩溃到恢复链路，分别验证正常退出、主动重启、受支持异常、有限重试和最后已提交 UInk；GUI 场景按仓库规则留人工门禁。
- [ ] 阶段结束完整 Solution 构建与适用测试；依赖安全公告用执行时官方原始 advisory 核对。

## Work 5 — 集成与交付

- [ ] Debug|ARM64 完整 Solution；Release 路径及可得 Win32/x64/ARM64 矩阵；工程/ABI/HLSL 变动时适当 clean Rebuild。
- [ ] 核 Win7 SP1+KB2670838 的两类硬件与 WARP 退路；无真机时保留人工设备步骤，不宣称 Win7 已通过。
- [ ] `InkeysHeadlessTests --no-window`、Draw3、PptCOM、脚本与跨模块回归；查真实用例、退出码、产物和失败首因。
- [ ] 检查 PPT 末页/结束页、清屏/撤销/保存、窗口/DPI/fallback、定格/设置 owner、崩溃/恢复与关闭 gate；不可运行项留人工步骤。
- [ ] 删除一次性调试代码/产物，独立 reviewer 审 H0→HF diff 与审计覆盖；最后修补后重测受影响项。
- [ ] 更新 HF、账本和最终报告；保留未提交、未归档、未发布状态和建议分批提交结构。
- [ ] 主 agent 根据最终可选 presenter 决策更新 native-desktop/draw3-integration.md 与 native/platform-and-resources.md 中过时的 DWM 路径；记录 FLIP 文档/用户实测差异，避免 JSONL 旧规范误导后续 agent。

## 操作约束

- 任何产品代码修改前按层读 spec 与 `trellis-before-dev`；子 agent 提示以 `Active task: <实际路径>` 开头，独立检查者直接审 diff/调用链/测试。
- 构建/测试不并发写相同目录，性能采样与构建、扫描不并行；一阶段失败不丢弃其他可继续工作。
- `validation.md` 记命令、配置、退出码、关键输出和人工项；`handoff.md` 在长会话切换前更新所有权与下一步。
