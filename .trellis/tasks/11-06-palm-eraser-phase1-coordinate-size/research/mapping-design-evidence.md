# 窗口映射方案补充证据与决策

日期：2026-11-06。承接code-and-api-evidence.md，当前基线42f4b9a8。

## 官方来源及选择

- [Microsoft Windows-classic-samples CustomRenderer.cpp](https://raw.githubusercontent.com/microsoft/Windows-classic-samples/main/Samples/Win7Samples/tabletpc/realtimestylusplugin/cpp/CustomRenderer.cpp)：本轮成功读取main版本。HiMetricToPixel读取m_hDC的LOGPIXELSX/Y；Packets把X/Y视为ink坐标并调用该转换。这里验证的是官方示例的单位来源，未把main内容冒充用户给出的固定commit。
- [Microsoft Learn RTS plug-in sample](https://learn.microsoft.com/en-us/windows/win32/tablet/realtimestylus-plug-in-sample)：managed示例采用浮点DpiX/Y除2540。与原生示例共同支持采用HDC比例且避免其旧LONG除数截断。
- [GetDeviceCaps](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdevicecaps)：LOGPIXELS对多显示器具有全局语义，因此不能替换为GetDpiForWindow或显示快照里的effectiveDpi。该选择消除输入设备原生计数倍率的误用，但跨屏实际RTS坐标仍单列实机验收。
- [GetDC](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdc)：返回指定HWND的客户端DC；单个DC同一时间只能由一个线程使用，ReleaseDC必须与GetDC同线程调用。查询结果只保存数值，不保存HDC。
- [GetWindowDpiAwarenessContext](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowdpiawarenesscontext)：取得窗口本身的DPI context，结果不受调用线程awareness影响，最低Win10 1607。
- [SetThreadDpiAwarenessContext](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddpiawarenesscontext)：返回旧context，可在短作用域结束时恢复，最低Win10 1607。两者仅动态解析，Win7不能静态依赖它们。
- [UpdateMapping](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-updatemapping)：通知显示DPI/方向变化，文档关联WM_DISPLAYCHANGE；补齐窗口映射失效通知而不假定所有跨屏变化一定收到它。
- [InkSpaceToPixel](https://learn.microsoft.com/en-us/windows/win32/api/msinkaut/nf-msinkaut-iinkrenderer-inkspacetopixel)：另一官方路径，可以承担单位变换，但当前方案不需要新增renderer object/view transform，故不新增COM对象及整数基向量近似。

选择HDC比例是技术实现收敛，不变更用户目标；既不是恢复各设备自己的倍率，也不是取窗口UI缩放。没有宣称静态证据能保证任意驱动的运行结果。

## 生产代码细化

- Inkeys/IdtMain.cpp:142：EnsureProcessDpiAwareness优先动态SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)，缺失时SetProcessDPIAware；不改进程设置。
- Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1597、1607：PublishDisplaySnapshot/PumpDisplayScale是绘制owner消费显示快照的现有路径；显示物理标尺不等于RTS单位映射。
- 同文件:2444：RTS Initialize失败并安全停产后，Host通过窗口owner启用Win32Mouse；复用这条回退，不新增第三输入后端。
- Draw3.RealtimeStylus.cpp:1121、1154、1179：Enable/InRange/Down已存在writer慢路径，补建decoder可在这些边界完成。
- 同文件:1538：UpdateMapping已有取消和重建边界；:2943提供diagnosticsMutex及插件lease保护的读取入口，可作为owner访问插件生命期的参考，不跨线程裸用stylus COM。
- Draw3.RtsStartup.h:54：CanRetryBasic仅对PacketDescription/InputConfiguration/Enable等相关失败重试。公共HDC映射失败不应伪装成包属性失败。
- Inkeys/IdtMain.cpp:1897：既有Palm CPU probe早退入口；RTS验证采用同样的严格参数和启动前退出模式，不执行CreateWindow。

## 确认与技术待项的处理

- 用户已选择A，识别/预览不改。无剩余产品范围问题。
- 映射来源、资源释放、可选DPI API、缓存刷新、失败行为、生产测试接缝均在design.md明确。
- helper具体名称与最小内部代码组织在实施中依现有模式落实；不改变上述行为合同。
- 真实Surface/异常设备、多显示器、Win7 x86/x64的运行结果留作验收证据，当前不声称通过；不以等待日志替代正式修复。
