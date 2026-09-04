# 启动预览与快速显示主栏

## Problem Statement

最终 Inkeys 进程越过 SuperTop 后仍需完成日志、配置、PptCOM、共享渲染管线、Window Service、Draw3、Setting、Whiteboard 和 Bar 初始化。初始化期间应尽快给用户一个可信的主栏外包络，同时显示真实 milestone 进度；预览不能复制图形设备、改变进程边界或把未完成工作伪报为完成。

本轮产品形态是一个完全程序化绘制的占位：没有文字、图标、按钮分隔或其他内容装饰，仅有一个半透明中性灰色圆角矩形。它代表完整主栏外包络，包括主按钮、主按钮到 MainBar 主体的 10 DIP 间隔和主体本身。

## Product Requirements

- **PR-01 启动边界**：只有最终进程越过 SuperTop 的重启、提权和辅助进程分支后，才能记录唯一 T0、建立 tracker 或创建 Preview。父进程和 helper 不得显示 Preview。
- **PR-02 配置**：`Experimental.Inkeys3.UI3.StartupPreview.Enable` 默认 `true`，设置页说明“下次启动生效”。关闭时不创建 Preview，并保留现有初始化顺序。
- **PR-03 真实进度**：T0 使用 `steady_clock`；milestone 只在真实工作完成时一次性计数，重复、乱序、并发报告不重复计数。显示比例只能追赶实际比例。
- **PR-04 完成门**：只有正式 Bar 首个完整 `presentCompletion.IsCommitted()` 才能把启动进度置为真实 100%。失败冻结进度，不补齐未执行单位。
- **PR-05 新阶段**：旧 `CacheClassified` 不再使用，Preview 的 20 nominal units（启用时）用于 `PreviewGeometryReady`，表示 mini 宽度读取/校验、DPI/zoom 换算和目标 bounds 已完成；禁用 Preview 时从 immutable plan 删除该单位。
- **PR-06 几何**：缓存值的语义始终是完整主栏总宽度 DIP。缺失或非法值回退 `470.0`；本次像素尺寸只由当前 DPI 和 `UI.Bar.Zoom` 换算，不能把像素或 zoom 后结果写回。
- **PR-07 Preview 窗口**：使用独立 owner/message thread 的 top-level `WS_POPUP` layered tool window，带 `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，不带 `WS_EX_TRANSPARENT`。创建、定位、显示、隐藏和销毁均由 owner thread 完成；点击被吞掉且不激活。
- **PR-08 共享图形**：Preview 作为唯一 `Inkeys.UI.RenderPipeline` 的 `StartupPreview` client，复用 D2D1.1 device/context/scheduler；不得创建第二个 device、render thread、WinUI Runtime、DirectComposition 或 Win10-only effect。
- **PR-09 程序化绘制**：每帧绘制固定中性灰圆角矩形，使用 `#808080`、形状 alpha `0.74` 和 1 DIP 内描边 alpha `0.16`，圆角/高度为当前 Bar 的 `8/80 DIP`。颜色与 alpha 集中为具名常量，深浅桌面均可辨认。
- **PR-10 Alpha/ULW**：D2D/DIB 为 32-bpp BGRA8 premultiplied alpha；ULW 使用 `AC_SRC_OVER`、`AC_SRC_ALPHA` 和 Preview 的 `SourceConstantAlpha`。每次 `UpdateLayeredWindowIndirect` 都显式提供 `pptDst`、`psize`、`pptSrc`，并与 owner geometry snapshot 通过 mutex 串行。
- **PR-11 Shimmer**：先缓存包含填充、内描边和抗锯齿边缘最终 alpha 的静态形状 mask，再以 `FillOpacityMask` 调制默认左上到右下的斜向多段低强度渐变。调用时切换到 `D2D1_ANTIALIAS_MODE_ALIASED`，结束时无条件恢复；进度条在其后绘制。
- **PR-12 无跳闪动画**：相位以 Preview 首次显示的本地 `steady_clock` epoch 计算，速度两端慢、中间快。纯函数根据 mask bounds、渐变方向和全部非零 soft-tail 支撑计算离屏起止点；默认 shimmer 不得退化为纯水平或纯竖直。phase 0/1 的支撑完全在窗口外，wrap 两侧均为 base-only。
- **PR-13 进度条**：从 Preview 首帧 ULW 成功 committed 并请求 owner 显示开始计时。满 3 秒仍未完成才以约 180ms 显示现有居中 Fluent 风格进度条；3 秒内完成不显示。进度条在 shimmer 后最后合成，使用真实 ratio。
- **PR-14 顺序交接**：Bar 先以 presentation alpha 0 committed；正常完成时 Preview 整窗以 committed alpha 渐隐到 0，紧接着 Bar 从 committed alpha 0 渐显到 255。只有 Bar alpha 255 committed 后才隐藏/销毁 Preview；不做双 layered HWND 的中间 alpha cross-fade 或人为透明停顿。
- **PR-15 失败/重试**：最终 fatal 时冻结进度，错误进度条立即变红；先等错误帧 committed 或达到有界上限，再显示现有错误对话框，确认后才让 Preview 渐隐。首次自动重试保持普通颜色并先渐隐。Preview 失效时使用既有 MessageBox fallback，Bar 尽最大安全努力恢复到 255 可见。
- **PR-16 首帧宽度回写**：首个完整 Bar frame committed 后发布有限的 `expandedTotalWidthDip = mainButton->GetW() + 10.0 + layoutTotalWidth`。render thread 只发布去重后的普通 `double`；主线程或现有配置安全路径在值有效且有实质变化时写入 `main.json`，失败只记录并忽略。
- **PR-17 测试钩子**：保留 `ReportStartupMilestoneForManualTest`、`RunStartupPreviewRetryFailureForManualTest`、`INKEYS_STARTUP_PREVIEW_RETRY_FAILURE`、分阶段延迟、`INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1`、`--startup-preview-manual-delay` 和 `--startup-preview-smoke`。钩子未启用时不得拖慢正常启动，也不得让 RenderPipeline/Draw3 callback thread sleep。

