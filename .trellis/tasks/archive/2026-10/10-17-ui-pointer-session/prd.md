> 2026-10-17 最新批准修正：请先读 [correction.md](correction.md)。普通点击仍由 HiMsg Down/Up 完成；Raw 只补充拖动。该修正覆盖下文旧的 Raw 独占与精确绑定要求。

# UI 指针会话与窗口外拖动

## Goal
替换 UI 按住交互对全局按键缓存/异步按键查询的依赖，让鼠标与 Win7 WM_TOUCH 在窗口外仍能可靠移动和结束，覆盖 Bar 与四个 PPT 控件。

## Approved scope and authorization
前序多轮调查已完成；用户确认 Raw Mouse 不进入 HiMsg、在 Bar 来源归属状态机汇合，并纠正 PPT 拖动为必接入项。2026-10-17 用户明确要求“创建任务并开始实现。本次不修改 hook。（原来的键盘和鼠标保留）”。该消息批准紧邻的完整设计总结及实施；本任务记录已有决定，不重新发起相同的规划批准。

## Requirements
- R1: 共享进程级 Raw Mouse 注册/采集，独立于边缘光效生命周期。WM_INPUT 不进入通用 HiMsg。
- R2: Bar 触摸维持 WM_TOUCH -> 有触摸标记的 ExMessage -> HiMsg；原生鼠标 Down/Move/Up 保留 HiMsg 入口，Raw 只经私有通道补充拖动，由 Bar 交互线程拥有会话状态。保留 Win7，不引入强依赖 WM_POINTER。
- R3: 单一会话区分 Mouse/Touch、活动触点和会话代次。移动/结束只作用于当前归属。触摸接管鼠标时取消旧会话，避免跨来源松开误提交。
- R4: 完整覆盖主按钮移栏/停靠、预设颜色拖动选色、二维选色板、粗细滑轨/预览相对拖动/数值浮窗、FineDial 拖动/精调转换；普通 Bar 按钮复用可靠的按下/松开/取消。
- R5: 完整覆盖 PageControl 四个 PPT 控件窗口的拖动，保留原命中区、阈值、配对位置更新/保存。翻页长按可靠结束/取消。PageControl 保留自己的状态机和处理上下文，不放入 Bar 业务线程。
- R6: Down 需要命中，取得会话后越界不丢失 Move/Up。结束位置取输入样本，触摸不读取系统光标。Cancel 与正常 Up 分开，保留各控件即时更新/提交/回退规则。
- R7: 无消息时仍推进停留、静止锁定、长按计时；FineDial 模式转换延续会话，释放后惯性独立推进。移动合并不得破坏速度/路径判定。溢出必须显式取消，不能卡住。
- R8: 控件隐藏、模式/版本改变、取消模式、销毁等路径清理当前会话；不能全局清空下一手势消息。

- R9: Raw 未绑定启动期限只结束补充尝试，不限制原生拖动时长。不得凭业务 Dragging 将 generation=0 的超时/失败提升为业务取消；两个客户端服从共享裁决。

## Acceptance criteria
- AC1 (R1-R3): 公共注册只有一个权威入口；关闭边缘光效后鼠标会话仍有输入；原有键盘/鼠标 hook 源码和安装调用保持原样。
- AC2 (R4,R6): 上述所有 Bar 连续交互使用同一输入机制，鼠标按住循环不再依赖 GetAsyncKeyState 或 IsKeyBoardDown；不向 HiMsg 伪造 Raw Mouse。
- AC3 (R5): 四个 PPT 控件鼠标/触摸均可越界拖动和结束，配对位置、翻页长按与 Whiteboard 原有语义不变。
- AC4 (R3,R7,R8): 无窗口测试验证来源隔离、代次、快速相邻会话、Up/Cancel、有界队列溢出、释放后无残留；计时和轮盘历史边界有针对性证据。
- AC5: 完整 InkeysRepo.sln Debug|ARM64 编译，适用 headless --no-window 测试；无 GUI 授权不创建测试 HWND 或启动主窗口。保留原编码/换行，git diff 无无关格式变动。

## Out of scope
修改或删除任何键盘/鼠标 hook、改变 PPT 绘制键盘接管/退出确认、未来快捷键、Draw3 绘制管线、设置/消息框原生或 ImGui 拖动、第三方 HiMsg 修改、配置/UI/COM 协议变化、提交或推送 Git。

## Evidence and runtime limits
Bar.Interaction.cpp:458 等待入口、585 WM_INPUT、874 WM_TOUCH、2638 选色板、4134 粗细、5553 色块、6219 Raw 注册、6677 主按钮。PageControl.cpp:2080 输入、2174 拖动区域、2252 按下、2423 取消。真实 Win7 触摸与普通权限程序在管理员前台下的事件完整性必须现场验证，编译和注入测试不能证明已彻底绕过 UIPI；安全桌面不作接收保证。
## 后续实机反馈与修正边界

用户再次确认抓手偏移在普通权限前台、同一显示器内也能复现，故不能归因 UIPI 或真实跨屏。最新实施与证据边界见 [edge-and-stream-correction.md](edge-and-stream-correction.md)：共享仲裁修复 Raw 静默后的永久 native Move 门控，Seek 保持逻辑抓点并记录有界输入/几何诊断；两流同时缺失仍无跨权限保证。

## Final acceptance and commit authorization

2026-10-17 用户反馈人工验收“基本通过”，明确要求提交 commit 并结束当前任务。本次据此结案；本节覆盖此前“不提交”的阶段性限制，提交包括当前任务代码、测试、spec、验收记录及 Trellis 收尾记录，仍不推送远程。详细自动化与人工证据边界见 validation.md。
