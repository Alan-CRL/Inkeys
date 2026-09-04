# Startup Preview and Fast Bar Display

本规范记录 Inkeys UI3 启动预览的可执行长期合同。它只适用于最终进程越过 SuperTop 后的启动编排、`Inkeys.UI.StartupPreview`、共享 RenderPipeline client、Bar presentation alpha、真实启动进度和相关配置/测试。旧图片资源、BIN/cache、模糊和 proxy 方案均已废弃。

## 1. Scope and non-goals

- Preview 是完全程序化的、无文字无图标无按钮分隔的半透明深色圆角矩形，颜色与透明度复用正式主栏 Dark Surface，代表完整主栏外包络。
- 正式 Bar 仍是唯一业务 UI；Preview 不复制 Bar target，不建立第二个 D3D/D2D device、render thread、WinUI Runtime、DirectComposition 或 Win10-only effect。
- 不改变 SuperTop/单实例边界、PptCOM activation manifest 资源 221、Draw3 独立 device、普通 Cache 目录或与 Preview 无关的 CRC/SHA/Gaussian 代码。

## 2. Configuration and geometry signatures

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

ShimmerGradient ResolveStartupPreviewShimmerGradient(
    double width, double height) noexcept;
ShimmerTravel ResolveShimmerTravel(
    const GeometryRect& maskBounds,
    const ShimmerGradient& gradient,
    double outsideMargin = 1.0) noexcept;
ShimmerTranslation ResolveShimmerTranslation(
    const ShimmerTravel& travel, double easedPhase) noexcept;

class OrderedHandoffReducer {
public:
    HandoffSnapshot PreviewFirstAlpha0Committed() noexcept;
    HandoffSnapshot BarAlpha0Committed(bool startupComplete) noexcept;
    HandoffSnapshot PreviewFadeOutCommitted() noexcept;
    HandoffSnapshot BarAlpha255Committed() noexcept;
    HandoffSnapshot Bypass() noexcept;
};
}
```

`ResolveCachedStartupBarWidthDip` 对缺失、非 finite、非正、过小或超过合理上限的值返回 `470.0`。值的语义始终是完整主栏外包络逻辑 DIP，不是 MainBar body、物理像素或乘过 zoom 的结果。

mini config 只读取：

| 字段 | 默认/语义 |
| --- | --- |
| `Experimental.Inkeys3.UI3.StartupPreview.Enable` | `true`；设置页改动下次启动生效 |
| `Experimental.Inkeys3.UI3.StartupPreview.CachedStartupBarWidthDip` | `470.0`；总宽度 DIP |
| `UI.Bar.Zoom` | 既有 zoom；仅用于本次 DIP→pixel 换算 |

旧 main.json 缺少新字段时使用默认值；曾写入的旧图片/CRC 字段安全忽略。设置页不暴露 cached width。

测试与人工观察入口固定为：

| 入口 | 语义 |
| --- | --- |
| `--startup-preview-smoke <report>` | 自动 smoke，报告 total width DIP、alpha committed、owner exit、preview inactive 和 recovery；不得输出旧 cache/signature/capture 字段 |
| `--startup-preview-manual-delay` | 仅人工观察时启用 `ReportStartupMilestoneForManualTest` 分阶段延迟 |
| `INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1` | 与命令行等价的人工观察延迟入口，便于脚本/临时环境使用 |
| `INKEYS_STARTUP_PREVIEW_RETRY_FAILURE=1` | 保留的首次重试/最终失败人工测试入口 |

`--startup-preview-smoke` 不隐式启用 manual delay；所有测试 hook 未显式开启时不得拖慢正常启动。

默认展开宽度必须可审计：

```text
gap = 5.0 DIP
oneSide = 32.5 DIP
twoSide = 32.5 * 2 + 5 = 70.0 DIP
columnStep = 70.0 + 5.0 = 75.0 DIP
MainBar body = 5.0 + 5 * 75.0 = 380.0 DIP
full envelope = 80.0 + 10.0 + 380.0 = 470.0 DIP
```

当前默认序列是 A1 Select/Draw/Clean 三列、More 一列、A2 Whiteboard/Freeze 共一列；Divider 只结束未填满列。`BarMainBarWidthDip=80` 只代表初始化/折叠目标。

## 3. Startup and progress contract

- T0、tracker 和 Preview 只能在最终进程越过 SuperTop 的重启/提权/helper 分支后建立；使用 `steady_clock`。
- Preview 启用时，`PreviewGeometryReady` 是真实阶段：mini width 校验、DPI/zoom 换算和目标 bounds 完成后报告；启用计划可保留 20 nominal units，禁用时从 immutable plan 删除 Preview 专属单位。不得再使用 `CacheClassified`。
- milestone 首次真实完成才增加 work units；乱序、重复、并发报告只计一次。displayed ratio 只能追赶 actual ratio；时间只控制动画/3 秒门。
- 只有 Bar 首个完整 `presentCompletion.IsCommitted()` 才报告 100%。失败冻结首个错误快照。

## 4. Window and ownership contract

Preview HWND 必须由独立 owner/message thread 创建和销毁，样式固定为 `WS_POPUP` 与 `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，不得加入 `WS_EX_TRANSPARENT`。`WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATEANDEAT`；可见 client 区返回 `HTCLIENT` 并吞掉鼠标按下/抬起/双击。透明圆角外像素遵循 ULW 原生 hit-test。

