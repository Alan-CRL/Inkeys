# S5 实施与验证记录

2026-10-28。延续当前任务，用户已批准规划和实现；不创建新任务、不提交、不推送、不归档。本文记录当前阶段，未完成项不作为通过结论。

## 基线

- 工作分支 `bugfix/mode`，HEAD `2c77d79c`。保留会话开始前的任务文档和 `PageControl.cpp` 性能驱动改动。
- 修改前 Debug ARM64 EXE SHA-256：`EA7B51CAA0423037568A432CA1222D3BC0DA8EDC98B421F5797CAD64FE2DEBFA`。
- 冻结产物：`TestResults/ui3-workspace-transition/s5/baseline/`，含原 EXE、原 PageControl 源码、原 diff、三轮 `frames.csv` / `summary.csv` / `metadata.json` 和退出码。
- 三轮命令均为 `INKEYS_PAGE_CONTROL_TRANSITION_PERF=1 Build/ARM64/Debug/Inkeys.exe --page-control-hidden-test`，每轮包含六方向三 cycle。三轮进程退出码均为 0。只运行屏外测试入口，没有启动产品主窗口或 Computer Use。
- 不把旧研究中的时长当作本次 A/B 分母；本次三轮基线另存，新版本在相同入口重测。

## 实现阶段与门禁

| 阶段 | 责任 | 状态 |
| --- | --- | --- |
| P1 | Window Service 固定 candidate/receipt、真实状态快照、幂等显隐；PageControl 固定帧候选与 ACK 后提交 | 独立门禁通过 |
| P2 | Whiteboard 背景复用同一窗口回执、像素准备代次、inactive / final active ACK | 独立门禁通过 |
| P3 | pair 直移、Pointer Down promote、Bar 直移 | 实现与本轮自动化验证完成 |
| P4 | 原协调器 Prepare/Complete 非阻塞回执与进展唤醒、规范同步 | 实现与本轮自动化验证完成 |

窗口候选不引入额外窗口 owner 或渲染线程。ULW 已经携带位置的路径只让 owner 核验真实矩形，不在迟到回执处理中把旧 bounds 再写回新像素。几何/显隐恢复必须按实际写集执行；输入和渲染的直移竞争需要独立的 try-only 呈现互斥，不能只依赖取消标记。

## 红测与覆盖

- P2：生产 `Whiteboard.BackgroundState.h` 的 native ARM64 独立探针证明同一请求在 `Invalidate()` 后仍可接纳旧帧：`stale_after_invalidate_accepted=1`，编译退出 0，测试退出 1。证据位于 `TestResults/ui3-workspace-transition/s5-whiteboard/`。该测试针对真实生产 helper，不代表完整 HWND 链。
- P1 源码基线确认生产 `RenderSurface` 调用同步 `SetBounds/Show/Hide`；这是静态红项，不冒充执行红测。
- 后续完整 solution、hidden/offscreen/state/headless 和原生成功呈现间隔结果将在各阶段实际执行后补充。

## 环境与证据限制

- 构建使用动态发现的 Visual Studio ARM64 原生 MSBuild，完整 `InkeysRepo.sln Debug|ARM64`，同 invocation 规范化 PATH，600 秒期限；不修改 SDK、依赖或项目构建配置。
- 系统 Python 的 WindowsApps 别名无法执行，任务工具使用已安装的 `C:/Program Files/KiCad/10.0/bin/python.exe`。这是本机启动别名问题，不是源码问题。
- 普通 `git status` 因 HiMsg 子模块 gitdir 权限失败；只读检查使用 `--ignore-submodules`。Trellis `get_context.py` 显示的零改动不可信，以该命令为准。
- 屏外原生测试可以证明真实 ULW 与 Window Service 软件事务，不能证明实际 Office/WPS、DWM 光学呈现、触控或人工观感。

## 阶段构建与环境诊断

- `s5/p1p2/build.exit.txt`：退出 1，首个有意义错误为 C1902（PDB manager mismatch），与本任务历史中已确认的 Codex 沙箱 IPC 问题一致；没有为此修改源码或构建配置。
- `s5/p1p2-user/build.exit.txt`：正常用户环境完整 Solution，所有修改源码完成编译，headless 目标已链接；主目标在增量链接的 `IncrBuildImage` 内部异常 C0000005 / LNK1000，仍退出 1，不能记构建通过。只将准确定位的 `Build/ARM64/Debug/Inkeys.ilk` 移入该轮证据目录，保留失败状态供检查，让下一轮按原配置重新链接；没有禁用增量链接或更换工具链。
- 修改前 `--state-mode-production-test` 在 sandbox TEMP 下退出 1，第一失败为 `seed-private-memory`。仅将该测试进程的 TEMP/TMP 指向工作区专用目录后原 EXE 退出 0；证明此失败属于临时文件环境，不改产品保存实现。
- Window 生产 mailbox 独立 ARM64 探针：旧 singleton receipt 被新 overlapping pair 取代后仍可读的反例退出 1；校验 slot 与全部 role revision 后退出 0。另覆盖 coalescing、迟到完成、取消、重启版本、native policy 失效和恢复阻塞槽不自旋。源码及结果在任务 research 与 `TestResults/ui3-workspace-transition/window-candidate-probe/`。
- 独立复审提出并修正：receipt 消费需复核当前 known/bounds/visible/iconic；旧 generation Hide 不能在验证前锁存新代际的安全覆盖；失败回滚期间 pending 槽不能无条件自置 event 形成忙循环。

P1/P2 完整阶段证据见 [S5 P1/P2 验证](s5-p1p2-validation.md)。

最终交付与剩余验收以 [S5 最终验证](s5-final-verification.md) 为准；任务整体性能预算未全部达标。

证据补充：Window 两次红测未单独保存 stdout/退出码或红测 EXE，只在代理工具会话观察到 failures=1；后续绿测工具 exit0。不得把最终 probe.exe 或源码当作原始红测冻结产物。最终完整 solution/native/headless 结果均有独立持久化退出码，见最终验证。
