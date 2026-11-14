# 第六轮原生形状编辑输入

WM_POINTER 笔采用 Windows 已有接触隐式捕获；只动态读取文档化 GetPointerInfo，以保持旧系统加载兼容。官方说明：<https://learn.microsoft.com/en-us/windows/win32/inputmsg/wm-pointerupdate>（Remarks 中说明接触在指定窗口隐式捕获，终态释放）。

本机只读 GetProcAddress 探针确认 GetPointerInfo 非零，而 SetPointerCapture / ReleasePointerCapture 不存在。这两个名称并非本实现可依赖的 Win32 桌面 API，已移除其探针与假想分支。

所有输入来源共用 SetCapture/ReleaseCapture 的窗口手势生命周期。WM_CAPTURECHANGED 除自身释放期间外一律取消当前会话；笔 WM_POINTERCAPTURECHANGED 按原生 pointer ID 独立取消。WM_TOUCH 解码失败取消当前 Touch/TouchPen，避免终态包丢失后永久持有。

--shape-editor-native-input-test 在产品初始化之前调用生产 NativeContactRouting 和捕获取消判断，不创建 HWND。覆盖 source/id、重复包、增减指、输入互斥、16 指容量、取消清理与过期释放票据。这些数值/规则测试不能证明实体设备消息交付或 Windows 实际捕获顺序；实机鼠标、触摸、笔仍需用户验收。
