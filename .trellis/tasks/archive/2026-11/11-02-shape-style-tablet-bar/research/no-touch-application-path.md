# 无WM_TOUCH日志：应用消息路径与配置核查

日期：2026-11-03。本轮只读源码/旧日志，只写本文；无联网、产品/测试修改、构建、GUI或提交。用户确认新日志来自另一块数位板，文件名不能作为设备型号或Windows Ink状态证据。

## 结论

未找到应用在原始日志之前统一吞掉五窗Touch、注销五窗Touch或用进程级开关关闭Ink/Touch的代码。五窗均尝试RegisterTouchWindow，调用处也处于正确overlay owner线程。但应用忽略注册返回值且没有IsTouchWindow/错误日志，所以只能确认“代码会尝试注册”，无法确认本次运行全部注册成功；这是一项真实观测盲点，不是已证明的失败。

代码和日志仍不能确认新设备关闭了Windows Ink。两份记录显示同一组应用构建标识与同样RTS多点设置，但只有一份看到Touch；设备/驱动配置或Windows的Pen/Touch呈现路径差异是待解释变量。驱动开关与平台API合同由主会话另查官方资料，本文不猜测。

## 五窗注册及线程

- IdtMain.cpp:3438-3442：touchRegisterFuc直接RegisterTouchWindow(hwnd,0)，不保存BOOL、不读失败GetLastError；随后只调用窗口级DisableEdgeGestures。
- IdtMain.cpp:3572-3584：四分页与Bar均传此created回调。Ppt.cpp:259-261只是返回PageControl::WindowProc，没有额外Touch过滤。
- Window.cpp:1060起RunGroup在native线程调用CreateGroup；:1435 CreateWindowExW，随后保存HWND和threadId、:1500附近绑定消息通道，:1578标ready并:1581执行spec.created；同线程完成创建和注册，主消息循环:1132-1133带MessageDispatch::Scope后DispatchMessageW。未见拿主线程去注册另线程HWND的路径。
- 当前IdtMain.cpp:3530 created包装先执行原created（含Touch注册），再RegisterNativeTouchInput接入五窗本地registry。旧cbd2294d仅spec.created=created。五窗RegisterTouchWindow本身及入参未因本轮变动。
- 未找到这些五窗的UnregisterTouchWindow、IsTouchWindow或注册结果诊断。因此日志不能排除某次注册失败、也不能把注册成功当成已观测事实。

## HiMsg/默认处理是否能先吞Touch

- 四Page明确spec.bindMessages=false（IdtMain.cpp:3550附近），WndProc直接处理。它们不经过HiMsg，不能用HiMsg统一吞Touch解释五窗全无原始Touch。
- Bar绑定HiMsg的应用callback。HiMsg.hpp:834先调用callback，:843-846区分Handled和Discard，:905最终DefSubclassProc。Bar QueueWindowMessageInLayoutSpace开头记录raw；WM_TOUCH不是IsBarCoordinateMessage，不是键盘，也不属于IsPointerGeneratedMouseMessage所检查的Mouse消息，应用callback对此返回Default。
- HiMsg.hpp:588-626的分类不把WM_TOUCH入ExMessage队列，但它不因此消费原生消息；未Handled仍走DefSubclassProc到Bar WndProc，不解码/关闭Touch handle。
- Window.cpp:1346把spec.windowProc直接注册为窗口类WndProc；MessageDispatch::Scope只保存/恢复当前MSG指针（Window.MessageDispatch.h:33起），没有过滤行为。
- 诊断raw位于Bar callback/Page WndProc业务过滤之前。即使后续GetTouchInputInfo失败或lease拒绝，原始入口仍应有机会记录，不能把零raw归因后端业务拒绝。Move/batch日志有限频且输出有掉样，因此零日志仍不等于数学证明所有上游消息均不存在。

## RTS、窗口属性与手势设置

- StartProduct(drawpad,drawpadPresentation)（Draw3.Product.cpp:32）进入Host::Start，Host.cpp:2339只对drawpad HWND调用stylus.Initialize；RealtimeStylus.cpp:2627 put_HWND绑定该window，:2639 SetAllTabletsMode(TRUE)，:2702 put_MultiTouchEnabled(TRUE)操作同一个RTS对象。没有把RTS对象绑定五个UI窗或修改进程级五窗路由的应用代码。
- Draw3.WindowControl.cpp:366-369的MICROSOFT_TABLETPENSERVICE_PROPERTY设置在所附着画布window，脱离时按同HWND恢复；不是对所有窗口SetProp。
- Bar.Interaction.cpp:3200与PageControl.cppm:130使用相同gesture flags：0x1、0x8、0x100、0x200、0x10000。SDK tpcshrd.h:69-80对应press-and-hold、tap反馈、Touch UI force on/off、flicks；它们没有“禁止全部pen/touch输入”的位。0x100/0x200同时出现是既有配置，旧cbd代码相同；不能据此认定它造成某设备完全无Touch。
- Other.Gesture.cppm:14起DisableEdgeGestures仅用SHGetPropertyStoreForWindow设置该HWND边缘手势属性，不是输入源注册、解码或全局禁Ink。
- 在已核五窗/Window/Message相关路径未发现EnableMouseInPointer/RegisterPointerInputTarget调用；不从它们不存在推导Windows必须给笔生成WM_TOUCH。

## 两份旧日志的应用侧对照

原路径分别为ignored TestResults/tablet-ui-diagnostics-x64/other-tablet-capture-20261102.txt和TestResults/touch-fix/additional-user-capture-20261103.txt。

- 两份L5均draw3-Nov 2 2026T22:01:56 arch=x64 msvc1944；两份L41 ui_schema均Nov2 22:02:09。标识相同不等于已核EXE hash，但日志不支持先归因应用构建变化。
- 两份L22/23均tabletWindowFlags=0x1010219、MultiTouchEnabled=true；digitizer分别0xc5/0xcd，是系统能力字段，不能反推当前数位板驱动Windows Ink复选框。
- 有Touch的文件L114为Bar callback raw WM_TOUCH、L115 touch_read success、L116真实Touch hSource=0x23410259；说明该消息框架及诊断确实能接收/记录Touch。
- 另一设备文件可见Pen Pointer+兼容Mouse，而无Touch记录，详见additional-log-coverage.md和additional-log-coverage-check.md；其device=0仅Raw/Mouse未填设备字段，不是Touch.hSource=0。

## 本轮能排除和不能排除的范围

可排除静态源码中“只给某一个五窗注册”“把四Page统一交HiMsg过滤”“将WM_TOUCH当兼容Mouse过滤”“RTS初始化代码直接注销五窗”等具体解释。不能排除注册API在现场失败、消息被投到采集之外的窗口、驱动/Windows模式差异或有限诊断丢失，也不能证明Windows Ink关闭。

若后续需要更少复测的应用证据，最小观测缺口是五窗owner created时的RegisterTouchWindow BOOL、即时失败GetLastError及IsTouchWindow/flags读回、HWND/thread/generation；本轮未加代码，也不要求用户立即采集。当前无测试环境时，应保留这些未知，先由官方API/设备模式资料判断哪些路径本就不会产生WM_TOUCH。
