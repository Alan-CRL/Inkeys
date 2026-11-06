# Startup Preview and Fast Bar Display

本规范记录 Inkeys UI3 启动预览的可执行长期合同。它只适用于最终进程越过 SuperTop 后的启动编排、`Inkeys.UI.StartupPreview`、共享 RenderPipeline client、Bar presentation alpha、真实启动进度和相关配置/测试。旧图片资源、BIN/cache 和 proxy 方案均已废弃；当前效果是程序化绘制，可使用共享 D2D 的基础图元、效果与 SpriteBatch。

## 1. Scope and non-goals

- Preview 是完全程序化的、无文字无图标无按钮分隔的半透明占位，支持掠光、柔光流彩（原彩虹晕染）、像素拼装。居中为主栏圆角矩形，随用户主题取深色/雾白；角落为80 DIP/n=3超椭圆并始终使用深色。背景/内容透明度由 Mini 冻结，默认展开深色0.8/浅色0.92、角落0.6；自定义背景沿正式主按钮比例。
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
    double dip, double dpi, double zoom, double uiBaseScale = 0.0) noexcept;
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
| `Experimental.Inkeys3.UI3.StartupPreview.Effect` | 整数0掠光/1柔光流彩（原彩虹晕染）/2像素拼装；缺失/非法回退0 |
| `Experimental.Inkeys3.UI3.ThemeMode` | 1深色/2浅色；角落仍取深色材质 |
| `Experimental.Inkeys3.UI3.BackgroundOpacity` | 0主题默认；显式30~100，背景与光效整组合成 |
| `Experimental.Inkeys3.UI3.ContentOpacity` | 默认100，30~100，独立控制进度轨道/填充 |
| `UI.Bar.Zoom2` | 用户倍率，默认1.0；不迁移旧Zoom |
| `UI.Bar.UseTaskbarHeightAsScaleBase` | 整数0关闭、1任务栏基础量、2任务栏真实量；旧bool false→0/true→2；冻结大屏缺省1、其余0。预览与真实Bar共用DPI/固定48DIP/真实任务栏三态解析，Shell缺样也不得把基础量退回普通DPI |
| `UI.Bar.StartupPlacement` | 0居中展开底栏、1左下角主按钮、2右下角主按钮；缺失/非法回退0 |

旧 main.json 缺少新字段时使用默认值；曾写入的旧图片/CRC 字段安全忽略。设置页不暴露 cached width。

测试与人工观察入口固定为：

| 入口 | 语义 |
| --- | --- |
| `--startup-preview-smoke <report>` | 自动 smoke，报告 total width DIP、alpha committed、owner exit、preview inactive 和 recovery；不得输出旧 cache/signature/capture 字段 |
| `--startup-preview-manual-delay` | 仅人工观察时启用 `ReportStartupMilestoneForManualTest` 分阶段延迟 |
| `INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1` | 与命令行等价的人工观察延迟入口，便于脚本/临时环境使用 |
| `INKEYS_STARTUP_PREVIEW_RETRY_FAILURE=1` | 仅启用 Harness 的测试构建，且慢速观察环境变量也严格为1、Preview活动时，注入一次重试及最终失败；见第15节 |

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

最终失败的首个可见进度帧必须直接使用红色；失败颜色不得从正常蓝色渐变到红色，避免快速启动失败时出现蓝色闪帧。

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

底板的居中几何为高度/圆角80/8 DIP，角落为80×80 DIP/n=3超椭圆；主题颜色复用 Bar 角色。默认深色仍为 `#181818`，浅色 `#FBFBFB`；透明度和三种效果见第16节。1 DIP描边计入可见包络：80名义按钮包络81DIP，默认470名义展开宽包络471DIP。Shared Bar/Preview 的颜色和默认alpha必须用同源常量或生产策略测试锁定，避免漂移。像素效果运动段可暂时分散外形，但归位终点必须与同一底板RGBA等价。禁止文字、图标、按钮槽、分隔线或假主栏内容。

尺寸变化时缓存组内归一底板及包含内描边/抗锯齿边缘的静态 opacity mask；用户背景透明度在整组最后应用一次。反光用默认左上到右下的斜向多段线性渐变 brush（宽低强度漫反射、窄核心、柔和尾光）配合 `ID2D1DeviceContext::FillOpacityMask`；调用前切 `D2D1_ANTIALIAS_MODE_ALIASED`，成功/失败均恢复原 mode。居中条形进度或角落进度环在 shimmer 后最后合成，不进入 mask。

