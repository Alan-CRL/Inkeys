# Windows 尺度 API：外部依据与本任务边界

核查日期：2026-11-13。只采用 Microsoft 官方资料，未依此新增 API、驱动设置或硬件探测。

1. [MONITOR_DPI_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/shellscalingapi/ne-shellscalingapi-monitor_dpi_type)：effective DPI 含用户缩放，用于 UI；raw DPI 是另一个概念。由此可知，`96/effectiveDpi` 解决 DIP/像素转换，不能单独证明真实 mm/像素。API 也受进程 DPI awareness 影响。
2. [DPI and device-independent pixels](https://learn.microsoft.com/en-us/windows/win32/learnwin32/dpi-and-device-independent-pixels)：DIP 和像素通过系统 DPI 比例转换。文中物理像素一词指屏幕像素坐标，不表示把 DIP 换算后就取得真实毫米。
3. [GetDeviceCaps](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdevicecaps)：驱动未提供或提供错误的信息时，GDI 尺寸可能由固定 DPI/固定尺寸推算，官方明确提示可能不正确。本任务不建议用 HORZSIZE/HORZRES 当可信 EDID 的无条件替代。
4. [DISPLAYCONFIG_TARGET_DEVICE_NAME_FLAGS](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_target_device_name_flags)：`edidIdsValid` 只证明制造商/产品 ID 来自 EDID 且有效；不能据此跳过活动路径映射、尺寸内容与拓扑校验。
5. [GetPointerDeviceRects](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerdevicerects)：提供输入设备 HIMETRIC 范围和映射显示的当前像素范围；最低 Windows 8。它是未来输入后端可调查的事实来源，不是在本任务中开启 WM_POINTER/触控板或突破现有兼容边界的理由。
6. [Using an INF file to override EDIDs](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/overriding-monitor-edids)：厂商可提供 EDID 修正块，Windows 使用注册表 override 替代 EEPROM 对应块。当前项目是否读取这层需由源码核实；不把基本 EDID checksum 成功等同于硬件尺寸绝对真实，也不将显示服务扩展隐式纳入惯性首版。

本任务推论：可信应表示满足项目定义的可用条件和来源一致性。它不是操作系统对真实测量精度的保证。不可信时用有来源标识的系统 DIP；分辨率/DPI 启发式如果用于大屏响应，应只作为有界策略，不能标成毫米标尺。

以上各条为官方文档的概括；具体当前实现、缺口和集成建议以 `display-trust-and-fallback.md` 的源码证据为准。
