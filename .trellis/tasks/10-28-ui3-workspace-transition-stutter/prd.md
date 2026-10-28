# 修复三态切换时 UI3 主栏动画卡顿

## 目标与价值

桌面、PPT、白板切换时主栏动画连续、响应及时，保留既有外观和交互。调查及可审阅修复规划已完成，用户批准后按证据实施最小修复；当前主要修复已验证，性能预算尚未全部达标。

## 背景与已确认事实

- 用户报告三态切换期间明显卡顿；目标为当前唯一产品入口 UI3，复用现有主栏按钮和统一绘制封装。
- 当前分支为 `bugfix/mode`，调查基线为 `1a5e9ff24c6b5cc186696e78bcbd90a7a9d512b4`。
- 已确认居中转场整viewport重画、白板按钮换色重建SVG、白板背景共享串行渲染及同步owner等待的可达成本链。首次容量预留、背景ACK轮询和重复投影为条件候选；96组旧转场轨迹无组内扩容，不能把容量重建当作已测通因。
- 旧多目标性能基准测持续按压/页码，转场正确性fixture使用固定时间Step；调查基线当时没有完整六向真实共享调度性能证据；实施轮次已补齐Bar/PageControl真实Scheduler采样，原生Freeze/协调链仍未覆盖。详细证据与源码锚点以 `findings.md` 和 `research/` 为准。

## 需求与验收映射

| ID | 需求 | 可观察的验收结果 |
| --- | --- | --- |
| R1 | 调查六方向及快速反向，区分响应迟延与动画活动卡顿 | 每条候选有源码调用链、影响方向、触发条件、反证和证据等级；不把历史数据当本轮实测 |
| R2 | 后续修复保持主栏转场与公共UI3能力 | 按钮/工具/属性、Freeze显隐、贴底居中与Desktop基线恢复不跳变、不残影；快速反向续接当前值，同状态不重启动画 |
| R3 | 保持三态安全和资源事务 | pending/Committed、输入fence、PPT身份、首帧/背景/owner回执及失败恢复均不提前放行；失败不发布新命中或消费未提交damage |
| R4 | 可复现地验证性能收益 | 当前二进制的冷/热六向共享调度原始采样完整、sealed且零掉样；热态目标沿用batch P95≤16.67ms/P99≤33.4ms，同时报告活动主栏成功提交间隔及>50ms来源；未达标不得宣称修复完成 |
| R5 | 控制改动及功耗边界 | 最小diff，公共按钮和统一renderer保持单一来源，默认光效/字体/动画轨迹不降级，稳定后回Idle；不引入额外渲染线程或切换默认后端 |
| R6 | UI渲染不跨Window owner阻塞 | PageControl/Whiteboard生产render callback不再同步Submit等待SetBounds/Show/Hide；动画和D2D/ULW保留在UI，其它客户端在owner延迟时仍推进 |
| R7 | 原生窗口状态由Window Service独立管理 | 使用revisioned candidate/receipt、latest-wins合并和实际bounds/visible/generation回执；旧命令不积压，过期回执不覆盖新状态 |
| R8 | 显隐与Z-order按事件和幂等执行 | 重复bounds/显隐不提交；普通Show不逐帧RefreshOverlayGroup；仅明确promote、workspace/topology/band/owner变化及恢复执行必要Z-order |
| R9 | 异步化不削弱呈现安全 | 完整匹配回执前不ConsumeDamage、不发布命中/UI-ready；pair半完成不算成功，失败/取消/代际变化保留旧快照并正确恢复 |
| R10 | 按阶段覆盖同类链路 | P1 PageControl、P2 Whiteboard Freeze、P3 direct-move/Promote、P4 StateMonitoring；每阶段独立回归和性能A/B，不以只搬线程冒充完成 |

## 规划轮次交付验收（历史记录）

- [x] 新任务建立在当前分支，保持 `planning`，不运行 `task.py start`。
- [x] 六方向、关键线程/等待与公共渲染成本调查落盘，已记录未测项和反证。
- [x] `design.md` 给出边界、分阶段候选及选择门；`implement.md` 给出执行顺序、验证矩阵、命令和回退。
- [x] `implement.jsonl` / `check.jsonl` 真实上下文有效，文档及任务元数据检查通过。
- [x] 确认仅本任务文件变化，无业务源码修改、编译、GUI、commit或push。

## 范围之外

- 规划轮次不改代码已完成；实施轮次仅作本任务必要源码/测试/spec修改，不改无关任务、依赖或构建配置，不切换分支或创建worktree。
- 不另造平行三态、按钮或渲染架构；S5仅按用户批准的UI候选帧/Window Service异步所有权调整既有接缝，不直接恢复旧无可靠收益的mask分片候选，不以关闭光效/禁动画/降低帧率掩盖卡顿。
- 实施轮次优先离屏；用户已允许必要GUI窗口但不使用Computer Use。本次未启动GUI，真实Office/WPS、ULW/DWM、触控和主观手感仍未验收，不据无窗口结果扩大平台支持结论。
- 未经当前任务明确授权不commit/push，不自动归档。

## 决策状态

规划不存在必须由用户补充的范围问题；尚待技术采样确认首要耗时占比，已作为实施S0门槛，不预选所有优化。2026-10-28用户已在最终方案之后明确批准实施；优先离屏，必要时允许GUI窗口但不使用Computer Use。

## 实施轮次验收状态（2026-10-28）

| ID | 当前结论 |
| --- | --- |
| R1 | 已确认调度粗粒度等待和过大target容量；六向真实Scheduler采样已完成。原生协调/Freeze端到端延迟未覆盖。 |
| R2/R3 | 当前改动的离屏几何、快速反向、失败恢复、资源/成功事务及生产状态回归通过；未修改原生ACK/fence链，真实窗口交接尚未实测。 |
| R4 | 未完成。同Debug对照明显改善，最终Release热态24/54组通过预算；继续独立SVG细分，不宣称稳定60FPS或完整修复。 |
| R5 | 保留公共按钮/renderer、默认WARP/光效/动画；没有新渲染线程、全局timer period、commit或push。 |

实际构建、分母、版本哈希与未达标样本以verification/final-verification.md为准；新增计量另记verification/svg-numeric-timing.md，不混用二进制版本。

## S5 实施状态（2026-10-29）

R6–R10 的异步窗口所有权、latest-wins/回执、幂等与事件层级、pair/Bar 直移、原协调器异步推进已实现；完整 Debug|ARM64 及本轮自动化正确性回归通过。真实四 HWND 交替三轮 A/B 的 batch P95 11.866→3.178ms（约下降73.2%），候选→回执与实际成功间隔均有记录。

R4 仍未闭环：不含 native Window Service 的共享 Scheduler 三倍率短测，两版均仅1/54组达到16.67/33.4ms预算；未证明消除剩余绘制/GetDC成本或用户现场所有卡顿。真实 Office/WPS/DWM/设备端到端未验收。任务继续 in_progress；不 commit/push/归档。证据、完整哈希、命令/退出码和限制见 [S5 最终验证](verification/s5-final-verification.md)。