create、SetWindowPos、show、hide、z-order、destroy 都回 owner thread；其他线程只 post 命令。topmost observer 只能异步 post，停止前先注销。

## 5. D2D/ULW and alpha contract

- Target/DIB 为 32-bpp BGRA8 premultiplied alpha。
- `BLENDFUNCTION` 为 `AC_SRC_OVER`、`BlendFlags=0`、`AlphaFormat=AC_SRC_ALPHA`；`SourceConstantAlpha` 是 Preview 整窗 committed fade 值。
- 每次 `UpdateLayeredWindowIndirect` 必须显式传入 `pptDst`、`psize`、`pptSrc`。省略 geometry 在本项目路径可能返回成功但不可见/不刷新。
- owner `SetWindowPos` 与 render-thread ULW 通过专用 presentation mutex 串行；render thread 只回显 owner 已提交 geometry snapshot。
- Preview D2D target、mask、brush 只在唯一 `Inkeys.UI.RenderPipeline` render thread 创建/优先释放；dispatch order 为 Bar -> StartupPreview。device generation 变化时清空并重建全部 Preview 资源。公开 `Stop()` 必须先 `Unregister(Client::StartupPreview)` drain，再用 `RenderPipeline::PostControl` 释放资源；若调度器已经停止，只允许直接 reset 作为进程收尾兜底。

## 6. Programmatic shape and shimmer

每帧先绘制一个连续圆角矩形：复用 MainBar Dark Surface `#181818`、形状 alpha `0.8`，白色 1 DIP 内描边 alpha `0.18`，高度/圆角 `80/8 DIP`。颜色通道与 alpha 必须由 MainBar 和 Preview 共用的具名常量提供，避免两边漂移。描边必须向内绘制；禁止文字、图标、按钮槽、分隔线或假主栏内容。

尺寸变化时缓存包含填充、内描边和抗锯齿边缘最终 alpha 的静态 opacity mask。反光用默认左上到右下的斜向多段线性渐变 brush（宽低强度漫反射、窄核心、柔和尾光）配合 `ID2D1DeviceContext::FillOpacityMask`；调用前切 `D2D1_ANTIALIAS_MODE_ALIASED`，成功/失败均恢复原 mode。进度条在 shimmer 后最后合成，不进入 mask。

相位以 Preview 首次显示的本地 `steady_clock` epoch 为基准。默认周期为 4.0 秒：前 70%（2.8 秒）用 `phase=(1-cos(pi*t))/2` 实现两端慢、中间快的非线性扫过，后 30%（1.2 秒）保持在完整离屏终点，以拉长两次反光经过的间隔。默认 shimmer gradient 必须同时包含 X/Y 分量，形成左上到右下的斜向质感，不得退化成纯水平或纯竖直。纯行程函数必须根据 mask bounds、渐变方向和全部非零 soft-tail 支撑计算端点：phase 0 支撑完全在左上侧外，phase 1 支撑完全在右下侧外，中点穿过 mask，停驻期间和 wrap 两侧都是 base-only。不得用固定 magic 行程掩盖跳闪。

## 7. Progress visual and handoff

首个 Preview ULW alpha 0 frame committed 且 owner 请求显示后调用一次 `MarkPreviewShown`；重复通知不重置。显示后满 3 秒仍未完成才约 180ms 渐显居中 192 DIP 双层进度条；3 秒内完成不显示。进度条使用真实 ratio，在 shimmer 后绘制。

Bar 的 presentation alpha 始终区分 requested/attempted/committed；完整 GetDC -> ULW -> ReleaseDC -> EndDraw -> `presentCompletion.IsCommitted()` 前不得推进 committed。alpha 改变强制 full-window ULW，失败保留旧 committed 并请求重试；业务 dirty transaction 不受污染。无 Preview 时 Bar 以 255；有 Preview 时先以 0 committed。

