# Draw3 画布置顶回归与 RTS 初始化顺序修复

## Goal

恢复产品“画布在 RTS 注册前已进入预期置顶状态”的启动合同，修复 Draw3 接入后画布未置顶、主栏仍置顶的回归，并能区分 API 成功与实际窗口状态。

## Background and evidence

用户报告约 8 月 20 日出现回归，SetWindowPos 返回成功仍出现画布未置顶。历史 Inkeys2 在 2025-02-15 0c0aaf0e99a4、2025-03-26 649de99daecc 均明确将根窗口置顶置于 RTS 创建之前；当前首次和回退 Host 启动前均缺少该步骤。详细文件/行号、历史和 API 边界见 research/。

确定的是启动顺序合同缺失；RTS/Windows 内部为何形成该症状尚未运行验证。8 月 24 日任务已修复周期/放映刷新与失败收敛，未覆盖 RTS 注册前的前提。

## Requirements

- R1：每次产品窗口链创建或重建后，必须完成窗口层级准备，才允许启动对应 Draw3 Host 和 RTS。
- R2：保持根置顶、owner 链、无激活、隐藏窗口和白板非置顶策略；不独立置顶画布/主栏/PPT，不提前显示分页窗。
- R3：窗口准备失败必须阻止该次 Host/RTS 启动；不得将失败视为 DComp 后端故障转入 ULW 掩盖。
- R4：保留已有首帧后 Preview observer/InitialTopmostRefresh、周期刷新、失败清理 deadline 和退出监督流程。
- R5：日志能区分前置准备失败、API 成功但窗口属性/owner 不符，以及后续恢复；验证结果不将静态证据写成实机通过。

## Acceptance Criteria

- AC1/R1：首次 DComp、直接 ULW、DComp 失败后新 HWND 的 ULW 重试，都先完成准备再进入 StartProduct/RTS。
- AC2/R2：只修改根窗口置顶，关键 HWND/owner/WS_EX_TOPMOST 与策略一致；Mag 不可用时使用既有根回退；不显示窗口、不抢焦点。
- AC3/R3：刷新失败或状态不符时不启动 RTS；首次准备失败不触发 DComp 回退。
- AC4/R4：首帧后刷新与 Preview、milestone、既有 cleanup/监督不变。
- AC5/R5：静态检查、已有无窗口测试和完整 Debug | ARM64 Solution 构建记录明确；真实 Windows/笔输入回归仅获授权后执行。
- [x] 规划阶段历史/当前证据与设计、执行计划落盘，当时无产品代码修改。

## Out of scope

窗口树重构、Draw3 渲染后端/RTS 算法修改、新配置、提高周期刷新频率、逐个窗口强制置顶、默认 GUI 测试。

## Risks and deferred verification

历史顺序是产品经验合同，不是微软文档声明的 RTS API 前提。读取 WS_EX_TOPMOST 不等于证明视觉遮挡正确；仍需故障机器实际验证。此前本机完整编译曾超时，不修改依赖/工程绕过。当前无阻塞产品决策；2026-10-12 用户已批准实施；源码、构建和无窗口回归完成，独立审查通过；用户已确认当前任务核验通过，要求结束任务和提交 commit。

## Implementation progress

- 已实现 AC1/AC3 两处前置准备及失败边界；AC2/AC4 经源码审查核对，实际 HWND/视觉效果未测试。
- AC5：完整 Debug ARM64 solution 构建和 --no-window 测试退出码均为 0；未运行 GUI。详见 research/implementation-verification.md。

## User acceptance

- 2026-10-12 用户确认“当前任务核验通过”，授权结束本任务并提交 commit。
- AI 未执行 GUI/真实 RTS 测试；用户验收与已完成构建/无窗口测试分别记录，不推导 Windows 内部机制已获证明。
