# Startup Preview and Fast Bar Display

## 1. Scope And Triggers

本规范适用于以下改动：

- `wWinMain` 在 SuperTop 后的启动编排、启动进度、启动失败展示；
- `Inkeys.UI.StartupPreview`、默认 embedded BIN、磁盘 cache；
- RenderPipeline 新启动 client、Bar committed-frame bridge、layered-window presentation alpha；
- 为 Window Service、Draw3、Freeze、PPT UI、Setting/config 添加启动 milestone；
- 任何会改变默认 Bar 可见像素、embedded layout epoch 或 cache compatibility 的改动。

不适用于 T0 之前的路径/单实例错误、Draw3 自有绘图 device、Office 后台连接状态，或旧 `IdtFloating/IdtWindow` 路径。

## 2. Signatures And Shared Types

实现可按仓库 module 规则调整文件拆分，但若改变以下语义签名，必须同步本规范和任务设计：

```cpp
namespace Inkeys::Startup {
enum class Milestone : std::uint8_t;

struct Plan final {
    std::uint32_t totalUnits;
    bool Contains(Milestone) const noexcept;
};

struct Snapshot final {
    std::uint32_t completedUnits;
    std::uint32_t totalUnits;
    double actualRatio;
    bool failed;
    std::uint32_t failureCode;
    std::uint64_t revision;
};

class ProgressTracker final {
public:
    bool Complete(Milestone milestone) noexcept;
    bool Fail(std::uint32_t failureCode) noexcept;
    Snapshot GetSnapshot() const noexcept;
};
}

namespace Inkeys::UI::StartupPreview {
enum class CacheState : std::uint8_t {
    Missing, Valid, Incompatible, Corrupt
};

enum class BarStartupState : std::uint8_t {
    NotStarted, Initializing, RenderClientRegistered,
    FirstFrameCommitted, WindowMissing,
    ClientRegistrationFailed, StartupFailed, StoppedBeforeReady
};

struct CommittedBarFrame final {
    // Plain metadata plus a render-thread-owned non-target bitmap.
    std::uint64_t deviceGeneration;
    std::uint64_t visualRevision;
    bool stableForCache;
};
}
```

BIN/cache header 是 wire format，不暴露为可直接写盘的 C++ layout。只允许 `ReadLe16/32/64`、`WriteLe16/32/64` 等逐字段 API。

## 3. Executable Contracts

### 3.1 Startup Boundary

- Preview/Tracker 的 T0 **必须**位于最终进程越过 SuperTop 所有重启/提权/辅助分支之后，当前基准是 `IdtMain.cpp:743` 后。
- SuperTop 父进程和 helper **不得**创建 Tracker、Preview、cache worker 或 early RenderPipeline。
- T0 使用 `steady_clock`；5 秒只从 T0 计算。
- `Experimental.Inkeys3.UI3.StartupPreview.Enable` 缺失时默认 true；Setting 变更只对下次启动生效。
- 开关关闭时不得创建 Preview；RenderPipeline 保留原初始化位置。开启时允许 early initialize，原位置必须接受 already initialized。

### 3.2 Real Progress

- 每次增长必须对应代码中已完成的真实 milestone；禁止按 elapsed time、帧数、shimmer phase 或“预计耗时”增加。
- Plan 在 mini config 后冻结；未执行的 optional branch 从 plan 移除，不得标为完成。
- Milestone 首次完成才增加 work units；重复、乱序、并发上报不得回退或重复计数。
- UI displayed ratio 可 ease，但永远不超过 tracker actual ratio。
- 致命失败冻结首个 failure snapshot；后续 milestone 不改变 actual ratio。
- 只有正式 Bar 首个 `presentCompletion.IsCommitted()` 可产生 100%。调用 ULW、排队 alpha 或注册 client 都不是完成。
- milestone callback 必须 noexcept、低开销；只做原子状态更新/无阻塞通知。

### 3.3 Window And Render Ownership

