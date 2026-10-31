# 平台与 API 证据

## Microsoft 官方文档

- [Magnification API Overview](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/magapi/magapi-intro)
  - Microsoft 建议新捕获场景考虑 Windows.Graphics.Capture 或 Desktop Duplication。
  - Magnification API 不支持 WOW64；32 位 magnifier 应用在 64 位 Windows 上不能正确工作。
  - 窗口化 magnifier control 支持过滤列表，而 full-screen magnifier 不支持。
- [`MagSetWindowFilterList`](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetwindowfilterlist)
  - EXCLUDE 模式依赖 WDDM-capable 显卡；同一 magnifier 只有一个列表；magnification window 自身会被自动排除。
- [`MagGetWindowFilterList`](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-maggetwindowfilterlist)
  - 可查询实际过滤模式、窗口数量和列表；当前代码没有使用它验证写入结果。
- [Desktop Duplication API](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)、[`IDXGIOutputDuplication`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nn-dxgi1_2-idxgioutputduplication) 与 [`DuplicateOutput`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgioutput1-duplicateoutput)
  - D3D 设备必须从目标 output 所连接的 adapter 创建；错误 adapter 会导致 `E_INVALIDARG`。
  - Windows 7 Platform Update 中 `DuplicateOutput` 返回 `E_NOTIMPL`。
  - 显示模式变化、DWM/fullscreen 转换和会话切换会使 duplication 失效，调用方必须重建。
  - 桌面图像为 BGRA8，旋转需由调用方处理。
- [`IDXGIAdapter::EnumOutputs`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiadapter-enumoutputs) 与 [`DXGI_OUTPUT_DESC`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_output_desc)
  - `DXGI_OUTPUT_DESC::Monitor` 提供从目标 `HMONITOR` 精确匹配 output/adapter 的正式路径。
- [DDA against discrete GPU on Microsoft Hybrid systems](https://learn.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/error-when-dda-capable-app-is-against-gpu)
  - 混合显卡设备上针对 dGPU 建立 DDA 可能返回 `DXGI_ERROR_UNSUPPORTED`；关键不是一律选 iGPU，而是选目标 output 实际所属 adapter。
- [`SetWindowDisplayAffinity`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity)
  - 仅适用于当前进程的顶层窗口并依赖 DWM。
  - `WDA_EXCLUDEFROMCAPTURE` 从 Windows 10 2004 起提供；旧版本把该值当作 `WDA_MONITOR`，内容会显示为黑色而不是透明消失。
  - 它不是安全保证，不能假定所有捕获/驱动都遵守。
- [DWM window composition attributes](https://learn.microsoft.com/en-us/windows/win32/dwm/windowcompositionattrib)
  - 文档列出 `WCA_EXCLUDED_FROM_DDA`，但没有配套的正式 SDK setter 合同，不适合作为跨 Win7/多架构产品依赖。
- [Windows.Graphics.Capture screen capture](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture) 与 [`CreateForMonitor`](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
  - monitor interop 要求 Windows 10 1903+；捕获边框/指示语义不适合作为本任务的默认无感一帧路径。
- [`DwmIsCompositionEnabled`](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmiscompositionenabled) 与 [Windows 8 起 DWM 始终启用](https://learn.microsoft.com/en-us/windows/win32/w8cookbook/desktop-window-manager-is-always-on)
  - Win7 仍需考虑 Aero/DWM 关闭；Win8+ 可依赖 compositor 存在但仍需处理会话/模式切换。
- [GDI `BitBlt` 截图示例](https://learn.microsoft.com/en-us/windows/win32/gdi/capturing-an-image)
  - 为 Windows 7/不支持 DDA 的环境提供公开且低版本可用的单帧兼容后端。

## 现场和社区证据（仅辅助，不视为已复现）

- [Intel Iris Xe 环境中 Windows Magnifier 黑屏](https://learn.microsoft.com/en-us/answers/questions/3849538/windows-magnifier-not-working-%28black-screen-shows) 与 [另一例内置 Magnifier 黑屏](https://learn.microsoft.com/en-us/answers/questions/5749778/how-to-fix-magnifier-screen-that-is-blacked-out)
  - 说明内置放大镜也可能受驱动/显示配置影响，不能把“系统程序通常正常”当作当前 API 用法正确的证明。
- [系统更新后 Magnification API 在扩展屏黑屏](https://learn.microsoft.com/en-us/answers/questions/1319321/kb5027223-and-kb5027231-cause-the-magnification-ap)
  - 支持把驱动、显示拓扑和 OS 更新纳入人工矩阵。
- [Windows 7 64 位 Magnification 捕获失败案例](https://stackoverflow.com/questions/53295895/screen-capturing-based-on-windows-magnification-api-fails-on-windows-7-64-bit-wi)
  - 报告 API 调用成功但得到空白/异常小画面，并与 Aero/DWM 状态相关。
- [旧 NVIDIA Win7 多显示器 Magnification 崩溃案例](https://forums.developer.nvidia.com/t/access-violation-while-executing-windows-magnification-api/30356)
  - 表明旧驱动和 secondary monitor 组合可能产生 GPU 相关异常。
- [NVIDIA Optimus 显示连接说明](https://nvidia.custhelp.com/app/answers/detail/a_id/2757/kw/hotfix)
  - 内屏和外接端口可能连接到不同 GPU，进一步说明不能用“机器有 iGPU/dGPU”直接决定捕获 adapter。

## 推论边界

- 官方文档可证明 API 合同、版本下限和 adapter 匹配要求。
- 社区案例只能证明风险真实存在，不能证明当前 Inkeys 已在相同驱动上复现。
- 最终兼容性结论必须来自实施后的 Windows 7、x86-on-x64 和混合显卡真机矩阵。
