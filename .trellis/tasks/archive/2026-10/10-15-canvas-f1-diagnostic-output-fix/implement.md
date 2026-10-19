# F1：执行计划

## Before Start

- [x] 明确标准流scope、必要sink清单、独立worker和有界停止合同。
- [x] implement/check清单包含真实spec和专项研究。
- [x] 父最终摘要已获后续用户明确批准，已start并派implementation agent。

## Implementation / Check

1. Trellis implement子代理读取本任务全部artifact/manifest；记录原编码/换行并核对每个生产输出入口。仅迁移标准流文字日志，真实持久化I/O与专用尾轨迹文件保持。
2. 实现固定队列/try-only生产者、输出兼容、自有句柄和有界Stop；消除同路径旁路，保存/恢复GetLastError，不改业务分支。
3. 在真实生产实现上补早期自检入口及有限harness接缝；严格参数和Release harness-off不落入GUI。
4. Trellis check子代理审查取消提交竞态、route/queue/event生命周期、UAF、初始化失败、饱和统计及残留标准流。共享源文件由主会话与F2/F3顺序整合。
5. 主会话串行完整InkeysRepo.sln Debug|ARM64；动态ARM64 MSBuild，同invocation PATH规范化，至少五分钟，核对退出码及首个有意义错误。
6. 运行生产自检、headless及完整产品原生GUI。确认正常Stop不是由15秒监督结束；记录deadline测量、drop/隔离状态及binary hash。
7. 实测后更新日志/退出规范，最小diff/编码/清单复核；无commit/push。

## Validation

- 计划新增 `Build/ARM64/Debug/Inkeys.exe --draw3-diagnostic-output-test`：真实标准句柄、队列、writer和停止；此处为实施前计划，当时尚无此入口；现已新增并执行，结果见末节。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`。
- 原pipe_backpressure_probe.py同条件修复后GUI：持续停读stdout，再分别覆盖stderr、TouchArea与诊断关闭，实际新笔/Clear/工具响应及正常关闭。
- 普通文件/正常pipe/console、满队列、多生产者、Stop竞争、取消提交间隙、broken pipe、期限兜底及初始化失败矩阵按design/research执行。
- 所有GUI串行，隔离副本；不使用Computer Use，保留用户已有窗口/安装/配置。

## Dependencies / Rollback

F1无功能依赖；F2/F3共享日志调用文件，必须协调修改后统一构建，不并发写缓存。父摘要一次确认覆盖本子任务，修复前实验不转记为修复通过。按本项完整生命周期/调用接线回退，保留他人修改，不自动归档产生commit。

## 本轮实际验证（2026-10-15）

四项补丁及独立检查已完成；最终主 Solution Debug|ARM64 和独立存储目标均退出0。F1真实生产背压自检、F2两项生产CLI、F3控制边界、既有存储矩阵、Headless --no-window通过。用户解锁后，同一最终EXE的F1 stdout背压完整产品鼠标GUI通过，停读时新墨迹/擦除/Clear/Undo/设置仍响应，恢复输出后再停读，真实设置关闭约0.9615s正常退出；F4无诊断强触发四轮、普通X、重开首次切页、Explorer/选择往返及按住退出通过，补测退出约1.2343s。首轮F4验证器600秒清理保留为forced，不算正常退出通过。默认窗口整套复测exit1/11 fail；HEAD A/B仅证明预置顶owner/Z序条件已有，不替全部失败归因。独立真实capture回归exit0。F3物理触摸/笔、F2真实Office、Win7、多屏、console/QuickEdit及原用户共同根因未确认。准确命令、SHA、原始日志和限制见父任务 research/fix-validation.md。无commit/push，任务不归档。
