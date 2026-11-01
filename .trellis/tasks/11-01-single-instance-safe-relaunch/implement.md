# 实施计划（已批准）

- [x] 先完成逐分支状态表：普通/并发、允许模式、Restart/CrashTry、更新主→installer→主/回滚、SuperTop三段式/取消/失败；每格明确逻辑目录、占位持有人、何时可退出、接任完成条件。
- [x] 核实可复用启动握手和早期ReadMini，固定最小Launch接口及真实目录身份方案；必要时先补充设计复审。
- [x] 添加默认false配置与早期读取、健壮目录键和RAII占位，加入无窗口竞争/句柄/身份失败测试。
- [x] 逐个接入正常重启、更新/回滚、SuperTop；每次改动附可复现的成功和失败交错测试。内部测试身份先分流，禁止触发真实升级/UAC/产品GUI。
- [x] 接入设置开关、三语言和生成键；验证开→关、缺键/错类型、保存失败与后续启动读取。
- [x] 最终检查现有ShutdownSupervisor、更新验证及SuperTop合同；完整 InkeysRepo.sln Debug ARM64 原生MSBuild，PATH规范化，同次构建等待至少5分钟。
- [x] 执行有界无窗口多进程/配置/UI布局回归，git diff --check、BOM/CRLF与最小diff审查；GUI/UAC/Win7未测分列。
- [x] 更新相关规范及验证报告；未经本次明确授权不commit/push/归档。

2026-11-01：用户批准规划；task.py start成功，当前任务in_progress。启动实现与设置配置分别由实现代理负责，根代理统一构建与验收。

最终无窗口验证与独立复核已完成，详见validation.md；真实GUI/UAC/Win7与跨架构运行未执行，不列为通过。未commit/push/归档。
