# Draw3 主画布链路与可验证假设

基于当前 `f42083f4f0b5b969b25abc68672df0eeda070f75` 源码静态追踪，2026-10-02。此记录是当前代码事实，不是 Win7 根因结论。

## 主链路

1. `Inkeys/IdtMain.cpp:2793-2850` 创建 Freeze、DrawpadPresentation、Drawpad、PPT 与 Bar specs。两个画布为不同 role；DrawpadPresentation 固定为分层/穿透/不激活窗，Drawpad 样式根据首选 DComp 能力预配置。
2. `Inkeys/Inkeys/Window/Window.cpp:926-1018` 在 Window Service owner 线程创建 HWND、应用 owner/style、绑定消息及初始可见性；`1117-1162` 销毁 HWND。Drawpad 的 owner 链为 `Freeze -> DrawpadPresentation -> Drawpad`。`1804-1899` 成对切换两窗可见性并读回状态，`1903-1923` 成对提交 bounds。
3. `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1347-1445` 按 GraphicsReady、PresenterReady、ControllerReady、首帧提交推进启动；主线程绘制线程初始化资源、选 Primary 或 Selection ULW target、清屏并将最后一次 present 结果作为 `firstFrameReady`。
4. `Draw3.GraphicsInitialization.cpp:49-70,77-117` 请求硬件 D3D11；Windows 7 不接受 feature list 中的 11_1 时以仅 11_0 列表重试；硬件最终失败才回退 WARP。当前设备创建路径没有输出失败阶段/HRESULT。
5. `Draw3.TransparentPresentation.cpp:595-665` 创建带 waitable flag 的交换链；创建或配置失败会释放并重试普通 swapchain。交换模式为 FLIP_SEQUENTIAL。`298-350` 的 ULW presenter 从最终纹理复制到 CPU DIB，并用目标 HWND 窗口矩形、DIB size、alpha blend 和 dirty rect 调用 UpdateLayeredWindowIndirect；失败只返回 false，当前不保留对应 GetLastError/提交参数快照。
6. `Inkeys/IdtMain.cpp:3037-3049` 首帧未就绪会报告致命启动错误；正常继续后调用 `ReconcileDraw3Presentation`。`Inkeys/IdtState.cpp:407-590` 根据 workspace、selection、页面内容、辅助帧清洁度及目标 revision 选择主窗、Presentation 窗或 Hidden，再通过 Window Service 互斥显示。

## 关键状态事实与待证伪判断

- `Draw3.PresentationState.h:14-22`：书写模式选择 Primary；选择模式且当前页无内容、辅助全帧已清洁时选择 Hidden；其余选择模式选择 Presentation。Hidden 在空白选择页是当前明确行为。
- `Draw3.HiddenWindowTest.cpp:740-760` 已断言选择模式空白页隐藏两窗、有内容时显示 Presentation。用户的“选择模式穿透”必须保留此类行为。
- `Window.Legacy.cpp:94-122` 的 `TopWindow` 打印“等待覆盖层首帧”，但其 predicate 检查 Bar startup state、PPT 和 Freeze；它不是 Draw3 首帧门。Draw3 首帧已在 `IdtMain.cpp:3037` 单独检查。附件到达普通主线程/窗口线程初始化，因此整个进程没有卡在 Draw3 的同步启动门控；但实际绘制目标、窗口可见性和用户视觉仍未知。
- 附件的 `UI3Diag` 成功数据标签为 `Bar`；不能推出 `Drawpad` 或 `DrawpadPresentation` 有相同 ULW 结果。
- waitable 交换链 `0x887a0001` 对应的代码会回退普通 swapchain；附件明确记录回退后普通 swapchain 与 `UlwDirtyRect` 已启用。因此此 HRESULT 不是单独足以成立的故障证据。

## 最小诊断应回答的问题

| 检查点 | 记录字段 |
| --- | --- |
| HWND 生命周期及窗口服务 | role、HWND、owner、有效/可见、style/ex-style、bounds、创建/销毁/显隐/resize 请求和读回结果 |
| Host/首帧与状态门 | startup stage、frame attempt/result、workspace/selection/page content、requested/ready target+revision、Retry/等待原因及唤醒原因 |
| 设备和 presenter | Hardware/WARP、feature-list attempt、D3DCreateDevice HRESULT；活动模式、primary/selection target、交换链创建/普通回退结果 |
| 主画布提交 | 目标 HWND、尺寸/格式/swap effect/flags、dirty/full、ULW destination/size/source/blend、API 结果与失败 HRESULT/GetLastError |
| 构建身份 | 每进程一次可与交付清单关联的 diagnostic build ID；交付清单另记录源码状态、EXE SHA-256、架构与配置 |

诊断只在启动、表面/目标状态变化、首帧和失败/恢复时产生；不可逐帧格式化或阻塞绘制。若这些记录显示调用成功而窗口仍不可见，最后一步仍需 Win7 桌面视觉验收。