相位以 Preview 首次显示的本地 `steady_clock` epoch 为基准。默认周期为 4.0 秒：前 70%（2.8 秒）用 `phase=(1-cos(pi*t))/2` 实现两端慢、中间快的非线性扫过，后 30%（1.2 秒）保持在完整离屏终点，以拉长两次反光经过的间隔。默认 shimmer gradient 的 X/Y 跨度绝对值必须相等，使可见高光带与水平线夹角固定45°；不得由宽高比或固定像素下限决定斜角。三种位置及DPI/zoom共用该方向，保留既有梯度向量长度和多段soft-tail宽度。纯行程函数必须根据 mask bounds、渐变方向和全部非零 soft-tail 支撑计算端点：phase 0 支撑完全在左上侧外，phase 1 支撑完全在右下侧外，中点穿过 mask，停驻期间和 wrap 两侧都是 base-only。不得用固定 magic 行程掩盖跳闪。

## 7. Progress visual and handoff

首个 Preview ULW alpha 0 frame committed 且 owner 请求显示后调用一次 `MarkPreviewShown`；重复通知不重置。掠光/柔光流彩 Preview 整窗以300ms渐显；PixelAssembly 保留首帧ULW alpha0，显示后窗alpha255，由每颗像素随自身飞行进度渐显，避免与窗alpha重复压暗入场。显示后满3秒仍未完成才约180ms渐显进度。2026-10-14用户现场反馈覆盖原三位置统一环形方案：居中底栏恢复旧水平条形进度，最大192 DIP、左右至少12 DIP留白、居中显示，轨道max(1px,1DIP)、指示填充max(3px,3DIP)、圆角端点；左右角落保留32 DIP外径、3 DIP描边、圆头、顶部顺时针环形进度。正常轨道深色灰白0.24，浅色为有对比的深灰；0%不伪造填充；最终失败0%轨道立即红。3秒内正常完成不显示。两种进度比例由 Preview 自己维护的 300ms smoothstep 时间轴追赶真实 ratio：新目标先采样当前插值值再连续 retarget，帧率无关且不得超过 actual；完成不直接跳 1，失败颜色立即红并冻结首次失败目标。条形进度或进度环在 shimmer 后绘制。

Bar 的 presentation alpha 始终区分 requested/attempted/committed；完整 GetDC -> ULW -> ReleaseDC -> EndDraw -> `presentCompletion.IsCommitted()` 前不得推进 committed。alpha 改变强制 full-window ULW，失败保留旧 committed 并请求重试；业务 dirty transaction 不受污染。无 Preview 时 Bar 以 255；有 Preview 时先以 0 committed。

正常完成由 `OrderedHandoffReducer` 或等价单一 reducer 严格推进，不能只靠分散的 phase 判断。顺序为：

1. Bar alpha 0 committed；Preview 继续 shimmer。
2. 只有实际成功绘制对应条形进度或进度环、Preview alpha 大于 0 且完整 ULW transaction 成功的帧，才 sticky 记录 `progressEverCommittedVisible`；handoff 开始时据此冻结 300ms（未记录）或 1000ms（已记录）档位。
3. Preview 整窗按冻结档位 committed fade 到 0，期间 Bar 不上升，shimmer 与 progress ratio 每帧继续推进和重绘。
4. Preview alpha 0 committed 后，Bar 按同一冻结档位从 committed 0 单调渐显到 255；整窗 alpha 同时覆盖 MainBar 与主按钮。
5. Bar alpha 255 committed 后 owner 才 hide/destroy Preview。

Preview 未启用或未成功启动时，Bar 直接以 alpha 255 显示，不运行渐显。禁止两个 layered HWND 长时间中间 alpha 同时可见、cross-fade 或人为透明停顿。300/1000ms 只属于正常成功 handoff；首次自动重试保持正常颜色并先渐隐，退出淡出保留独立 160ms，最终 fatal 才红色。Preview/ULW/device/handoff 失败均有界 bypass/recovery，尽最大安全努力恢复 Bar 255 可见。

## 8. Width publication and I/O boundary

Bar 首个完整 committed frame 发布：

