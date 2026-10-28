# S5 P1/P2 独立复审

2026-10-29。范围：Window candidate/receipt、PageControl 固定帧候选、BarPageSurface 命中/damage 提交与 Whiteboard 背景 ACK。当前为源码复审记录，完整模块和生产测试由主会话统一执行；不能由本记录认定性能或全部验收通过。

## 已核对范围

- `Window.CandidateMailbox.h`：固定槽、角色重叠失效、取消、迟到完成、reset 单调版本、队列计量和恢复中的可消费掩码。
- `Window.cpp/.cppm`：现有 overlay owner 消费、普通显隐与 promote 分离、真实 RECT/visible/iconic/HWND generation、pair 回滚、恢复屏障和安全覆盖。
- `PageControl.cpp`：回执前不发布新命中/页码完成、不清新 damage；ULW 后 verify-only bounds；当前 native 快照复验；静态 pending 和动画帧替换；输入门禁与既有指针会话。
- `Bar.Scene.cpp/.cppm`：固定命中候选 serial、damage revision、同 revision 新光照失效。
- `Whiteboard.cpp/Whiteboard.BackgroundState.h`：失效内容代次、prepared 与 final active 分离、实际 inactive Hide、像素成功而 native pending 时复用。
- `window_tests.cpp`、`whiteboard_ui_tests.cpp`、生产 hidden fixture：纯邮箱与实际窗口合同、半对失败回滚、重复 Show 与显式 promote、延迟 owner、ULW 成功而 native Show 失败。

复审使用任务 PRD/design/implement、异步所有权研究和 reading-map；补核 PageControl/共享调度/脏区成功事务、Window/Whiteboard 生命周期、canvas 场景保护与恢复写集、PPT session/target/UI-ready 与诊断口径。旧 P3/P4 文字由主会话在对应阶段完成时同步，不把尚未迁移的直移和协调器标为已完成。

## 已反馈并由实现负责人修正

1. PageControl 接纳回执原先未要求 `actual.known`，也只核验当前 owner 快照的 HWND/generation；同一代的后续 Hide/最小化/窗口政策可以使旧 Show 回执失效。已增加当前 known、visible、iconic 与实际 bounds 复验，并为普通原生状态变更退休旧角色回执。
2. owner 在 failed rollback 留下 Executing retirement 时，按总 queue depth 自唤醒会忙转。已改为 pending 掩码排除 retiring 槽；被阻挡槽沿原恢复期限等待，其他槽仍可消费。
3. 等价且已安全隐藏的候选仍推进 overlay safety revision，会无谓退休 WorkspacePrepared。安全 latch 已改为仅新增安全位/代次或纠正实际违规状态时推进；覆盖仍保留。
4. PageControl 新的 geometry/publication 尚未收到 ACK 时，新业务输入不得使用旧命中。已添加 native pending 门，既有有效按压会话不因普通反馈帧反复取消。
5. 静态 pending 原先按旧 `keepAnimating=false` 跳过同 publication 的新 hover/press/lighting。已将轻量 Configure/Advance 放在复用判断前，并比较捕获时 damage revision；同时间 Advance 不重复积分，真正未变化的像素继续复用。
6. `DrainCandidates` 的接纳后 completion 分配原先可能越出 owner 消息循环。现已移除候选的大型 function 捕获，并把 completion 分配失败转换为同 revision 的 `Rejected/ERROR_NOT_ENOUGH_MEMORY`、mask 0 回执；尚未触碰 HWND，不丢失已接纳请求。

## 尚待落地/执行的门禁

- 失败诊断宜保留首个原生命令错误，再独立报告恢复错误；只在 `ExecuteTracked` 整个恢复返回后读取 GetLastError 可能丢失最初错误。P3 的 first/second/rollback 阶段诊断不能退化。
- 完整 `InkeysRepo.sln Debug|ARM64`、本轮 EXE 的 hidden/offscreen/state/headless 与同机 A/B 尚由主会话执行；尤其 P2 必须运行真正 `RenderBackground`，不能只凭 header helper 通过。

截至本次复查，上述六项源码修正已直接查看；未再发现阻断 P1/P2 构建与动态验证的源码问题。此结论为有条件的源码检查通过，不能替代实际测试或 P3/P4 完成。

## 当前验证状态

- 静态 `git diff --check --ignore-submodules`：退出 0。
- 本复审未另行启动编译或测试，避免与主会话构建和采样竞争。首轮 C1902 属既有沙箱 PDB 环境故障，不据此判断源码失败或通过。
- 本复审未修改产品源码；以上修正由文件负责人按约定完成，保留会话前的性能驱动改动。
