# F4：设置隐藏时收束鼠标捕获

## Goal

设置在按住鼠标期间隐藏后释放自身捕获并收束本次输入，使工具栏点击及重开设置正常。

## Confirmed Facts

- 父任务需求：R4 / F4。
- `Setting.cpp:610–617,647–658,8860–8869` 逻辑隐藏后跳过Up，`imgui_impl_win32.cpp:719–721` 的ReleaseCapture未执行；`Window.cpp:1733–1741` 原生Hide未收束capture。
- 原始报告共同根因仍未确认；本项成功不代表所有用户现场已解决。

## Requirements

1. 只在Settings HWND所属线程的隐藏/会话结束边界取消本窗口仍持有的capture。
2. 不抢或释放Bar/Drawpad/其他HWND捕获，不把失焦/capture变化变成RTS Cancel。
3. ImGui/backend状态只能在合法所属会话中收束或失效重建；避免owner线程新增等待共享渲染锁。
4. 兼容Hide→Show快速合并、无context、context销毁、普通X、重开及退出；尽量不修改vendored ImGui。

## Acceptance Criteria

- [x] 真实完整程序重复Down→Alt+F4→正常Up后Settings无残留capture，Bar同位置点击可用且新线正常。
- [x] 普通X、隐藏后重开、Hide→Show合并与最终退出均无按钮/拖动残留。
- [x] 已有其他HWND捕获时隐藏Settings不影响它；owner线程和会话失效生产路径有回归。

## Scope and Dependencies

- 功能依赖：无。可独立实施、检查、回退；最终交付必须与父任务其他三项一起通过集成验证。
- 执行依赖：父任务最终规划摘要经后续用户确认后才允许 task.py start；F1合入后再进行开启诊断的跨项GUI验证。
- 共用 IdtMain.cpp / 项目登记 / DrawingController 测试块的修改由主会话按序整合；F2/F3若涉及同文件，由同一实现者先F2后F3处理，不并发覆盖。
- 完整构建由主会话串行执行，不同时写同一MSBuild中间目录。
- 已授权隔离GUI原生验证；不使用Computer Use，不修改用户安装/原配置/原视频。
- 最小diff并保留编码/换行；不commit/push，不做未证实的其他冻结修复。

## Evidence

- 父PRD：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/prd.md`。
- 专项实施前研究：`.trellis/tasks/archive/2026-10/10-14-canvas-freeze-investigation/research/f4-fix-plan.md`。
- 历史实验/模型与完整产品作用域以父任务两份调查报告为准，测试须执行修复后的真实生产代码。

## 最终验收范围

无诊断同一最终 EXE、真实鼠标四轮 Down→Alt+F4→Up，均在物理 Up 前释放 capture，工具实际擦除/新线正常；普通 X、快速重开首次 Language 点击、Explorer/选择往返与真实按住退出通过（独立短轮 exit0/1.2343s、forced=false）。Hide→Show 精确合并及迟到 native Hide 由生产 SessionState 确定性测试覆盖，GUI 快速操作不冒充已观测特定线程交错。首轮验证器 600 秒清理明确不算正常退出通过。默认完整 HWND 套件仍 11 fail，准确证据见父任务 research/fix-validation.md。
