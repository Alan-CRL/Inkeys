# 此前获准 GUI 现场证据（本轮不再采样）

## 授权边界

此前用户明确允许本机 GUI 复现，并人工点击白板/任务栏；采集记录位于 Build/WhiteboardGUIInvestigation20261027/。最新指令禁止后续 GUI 和现场采样，只允许离屏测试。以下只引用已保存资料。

## 现场对照

| 状态 | 事实 |
| --- | --- |
| 初次点击白板 | 任务栏出现，Freeze/Drawpad 隐藏，Bar 可见；epoch=3/desired=true；20次样本 phase Inactive/Entering，workspace=Desktop，背景 active/committed=false，Recovery仅3/20存在 |
| 通过任务栏进入再回 Codex | 20/20 phase=Active、workspace=Whiteboard、mode=true、背景active/committed=true；Recovery20/20；Primary显隐变化；全部非置顶、owner正确，9成员连续、foreignGap=0，隐藏Middle位于Bar之上 |
| 白板获前台对照 | Bar位于全部PPT之上；5/5 Active、Recovery为空，owner/band不变 |

原始文件：whiteboard-failure-state-1.json、whiteboard-failure-series.json、whiteboard-runtime-series.jsonl、whiteboard-after-taskbar-background-series.json、whiteboard-after-taskbar-runtime-series.jsonl、whiteboard-raised-window-state.json、whiteboard-raised-runtime-series.jsonl。日志已另存 whiteboard-after-taskbar-log.log。

13:13:30.254 的 TopWindow 日志记录 overlayTopmost=false、whiteboardMode=true 时刷新失败。日志也可能是已在恢复屏障内的失败，不能据此断言最早命令类型。未捕获最早原生排序 API 的逐次返回，因此不声称 Windows 的具体内部拒绝已经直接观测。

## 与确定源码联系

77f76038 把完整组排序加入模式准入，包含隐藏 Middle 的全组 HWND_TOP；Bar必须高于四个PPT。该现场是内部条件失败，外部连续性并未失败。前台恢复时相同组可满足内部条件，说明当前后台全局置前策略不可靠。

同一提交把纯层级命令计为canvas事务，恢复会强制画布策略、FRAMECHANGED与显隐。它把普通排序失败放大成整组恢复与可见性抖动；初次模式失败又在同epoch反复收起主栏面板。首次并非始终Recovery阻塞，进入后才稳定持有恢复责任。

## 资料解释修正

- sampler v1 的成员分类不适合实际旧类名，不能引用其派生空数组证明无外部穿插；原始九成员rank本身连续，v2分类已修正并验证。
- PowerShell 数组的 .rank 是数组维数，不是成员rank；旧控制台pptRanks=[1]不是窗口次序证据。应逐个读取原始windows记录的rank。
- PDB数据由只读OpenProcess/ReadProcessMemory读取，未attach debugger或挂起线程。

本报告不新增运行时读取，不代表修复后 GUI/Win7/Win11 实测。

## 官方 API 语义核对

Microsoft 的 [SetWindowPos 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos) 明确全局置前受前台许可约束，NOOWNERZORDER 保留 owner 的 Z 位置，已经非置顶的窗口使用 HWND_NOTOPMOST 不改变其位置。[Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows) 规定 owned 必须在 owner 之上。相对下调方案据此保留 Bar/外部顺序，并继续核验内部 owned 关系；文档不替代修复后的 USER32 实测。