正常完成由 `OrderedHandoffReducer` 或等价单一 reducer 严格推进，不能只靠分散的 phase 判断。顺序为：

1. Bar alpha 0 committed；Preview 继续 shimmer。
2. Preview 整窗 ULW committed fade 到 0，期间 Bar 不上升。
3. Preview alpha 0 committed 后，Bar 从 committed 0 单调渐显到 255。
4. Bar alpha 255 committed 后 owner 才 hide/destroy Preview。

禁止两个 layered HWND 长时间中间 alpha 同时可见、cross-fade 或人为透明停顿。首次自动重试保持正常颜色并先渐隐；最终 fatal 才红色，错误帧 committed 或有界等待后显示对话框，确认后再渐隐。Preview/ULW/device/handoff 失败均有界 bypass/recovery，尽最大安全努力恢复 Bar 255 可见。

## 8. Width publication and I/O boundary

Bar 首个完整 committed frame 发布：

```cpp
expandedTotalWidthDip = mainButton->GetW() /* target, not w.val */
    + 10.0 + layoutTotalWidth;
```

`layoutTotalWidth` 是目标布局宽度。不得从动画值、ULW crop、透明 padding 或 bitmap 反推。render thread 只发布有限、去重的普通 `double`；主线程或既有配置安全路径在有实质变化时写 `main.json`。render thread 禁止 `config.Write()`、文件 I/O 或等待磁盘；写失败只记录/忽略，不影响启动。接口应可接受未来折叠宽度 80 DIP，但本轮不改折叠恢复产品行为。

## 9. Failure, lifecycle and validation

Preview 失败、mask/shimmer 失败、owner 超时、device loss 或 ULW failure 不得阻止正式启动；核心 RenderPipeline、Window Service、Draw3、Setting、Whiteboard 和 Bar failure 仍按既有 fatal 语义处理。退出顺序至少为：停止新 frame/width -> 注销 observer -> unregister/drain StartupPreview -> render-thread resource release -> owner hide/destroy/join -> Bar/Window/RenderPipeline 既有 shutdown；所有 wait 有界。

保留 `ReportStartupMilestoneForManualTest`、`RunStartupPreviewRetryFailureForManualTest`、`INKEYS_STARTUP_PREVIEW_RETRY_FAILURE`、分阶段延迟、`INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1`、`--startup-preview-manual-delay` 和 `--startup-preview-smoke`。测试钩子未启用时不得拖慢正常启动，也不得让 render/callback thread sleep。Smoke 应报告 total width DIP、Preview alpha-0/fade-out committed、Bar alpha-0/255 committed、owner exit、Preview inactive 和 recovery；不得输出旧 cache/signature/capture 字段。

## 10. Validation matrix

必须覆盖：默认 380/470 几何与非法缓存回退；DPI×zoom 舍入；target width 发布和去重 I/O；alpha transaction 与 ULW explicit geometry；shimmer X/Y 方向、endpoint/midpoint/wrap；3 秒进度门；manual delay hook 不影响默认启动和 smoke；顺序 handoff、重试/fatal/recovery；Preview disabled/device loss；headless/smoke、完整 Solution 构建、`git diff --check`、旧符号/资源残留和高版本静态导入审查。Win7 SP1 + KB2670838、WARP、mixed DPI、多显示器、SuperTop/UIAccess、真实 input/topmost/device-loss 需专用环境，未执行时不得声称 PASS。

## 11. Validation and error matrix

| 条件 | 必须行为 |
| --- | --- |
| Cached width 缺失、NaN、Inf、非正、过小或异常大 | 回退 `470.0 DIP`，仍尝试快速显示；不读取/删除任何图片 cache |
| Preview disabled | 从 immutable plan 删除 Preview 专属单位；Bar 保持既有 alpha 255 启动语义 |
| Preview owner/首帧/D2D/mask/shimmer 失败 | 有界 bypass；正式启动继续，Bar 最终尽力 committed 255 可见 |
| ULW 任一步失败 | Preview/Bar 不推进对应 committed alpha；请求完整重试或有界 recovery，不能遗留 alpha 0 Bar |
| Device generation 改变 | render thread 丢弃旧 Preview resources，重建或 bypass；不使用旧 generation COM 对象 |
| 公开 `Stop()` 且 RenderPipeline 正在运行 | `Unregister` drain 后用 `PostControl` 在 render thread reset target/mask/brush；等待有界 |
| 公开 `Stop()` 且 RenderPipeline 已停止 | 不等待无法入队的 control task，只做进程收尾兜底 reset |
| `--startup-preview-manual-delay` 或 `INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1` | 仅人工观察路径启用分阶段延迟；render/callback thread 不 sleep |
| 未启用 manual delay 或仅启用 `--startup-preview-smoke` | 正常启动和自动 smoke 不被人为延迟拖慢 |
| 首次自动重试 | 保持普通 progress 颜色，Preview 先整窗渐隐，再按既有重启/重试流程继续 |
| 最终 fatal | 冻结真实 progress，提交红帧或达到有界等待后显示现有对话框；用户确认后才渐隐/销毁 Preview |
| Bar 首帧/交接超时 | 请求并尽力确认 Bar alpha 255，再清理 Preview；不能静默退出或永久空白 |
| 宽度写入失败 | 限频记录/忽略；不影响启动，render thread 不重试磁盘 I/O |