## 默认几何合同

首次启动和无效缓存值使用整个外包络宽度 `470.0 DIP`。推导必须在代码注释和 headless 测试中可审计：

| 项目 | 值/推导 |
| --- | --- |
| `BarButtonGapDip` | `5.0` |
| `BarButtonOneSideDip` | `32.5` |
| `BarButtonTwoSideDip` | `32.5 * 2 + 5 = 70.0` |
| 完整按钮列步长 | `70 + 5 = 75 DIP` |
| 默认 A1 列 | Select、Draw、Clean，共 3 列 |
| 默认 A2 列 | Whiteboard、Freeze 共用 1 列；EndShow 隐藏 |
| MainBar 主体 | `5 + 5 * 75 = 380 DIP` |
| 主按钮/间隔 | `80 + 10 DIP` |
| 完整外包络 | `80 + 10 + 380 = 470 DIP` |

`BarMainBarWidthDip = 80` 是初始化/折叠目标，不能作为展开主体宽度。未来若支持折叠启动，宽度发布接口应可接受主按钮目标宽度（当前为 80 DIP），但本轮不改变折叠持久化行为。

## 唯一缓存标量

只读取 `Experimental.Inkeys3.UI3.StartupPreview.Enable`、`CachedStartupBarWidthDip` 和 `UI.Bar.Zoom`。`CachedStartupBarWidthDip` 为 `double` DIP，schema 默认 `470.0`，应检查 `std::isfinite`、正值和合理上限；缺失、NaN、无穷、过小或明显异常均回退默认值。旧图片 cache 和旧 JSON 字段安全忽略，不创建、读取、校验或删除图片文件，也不删除 Cache 目录。

## Non-goals

- 不缩短所有初始化耗时，不改写整个 `wWinMain` 或 Bar 渲染循环。
- 不保留或新增 embedded/disk image、BIN、CRC、visual signature、layout epoch、主题/语言分支、图片 parser/writer、Gaussian blur、live proxy、CPU staging 或 developer capture。
- 不改变 PptCOM 资源 221、EXE manifest、Win7 基线或 Draw3 自有 device。
- 不新增折叠状态恢复、第二个图形设备、第二个渲染线程或长期双窗口叠加。

## Acceptance Criteria

- [ ] 设置默认开启且下次启动生效；关闭后无 Preview。
- [ ] 只有最终进程有一个 T0 和一个 Preview owner。
- [ ] 默认几何可推导到 MainBar `380 DIP`、完整外包络 `470 DIP`；坏缓存值都回退 470。
- [ ] 首帧以 ULW `SourceConstantAlpha=0` committed 后才无激活显示，之后 alpha 单调渐显。
- [ ] 圆角灰色占位、静态 mask、offscreen shimmer 和最后合成的进度条均满足 alpha/几何合同。
- [ ] 正常 handoff 为 Preview committed 0 后 Bar committed 255；失败恢复到正式 Bar 可见。
- [ ] 首帧宽度回写使用 target `GetW()`，且不在 render thread 做 I/O。
- [ ] smoke/headless/static/build 结果只在本轮真实执行后记录；未执行的架构、Win7、DPI、多屏和人工输入测试单独列出。

## 明确删除范围

删除 Startup Preview 专属的图片资源登记、图片文件及其生成说明；旧格式/分类/parser、图片 cache reader/writer、签名/CRC/epoch 分支、模糊和 proxy/staging/capture 逻辑、相关 CLI/诊断/project/filter 引用。保留 ULW、D2D1.1、owner thread、RenderPipeline client、presentation alpha 事务、真实进度和人工测试钩子。