```cpp
expandedTotalWidthDip = mainButton->GetW() /* target, not w.val */
    + 10.0 + layoutTotalWidth;
```

`layoutTotalWidth` 是目标布局宽度。不得从动画值、ULW crop、透明 padding 或 bitmap 反推。render thread 只发布有限、去重的普通 `double`；主线程或既有配置安全路径在有实质变化时写 `main.json`。render thread 禁止 `config.Write()`、文件 I/O 或等待磁盘；写失败只记录/忽略，不影响启动。纯宽度计算仍可计算折叠80DIP；缓存发布固定采用展开目标包络，即使正式首帧fold=true也不得写80污染展开缓存。Diagnostics.totalWidthDip报告实际预览名义宽度（角落80，居中cache），与缓存发布值分开。

## 9. Failure, lifecycle and validation

Preview 失败、mask/shimmer 失败、owner 超时、device loss 或 ULW failure 不得阻止正式启动；核心 RenderPipeline、Window Service、Draw3、Setting、Whiteboard 和 Bar failure 仍按既有 fatal 语义处理。退出顺序至少为：停止新 frame/width -> 注销 observer -> unregister/drain StartupPreview -> render-thread resource release -> owner hide/destroy/join -> Bar/Window/RenderPipeline 既有 shutdown；所有 wait 有界。

保留 `ReportStartupMilestoneForManualTest`、分阶段延迟、`INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1`、`--startup-preview-manual-delay` 和 `--startup-preview-smoke`。发布构建不得保留仅凭环境变量存在即可触发重启/fatal 的 `INKEYS_STARTUP_PREVIEW_RETRY_FAILURE` 故障注入入口；真实失败路径另行隔离验证。测试钩子未启用时不得拖慢正常启动，也不得让 render/callback thread sleep。Smoke 应报告 total width DIP、Preview alpha-0/fade-out committed、Bar alpha-0/255 committed、owner exit、Preview inactive 和 recovery；不得输出旧 cache/signature/capture 字段。

## 10. Validation matrix

必须覆盖：默认 380/470 几何与非法缓存回退；DPI×zoom 舍入；target width 发布和去重 I/O；alpha transaction 与 ULW explicit geometry；shimmer X/Y 方向、endpoint/midpoint/wrap；3 秒进度门；manual delay hook 不影响默认启动和 smoke；顺序 handoff、重试/fatal/recovery；Preview disabled/device loss；headless/smoke、完整 Solution 构建、`git diff --check`、旧符号/资源残留和高版本静态导入审查。Win7 SP1 + KB2670838、WARP、mixed DPI、多显示器、SuperTop/UIAccess、真实 input/topmost/device-loss 需专用环境，未执行时不得声称 PASS。

## 11. Validation and error matrix

| 条件 | 必须行为 |
| --- | --- |
| Cached width 缺失、NaN、Inf、非正、过小或异常大 | 回退 `470.0 DIP`，仍尝试快速显示；不读取/删除任何图片 cache |
| Preview disabled / 未成功启动 | 从 immutable plan 删除或收敛 Preview 专属单位；Bar 保持 alpha 255 直接显示，不渐显 |
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
- **Base**：首次启动没有宽度字段，使用 470 DIP；3 秒内完成则进度不出现，Preview 与 Bar 以 300/300ms 顺序交接；更慢时进度平滑追赶真实 ratio，并在曾 committed 可见后选择 1000/1000ms 顺序交接。
- **Manual**：开发者显式传入 `--startup-preview-manual-delay` 后 milestone 间出现可观察停顿；去掉该参数后同一启动路径不等待。
- **Recoverable bad**：坏宽度、Preview/D2D/shimmer/ULW/device loss、首次重试或 owner 超时；回退/绕过并尽力让正式 Bar committed 255 可见，不阻塞主程序。
- **Fatal bad**：RenderPipeline、Window Service、Draw3、Setting、Whiteboard 或正式 Bar 失败；冻结真实进度，最终 fatal 才显示红帧，在有界等待后进入现有错误对话框和清理流程。

## 13. Tests required