- Preview HWND 由独立 owner/message thread 创建和销毁；create/position/show/hide/z-order/destroy 都回到该线程。
- 窗口样式固定为 `WS_POPUP`、`WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，不得加入 `WS_EX_TRANSPARENT`。`WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATEANDEAT`。
- RenderPipeline 新 client 名为 `StartupPreview`；dispatch order 中 Bar 必须先于 StartupPreview。
- Preview 的 target/bitmap/effect/brush/proxy 只在唯一 RenderPipeline thread 创建、复制和释放。不得暴露 mutable Bar target，也不得创建第二套 D2D/D3D device/render thread。
- Window topmost observer 在成功 refresh 后只能异步 post Preview owner；双方不得形成同步反向等待。Preview 停止前先注销 observer。
- device generation 改变后不得使用任何旧 generation 资源。

### 3.4 Bar Commit And Presentation Alpha

- Bar commit 必须代表 GetDC、UpdateLayeredWindowIndirect、ReleaseDC、EndDraw 全部成功；失败 attempt 不得发布 frame、报 100%、更新 cache 或开始 handoff。
- Preview 存在时 Bar 初始 presentation alpha 为 0；无 Preview 时为 255。
- Alpha 必须有 requested、attempted、committed 三态；只有完整 present commit 才更新 committed。
- Alpha 变化强制 full-window ULW，不得使用局部 dirty rect；失败保留旧 committed alpha，并请求 full-dirty retry。
- Presentation alpha demand 与业务 scene dirty 分离；alpha 成功/失败不得错误推进业务 dirty transaction。
- 成功帧发布 exact source crop、viewport、screen destination、monitor geometry、visual signature、device generation 和 render-thread copy 得到的非 target bitmap。

### 3.5 BIN And Cache Wire Format

- v1 是 160-byte little-endian header + exact premultiplied BGRA payload；magic 为 `IKSPRVW\0`，version=1，headerSize=160，pixelFormat=1。
- 固定 offsets：epoch 12、pixel format 16、flags 20、revision 24、SHA-256 signature 32、width 64、height 68、stride 72、payload size 80、DPI X/Y 88/92、monitor/work geometry 96..108、window offset 112/116、anchor 120/124、progress rect 128..140、CRC32 144、reserved 148..159。
- v1 要求 `stride == width*4`、`payloadSize == stride*height`、file size exact、width/height 1..8192、payload <=64MiB、reserved bits 为 0。
- CRC 是 IEEE CRC-32，计算覆盖 header+payload，计算时 crc field 视为 0。
- 分配 payload 前先做 file-size、checked arithmetic、stride/payload、矩形和上限验证；短读、overflow、非法字段、CRC 错误为 Corrupt。
- 完整/可安全识别但 epoch/signature/DPI/geometry 不匹配为 Incompatible；不存在为 Missing；全部匹配为 Valid。不得尝试“修复”Corrupt 文件。
- DPI 保存在 header，因为像素/geometry 兼容真实依赖它；主题/语言进入 signature，不重复设字段。

### 3.6 Embedded Asset Regeneration

默认 BIN 只能来自真实正常 Bar scene 的稳定 committed frame capture，基准为 96 DPI、中文、深色、当前默认按钮/尺寸/显隐/expanded 启动状态。禁止手绘、AI 生成或独立 mock renderer。

出现任一情况，必须递增 embedded layout epoch 并重新执行可复现 capture：

- 增加浅色模式或默认主题变化；
- 正式完成 i18n，默认语言或默认文字变化；
- 默认按钮序列、尺寸、显隐或 extension 改变；
- Bar 默认布局、图标、颜色、圆角、阴影或启动状态发生可见变化；
- BIN pixel format、alpha 或 serialization format 改变。

生成结果必须由生产 parser 反读并核对 CRC、signature、epoch 和尺寸后才能嵌入；当前只允许一份 canonical asset，不建立 DPI/theme/language matrix。

### 3.7 Stable Cache And Atomic Write

- 只从成功 committed、当前 generation、无 hover/press/capture/menu/panel/transient animation/debug overlay 的帧抓取。
- 持久视觉 revision 安静至少 750ms 后才成为候选；防抖不影响 startup progress。
- render thread 做 exact crop -> normal bitmap -> `CPU_READ | CANNOT_DRAW` staging -> Map -> ordinary memory。writer 不得持有 D2D COM object。
- 单后台 writer 只保留最新 revision；同目录 temp 用 CREATE_NEW，完整写入后 FlushFileBuffers、close，再 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`。
- 目录只读、磁盘满、写/flush/replace 失败只限频日志，不阻止主程序，不破坏旧 cache。

### 3.8 Rendering, Handoff And Failure

- Embedded 使用 cubic + D2D1.1 GaussianBlur，约 6 DIP、Balanced、Soft；必须按 effect bounds 扩展，禁止裁 blur。
- Shimmer 用源 alpha + `FillOpacityMask`；调用时切 ALIASED 并恢复原 antialias。不得使用 Win10-only AlphaMask effect。
- 5 秒后进度条展示真实值；成功先隐藏。失败立即红色并保留实际 fill，最多等 350ms 的 Preview ULW commit，再进入现有 popup。
- Valid cache 在单 Preview HWND 内切到 live proxy，再以 committed alpha 边界切到 Bar；不得用两个 layered HWND 做持续中间-alpha cross-fade。
- Missing/Incompatible 用 embedded blur、高 blur 替换 live proxy、再 deblur；Corrupt 必须 Preview 淡到 0、短暂全透明、Bar 再淡入，且不做 proxy deblur。
- Preview/cache/blur/shimmer 失败可 bypass；共享 RenderPipeline 和现有核心启动失败维持致命语义。
- 正常/失败退出都按 observer unregister -> StartupPreview client unregister/drain -> owner destroy/join -> cache writer stop/join -> Bar/Window/RenderPipeline shutdown 排序。所有等待有上限。

### 3.9 DPI And Compatibility

- Process DPI awareness 必须早于任何 Preview/Window Service HWND 和 Display enumeration。
- Win8.1+ Shcore 仅动态解析，并且必须用 `GetSystemDirectoryW` 构造 System32 绝对路径后再 `LoadLibraryW`；不得用裸 `Shcore.dll` 名称搜索应用目录。Win7 缺失时回退 `SetProcessDPIAware`。`E_ACCESSDENIED` 表示 awareness 已设置，不作为错误。
- 保持 Windows 7 SP1 + KB2670838、D2D1.1、WARP FL11.0；不得静态依赖 Win8.1+/Win10 API、GDI+、WinUI Runtime 或 DirectComposition。
- 保持 PptCOM 资源 221；不因本功能自动添加 EXE ID 1 manifest。

