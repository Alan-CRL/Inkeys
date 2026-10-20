# F2：PPT冷槽暂时读取失败自动重试：执行计划

## Before Start

- [x] 父需求、专项研究、最小变更边界及真实生产测试入口已确定。
- [x] implement/check manifests包含真实spec/research，不注册待改源码。
- [x] 父任务最新最终摘要已获后续用户明确批准，已按session执行task.py start。

## Implementation and Review

1. 读取本子任务PRD/design/两个manifest以及父集成计划；确认编码/换行和共享文件所有权。
2. Trellis implement子代理只改design所列必要生产点与同权威实现的回归；保留中文关键注释。
3. Trellis check子代理核对竞态/失败/生命周期与测试作用域；机械问题可自修，范围变化由主会话处理。
4. 主会话整合共享文件并串行构建完整InkeysRepo.sln Debug|ARM64，使用动态发现的ARM64原生MSBuild，同invocation规范化PATH，允许至少五分钟。
5. 执行下面的针对性测试与父任务最终GUI集成，记录退出码、关键输出和二进制hash。
6. 最小diff/格式/manifest复核；实测后更新相关规范。四项完成不冒充原现场归因。

## Validation

- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`。
- `Build/ARM64/Debug/Inkeys.exe --draw3-ppt-current-load-retry-test`，扩充相应生产用例后执行。
- `Build/ARM64/Debug/Inkeys.exe --draw3-pending-topology-load-test`，扩充相应生产用例后执行。
- 按design的失败/边界矩阵执行；完整产品原生GUI串行，不使用Computer Use，测试只操作隔离副本。
- 以上为实施前验证计划；最终实际构建与运行结果见末节，前轮修复前结果不转记为修复通过。

## Dependencies / Rollback

执行与回退依赖以design和父implement为准；无隐含树依赖。无commit/push，关闭可确认属于本轮的测试程序，保留用户已有窗口。

## 实施进展（2026-10-15）

用户已在最终F1–F4摘要后明确批准“批准，并开始修复”，本子任务已由主会话start。产品 `Draw3.PresentationAutoSave.cpp` 仅新增读取IoError分类；既有四次预算与其余严格校验未改。

`Draw3.DrawingController.cpp` 的既有 `--draw3-ppt-current-load-retry-test` 新增真实worker与私有临时根：保存已知非空墨迹、持有真实文件不共享句柄直至IoError回执、同target/generation按既有安全点到期重试、真实materialize/install后核原笔迹和准入。另覆盖源/key冲突、workspace不符、合法源revision变化、坏header及截断tail，断言失败未写空白覆盖；保留原四次耗尽、generation/target变更及PrepareExit用例。

实施者已完整加载implement/check清单以及两份大型Draw3/input规范，执行 `git diff --check` 和逐文件BOM/CRLF核对通过。本阶段未编译或运行新测试，不能记作修复验证通过；完整Solution、两条PPT生产CLI及headless由主会话串行执行。Controller与PptAutoSave行为/测试编辑完成，后续仅由F1实现者迁移这些文件的生产诊断输出，避免并行覆盖。没有commit/push。

## 本轮实际验证（2026-10-15）

四项补丁及独立检查已完成；最终主 Solution Debug|ARM64 和独立存储目标均退出0。F1真实生产背压自检、F2两项生产CLI、F3控制边界、既有存储矩阵、Headless --no-window通过。用户解锁后，同一最终EXE的F1 stdout背压完整产品鼠标GUI通过，停读时新墨迹/擦除/Clear/Undo/设置仍响应，恢复输出后再停读，真实设置关闭约0.9615s正常退出；F4无诊断强触发四轮、普通X、重开首次切页、Explorer/选择往返及按住退出通过，补测退出约1.2343s。首轮F4验证器600秒清理保留为forced，不算正常退出通过。默认窗口整套复测exit1/11 fail；HEAD A/B仅证明预置顶owner/Z序条件已有，不替全部失败归因。独立真实capture回归exit0。F3物理触摸/笔、F2真实Office、Win7、多屏、console/QuickEdit及原用户共同根因未确认。准确命令、SHA、原始日志和限制见父任务 research/fix-validation.md。无commit/push，任务不归档。
