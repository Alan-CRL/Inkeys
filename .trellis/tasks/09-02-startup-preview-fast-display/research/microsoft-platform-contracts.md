# Microsoft 平台合同（历史研究，按当前方案筛选）

> **历史记录，不是当前产品需求。** 下文部分链接和结论来自旧图片/blur/cache 设计。当前 Startup Preview 不再创建或校验 BIN、图片 cache、CRC、签名、layout epoch、Gaussian blur、CPU staging 或 cache writer；相关段落只保留为平台背景，不能作为当前实现要求。当前仍适用的是动态 DPI、D2D1.1、`FillOpacityMask`、premultiplied alpha、ULW 和 owner-thread 生命周期合同。

以下结论只采用 Microsoft 官方文档/API 文档。

## DPI 与 Manifest

- [Setting the default DPI awareness for a process](https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process)：进程 DPI awareness 应在创建任何依赖 DPI 的 UI/HWND 前确定；`SetProcessDPIAware` 最低 Vista，`SetProcessDpiAwareness` 最低 Windows 8.1。
- [SetProcessDpiAwareness](https://learn.microsoft.com/en-us/windows/win32/api/shellscalingapi/nf-shellscalingapi-setprocessdpiawareness)：若 awareness 已由 manifest 或先前 API 设置，会返回 `E_ACCESSDENIED`；本任务把它视为“已有有效来源”，不再错误回退覆盖。
- [MSBuild MT Task](https://learn.microsoft.com/en-us/visualstudio/msbuild/mt-task?view=visualstudio)：`EnableDPIAwareness` 是 manifest 工具输入；工程禁用 manifest 生成时不会单独产生运行时效果。
- [/MANIFEST](https://learn.microsoft.com/en-us/cpp/build/reference/manifest-create-side-by-side-assembly-manifest?view=msvc-170)：`/MANIFEST:NO` 不创建 side-by-side manifest，印证当前主要依赖运行时 DPI API。
- [GetDpiForMonitor](https://learn.microsoft.com/en-us/windows/win32/api/shellscalingapi/nf-shellscalingapi-getdpiformonitor)：返回值受调用进程 awareness 影响，且不建议作为每窗口 DPI API；现有 Win7 兼容路径可保留，但必须先设定进程 awareness。

## D3D11、D2D1.1 与 Effect

- [Platform Update for Windows 7](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)：Win7 SP1 + KB2670838 提供 D2D1.1/effects，并让 WARP 支持 Feature Level 11.0；不提供完整 D3D11.1/DirectComposition 功能。
- [D3D11CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)：在不认识 FL11_1 的 runtime 上把 11_1 放入 feature-level 数组会返回 `E_INVALIDARG`，现有仅用 11_0 重试路径必须保留。
- [Gaussian blur effect](https://learn.microsoft.com/en-us/windows/win32/direct2d/gaussian-blur)：Platform Update Win7 支持该 effect；Soft border 的输出会按约 `sigma * 6 * DPI / 96` 扩张，必须用 effect bounds/额外目标空间避免裁边。
- [ID2D1RenderTarget::FillOpacityMask](https://learn.microsoft.com/en-us/windows/win32/direct2d/id2d1rendertarget-fillopacitymask)：调用要求 `D2D1_ANTIALIAS_MODE_ALIASED`，错误可能延迟到 `EndDraw/Flush`；调用后必须恢复原 mode。
- [ID2D1DeviceContext::GetImageLocalBounds](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1devicecontext-getimagelocalbounds)：用于取得 effect 输出实际边界，解决 blur 左上负偏移与裁切。
- [D2D1_BITMAP_OPTIONS](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/ne-d2d1_1-d2d1_bitmap_options)：CPU_READ bitmap 必须同时 CANNOT_DRAW，并通过 copy 后 Map；符合拟定 staging 路径。

## Layered Window 与线程

- [WM_MOUSEACTIVATE](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mouseactivate)：返回 `MA_NOACTIVATEANDEAT` 可阻止激活并吞掉触发鼠标消息。
- [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow) 与 [UPDATELAYEREDWINDOWINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-updatelayeredwindowinfo)：layered window 支持 per-pixel alpha 与全局 `SourceConstantAlpha`；全局 alpha 改变需要完整 presentation transaction，不能伪装成业务局部 dirty 更新。
- [DestroyWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow)：线程不能销毁由另一线程创建的窗口，因此 Preview 的销毁必须回到 owner/message thread。
- [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos) 与 [ShowWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow)：Preview 用 `HWND_TOPMOST`、`SWP_NOACTIVATE` 和 `SW_SHOWNOACTIVATE` 重验 z-order，不取得前台焦点。
- [Extended Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)：`WS_EX_NOACTIVATE` 防止窗口因点击成为前台窗口；配合 TOOLWINDOW 可避免普通任务栏入口。

## 文件持久化

- [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)：使用唯一临时路径和 `CREATE_NEW` 避免覆盖正在写的 temp。
- [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)：在关闭前显式推进缓存数据；本任务只对后台 cache writer 使用，不阻塞关键启动线程。
- [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)：`MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH` 在同卷目标上完成替换并等待移动完成；与关闭后的 durable temp 配合。

## 直接设计结论

- 不增加 EXE manifest：当前目标可由更早且正确处理返回值的动态 DPI API 完成，同时保持 PptCOM 221 activation manifest 和 SuperTop 行为不变。
- 不使用 `CLSID_D2D1AlphaMask` 或 DirectComposition：`FillOpacityMask` 与 GaussianBlur 已覆盖 Win7 Platform Update 基线。
- Preview HWND 与 RenderPipeline 资源所有权分离：owner thread 只管 HWND/消息/ULW 命令，唯一渲染线程只管 D2D resource/copy/effect；停止顺序用显式 unregister 和 bounded join 串联。