- **纯逻辑**：`ResolveCachedStartupBarWidthDip`、默认 380/470 推导、DPI×zoom 舍入、`CalculateStartupBarTotalWidthDip` target-vs-animation、去重发布、milestone/failure/100% gate。
- **动画/alpha**：Preview 使用 MainBar Dark Surface `#181818` / `0.8` 和白色边框 alpha `0.18`；shimmer 默认 gradient 同时包含 X/Y 分量，4.0 秒周期内 2.8 秒非线性扫过、1.2 秒离屏停驻；掠光/柔光流彩 Preview 300ms窗渐显，PixelAssembly 由像素逐颗渐显且只首帧窗alpha0；progress 300ms smoothstep retarget；normal handoff 根据 committed-visible progress 冻结 300/1000ms，渐隐期间 shimmer/progress 继续推进，Preview alpha 0 committed 后 Bar 才按同档位整窗渐显；首次重试颜色、最终 fatal 红帧和 recovery 不退化。
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


## 14. 启动位置与 Mini 冻结合同

### Scope / Trigger
2026-10-13 用户批准三种启动位置，角落24DIP等边距，设置仅下次启动生效；此合同覆盖此前仅展开居中的启动假设。

### Signatures
- main.json：UI.Bar.StartupPlacement : IdtAtomic<int> = 0；StartupPlacement枚举0/1/2，NormalizeStartupPlacement统一非法回退。
- StartupPlacement.h::ResolveStartupPlacementBounds(placement, monitorBounds, workArea, visibleWidthDip, visibleHeightDip, pixelScale, dpiScale)。
- StartupPreview::StartOptions.startupPlacement；Bar::Initialization(StartupPlacement)。
- Display::QueryEffectiveDpi(HMONITOR)只读查询，复用Display内部动态GetDpiForMonitor/GDI回退，不进行完整枚举。

### Contracts
- Mini阶段冻结唯一选择并传两种UI；ReadAll及设置修改不得重选本次启动。配置I/O仍冻结payload进入FIFO。
- Preview早期使用真实主屏有效DPI，正式Bar使用同策略Display快照；pixelScale仅一次UIBase×configZoom。关闭时UIBase=DPI/96；基础量固定为48/80×DPI/96，即使无Shell样本也保持；仅真实量在无可信任务栏时回退DPI/96。不用系统DPI替代真实主屏DPI。
- 角落间距24DIP按可见轮廓测量，排除透明window padding；工作区为有效边界，不可用回退监视器bounds，保留负坐标。
- 角落在注册渲染前Floating+Free+fold=true；MainBar和子按钮/图标/文字/PNG/描边必须直接初始化透明折叠终态，不能首帧再SetTar收起。
- 首次布局只应用一次启动位置；左展开向右、右展开向左；后续PPT/白板/拖动吸附保留原策略。corner不以展开栏宽做启动auto-fit而改变按钮与预览缩放。
- 超椭圆共享仅无状态geometrybuilder；Preview不导入完整Bar renderer。窗口、mask、deviceepoch和线程所有权保留。
- 最终失败进度在ratio0也应有红色轨道，不虚构ratio。DrawRing创建路径/结束sink失败不得记作可见进度或成功ULW，先EndDraw清理后沿原Retry/device-loss/bypass处理。

### Validation / Error Matrix
| 条件 | 行为 |
| --- | --- |
| 旧配置/非法值 | 居中展开默认，不恢复旧Preset记忆 |
| Preview禁用/启动失败 | 正式Bar255按冻结位置显示 |
| corner首帧 | 只有主按钮，无展开/居中闪帧 |
| center→corner→center | 展开宽缓存保持真实目标width，角落不写80 |
| mixed DPI/zoom/负坐标 | 同resolver及主屏DPI，24DIP边距一致 |
| ring绘制失败 | 不sticky记录visible，不完成失败可见帧，正常重试/恢复 |

### Good / Base / Bad
- Good：左角Mini选择一直交接到正式左角折叠按钮，后续手动展开向右。
- Base：缺失新字段沿现有居中底栏启动。
- Bad：只修改InitializeUI坐标，首轮完整layout仍force居中/展开；或corner回写80缓存。

### Tests Required
真实Config Mini/All/冻结写盘、共享几何DPI×zoom/负坐标、缓存切换、既有reducer/handoff；无HWND生产状态赋值回放；真实WARP/D2D超椭圆mask/居中linear/角落ring/shimmer像素（0/.5/1、圆角/圆头、失败0立即红、45°方向及DPI/zoom一致性）。三语设置/导航实际ImGui回放检测裁切与固定页脚重叠。完整ARM64+Headless --no-window。静态/模型测试不能声明真实HWND committed首帧或GUI验收。

