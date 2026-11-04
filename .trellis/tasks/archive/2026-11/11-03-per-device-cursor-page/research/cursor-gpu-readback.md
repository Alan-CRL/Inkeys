# 生产光标 GPU 像素取证（2026-11-04）

## 方法

主代理完整 `InkeysRepo.sln Debug|ARM64` 构建后运行既有 `Inkeys.exe --draw3-renderer-upload-test`。此入口只创建 WARP 离屏资源，无 HWND、无 swapchain。以下像素结果来自真实 resolver → DrawTransientDrawingCursor → 产品 VS/PS → BGRA staging 读回，区别于理论 appearance 盒与 CPU 尺寸测试。

原生系统箭头读取方法、位图与实际可见范围见 `native-arrow-readback.md` 和 `native-arrow-measurements.json`：实际系统 DPI=192，当前 GetCursorInfo 句柄与 LoadCursor(IDC_ARROW) 一致，64×64透明画布内可见25×39 px/493像素，热点(0,0)。

## 校准前实测（历史阶段）

| DPI | 原appearance盒(px) | 正式PS可见bbox(px) | guarded PS可见bbox(px) | 非零alpha像素 |
| --- | --- | --- | --- | --- |
| 96 | 12×19 | 12×19 | 12×19 | 119 |
| 144 | 18×28.5 | 17×29 | 17×29 | 250 |
| 192 | 24×38 | 23×38 | 23×38 | 429 |
| 264 | 33×52.25 | 32×53 | 32×53 | 797 |

正式与完整guarded PS已正确跟随DPI，未发现旧链漏乘。192 DPI原生25×39与自绘23×38的差异由实际像素证明；不能把appearance24×38或透明位图64×64当作可见尺寸。用户已确认真实同屏偏小，主代理据此选择在共享常量处校准默认大小，而不是额外再乘DPI。

## 最终校准与实际结果

共享箭头大小为 **13×19.5 DIP**。宽度计入原轮廓最右仅占参考盒95%的留白，高度取原生192 DPI实测39/2。保留HLSL的12×19归一参考轮廓、尖端热点与既有DPI传递链；不改shader。Renderer测试对192 DPI增加独立、严格的25×39可见bbox与尖端(8,8)断言。

| DPI | 最终appearance盒(px) | 正式PS可见bbox(px) | guarded PS可见bbox(px) | 非零alpha像素 |
| --- | --- | --- | --- | --- |
| 96 | 13×19.5 | 13×20 | 13×20 | 128 |
| 144 | 19.5×29.25 | 19×30 | 19×30 | 276 |
| 192 | 26×39 | **25×39** | **25×39** | 469 |
| 264 | 35.75×53.625 | 34×54 | 34×54 | 875 |

全部bbox左上仍为输入尖端(8,8)，没有热点偏移。正式与完整guarded PS结果一致，192 DPI严格断言通过；真实系统与自绘可见外框对齐，形状/AA不同仍使像素数469与原生493不完全相同。

最终完整solution构建退出0；`Build/cursor-continuation/test-results.json` 记录cursor-gpu、cursor-ui、pen-width均退出0。GPU日志末尾为 `PASS failures=0 no_hwnd=1 no_swapchain=1`。完整headless由主会话另行记录最终退出码，本文件不提前宣称其已完成。

## 最终原始关键行

```text
[Draw3CursorPixels] shader=production dpi=96 appearance=(13.00,19.50) hotspot=(8.0,8.0) bbox=(8,8,21,28) visible=(13,20) count=128 no_hwnd=1
[Draw3CursorPixels] shader=production dpi=144 appearance=(19.50,29.25) hotspot=(8.0,8.0) bbox=(8,8,27,38) visible=(19,30) count=276 no_hwnd=1
[Draw3CursorPixels] shader=production dpi=192 appearance=(26.00,39.00) hotspot=(8.0,8.0) bbox=(8,8,33,47) visible=(25,39) count=469 no_hwnd=1
[Draw3CursorPixels] shader=production dpi=264 appearance=(35.75,53.62) hotspot=(8.0,8.0) bbox=(8,8,42,62) visible=(34,54) count=875 no_hwnd=1
[Draw3CursorPixels] shader=guarded dpi=96 appearance=(13.00,19.50) hotspot=(8.0,8.0) bbox=(8,8,21,28) visible=(13,20) count=128 no_hwnd=1
[Draw3CursorPixels] shader=guarded dpi=144 appearance=(19.50,29.25) hotspot=(8.0,8.0) bbox=(8,8,27,38) visible=(19,30) count=276 no_hwnd=1
[Draw3CursorPixels] shader=guarded dpi=192 appearance=(26.00,39.00) hotspot=(8.0,8.0) bbox=(8,8,33,47) visible=(25,39) count=469 no_hwnd=1
[Draw3CursorPixels] shader=guarded dpi=264 appearance=(35.75,53.62) hotspot=(8.0,8.0) bbox=(8,8,42,62) visible=(34,54) count=875 no_hwnd=1
[Draw3Upload] PASS failures=0 no_hwnd=1 no_swapchain=1
```

## 证据边界

这是有原生/GPU实测支持的默认大小校准，不能宣称旧DPI链漏乘。真实同屏观感、真实屏幕笔/数位板、Win7和自定义pointer主题尚未GUI验收。保留已有Cursor诊断门禁与限频内的frame-scale及visual记录，供用户真机回核。未启动产品GUI或操控桌面。