## 12. Good / Base / Bad cases

- **Good**：合法有限宽度（或缺失而回退 470），Preview alpha 0 首帧 committed；与主栏一致的深色占位和低频斜向 shimmer 可见，真实进度单调；Bar alpha 0 committed 后 Preview committed 到 0，Bar committed 到 255 才销毁 owner。
- **Base**：首次启动没有宽度字段，使用 470 DIP；3 秒内完成则进度条不出现；更慢时进度条只显示真实 ratio，随后按同一顺序交接。
- **Manual**：开发者显式传入 `--startup-preview-manual-delay` 后 milestone 间出现可观察停顿；去掉该参数后同一启动路径不等待。
- **Recoverable bad**：坏宽度、Preview/D2D/shimmer/ULW/device loss、首次重试或 owner 超时；回退/绕过并尽力让正式 Bar committed 255 可见，不阻塞主程序。
- **Fatal bad**：RenderPipeline、Window Service、Draw3、Setting、Whiteboard 或正式 Bar 失败；冻结真实进度，最终 fatal 才显示红帧，在有界等待后进入现有错误对话框和清理流程。

## 13. Tests required

- **纯逻辑**：`ResolveCachedStartupBarWidthDip`、默认 380/470 推导、DPI×zoom 舍入、`CalculateStartupBarTotalWidthDip` target-vs-animation、去重发布、milestone/failure/100% gate。
- **动画/alpha**：Preview 使用 MainBar Dark Surface `#181818` / `0.8` 和白色边框 alpha `0.18`；shimmer 默认 gradient 同时包含 X/Y 分量，4.0 秒周期内 2.8 秒非线性扫过、1.2 秒离屏停驻，phase 0/1 soft-tail 离屏、中点穿过 mask、停驻及 wrap base-only；Preview alpha 0 首帧、单调 fade；Preview→Bar committed reducer 顺序、首次重试颜色、最终 fatal 红帧和 recovery。
- **窗口/渲染**：ULW 三个 geometry 指针始终非空；owner-thread lifecycle、click swallow、presentation mutex、mask antialias restore、Bar-before-Preview scheduler、device generation 重建。
- **集成/静态**：旧 JSON 兼容、smoke 字段、manual delay hook 正/负向、`rg` 残留审查、`git diff --check`、完整 Solution/headless；Win7、DPI、多屏、SuperTop/UIAccess 和真实系统失败在可用环境执行并记录。

### Wrong vs Correct

```cpp
// Wrong: 依赖系统保留上次 layered geometry。
update.pptDst = nullptr;
update.psize = nullptr;

// Correct: 每帧回显 owner 已提交 geometry，并与 SetWindowPos 串行。
std::scoped_lock lock(ownerPresentationMutex);
update.pptDst = &ownerDestination;
update.psize = &ownerSize;
update.pptSrc = &sourceOrigin;
```

```cpp
// Wrong: render thread 直接写配置。
config.Write();

// Correct: 发布有限 DIP 值，由主线程/配置安全路径去重后写回。
PublishStartupBarWidthDip(expandedTotalWidthDip);
```

```cpp
// Wrong: 默认反光退化成纯水平或纯竖直，人工观察会丢失旧版斜向质感。
ShimmerGradient gradient{ 0.0, 0.0, support, 0.0, 0.0, 1.0 };

// Correct: 默认 gradient 含 X/Y 分量，行程按投影动态求离屏端点。
const auto gradient = ResolveStartupPreviewShimmerGradient(width, height);
const auto travel = ResolveShimmerTravel(maskBounds, gradient, outsideMargin);
```

```cpp
// Wrong: 把调试延迟绑定到 smoke 或默认启动。
ReportStartupMilestoneForManualTest(milestone);
std::this_thread::sleep_for(3200ms);

// Correct: milestone 正常报告；只有显式 manual hook 才追加观察延迟。
Inkeys::Startup::Report(milestone);
if (manualDelayRequested && Inkeys::UI::StartupPreview::IsActive())
    std::this_thread::sleep_for(delay);
```