### Wrong vs Correct
```cpp
// Wrong：缓存由正式折叠状态决定，污染下一次居中预览。
PublishStartupBarWidthDip(CalculateStartupBarTotalWidthDip(mainWidth, layoutWidth, expanded));
// Correct：缓存始终是展开包络；显示预览宽度另由placement决定。
PublishStartupBarWidthDip(CalculateStartupBarTotalWidthDip(mainWidth, layoutWidth, true));
```

## 15. 人工重试／失败观察入口的测试边界

### Scope / Trigger
2026-10-14用户要求恢复旧环境变量，保证非测试用途不受影响；直接修改已有入口，不创建新Trellis任务。

### Signatures
- 现有工程开关EnableUi3PresentationHarness → INKEYS_ENABLE_UI3_PRESENTATION_HARNESS；默认Debug=1、Release=0。未定义宏也按关闭处理，不修改工程默认。
- INKEYS_STARTUP_PREVIEW_RETRY_FAILURE=1 与 INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1必须同时严格匹配；仅命令行manual-delay不授权失败注入。
- IdtMain::RunStartupPreviewRetryFailureForManualTest在PptCOM成功后、自动更新初始化前运行。

### Contracts
- 测试宏关闭时完全不编译注入分支；不能使用IDT_RELEASE或_DEBUG判断测试构建，前者由头文件固定定义，后者在部分Debug配置也被取消。
- 环境读取检查返回长度恰好1且值恰好L'1'。未设置、空、0、10、1x、true、超长值均不得触发；未启用慢速观察或Preview禁用/失败绕过时也不得触发。
- 未命中入口不增加等待、窗口操作、退出意图、环境/配置写入或子进程。注入只在初始化主线程执行，不阻塞渲染回调。
- 首次尝试保持普通进度颜色并有界淡出，只有一次-WarnTry测试重试；已处于warnTry时只发布人工fatal 0xD0FE，不再启动子进程。复用现有退出监督与失败呈现，不增加正常启动失败策略。
- 测试child继承双环境开关；注入不持久化到用户配置。child启动失败须明确记录并退出清理，不将失败启动视为成功，不形成循环。

### Validation / Error Matrix
| 条件 | 必须行为 |
| --- | --- |
| 测试宏0或未定义 | 无注入环境读取或副作用 |
| 仅failure=1、manual-delay未启用 | 正常启动 |
| 任一变量非精确1 | 正常启动 |
| 双1但Preview不活动 | 正常启动 |
| 双1+测试构建+Preview活动，初次 | 淡出并仅一次重试 |
| 同上且warnTry | 最终红帧/错误提示/清理，无第二次spawn |
| 重试child启动失败 | 记录错误、有界失败/清理 |

### Good / Base / Bad
- Good：局部PowerShell环境设双1，测试版本先重试一次再最终失败，可观察普通进度、淡出与红色。
- Base：普通启动未设双开关，默认发布构建不含注入分支。
- Bad：仅判断变量存在、只看第一个字符、使用IDT_RELEASE作测试门或重试不携带状态，造成普通用户误失败/循环。

### Tests Required
无窗口测试覆盖生产准入策略及真实Win32环境变量长度/值边界（测试恢复原环境）；宏1/0/未定义的生产入口提取回放覆盖不命中零副作用、单次child、warnTry终态、启动失败和调用清理顺序。完整Debug|ARM64 solution与Headless --no-window。不得将替身回放声称为真实GUI/子进程重启/ULW故障验收。

### Wrong vs Correct
- Wrong：GetEnvironmentVariableW返回非0就触发，或enabled[0]==L'1'就接纳长值。
- Correct：编译测试门、双变量精确1、Preview活动同时满足后，才进入有界注入流程。

## 特殊任务栏的冻结贴底线（2026-10-19）

