# F4：设置隐藏时收束鼠标捕获：执行计划

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
- WindowService真实窗口测试单列执行；测试窗口与完整Settings原生GUI强触发均使用已授权桌面环境，不能混入--no-window结论。
- 按design的失败/边界矩阵执行；完整产品原生GUI串行，不使用Computer Use，测试只操作隔离副本。
- 当前只是验证计划，尚未构建或运行新回归；前轮结果不得记成修复通过。

## Dependencies / Rollback

执行与回退依赖以design和父implement为准；无隐含树依赖。无commit/push，关闭可确认属于本轮的测试程序，保留用户已有窗口。

## Implementation Progress（2026-10-15）

已在本工作树实施待检查补丁，未commit/push：

- `Window.cpp`新增私有`HideSettingWindow`，仅在Setting owner thread且`GetCapture()==本窗口`时ReleaseCapture；复查捕获及实际隐藏结果。Hide、HideAll和Show期间shutdown回退复用同一入口，其他角色原行为保持。
- `Setting.cpp`在visible guard/ImGui mutex前轻量处理WM_CAPTURECHANGED及WM_SHOWWINDOW(FALSE)，不读context、不发RTS Cancel。真实原生隐藏再次锁存输入失效并Request(Settings)，不改变最新逻辑visible；无旧session的新建边界在短状态锁内消费历史Hide，Drain后仍沿用原Release。
- `Setting.SessionState.h`由窄InvalidateSession统一锁存逻辑/原生隐藏失效，Show和CommitEpoch不清除；旧session已Drain或确认无旧session时Release消费，避免Hide→Show合并复用旧backend，也避免首次无session反复重建。
- `setting_session_state_tests.cpp`直接测试生产状态对象：合并Hide→Show、重复Hide、Resolve后晚到Hide/旧CommitEpoch、新建边界后Hide、首次空session、创建失败重试及幂等Drain；确定性覆盖逻辑Hide→Show→旧session已Drain/新context Commit→迟到native Hide→旧Commit→下一Resolve release/rebuild，逻辑Show意图保持。
- `window_tests.cpp`新增`RunSettingCaptureTests`并由既有`RunWindowTests`调用：真实WindowService owner线程捕获、owned/unowned、重复Hide、其他Bar HWND捕获保护、HideAll、ShowWindow回调期间shutdown及已关门HideAll。测试WndProc仅证明WindowService合同，不代表完整Settings/ImGui回归。

已执行：五个owned源文件`git diff --check`，无空白错误；源文件UTF-8无BOM/CRLF检查通过。子任务`task.py validate`通过（implement 7项/check 8项）。未触碰vendor或新增工程/CLI。完整Solution构建、无窗口状态测试、真实HWND窗口测试及完整GUI强触发由主会话统一执行；本子代理未启动构建或GUI，因此本段不记任何运行验收通过。

主会话复核后已闭合迟到native Hide交错：真实WM_SHOWWINDOW(FALSE)在可见性/ImGui锁前通过既有短状态锁失效当前输入，Request在状态锁外；旧native Hide不把最新Show意图改回false。原生通知发生在旧Drain之前则由旧销毁消费；发生在新建边界之后则保留至下一次Resolve，不被旧CommitEpoch清除。没有新增WindowService→Settings公开回调或owner访问ImGui。仍需完整产品GUI验证真实FIFO接线；本段确定性状态测试不冒充GUI通过。

## 本轮实际验证（2026-10-15）

四项补丁及独立检查已完成；最终主 Solution Debug|ARM64 和独立存储目标均退出0。F1真实生产背压自检、F2两项生产CLI、F3控制边界、既有存储矩阵、Headless --no-window通过。用户解锁后，同一最终EXE的F1 stdout背压完整产品鼠标GUI通过，停读时新墨迹/擦除/Clear/Undo/设置仍响应，恢复输出后再停读，真实设置关闭约0.9615s正常退出；F4无诊断强触发四轮、普通X、重开首次切页、Explorer/选择往返及按住退出通过，补测退出约1.2343s。首轮F4验证器600秒清理保留为forced，不算正常退出通过。默认窗口整套复测exit1/11 fail；HEAD A/B仅证明预置顶owner/Z序条件已有，不替全部失败归因。独立真实capture回归exit0。F3物理触摸/笔、F2真实Office、Win7、多屏、console/QuickEdit及原用户共同根因未确认。准确命令、SHA、原始日志和限制见父任务 research/fix-validation.md。无commit/push，任务不归档。
