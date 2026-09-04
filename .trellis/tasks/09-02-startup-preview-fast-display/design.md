# 技术设计：程序化 Startup Preview

## 1. 设计目标与边界

Startup Preview 是启动期间的轻量视觉反馈，不是 Bar 的第二份内容模型。Preview 只绘制一个连续的中性灰色圆角矩形和可选进度条；它不解析或持久化图像，也不复制 Bar 的 target bitmap。正式 Bar 仍是唯一业务 UI 和最终呈现源。

本设计只覆盖最终进程越过 SuperTop 后的启动编排、Preview owner、共享 RenderPipeline client、D2D/ULW alpha 合同、真实进度、顺序交接和总宽度回写。路径检查、单实例、PptCOM 资源 221、Draw3 独立 device、普通 Cache 目录以及与 Preview 无关的 CRC/SHA 代码不在范围内。

## 2. 纯几何与配置合同

无 UI 副作用的几何常量位于 `Inkeys.UI.Bar.Metrics`，校验与换算接口位于 `Inkeys.UI.StartupPreview.State`：

```cpp
namespace Inkeys::UI::StartupPreview {
inline constexpr double DefaultCachedStartupBarWidthDip = 470.0;
inline constexpr double DefaultStartupBarHeightDip = 80.0;
inline constexpr double DefaultStartupBarCornerRadiusDip = 8.0;

double ResolveCachedStartupBarWidthDip(double value) noexcept;
std::int32_t RoundStartupPreviewDipToPixels(
    double dip, double dpi, double zoom) noexcept;
double CalculateStartupBarTotalWidthDip(
    double mainButtonTargetDip, double layoutTotalWidthDip,
    bool expanded = true) noexcept;
}
```

`ResolveCachedStartupBarWidthDip` 先检查 `std::isfinite` 和 `[80, 4096] DIP` 合法区间；非法、缺失或过小值返回 `470.0`。该值始终是完整外包络 DIP，包括 80 DIP 主按钮和 10 DIP 间隔；它不是 MainBar 主体、物理像素或已经乘 zoom 的值。

默认展开几何由当前 Bar 规则独立核对：`gap=5.0`、单边 `32.5`、双边 `70.0 = 32.5*2+5`、列步长 `75 = 70+5`；Select/Draw/Clean 为 A1 三列，More 为一列，Whiteboard/Freeze 为 A2 一列，Divider 不增加宽度。`CalculateButtonLayoutWidth()` 从首个 5 DIP gap 开始，故主体 `5 + 5*75 = 380 DIP`；完整外包络 `80 + 10 + 380 = 470 DIP`。`BarMainBarWidthDip=80` 仅代表折叠/初始化目标。

mini config 只读取：

| 字段 | 类型/默认值 | 语义 |
| --- | --- | --- |
| `Experimental.Inkeys3.UI3.StartupPreview.Enable` | bool / `true` | 下次启动是否创建 Preview |
| `Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip` | double / `470.0` | 完整主栏外包络 DIP |
| `UI.Bar.Zoom` | 现有数值 | 本次启动的 DIP 到像素换算 |

旧 JSON 中不存在新字段时按默认值读取；旧图片 cache/CRC 字段若存在则忽略，不使 full config 失败。设置页不暴露宽度字段，开关继续采用下次启动会话语义。

## 3. 启动时序与进度

1. `wWinMain` 完成 SuperTop 父/helper/重启边界，只有最终进程继续。
2. 记录唯一 `steady_clock` T0，冻结 immutable progress plan，报告 `SuperTopCrossed`。
3. 在任何 Preview/Window Service HWND 和 Display 依赖前建立 DPI awareness。
4. 读取 mini config，校验宽度，按当前 DPI 和 zoom 计算像素尺寸/定位 bounds，并报告 `PreviewGeometryReady`。启用时 nominal 20 units 放在此真实阶段；禁用时从 plan 删除 Preview 专属单位。
5. 条件化提前初始化唯一 RenderPipeline，创建 Preview owner thread 并注册 `Client::StartupPreview`；任何非核心失败都 bypass。
6. 继续既有日志、COM、Display、full config、PptCOM、字体、Window Service、Draw3、Setting、Whiteboard、TopWindow、Bar 等阶段；milestone 只反映真实完成。
7. Bar 首帧以 requested alpha 0 进入完整 ULW transaction。GetDC、ULW、ReleaseDC、EndDraw 和 `presentCompletion.IsCommitted()` 全部成功后才更新 committed alpha、报告 100% 并发布目标总宽度。
8. Preview 已显示且进度未完成满 3 秒时再淡入进度条；完成后执行顺序交接。所有等待有界。

进度 tracker 仍使用一次性 milestone bit/CAS 或等价短锁，重复、乱序、并发报告只生效一次。显示 ratio 只能 `min(eased, actual)`，时间只控制进度条门和动画相位。失败固定首个错误快照，后续完成报告不再增加 ratio。

## 4. Preview 窗口与呈现

Preview owner/message thread 创建 top-level `WS_POPUP`，扩展样式为 `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，不得加入 `WS_EX_TRANSPARENT`。`WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATEANDEAT`；client 区返回 `HTCLIENT`，吞掉左/中/右键按下、抬起和双击消息。创建、定位、显示、隐藏和销毁均回 owner thread；透明圆角外像素按 ULW 原生 hit-test 规则处理。

Preview target/DIB 使用 32-bpp BGRA8 premultiplied alpha。每帧先画静态形状：填充为具名中性灰 `#808080`，形状 alpha `0.74`，1 DIP 内描边 alpha `0.16`，高度/圆角为 `80/8 DIP`；描边必须向内绘制。形状是一个完整连续外包络，禁止文字、图标、按钮槽、分隔线或假内容。

