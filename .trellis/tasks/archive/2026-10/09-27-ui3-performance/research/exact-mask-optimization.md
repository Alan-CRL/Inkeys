# UI3 圆角光影 exact A8 整数平移候选：离屏实测与撤销

日期：2026-09-28。范围为生产 `BarUIRendering` 的圆角柔光路径及其显式无窗口离屏测试。测试使用当前工作区 Release|ARM64、RenderPipeline WARP/D2D 设备和 `BarUIRendering::Shape`、`PushFrameDirtyClip`、`HandleFrameEndDrawResult`；`RenderPipeline::Scheduler` 的当前回调 TLS 提供实际 `FillOpacityMask` 提交、exact hit 和 fallback 计数。没有复制另一套绘制算法。

## 冻结场景与原始证据

- 相同 512×256 BGRA premultiplied 离屏目标、96 DPI、zoom 1、210×90 圆角 16、描边 2、同一主光源、同一形状和背景。分别使用 Identity、`(32,24)` 纯整数平移、`(32.5,24)` 分数平移。
- 每场景先绘制 16 帧，使既有连续 8 实际绘制帧的 exact 晋升完成；每轮 11 个测量块，每块 64 次真实 `BeginDraw → Clear → Push clip → Shape → Pop clip → EndDraw`。CPU 侧时钟包括 D2D 提交和 `EndDraw`，不包括最终 BGRA readback。前后各独立运行三轮；33 个块均值并非逐帧延迟样本，不据此估计可靠 P99。
- 原始每块数值及每轮完整像素位于忽略目录 `TestResults/release-hardening/ui3-exact-mask-baseline/{1,2,3}/` 和 `ui3-exact-mask-candidate/{1,2,3}/`；构建日志为 `ui3-exact-mask-harness-final-release-arm64.log`、`ui3-exact-mask-candidate-release-arm64.log`、`ui3-exact-mask-reverted-final-release-arm64.log`。显式测试入口为 `Inkeys.exe --bar-eraser-offscreen-test`，通过 `Start-Process -Wait -WindowStyle Hidden` 运行；此入口在 HWND 创建前返回。

| 场景 | 基线三轮块中位数 ms | 候选三轮块中位数 ms | 基线→候选实际遮罩提交/64 帧 | exact hit/64 帧 | 逐像素结果 |
| --- | --- | --- | --- | --- | --- |
| Identity | 0.06536 / 0.05736 / 0.05799 | 0.06044 / 0.05664 / 0.06087 | 64→64 | 64→64 | 三轮 SHA-256 全相同 |
| 整数平移 | 0.06637 / 0.05581 / 0.06567 | 0.04292 / 0.04438 / 0.04823 | 576→64 | 0→64 | **不同**：451/131072 像素、1078 通道、最大差 1/255 |
| 分数平移 | 0.06620 / 0.06180 / 0.06419 | 0.06272 / 0.06703 / 0.06108 | 576→576 | 0→0 | 三轮 SHA-256 全相同 |

整数平移 33 块的合并中位数为 0.0608078→0.0447094 ms，约 26.5% 下降；轮间仍有噪声，且这是单个离屏控件的 CPU/D2D 块均值，不能作为主栏整帧、GetDC/ULW 或用户可见流畅度提升证据。基线三个场景的 BGRA SHA-256 分别为 Identity `E7795D9EE919C0361E75AC364B783D460945414F116E6264EC6AA4FEEB6738E7`、整数 `BF1B468CA8FCDA78CEA87878E44B0AA0E39C30C3C9208EF21B6B4C57CAFAC582`、分数 `DCC43CA69A1982C059141C2FBA3BF61E82B80089A3DEDBAE0BA38E585CBFE3BC`。候选仅整数输出改变，SHA-256 为 `070814B8ED1694955225D05F13751570688838B12A5E4E4A702D8A5A47EFEB98`；三轮内各自稳定。

## 决定与验证

候选只把 `ResolveRoundedRectExactMask` 的 Identity 判断放宽为纯整数平移，并再次检验变换后四边像素对齐；缓存键、尺寸、DPI、半径、预算、预热、失败 latch、AA、光源和绘制路径未更改。完整 BGRA 差异违反预先确定的像素等价门槛，故**已仅撤销该生产改动**；`git diff` 中 `Bar.Rendering.cpp` 为零。保留 `Bar.EraserAttribute.Test.cpp` 中仅在显式无 HWND 测试入口运行的生产路径采样与遮罩计数断言，作为将来验证更精确实现的离线基准。

撤销后完整 `InkeysRepo.sln Release|ARM64` 构建退出 0；再次执行 `--bar-eraser-offscreen-test` 退出 0、`failures=0`，三个 BGRA SHA-256 均逐字节回到基线，Identity 为 1、整数/分数为 9 次遮罩提交每帧。`git diff --check` 退出 0；测试文件保留 UTF-8 BOM/CRLF。候选和撤销都没有运行真实窗口、ULW、Win7 SP1+KB2670838、硬件 GPU、HC 或 Inkeys2，因此 UI3 最终性能门槛仍未通过。