## 4. Validation Matrix

| 变更 | 必须验证 |
| --- | --- |
| Tracker/stage | concurrent duplicate/out-of-order、conditional plan、failure freeze、elapsed no-growth、Bar-only 100% |
| Parser/serializer | little-endian roundtrip、CRC vector、逐字节截断、overflow、64MiB、非法 rect、四分类 |
| Preview window | no activate/focus/taskbar、click eat、owner-thread destroy、late post、topmost refresh |
| RenderPipeline | Preview-only registration、Bar-before-Preview、unregister drain、device epoch loss/recovery |
| Bar bridge/alpha | 四步 present 任一失败、full retry、business dirty independence、no-Preview 255 |
| Cache | stable-frame filter、latest revision、read-only/full disk、flush/replace fail、bounded shutdown |
| Animation | 5s from T0、real fill、red frame bounded wait、三分支、无双窗口半-alpha |
| Platform | full Solution Debug/Release Win32/x64/ARM64；Win7 KB2670838 WARP FL11.0；ARM64EC source audit |

## 5. Good / Base / Bad Cases

- **Good**：Valid cache + matching DPI/signature；Preview 很快显示，真实 progress 单调，Bar alpha 0 committed，单窗口 proxy 交接，Bar alpha 255 committed 后 Preview 销毁。
- **Base**：首次启动 Missing；embedded 96 DPI 高质量放大并模糊，Bar proxy 在高 blur 替换并 deblur，随后切 Bar。
- **Recoverable bad**：Corrupt cache、只读目录、blur/shimmer failure、Preview HWND failure；分别走 corrupt 动画或降级/bypass，主程序继续。
- **Fatal bad**：RenderPipeline/Window/Draw3/Setting/Whiteboard/Bar startup failure；真实进度冻结，red frame 最多等 350ms，popup 后按依赖顺序清理。

## 6. Required Tests

- `startup_progress_tests.cpp`：plan、milestone、failure、100% gate。
- `startup_preview_format_tests.cpp`：wire format、CRC、防御解析和 classification。
- `startup_preview_state_tests.cpp`：Preview/cache/handoff reducer 和 bounded stop。
- `bar_presentation_alpha_tests.cpp` + `present_decision_tests.cpp`：alpha transaction 与四步 commit。
- `render_scheduler_tests.cpp`：client order、unregister drain、epoch。
- `setting_session_state_tests.cpp`：默认 true、snapshot/write 和下次启动语义。
- `dumpbin /imports` + 源码审查：不得新增 Shcore/高版本 DPI 静态入口；可选系统 DLL 必须使用 System32 绝对路径，缺失时走既定 fallback。
- 受限环境运行 `InkeysHeadlessTests.exe --no-window`；Win7、HWND、SuperTop、mixed-DPI、ULW/device-loss 使用任务 test matrix 的手工步骤。

## 7. Wrong / Correct

### 进度

```cpp
// Wrong: 时间过去就伪造进度。
actual = std::min(0.95, elapsedSeconds / 10.0);

// Correct: 只有真实 milestone 首次完成才增长。
tracker.Complete(Milestone::WindowOverlayOwnerReady);
```

### Wire Format

```cpp
// Wrong: padding、endianness、ABI 都不稳定。
file.write(reinterpret_cast<const char*>(&header), sizeof(header));

// Correct: 固定 offset、little-endian、checked length。
WriteLe32(bytes, 64, width);
WriteLe64(bytes, 80, payloadSize);
```

### HWND Ownership

```cpp
// Wrong: render callback 跨线程销毁。
DestroyWindow(previewHwnd);

// Correct: 只向 owner queue 投递。
previewOwner.Post(PreviewCommand::Destroy);
```

### Alpha Commit

```cpp
// Wrong: setter 完成就宣称可见。
committedAlpha = requestedAlpha;

// Correct: 仅完整 presentation transaction 成功后提交。
if (presentCompletion.IsCommitted()) {
    committedAlpha = attemptedAlpha;
}
```

### Frame Sharing

```cpp
// Wrong: 把正在绘制的 Bar target 给 Preview/cache。
Publish(barTarget.Get());

// Correct: render thread 复制 exact crop 到非 target/staging。
CopyCommittedCropToProxyAndStaging(frame);
```

### 可选系统 DLL

```cpp
// Wrong: Win7 缺失 Shcore 时会继续搜索应用目录，存在同名 DLL 劫持风险。
LoadLibraryW(L"Shcore.dll");

// Correct: 先用 GetSystemDirectoryW 构造绝对路径，再动态解析可选入口。
HMODULE shcore = LoadSystemLibrary(L"Shcore.dll");
auto setAwareness = reinterpret_cast<SetAwareness>(
    GetProcAddress(shcore, "SetProcessDpiAwareness"));
```