每次 ULW 都显式填写 `pptDst`、`psize` 和 `pptSrc`。`BLENDFUNCTION` 固定 `AC_SRC_OVER`、`BlendFlags=0`、`AlphaFormat=AC_SRC_ALPHA`，`SourceConstantAlpha` 是 Preview 自己的 committed fade 值。owner 的 SetWindowPos 与 render thread ULW 通过 presentation mutex 串行，render thread 回显 owner 已提交的 geometry snapshot；不能把 geometry 置空来“复用上次值”。

## 5. Mask 与 shimmer

创建/尺寸变化时在 RenderPipeline render thread 缓存静态 opacity mask，mask alpha 必须包括填充、内描边和抗锯齿边缘的最终形状 alpha。每帧用多段线性渐变 brush（宽而低强度漫反射、窄核心、柔和尾光）调用 `ID2D1DeviceContext::FillOpacityMask` 调制亮度。调用前保存 antialias mode，切到 `D2D1_ANTIALIAS_MODE_ALIASED`，无论成功失败都恢复原值；进度条在 shimmer 后绘制，不进入 mask。

相位从 Preview 首次显示的本地 `steady_clock` epoch 计算，不使用 `frameTime.time_since_epoch() % period`。速度函数可用 `phase=(1-cos(pi*t))/2`，周期两端慢、中间快。行程纯函数必须以 mask bounds、渐变方向和全部非零 soft-tail 支撑计算：`t=0` 时支撑整体在左侧外，`t=1` 时整体在右侧外；因此周期 wrap 前后输出严格都是 base-only，不依靠停顿或 magic `-160/+160` 掩盖跳闪。该函数用不同宽度、DPI、zoom headless 验证。

## 6. 进度条与 alpha 状态机

首帧 Preview ULW committed 且 owner 请求显示后调用一次 `MarkPreviewShown`；重复调用不得重置计时。满 3 秒仍未完成时约 180ms 渐显 192 DIP 双层 Fluent 风格进度条；位置以完整外包络内容矩形居中，窄窗口按可用宽度收缩。失败立即显示当前真实 ratio 的红色进度（即使未越过 3 秒门），但先以 ULW committed 或有界等待保证错误帧机会。

Preview 与 Bar 的 presentation alpha 分开保存 requested/attempted/committed：setter/queue 成功不算屏幕提交；任一 GetDC/ULW/ReleaseDC/EndDraw 失败都保留旧 committed，并请求 full-window retry。alpha demand 与业务 dirty transaction 分离。无 Preview 时 Bar 仍以 255；有 Preview 时 Bar 先以 0 committed。

## 7. 顺序 handoff 与恢复

正常完成：

1. Bar alpha 0 已 committed，Preview 继续 shimmer。
2. Preview 整窗按自己的 committed ULW fade 到 0；fade 期间 Bar 不上升。
3. Preview alpha 0 committed 后，Bar 逐步请求 0 到 255，每一步只在对应 committed frame 后推进。
4. Bar alpha 255 committed 后，owner 才 hide/destroy Preview；不得两个 layered HWND 长时间以中间 alpha 同时可见，也不插入透明停顿。

首次自动重试先按同一 Preview 整窗 fade-out，再重启/重试，颜色保持普通。最终 fatal 冻结进度、提交红帧或达到既有上限后显示错误对话框；Preview 留在对话框后面，用户确认后再 fade/destroy。Preview、D2D、ULW、device loss 或 handoff 超时均进入有界 recovery，尽最大安全努力请求/确认 Bar 255，再清理 Preview；无法建立 Preview 时直接使用现有 MessageBox fallback。

## 8. 首帧宽度发布与配置写回

Bar 首个完整 committed frame 在 render thread 计算并发布：

```cpp
expandedTotalWidthDip = mainButton->GetW() /* target, not w.val */
    + 10.0 + layoutTotalWidth;
```

`layoutTotalWidth` 必须是目标布局值；不能从动画值、ULW crop、透明 padding 或 bitmap staging 反推。发布内容为有限、去重的普通 `double` 和必要 revision。主线程或现有配置安全路径在合适时机比较现存值，只有实质变化才写 `main.json`；render thread 禁止 `config.Write()`、文件 I/O 或等待磁盘。写入失败只限频记录，不影响启动。接口应允许未来折叠结果发布 80 DIP，但本轮不改变折叠恢复产品行为。

## 9. 生命周期与 device generation

共享 RenderPipeline dispatch order 保持 `Bar -> StartupPreview`。device generation 变化时 render thread 释放并重建 Preview target、mask、brush 等全部 device-dependent 资源；不得跨代使用旧对象。退出顺序为：停止接收新 frame/width -> 注销 topmost observer -> unregister/drain StartupPreview client -> owner hide/destroy/join -> 再按既有顺序停止 Bar、Window Service 和 RenderPipeline。任何 promise/event wait 都有上限；owner queue 关闭后拒绝新命令。

## 10. 明确删除与兼容

删除 Startup Preview 专属 embedded BIN、生成说明、RC/resource ID、Format/VisualConfig/CacheWrite（无通用职责时）、图片 parser/writer/temp/CRC/signature/layout epoch/CacheState 分支、Gaussian blur、embedded scaling/padding、live proxy/crop/CPU_READ staging/stable debounce、developer capture API、`--capture-startup-preview` 以及关联诊断/project/filter 文档。不得删除普通 Cache 目录或其他功能的 CRC/SHA/Gaussian 代码。

保留 `--startup-preview-smoke`，改为报告 total width DIP、Preview alpha-0 committed、Preview fade-out committed、Bar alpha-0/255 committed、owner exit、Preview inactive 和 recovery 结果。
