# 原生箭头无窗口测量（2026-11-04）

探针脚本：`Build/cursor-continuation/measure-native-arrow.ps1`（忽略目录）；原始结果：同目录 `native-arrow-measurements.json`，并保存原样副本于本研究目录。

## 方法与安全边界

PowerShell Add-Type P/Invoke，仅调用读取 API 和内存 DC/DIB。临时将当前线程 DPI awareness 切为 PMv2 并在 finally 恢复；不创建 HWND、不设置系统光标、不操控鼠标、不读写产品配置。对 LoadCursor(IDC_ARROW) 返回的共享句柄做 GetIconInfo/GetObject，并用 DrawIconEx 分别画到黑/白背景内存 DIB，以两次 RGB 变化并集辨认可见像素；同时独立测非零 alpha bbox。GetIconInfo 复制的位图均 DeleteObject，DC/DIB 释放，共享 cursor 不销毁。

## 实测

- GetDpiForSystem：192。
- 采样时鼠标所在 monitor：effectiveDpiX/Y=192，GetDpiForMonitor HRESULT=0。
- GetSystemMetrics(SM_CXCURSOR/SM_CYCURSOR)：64×64。
- LoadCursor(IDC_ARROW)：bitmap=64×64，hotspot=(0,0)，有颜色位图。
- **可见像素 bbox=(0,0,25,39)，尺寸25×39 px，493像素；非零 alpha bbox完全一致。**64×64 是含透明边距画布，不能当成箭头可见尺寸。
- 按实测 DPI 折算可见12.5×19.5 DIP；当前自绘12×19 DIP只是理论 appearance 盒，不能据此判定实际GPU可见大小相等。
- LoadImage 不带 LR_SHARED 返回1814（ERROR_RESOURCE_NAME_NOT_FOUND）；改 LR_SHARED 后返回与 LoadCursor 相同句柄0x10003。虽请求32/48/64等大小，实际始终返回64×64相同共享资源，因此这些项**不能当作各请求 DPI 的独立原生箭头样本**，原始数据明确保留 RequestedDpi 和实际 MeasurementReferenceDpi=192。

## 结论与下一步

读取证据排除了把透明64×64画布直接等同可见箭头大小的做法；单凭已有appearance计算无法复现用户已确认的同屏明显偏小。主代理已转发用户确认：自绘与原生同屏比较仍明显小，继续在真实renderer/PS/VS路径做离屏像素验证。已扩展现有无 HWND --draw3-renderer-upload-test，待主代理完整构建后执行；不运行会创建隐藏HWND的旧 renderer-pixel-test。此阶段未修改产品DPI常量或shader。

## 当前实际光标交叉验证

追加 GetCursorInfo 读取（不移动鼠标）：flags=1（显示中），实际 handle=0x10003，与同次 LoadCursor(IDC_ARROW) 相同；实际位图64×64、hotspot=(0,0)、可见bbox25×39。此时恰好是箭头，故可以确认共享资源与当前系统箭头一致；不是把I-beam当箭头比较。注册表只读另得 Arrow=aero_arrow.cur、CursorBaseSize=32（与主代理独立读取一致）。

## API 证据边界

Microsoft 官方文档说明 [GetIconInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-geticoninfo) 的输出不受调用线程 DPI 虚拟化影响；[Cursor Sizes](https://learn.microsoft.com/en-us/windows/win32/menurc/about-cursors#cursor-sizes) 区分系统名义图像缓冲区和可见形状，并说明系统光标尺寸采用档位。因此本轮比较实际非透明 bbox，不以 64×64 透明画布推算自绘几何，也不把共享句柄的多次 LoadImage 当成各 DPI 独立图像。
