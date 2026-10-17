# F1：隔离画布同步诊断输出

## Goal

stdout/stderr 标准流输出端停读时，绘制与正常退出继续前进；保留现有输出开关、可解析字段和正常 sink 输出。

## Confirmed Facts

- 父任务需求：R1 / F1。
- `Draw3.Diagnostics.cpp:48–55,574–617` 的同步 WriteFile 已在完整程序两次复现；恢复读端后同进程恢复绘制。
- 原始报告共同根因仍未确认；本项成功不代表所有用户现场已解决。

## Requirements

1. 实时生产者只进行有界、可拒绝的发布；队列满/停止允许丢诊断，不丢产品输入或墨迹。
2. 单独处理 stdout/stderr 的输出生命周期；正常管道、文件、控制台均兼容，不改 IDTLogger 普通业务日志策略。
3. 停止不做无限 drain/join；取消竞态和迟迟不返回的输出必须有隔离状态生命周期，避免 UAF 或窗口/画布引用。
4. 相关诊断关闭且无有效继承输出端时，不分配大队列或创建输出线程；有输出端时保留既有普通错误诊断，保留Windows 7既有兼容边界。
5. 不改默认关闭的 INKEYS_DRAW3_TAIL_TRACE 专用轨迹文件协议，也不承诺任意磁盘诊断均已异步；范围是标准流背压。

## Acceptance Criteria

- [x] stdout 停读的完整产品鼠标 GUI 中真实新线及工具响应继续，恢复读端后输出继续；stdout/stderr 独立满管另由真实生产 CLI 覆盖。
- [x] 满队列内存有界且不阻塞生产者；输出线程卡在写入或刚要写入时正常停止均有界。
- [x] 完整产品及真正生产输出实现的正常 sink、并发停止/启停和无诊断回归通过。

## Scope and Dependencies

- 功能依赖：无。可独立实施、检查、回退；最终交付必须与父任务其他三项一起通过集成验证。
- 执行依赖：父任务最终规划摘要经后续用户确认后才允许 task.py start；F1合入后再进行开启诊断的跨项GUI验证。
- 共用 IdtMain.cpp / 项目登记 / DrawingController 测试块的修改由主会话按序整合；F2/F3若涉及同文件，由同一实现者先F2后F3处理，不并发覆盖。
- 完整构建由主会话串行执行，不同时写同一MSBuild中间目录。
- 已授权隔离GUI原生验证；不使用Computer Use，不修改用户安装/原配置/原视频。
- 最小diff并保留编码/换行；不commit/push，不做未证实的其他冻结修复。

## Evidence

- 父PRD：`.trellis/tasks/10-14-canvas-freeze-investigation/prd.md`。
- 专项实施前研究：`.trellis/tasks/10-14-canvas-freeze-investigation/research/f1-fix-plan.md`。
- 历史实验/模型与完整产品作用域以父任务两份调查报告为准，测试须执行修复后的真实生产代码。

## 最终验收范围

同一最终 EXE 完整产品 stdout 背压下新笔迹、擦除、Clear/Undo、设置与恢复输出通过；再次停读后真实设置关闭 exit0/0.9615s，runner forced=false。stderr 独立满管/取消由生产 CLI 验证，GUI 中只有暂停和部分积压，未宣称独立填满。真实 console/QuickEdit、物理触摸/笔、Win7、多屏未运行；默认完整 HWND 套件仍 11 fail。准确证据见父任务 research/fix-validation.md。
