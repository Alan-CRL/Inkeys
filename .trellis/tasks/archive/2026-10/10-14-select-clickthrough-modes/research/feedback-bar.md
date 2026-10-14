# 主栏用户反馈修复（2026-10-14）

## 根因

- 穿透在 `BarButtonSetClass::Load` 已作为稳定 shared_ptr 运行时成员插在 Freeze 前面，不根据 hide 从列表删除。右侧飞入来自隐藏按钮的位置过渡：`RenderLoop` 原隐藏锚点覆盖 Eraser/Geometry/Recall/Redo/EndShow，没有 Pierce；所以隐藏穿透仍保留展开主栏时的旧右侧坐标。
- `PresetHoming` 只在调用历史显隐投影后临时把空页 Selection 的 Undo/Redo 隐藏，`NoteRecallHistoryAvailability`、其他历史刷新或 `AdvanceRecallChromeIdle` 可随后再次发布未加门禁的显隐。实际墙钟保护为 3 秒，不是用户猜测的 5 秒。

## 实现

- 不改变生产 Load、A2 配置、稳定按钮顺序或实例身份，仅为隐藏 Pierce 添加 Freeze 优先锚点。Freeze 不可见时保留现有最近可见邻居规则（等距离优先前项）。
- 为真正测试位置过渡，将现有隐藏锚点查找/目标提交提成 `AnchorHiddenBarButton` 模块内部非 export helper。RenderLoop 原 `SetButtonPositionTar` 回调仍决定批次、镜像和换边关键帧曲线，未改变通用网格计算。
- `ResolveRecallChrome(state, compactSelection=false)` 将空页 Selection 的主 Undo/Redo 和 popup 显隐统一覆盖为隐藏，保留 enabled、expanded、hold、pendingLayout 及墙钟期限。生产视觉入口、视图读取和迟到弹窗动作判定共享非白板空页 Selection 门禁，移除 Homing 后置临时 hide。其他工具/非空 Selection/白板原行为保持。

## 变更归属

- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.RecallChrome.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp`
- `InkeysHeadlessTests/recall_attribute_tests.cpp`

## 回归与验证

- 离屏 fixture 建立真实生产运行时按钮列表，断言 Pierce 只有一份且紧邻 Freeze，并在三轮 Selection/Pen、Combined/Separate、PPT/桌面切换中比较完整指针序列。没有依照测试手写列表。
- 使用同一个隐藏锚点 helper 与真实 `BarUiAdvanceAnimation`：从右侧旧值隐藏，零 dt 保持姿态，重复帧跟随 Freeze 收敛，重新显现时起点为 Freeze 列；Freeze 不可用时验证原最近可见邻居规则。源级回归同时要求生产 RenderLoop 接入此 helper 和 Pierce→Freeze 调用。
- 离屏直接执行真实 Recall 可用性刷新、视图读取、Poll timer/Advance idle，覆盖 Undo/Redo 两种 hold。空页 Selection 在到期前/后保持隐藏，期限及 hold 不被精简门禁清除；非空 Selection 恢复受保护按钮，返回 Pen 恢复正常待调整布局。
- Headless 覆盖三种可用性/两种命令的受保护、pending 和 0/2.99/3/5 秒视图。compact projection 只抑制显隐，退出精简态回到原投影。源级断言生产视觉及弹窗读取都接入统一门禁。
- 本代理仅执行静态 `git diff --check --ignore-submodules=all`，通过；逐一对比六个跟踪文件 HEAD，UTF-8、BOM政策及CRLF保持，未引入裸LF。
- 统一完整构建、Headless/离屏执行及最终 check 由主会话完成，本记录不提前宣称动态测试通过。

## 验证边界

没有启动 GUI 或 Computer Use，也未 commit/push/结束任务。真实桌面鼠标/触摸输入、PPT动画与真实 HWND 唤醒仍由主会话记录为人工待验；离屏回归消费生产 helper 和实际动画值，不替代硬件或窗口呈现验证。
