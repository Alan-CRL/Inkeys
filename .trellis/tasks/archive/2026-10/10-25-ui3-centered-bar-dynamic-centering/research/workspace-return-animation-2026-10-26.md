# PPT/工作区退出竖直动画恢复（2026-10-26）

> 2026-10-26 收尾授权：用户明确要求提交当前修复，并结束、归档本居中移动子任务；吸附闪动子任务继续保持in_progress。先提交代码及验证记录，再单独归档提交。真实PPT GUI未执行的验证边界保留，不将关闭任务视为新增人工验证。

## 当前范围

用户要求在松手闪动对照前，先恢复退出PPT时丢失的主栏上下动画。沿用既有任务，不新建、不结束、不提交；此前906251ee固定拖动实现保留，松手原点/source交接实验尚未开始。

## 原因与最小修复

`desktopRestorePending` 在ApplyDisplayTransition后直接SetDirect桌面Y并清除场景动画标记；既有回归仅统计整体Y行程，并在注释中允许Leave首帧直接到终点，因而没有覆盖“必须有上下动画”。基线CSV中6种显示/展开组合的LeavePpt/LeaveWhiteboard共12条序列全部在dt=0首帧完成位移，之后Y不再改变；单屏例1034.5→999.5，直接跳35px。

仅Bar.RenderLoop.cpp恢复桌面分支改为：从最后成功呈现的屏幕Y播种，复用现有0.4秒displayCenterY属性动画与whiteboardDockAnimationActive门禁；main.y使用当前值而非目标。该标记同时允许原有居中X按当帧宽度反推，避免恢复Y期间重现水平偏移。动画关闭仍当帧归位。返回期间再次进入工作区时保留第一次进入前的桌面快照，不用动画中间位置覆盖它。

## 回归与验证

- 在既有真实Seek/RenderLoop离屏框架中补Y移动帧数、idle首帧跳变量及单帧最大步长断言，保留逐帧联合居中、有效source与呈现失败保护。
- 扩展快速退出未完成便重新进入后再退出，以及动画关闭首帧回到原桌面Y。
- ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64：exit0；既有第三方C4267 warning保留，未改工具链/项目配置。
- --ui3-workspace-centering-test：exit0；6组合的18条普通退出序列均有23个Y移动帧，idle首帧Y差为0，最大联合水平中心误差约1.14e-13px（浮点舍入量级）。快速反向/再次进入、动画关闭、Freeze退场、负原点及100%/150%缩放均通过。
- --ui3-bottom-dock-transition-test：exit0，原有固定拖动表面、吸附形变、失败重试与动画关闭等19组合通过。
- 产品只改18行diff；UTF-8无BOM/CRLF保持，未加新字段、线程、后端或逐帧分配；spec已同步场景返回所有权与测试要求。

证据在TestResults/ui3-workspace-return-2026-10-26：baseline-return-jumps.json、validation-summary.json、build与两个回归日志、offscreen-bin/TestResults/ui3-workspace-centering/samples.csv。

本轮未打开真实PPT或GUI、未自动操控桌面，实际视觉仍待用户人工验证。未重复全量headless：本次未改其编译输入，针对真实RenderLoop的两个入口已覆盖变化；完整solution已构建。松手闪动仍为独立未解决项，任务保持in_progress。
