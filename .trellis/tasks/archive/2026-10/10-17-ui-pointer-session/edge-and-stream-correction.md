# 边缘抓手与 Raw 静默修正边界

继续已有用户批准的实现，不新建任务。用户反馈：快速拖到屏幕边缘后，回到可用区域仍以偏移抓手拖动；管理员前台下光标远离栏体后停止，靠近停止位置才可能恢复。两项实机现象尚无完整输入轨迹，不能把源码风险称为已测得的 OS 原因。

## 确认事实和最小变更

1. MouseGesture 在 Raw Down 绑定后永久拒绝 native Move，未检测 Raw 是否继续更新。共享策略应允许本代次仍然到达、事件时间比最后有效 Raw 活动晚至少 50ms 的 native Move 接续。50ms 仅是移动权威的切换条件，不是按住期限，不产生 Up/Cancel。Raw 正常活动时继续单流权威并保留同毫秒 Raw excursion；切换后 native 推进 Raw 重放下限，同毫秒及更旧 Raw 不回放，新 Raw 可重新接续；Raw Up 始终按原归属与实际拖动准入结束一次。原生 Up、来源接管、旧代次、未绑定1000ms修正保持。
2. Seek 的稳定环境拖动已是绝对 pointer-minus-grabOffset 加 clamp，不改成增量、不重写限位。唯一后续 grabOffset 赋值在显示环境重基准：当前 pointer 减已 clamp/滞后的呈现点，能锁存偏移。改为保持手势开始抓住的逻辑点：只有呈现 zoom 真正变化时按新旧比例缩放 offset，serial/monitor 更新不能从受限位置重抓。环境基准、屏障和 DockTracker 仍按原协议更新。此条件缺陷可确定构造，但不能断言用户此次一定触发了 DPI/serial 更新。
3. 给主按钮 Seek 添加限频、活动期间的诊断，用现有 IDTLogger，前缀 UiPointerDrag。记录会话起止、已消费 native/Raw 数量与末次坐标/时间、Raw pending/bound/disabled、accepted screen、真实 cursor（仅诊断）、grabOffset、呈现中心/serial/zoom、foreground/capture。必须覆盖没有消息时的停顿，不能仅在 Move 到达时记录。最多每500ms摘要，加起止/环境变化；不逐包刷日志，不记录键盘输入。可在 Bar 私有上下文记计数，不引入新线程或通用日志框架。

## 文件与所有权

实现子代理负责 UiPointerInput.h/.cpp、Bar.Interaction.cpp、必要的既有 Bar.BottomDock.h 小型纯几何入口和 ui_pointer_tests.cpp/bar_bottom_dock_tests.cpp。不修改其他产品模块。主会话负责本文、任务上下文、spec 和集成验证。必须保留之前未提交的所有修改，编码/BOM/CRLF 不变，关键逻辑中文注释。

## 明确限制

管理员前台时，若 Raw 与原生移动都没有送达，仲裁没有新的位置来源，修正不能承诺跨权限全局拖动。不得为此偷偷改变 MA_NOACTIVATE、激活/前台、SetCapture 策略、manifest/UIAccess、部署权限或 hook。GetCursorPos 只可用于诊断，不用它猜按键状态或生成输入；不恢复 GetAsyncKeyState/缓存轮询。WM_TOUCH、HiMsg 边界、PPT pair/阈值、FineDial 轨迹和终态协议保持。

## 必需验证

- 生产 MouseGesture 的绑定后静默 -> native 多秒接续 -> 迟 Raw 不回跳 -> 新 Raw 恢复 -> Raw/native Up 各种先后只结束一次；正常 Raw 同毫秒 excursion、时间回绕、静止按住、source takeover 和1000ms用例仍通过。
- 生产纯几何入口验证较大越界 -> 回归原锚点；重复 serial 不改变逻辑抓点；clamp/呈现滞后时缩放后仍保持原逻辑抓点。
- fresh trellis-check、完整现有 InkeysRepo.sln Debug|ARM64、headless --no-window 和两个既有早退出生产探针。禁止 GUI、隐藏 HWND、commit、push。构建成功不能替代用户设备复现。