- Mini 通过一次 `Display::QueryTaskbarMonitorInfo` 同时取得目标屏几何/真实 DPI/任务栏样本，冻结 `StartOptions.desktopDockLine` 与 UI基准，避免分别查询拼接不同显隐时刻；不初始化 observer；普通设备不提前枚举 EDID。为统一按设备分类的缩放默认值，仅 READY 触控设备在 Mini 前执行一次现有 Display::Initialize，冻结主屏物理分类并供后续完整初始化及设备默认赋值复用，不重复枚举；这可能增加触控机器预览出现前等待。schema 本身不触发硬件查询。
- `ResolveStartupPreviewBounds`、`ResolveStartupPlacementBounds` 与正式 CenteredBottomBar 使用同一可选桌面线。PPT/Whiteboard 场景优先级仍在正式 Bar 层；两角浮动启动语义不变。查询失败时回退既有几何与真实 DPI，不伪造底部 autoHide。
- Preview 的同线纯几何回归属于无窗口证据；真实首帧与正式 Bar 交接仍按已有呈现验收边界记录。

## 16. 多效果、分层透明度与像素入场（2026-11-04）

### Scope / Trigger

修改启动效果选择、深浅材质、透明度、粒子运动和扩大呈现区域时适用。2026-11-04 用户在第四版基础上反馈彩虹过淡发脏、像素卡顿且光晕过重，已确认第五版样式（`.trellis/tasks/11-04-startup-preview-effect-polish/research/startup-effects-review-v5.html`）：高彩度相邻色相彩虹、汇聚落位、短速度线、像素块加大；完整配置契约见 [configuration-i18n-and-assets.md](configuration-i18n-and-assets.md#启动加载效果选择合同2026-11-04)。

### Signatures

- `StartupPreviewEffect { Shimmer=0, Aurora=1, PixelAssembly=2 }` 与 `NormalizeStartupPreviewEffect(int)`。
- `StartOptions.effect/themeMode/backgroundOpacity/contentOpacity`：启动前冻结的效果、主题和两项整数百分比。
- `StartupPreview.Effects.Policy.h`：`Effects::ResolveAppearance`、`ScatterBounds`、`PresentationBounds`、`BuildParticles`、`Evaluate/Position`、`ParticleOpacity/ParticleScale/FlashOpacity`、`AssemblyReady`、`Oklch`、`GlowHueCenter/MakeGlowPalette/GlowField/GlowOpacity`。
- `StartupPreview.Effects.Drawing.h`：`Drawing::EffectRenderer::Initialize/Prepare/DrawBackground/ContentBounds`，生产和原生离屏测试共用；`Initialize` 的 `allowSpriteBatch=false` 仅供测试强制回退路径。

### Contracts

- 有效浅色只用于居中主栏；角落独立主按钮始终深色，但三种效果均可选择。背景默认深色0.8/浅色0.92、角落0.6；显式背景百分比在角落乘0.75，内容独立30~100%。
- 背景组包括底板、描边、掠光、柔光流彩、像素、速度线与落位微光，归一绘制后最后一次应用用户背景透明度；进度在组外绘制并单独乘内容透明度。ULW SourceConstantAlpha 只承担整窗交接，不能将用户两项偏好合并为单一窗透明度。
- 柔光流彩（原“彩虹晕染”，2026-11-04 用户确认改名并批准 `research/soft-glow-review-v6.html`；枚举仍为 `Aurora`、配置值仍为1、i18n key 仍为 `Aurora`，只改显示名，中文“柔光流彩”/英文“Soft glow”）：固定轮廓内的 OKLCH 色场，色相锁定粉紫系——`GlowHueCenter` 在306°~330°缓慢漂移，色块偏移为±22°、摆动±5°，整体约280°~357°（薰衣草→兰花粉→樱花粉→玫瑰），不得再出现蓝/青/红橙。`GlowField` 在1/4分辨率CPU生成（7个移动色块按权重线性混合色相，色块中心更亮），上传后经一次 HARD 边界轻模糊、线性放大，以预乘 source-over 裁入实际轮廓。浅色保持用户喜欢的淡粉质感：L.77~.84/C.16、横向低权重底色铺满、强度0.46、镜面光0.35。深色与浅色同一连续铺色方式（2026-11-04 用户比较 `research/soft-glow-review-v7.html` 后选定，取代 v6 的透明间隙方案）：色块间为深梅紫/暗玫瑰 L.33/C.11，色块中心升到 L.58/C.18，强度0.8、镜面光0.05；全程不使用高明度浅色，避免浅色半透明叠在暗底上整体发灰，也避免透明间隙形成竖向明暗柱。不得回到彩虹全色相、深色高明度淡彩、深色透明间隙、互补色相邻或四周羽化。
- 像素源取自实际底板RGBA；全透明块不参与，半透明边缘保留。背景形状不能被替换成稀疏点阵。像素块约5.5DIP、上限2048，只在资源/几何失效时构造，不能随DPI平方增长。
- 汇聚落位（取代先外散再回收）：来源区域居中约1.5W×2.4H且底边中心对齐目标，角落外扩半圈并裁入有效工作区。主栏的视觉汇聚点位于目标下方1.2H，像素从上方与两侧沿轻微弧线（两端偏移为0）以三次缓出飞入；角落按中心辐射。落位时刻由中心向两端分波（约0.62~2.18s，同波随机错开），飞行约0.78~1.14s；像素随进度在前35%渐显、尺寸由0.55倍放大到1。落位后约0.22s点状微光（深色浅青0.5、浅色深青0.28）。2.18s最晚落位保证2.4s时微光结束且全部像素与底板一致，随后直接进入掠光等待。
- 速度线取代模糊青尾：每颗像素只绘制最近45ms位移的主题青色渐变短线（深0.34/浅0.30），随速度伸缩，停下即消失；不得恢复历史缓冲、模糊累积光晕或外散爆开。全部内容是 elapsed 的纯函数，30/60/120Hz 在同一时刻逐字节一致，设备重建不重播。
- 像素、速度线与微光用 `ID2D1DeviceContext3` SpriteBatch 每帧三次批绘制（速度线→像素→微光），精灵颜色为 `(1,1,1,alpha)` 只调制不透明度；不支持时回退逐个 `DrawBitmap(opacity)`，两条路径的离屏像素必须一致。
- 呈现只提交脏区：`ContentBounds` 给出本帧内容（目标底板与在飞像素/速度线外扩2px），与上一成功提交帧求并集作为 `UPDATELAYEREDWINDOWINFO::prcDirty`；资源重建或首帧整窗提交。拼好后只回读并提交主栏区域，扩大的透明窗口不再逐帧整窗 ULW。
- 以下为已废弃第四版拖尾的历史约束，仅在重新引入历史缓冲时适用：历史扩散/点迹注入只按固定30Hz整数格推进，不足1/30s余量保留；跳帧/资源重建的有界回放起点对齐同一格。最终余光淡出消费真实elapsed，30/60/120Hz调用在同一时刻应得到相同像素；不得仅按dt缩放衰减而每次回调都做一次固定sigma模糊/注入。Effect输入不得同时作为当前target；RGBA保持预乘，透明处不得被ColorMatrix填成不透明色块。GaussianBlur SOFT会产生负输出边界，历史绘制必须显式使用零目标偏移与`(0,0,historyWidth,historyHeight)`源矩形，避免逐轮偏移。历史尺寸向上取整后，合成目标应为`historyWidth/historyScale × historyHeight/historyScale`并由目标裁剪，或使用等价精确源裁剪；不能把完整取整纹理拉回原width/height而改变注入坐标的逆比例。缓存随尺寸/epoch重建并保留本次seed/时间，不重新播放入场。
- 正式目标bounds和扩展presentation bounds分离；target相对窗口offset用于底板/进度/掠光，不能以扩展尺寸当作主栏尺寸，不能写回扩大后的缓存宽度。像素大小、位移、模糊与进度共用UIBase×Zoom/DPI单次换算。
- PixelAssembly成功交接必须额外满足 elapsed>=2.4s；此门只约束视觉交接，不阻塞真实初始化或伪造进度。其他效果保持快速交接；failure/exit不等待拼装门。成功仍依照 committed Preview0 -> Bar255 顺序，保留有无可见进度的300/1000ms档位。

### Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 旧/非法Effect | 默认掠光；其他配置不变 |
| 提前加载完成且选择PixelAssembly | 播完2.4s汇聚落位，再进入顺序渐隐/渐显 |
| 最终失败/退出 | 不被2.4s门阻塞；失败首帧主题红，0%不伪造完成 |
| 背景低透明度 + 柔光流彩/反光/速度线 | 背景组按一次opacity合成，不叠成更实的底板；内容独立 |
| 像素飞行 | 每颗在飞行中段与目标有可见距离，中心先于两端落位 |
| 归位完成 | 与同一底板RGBA一致，无残留拖尾，脏区收回主栏，掠光等待 |
| 资源尺寸/epoch改变 | 清理旧图与effects输入，重建同一快照；时间/seed不重置 |
| Effect初始化/提交失败 | 走既有Retry/device-loss/bypass，不遗留不可见正式Bar |

### Good / Base / Bad Cases

Good：浅色底栏使用淡彩色场，背景透明度变化不带动进度，粒子模式精准归位后只剩掠光。Base：旧配置保持原掠光。Bad：角落跟随全局变白、用整窗alpha控制两种透明度、柔光流彩漂出粉紫系、深色叠高明度淡彩发灰、互补色混成灰褐、恢复模糊累积青色光晕、拼好后仍整窗ULW、将扩展窗口宽度缓存为正式栏宽。

### Tests Required

真实Config Mini/All/写回、设置选择与输入退出；三位置、深浅、DPI/Zoom、负坐标、bounds裁剪、粒子上限/无效输入；RGBA非透明取样与半透明边缘、飞行可见位移/中心先落位/精确终点/微光在2.4s前结束、柔光流彩色场最小彩度≥45且绿色通道始终最低（粉紫锁定）、深浅都连续铺满、深色最亮≤230/最暗≤120/明暗差≥60、脏区覆盖飞行像素并在拼好后收回主栏、SpriteBatch与回退路径像素一致；生产WARP/D2D离屏图验证高彩度色场、无羽化、预乘alpha/分组透明度/进度置顶、循环首尾与设备重建。底板比较在不同呈现尺寸/平移下要求尺寸与alpha严格相同，RGB仅允许已实测的1个8位量化单位，不随DPI放宽；相同呈现条件下的30/60/120Hz比较仍为逐字节一致。最终记录完整Debug|ARM64 Solution与`--no-window`结果；不以无窗口证据宣称真实HWND/GUI或Win7运行验证。

### Wrong vs Correct

```cpp
// Wrong：为提高透明度反复降低每个背景图元，重叠处仍叠实。
DrawBase(userOpacity);
DrawGlow(userOpacity);
// Correct：内部材料先合成，统一一次背景透明度；进度独立。
effects.DrawBackground(context); // 内部消费已冻结的backgroundOpacity
DrawProgress(contentOpacity * progressReveal);
```

## 调试等待观察开关（2026-11-05）

### Scope / Trigger
调试页“为等待操作延迟”仅提供启动与主动点击分页的动画观察入口；不扩展 SuperTop/提权交接覆盖。

### Signatures
`Experimental.Inkeys3.UI3.Debug.DelayWaitingOperations`：`IdtAtomic<bool>`、默认 `false`、NoUpload，经现有 main.json 保存；说明为“延长加载等待，便于观察动画。”

### Contracts
启动 Mini 读取后只启用 `startupPreviewManualDelayRequested`；继续使用 `ReportStartupMilestoneForManualTest` 的 3200ms/600ms 分阶段慢速成功模式。Preview 未活动时不延迟，不授权重试/失败注入。
主动点击 `PageControl::InvokeDirection` 在控件层暂存单个 10s 意图，通过现有输入 worker tick 到期后派发真实业务请求；只投影现有 loading 视觉，不改真实状态、ready/ACK，不消耗 COM/存储超时。快捷键、滚轮、外部翻页及自动恢复不注入。

### Validation / Error Matrix
关闭→立即走原路径；重复点击→不叠加或续期；页/会话/白板 generation 改变、隐藏、销毁或停机→退休；到期→重新准入且最多派发一次；真实失败→原有处理。结束放映不延迟。

### Good / Base / Bad
Good：点击后观察转圈，10s 后真实翻页；Base：默认关闭；Bad：全局 Sleep 或在公共 loading 状态上无差别放慢。

### Tests Required
PageControl 离屏生产路径覆盖开关关闭、10s 截止、重复点击、视觉/业务隔离、页/会话退休、隐藏重现及停机；运行入口 `Inkeys.exe --bar-eraser-offscreen-test`。配合国际化 check、Debug|ARM64 Solution 构建，不以 headless 结果声称 GUI 观感已验证。

### Wrong vs Correct
Wrong：持有真实翻页请求 10s 使原超时到期。Correct：在点击入口等待 10s 后才提交真实请求，保持原协议和超时预算。