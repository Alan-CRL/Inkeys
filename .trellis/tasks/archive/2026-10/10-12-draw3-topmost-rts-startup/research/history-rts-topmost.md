# Research: 历史 RTS 注册前置顶合同与 8 月回归调查

- Query: Inkeys2 两次历史提交是否要求 RTS 注册前置顶；8 月调查是否验证或修复了此顺序。
- Scope: mixed（本地任务档案、Trellis 会话历史、GitHub 只读提交）
- Date: 2026-10-12

## Findings

### 已确认的历史合同

1. `0c0aaf0e99a4d5ced0902c1d692855ef61449f96`，作者时间 `2025-02-15T04:52:28Z`（上海 12:52），消息 `[auto]20250215 1252 update`。
   - `智绘教/IdtMain.cpp:869` 注释：`// 画板窗口在注册 RTS 前必须拥有置顶属性，在显示前先进行一次全局置顶`。
   - `:870` 调用 `SetWindowPos(magnifierWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)`。
   - `:885` 才调用 `CreateRealTimeStylus(drawpad_window)`。
   - 本次 patch 把原先 Freeze/Drawpad 的置顶操作收敛为 MagnifierHost 根置顶；窗口创建已有 MagnifierHost -> Freeze -> Drawpad owner 关系。
   - 原 patch 的删除部分已存在相同的 RTS 前置注释。因此这条经验早于 2025-02-15，本提交能证明当日保留并迁移它，不能证明当日首次发明或修复。
2. `649de99daecccb4e39fef7a6a8eb7a2c80968cb1`，作者时间 `2025-03-26T09:08:13Z`（上海 17:08），消息 `[auto]20250326 1708 update`。
   - `智绘教/IdtMain.cpp:872` 保留相同注释，`:873` 仍调用 MagnifierHost `HWND_TOPMOST`，`:890` 才 `CreateRealTimeStylus(drawpad_window)`。
   - 本次 patch 恢复 Freeze 的 owner=magnifierWindow，并将前置操作从 Freeze 改回 MagnifierHost；不是逐个置顶所有窗口。
3. 两个版本也都提前配置 Drawpad 的 `WS_EX_NOACTIVATE`。修复方案应保护完整的 HWND 初始化条件，而不把历史做法简化成首帧后周期刷新。

证据来源：本研究通过 `gh api repos/Alan-CRL/Inkeys/commits/<sha>` 验证上述日期、完整 SHA 和 IdtMain.cpp patch；具体旧文件行号及前后 RTS 调用顺序由主会话本地 `git show <sha>:智绘教/IdtMain.cpp` 独立核验提供。研究代理本身未执行 git 操作。

外部引用（历史源码，无软件版本猜测）：
- https://github.com/Alan-CRL/Inkeys/commit/0c0aaf0e99a4d5ced0902c1d692855ef61449f96
- https://github.com/Alan-CRL/Inkeys/blob/0c0aaf0e99a4d5ced0902c1d692855ef61449f96/智绘教/IdtMain.cpp#L869
- https://github.com/Alan-CRL/Inkeys/commit/649de99daecccb4e39fef7a6a8eb7a2c80968cb1
- https://github.com/Alan-CRL/Inkeys/blob/649de99daecccb4e39fef7a6a8eb7a2c80968cb1/智绘教/IdtMain.cpp#L872

### 8 月调查与修复的实际范围

Files found:
- `.trellis/tasks/archive/2026-09/08-24-overlay-topmost-presentation-recovery/prd.md`：非白板 overlay 置顶、PPT 可见、窗口操作失败重试要求及验收。
- 同目录 `design.md`：保留单根置顶、PPT false->true 即时刷新、PageControl/Drawpad 提交失败收敛。
- 同目录 `implement.md:5-11`：实际修改清单，没有 RTS 初始化前置条件。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1640-1643`：HWND 线程归属、当前 owner 链、唯一根置顶、PPT 边沿刷新；`:1714` 隐藏 HWND 的外部 topmost 竞争验证。

Trellis mem 检索：
- 在当前 cwd 搜索 `置顶 --since 2026-08-15 --until 2026-08-30`，随后扩大到 `--global --limit 30`。
- 找到 `01a031a5-032f-7031-ad16-b3906b25e9e7` 的 8 月 24 日调查，保留的转录含原用户报告：非白板进入 PPT 时可能只有画布/主栏但无分页；桌面时主栏可见而画布在其他窗口下面。
- 会话 `01a03185-7038-7923-ad84-b8a175fd9ab1`，2026-08-24，`context --grep 成功` 的 turn 41 记录已修正先前判断：无真实 RTS 的屏幕外 HWND 探针确认，无论根隐藏与否，重复置顶根均能整体抬升 owner 树；不能把外部 topmost 插入树中或 Bar/PPT 的 HWND_TOP 解释为已证实主因。
- 同 turn 41 最终方案改为 PPT 即时根刷新、失败传播、surface/呈现状态诊断、隐藏 HWND 回归测试。与归档 PRD/implement 一致。
- 归档 `implement.md:58-59` 记录 8 月 24 日 ARM64 solution build 与隐藏 HWND 测试通过；这些测试不包含真实 RTS 注册，因此不能证明初始化顺序满足历史合同。

### 结论及规划约束

历史证据足以确认，前置根置顶是明确且长期保留的项目经验合同。8 月 24 日的工作保护了运行时恢复与调用失败收敛，没有恢复或验证 RTS 注册前置顶；该修复不能覆盖此次初始化顺序回归。

不应重新改成逐窗口 HWND_TOPMOST，也不应通过更短定时器替代初始化条件。应由主会话核对现有 Draw3 主启动/回退入口，将 Window Service 根刷新与初始化前检查放在真实 RTS 注册之前；同时保留窗口所属线程、无激活、白板 NOTOPMOST 的规则。

## Caveats / Not Found

- 历史提交消息仅 auto update，没有 Windows/RTS 内部故障机制、API 说明或故障日志。只能确定项目合同及调用顺序，不可断言 RTS 永久缓存 z-band、SetWindowPos 成功但未改 band 等内部因果。
- 尚未从 8 月 20 日记忆里精确恢复用户所谓 SetWindowPos 返回成功的原始现场；已恢复 8 月 24 日对应的症状报告。当前用户陈述是现场现象来源，不应伪造为历史日志已独立验证。
- `trellis mem` 结果包含审批转录和已压缩摘要；其中重复工具证据与先前被纠正的推测必须剔除。01a03185 有 2 段加密代理消息无法恢复、压缩前助手回复缺失；不把这些缺失当作未讨论证明。
- GitHub commit API 读取成功，但随后 contents API 两次出现连接失败；没有因此修改网络、工具配置或仓库。
- 本文不覆盖当前完整启动链，由主会话负责。未运行 GUI、未改产品代码、未 commit/push。
