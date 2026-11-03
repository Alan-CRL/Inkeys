# 主栏点不动：已有诊断与缺口

## 用户补充及排查边界

用户屏幕笔可以点击主栏，数位板M6不可以。先保留主栏现有WM_TOUCH业务设计，以实际消息证据判断；不能仅凭屏幕笔成功推断M6相同，也不能把过滤差异直接写成已证实根因。PPT/白板笔振动暂不修。本轮只读调查，未改产品或新增开关。

## 现在可用的开关

设置 → 实验选项 → 控制台输出 → 测试触控反馈；配置Experimental.Inkeys3.ConsoleOutput.Haptics。Setting.cpp:4433 consoleToggle仅保存并标重启生效；IdtMain.cpp:3061启动启用HapticDiagnostics，:3067绑定控制台。正式构建也支持（InitializeDebugConsole :304），两路标准流均已重定向时不额外创建控制台，并保留文件/管道句柄。

## 日志含义与局限

| 输出 | 可观察 | 不可据此断言 |
| --- | --- | --- |
| [PenHaptics] event=bar_pointer | WM_POINTERDOWN/UP，pointer/type/valid/flags/cursor/source/time/point/run；Interaction.cpp:341 | valid是触觉笔身份可用性，不是按钮点击成功；不能单凭此证明收到WM_TOUCH |
| event=bar_touch | 已取得TouchInfo并通过触点锁定与BeginTouch路径的Down/Up、来源、时间、坐标、token/match；:3148/:3169 | 没有这一行不等于OS没发WM_TOUCH；早期GetTouchInputInfo失败/触点选择/租约拒绝都可能无日志 |
| event=bar_invalidate | 触觉凭据失效的leave/capture/hidden等原因；:313/:339 | 不是完整UI会话取消原因日志 |
| event=bar_click | 进入按钮业务接受点后的可选触觉请求；:591/:598 | accepted=0不是按钮业务失败，no_pen_session/token=0只表示缺少可核验笔触觉凭据 |
| [UiPointerDrag] | 主按钮移栏Seek的native/Raw/捕获/位置与取消摘要；:9240 | 不覆盖所有普通点击，更无法解释入队前已过滤的按下 |

HapticDiagnostics.h:81把这些日志写到DiagnosticOutput::Stream::Error，再由非阻塞worker送stderr。默认控制台输出不等于已写入普通log/idt时间戳.log；后者是IDTLogger文件sink（IdtMain.cpp:2778）。采集应保留控制台原文或启动时同时重定向stdout/stderr。日志满队列可能有丢弃统计，缺行也不应直接当作消息不存在。

“输入与橡皮诊断 控制台输出”主要是画布RTS/面积/设备信息；Draw3开关主要设备环境，光标开关主要画布光标，不能替代主栏窗口输入流水。

## 最小人工采集

同一版本、同一主栏位置与相同目标按钮，鼠标→屏幕笔→M6分别轻点3次，分段标明时间；保存全部PenHaptics输出并说明哪些操作生效。每次先正常启动并确认诊断初始化输出，不能只截最后一两行。无需先改Windows Ink或输入策略。

## 现有缺口及后续诊断建议（未实施）

- QueueWindowMessageInLayoutSpace（Interaction.cpp:9728）和WndProc（:3208）丢弃带Pen/Touch签名WM_MOUSE时立即返回，无原始消息/来源/过滤原因日志。
- WM_TOUCH原始到达、GetTouchInputInfo失败/GetLastError、触点不合格、BeginTouch拒绝与入队失败缺完整可区分日志。
- 普通按钮缺统一的窗口→入队→会话→命中→业务接受/取消流水；触觉发布日志不能代替该流水。

若要一轮闭环，最小补充应只观察：原始消息+extraInfo及source，WM_TOUCH读取和触点选择结果，generation/capture与取消原因，排队/消费，命中控件与业务接受。Down/Up/Cancel完整记录，Move限频/摘要，沿已有诊断开关与非阻塞输出，不在WndProc逐包阻塞写控制台、不修改输入/capture/去重策略。缺消息时才能区分OS未到达、过滤、触点拒绝、会话取消与命中/业务拒绝。

诊断补充属于后续实现方案，不把用户询问现有开关当作已批准新增产品设置或输入修复。
