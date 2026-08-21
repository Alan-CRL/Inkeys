# Draw3 集成规范

## 来源与文件边界

- 来源快照：`D:\Project\Inkeys\inkStrokeModelerTest\Inkeys3-Draw3@8d045298eaaac76f752b4f8b5f3303b3520e50b7`；目标仓库不嵌套源 `.git`。
- `draw3/*.cpp|*.cppm` 重命名到 `Inkeys/Inkeys/Drawing/Draw3/Draw3.*`；HLSL/HLSLI/CSO 到 `Inkeys/Inkeys/Drawing/Draw3/Assets/`；Ink Stroke Modeler 和 Abseil 仅复制头文件到 `Inkeys/additional/`。
- `Inkeys/resource.h`/`Inkeys/Inkeys.rc` 保留 Draw3 资源 ID：`301=IDR_DRAW3_INK_PIXEL_SHADER`、`302=IDR_DRAW3_INK_VERTEX_SHADER`、`303=IDR_DRAW3_LASER_UPDATE_CS`、`304=IDR_DRAW3_LASER_EMIT_CS`。
- 产品编译不得登记源 `main.cpp`、demo/独立测试窗口、`window_performance_hud`、`.git/.vs`、构建输出或第三方 `.cc`；只允许链接源快照自带的三架构 `ink_stroke_modeler_merge.lib`。

## Scenario: 固定版本 Ink Stroke Modeler ABI

### 1. Scope / Trigger

修改 Ink prediction、第三方头文件、平台/CRT/Vcpkg 配置或固定模型库时必须应用本合同。

### 2. Signatures

- `#pragma comment(lib, "ink_stroke_modeler_merge.lib")`
- `InkStrokeModelerLibraryDirectory`: Win32=`..\inkStrokeModelerTest\lib\lib32`，x64=`..\inkStrokeModelerTest\lib\lib64`，ARM64=`..\inkStrokeModelerTest\lib\libArm64`

### 3. Contracts

- `Inkeys/additional/{ink_stroke_modeler,absl}` 只保留公开/传递头文件，不登记或保留 `.cc`。
- 三架构固定库必须来自已合并的 Draw3 commit `8d045298`；不得用本机构建输出静默替换。
- 固定库使用 `/MT`、`NDEBUG` 和 `_ITERATOR_DEBUG_LEVEL=0`。Inkeys Debug 保留禁用优化/PDB，但必须使用 `MultiThreaded`、取消 `_DEBUG`，并在 `Microsoft.Cpp.targets` 导入前设置 `VcpkgConfiguration=Release`。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 任一架构库缺失 | 链接失败，不回退到源码直编 |
| Debug 使用 `/MTd` 或 `_DEBUG` | 视为 ABI 配置错误，禁止用 `/NODEFAULTLIB` 或忽略 LNK2038 |
| Vcpkg Debug 静态库进入 Inkeys Debug | 改为 Release 子目录，使完整本机依赖链保持 `/MT` |
| 头文件与固定库版本不一致 | 更新必须成套进行并重跑全量 Rebuild |

### 5. Good / Base / Bad Cases

- Good：Debug 自有代码可调试，但所有静态依赖 ABI 为 `/MT`，清空中间目录后成功链接。
- Base：Release 直接使用对应架构固定库。
- Bad：保留 Abseil/Modeler `.cc` 与固定库同时链接，或靠旧 `.obj` 让增量 Build 假通过。

### 6. Tests Required

- ARM64 Debug/Release 对完整 `InkeysRepo.sln` 执行 `/m:1 /t:Rebuild`。
- 静态断言四个第三方目录无 `.cc`、项目无第三方 `.cc` 编译项、三架构库与 pragma/目录映射齐全。
- 运行 `InkeysHeadlessTests.exe --no-window` 和 `Inkeys.exe --draw3-hidden-test`。

### 7. Wrong vs Correct

Wrong：`Debug /MTd + release merge.lib -> 忽略 LNK2038`。

Correct：`Debug 保留调试信息 + /MT + undef _DEBUG + Release Vcpkg libs -> 固定库 ABI 一致`。

## 窗口与所有权

- `WindowService` 是 `WindowRole::Drawpad` 与 `WindowRole::DrawpadPresentation` HWND 的唯一创建、显示、隐藏和销毁者。Draw3 只把主 Drawpad 交给 `AttachExternal(HWND, callbacks)`；辅助窗不得绑定 RTS、WndProc mailbox、document 或第二套 Host。
- Draw3 不创建/销毁顶层窗口，不修改标题、owner 或 Z 序；样式和 `Primary/Presentation/Hidden` 可见性变化必须通过 Window Service 的 owner-thread 命令完成。
- owner 层级保持 `MagnifierHost -> Freeze -> {DrawpadPresentation, Drawpad}`，辅助窗低于主 Drawpad/PPT/Bar 且高于 Freeze；PPT/Bar 继续 owned 到主 Drawpad。Draw3 presenter 不得调用 `SetWindowPos`、`SetParent` 或直接改 owner；`SetBounds(Drawpad)` 必须同步两窗。

## 设备与线程

- Draw3 独立创建 D3D11.1 hardware-first/WARP-fallback 设备、context、DXGI factory、交换链和呈现资源，禁止引用 `Inkeys.UI.RenderPipeline` 的设备。
- UI 线程只向 bridge 发布不可变快照和命令；renderer、document、history、RTS 消费只在 Draw3 绘制线程进行。
- 进程只允许一套 Draw3 Host、绘制线程、document/history 和 `RealTimeStylusInput` producer。辅助 HWND 仅是同一最终 backbuffer 的 presentation target。退出顺序固定为停止命令生产、停止 RTS、唤醒绘制线程、释放双 presenter/设备，最后由 Window Service 销毁两窗。

## 样式与透明度

- 主 Drawpad 固定不带选择语义的 `WS_EX_TRANSPARENT`；只有 `ShouldPreconfigureNoRedirectionBitmap()` 能力探测通过时才在创建前预置 `WS_EX_NOREDIRECTIONBITMAP`，随后按主 presenter 模式切换 `WS_EX_NOREDIRECTIONBITMAP`/`WS_EX_LAYERED`。
- `DrawpadPresentation` 出生即固定包含 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，运行期不得清除或借其承载输入。
- DComp 清除 `WS_EX_LAYERED`；DWM 清除 `WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP`；ULW 设置 `WS_EX_LAYERED` 并清除 `WS_EX_NOREDIRECTIONBITMAP`。
- ULW 必须提交 premultiplied-alpha、top-down 32-bit DIB 和 dirty rect；未绘制像素的 alpha 为零，禁止整窗不透明更新遮挡下层窗口。

Windows 对创建时带 `WS_EX_NOREDIRECTIONBITMAP` 且已经绑定过 DComp target 的主 HWND 可能拒绝后续清除该位（`ERROR_INVALID_PARAMETER`）。Window Service 必须写后读取并核对真实样式。若产品 DComp 启动失败，必须在首帧显示和 Setting 初始化前停止 Draw3 与整条隐藏窗口链，再顺序重建 legacy-compatible 主 Drawpad 及其辅助 sibling；新 Host 禁用 DComp 并从 DWM2 -> DWM -> ULW 继续。两个主 Drawpad generation 不得同时存在，但同一 generation 必须包含长期待命的 presentation-only sibling。

## 功能边界

桥接工具固定为 Pen、Highlighter、FixedEraser、SpeedEraser、Laser、SolidLine、DashedLine、OutlineRectangle、FilledRectangle。清屏、撤销/重做和页面切换接入已验证实现；保存、超级恢复、自动直线拉直和输入测试保留 `Unsupported/NotReady` 空接口并隐藏产品入口。保留 Draw3 速度橡皮、固定橡皮及 `SpeedEraserOcController`；仅删除旧 Draw2 压感橡皮实现和设置入口。

## Scenario: 图形识别与条件渲染修正

### 1. Scope / Trigger

修改 Draw3 图形识别、自动修正、`.uink` 条件渲染映射、runtime history 可见性或修正结果的 Undo/Redo/GPU 恢复时必须应用本合同。

### 2. Signatures

- `Inkeys.CV.ShapeRecognition::RecognizeInkShape(std::span<const InkStrokeView>, float dpiScale) noexcept -> ShapeResult`；导出接口不得包含 `cv::` 类型，OpenCV 头文件只存在于实现单元。
- `InkStroke::{RenderOnlyWhenLatest(), SetRenderOnlyWhenLatest(bool)}` 保存独立于 Undo 的内容元数据。
- `RenderItemState::{active, renderOnlyWhenLatest, visible}` 中 `active` 表示未撤回，`visible` 表示实际参与合成。
- `CanvasRuntimeHistory::{SetRenderOnlyWhenLatest(...), ClearLatestConditionalGroup()}` 负责条件尾组重算和新分支固化。

### 3. Contracts

- 当前 MVP 只识别水平/竖直矩形。候选必须以最新 Stroke 结尾，由最近最多 6 条连续、同样式、active 且 effectively visible 的 Pen 构成；遇到其他工具、条件/隐藏项、Redo 间隙或样式变化立即截断，并从最大后缀向小后缀尝试。
- 只有绘制 contact、手势手指、重连候选、导航和待处理输入全部为空，且最终 Pen 已提交后才消费一次触发。识别失败不反复尝试同一 history revision。
- 识别器最多处理每笔 1024 个采样点、总计 4096 点；非法坐标/DPI、超限、OpenCV 异常或任一硬门槛失败都返回 `Unknown`。矩形硬门槛包括最小尺寸/长宽比、矩形度、四角简化、局部与整边偏轴、单边/总覆盖、离边比例、逐笔边带比例和周长比；阈值变化必须同步正反样本。
- 条件可见性按 active 序列从后向前计算：`visible = active && (!renderOnlyWhenLatest || 后方不存在 active 非条件项)`。修正时原稿设为条件项，追加的 `OutlineRectangle` 保持非条件；因此矩形存在时隐藏原稿，撤回矩形后末尾条件原稿整体恢复可见。
- 撤回矩形仍只消耗一个历史项；之后原稿按原 Stroke 顺序逐笔撤回。Redo 先隐藏条件原稿再恢复矩形。撤回矩形后追加新内容时，必须先把当前末尾条件组固化为普通内容并同步 document sidecar，再丢弃 Redo 分支。
- 修正、Undo 和 Redo 的 dirty rect、热前像与 composition restore 必须覆盖原稿和矩形 footprint 的 Tile 并集。先用预演 history 生成目标画面，GPU 成功后才提交 CPU history、document 元数据、raster token 和 redo 游标。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 输入非法、OpenCV 异常、低置信度或负类冲突 | 返回 `Unknown`，原稿与 history 不变 |
| history/document 预演或修正 Stroke 追加失败 | 回滚暂存尾项，不设置条件标记 |
| GPU restore 在提交前失败或可能部分写入 | 用旧 history 重放受影响 Tile；仍失败则请求权威刷新，CPU 状态保持旧值 |
| Undo/Redo 可见性重算失败 | 不推进 active 游标或 redo 栈，并恢复旧条件可见性 |
| 撤回修正后追加普通 Stroke | 条件标记先固化，原稿保持可见，旧矩形不可再 Redo |

### 5. Good / Base / Bad Cases

- Good：四笔矩形被替换为同色、同透明度、同中位笔宽的 `OutlineRectangle`；Undo 一次恢复全部原稿，Redo 再次隐藏原稿并显示矩形。
- Base：候选不是矩形或仍有活动输入时完全保持普通 Pen 流程；单笔闭合、乱序反向、允许的小断角和重复边仍按同一合同判断。
- Bad：把 `renderOnlyWhenLatest` 当作 `active=false`、只更新 document 或 history 一侧、跨过隐藏/异样式项拼候选，或在 GPU restore 前提交 CPU 游标。

### 6. Tests Required

- 识别正例覆盖标准四笔、单笔闭合、乱序/反向、轻微抖动、小断角和重复边；负例覆盖小图形、U/C、圆/椭圆、三角形、平行四边形、倾斜矩形、X、缺边、局部锯齿/过大转角、内部乱画和最新无关笔画。
- History 覆盖修正、Undo、原稿逐笔 Undo、Redo、Redo 分支丢弃、页面隔离、条件尾组 Tile 可见性及可见性操作失败回滚。
- 触发门分别断言绘制 contact、手势、重连、导航和 pending input 阻止识别，全抬起后同一 revision 只执行一次。
- 构建完整 `InkeysRepo.sln Debug|ARM64`，运行 `InkeysHeadlessTests.exe --no-window` 与 `Inkeys.exe --draw3-hidden-test`；linker map 只允许 OpenCV core/imgproc 对象进入 EXE，PE 导入和输出目录不得出现 OpenCV/FFmpeg DLL。

### 7. Wrong vs Correct

Wrong：`标记原稿 inactive -> 追加 Shape -> GPU 失败后再猜测恢复`。

Correct：`复制 history 预演条件可见性 -> 恢复 footprint union -> 成功后原子提交 metadata/history/raster state`。

## Scenario: 工具光标样式与有效透明度

### 1. Scope / Trigger

修改 Pen、Highlighter、Eraser 或 Laser 的颜色、粗细、光标可见性、透明度或 Bar 显示值时必须应用本合同。

### 2. Signatures

- `Bridge::ProductState { tool, colorRgba, widthDip, revision }`
- `GetEffectivePenOpacity() -> float`
- `WindowController::SetProductVisualStyle(colorRgba, widthDip)` / `ProductVisualStyleSnapshot()`
- `ConfigureDrawingCursor(tool, appearance)` / `ResolvePrimaryDrawingCursorVisual(...)`

### 3. Contracts

- `stateMode` 只保存产品工具意图；`laserActive` 记录最后选择的 Pen 子类型是否为 Laser，不污染已记忆的 `Pen.ModeSelect`。它仅在顶层模式为 Pen 时映射为 Laser；选择、橡皮和图形必须覆盖当前工具但保留该记忆，返回 Pen 时恢复 Laser。
- bridge 只跨线程传递稳定工具、RGB 和粗细快照；活动笔画在 Down 时锁存 `ProductVisualStyle`，Hover 光标在帧边界跟随最新快照。
- 普通笔光标直径是 `max(widthDip, 5 DIP * dpiScale)`；只有最小光标值按 DPI 缩放，实际笔画粗细不重复缩放。
- 荧光笔当前绘制几何固定为 `6.25 × 50 px`，光标必须复用该尺寸；最终 alpha 为 `opacity * fillAlpha = 0.35`，Bar 显示同一有效透明度。
- Eraser Hover 整体 alpha 为 `0.5`，Contact 为 `1.0`；这一规则同时适用固定/速度橡皮、鼠标和倒转笔橡皮。
- Laser 当前没有产品宽度 state；光标与笔迹必须共用 `kLaserSolidDiameterAt96Dpi * dpiScale`。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 颜色/粗细在 Hover 期间改变 | 下一光标帧使用新样式；已 Down 笔画不重染 |
| 荧光笔经过 Pen 默认 alpha 归一化 | 仍保持 `0.35`，不提升为 `1.0` |
| 橡皮 Hover / Contact | 分别为 `0.5` / `1.0` |
| Laser 尺寸调整 | 同时修改共享直径来源，不单改光标或笔迹 |
| 记忆 Laser 后切换到选择/橡皮/图形 | 当前工具按顶层模式发布，Laser 记忆保留；返回 Pen 后重新发布 Laser |

### 5. Good / Base / Bad Cases

- Good：颜色/粗细快照由绘制线程在帧边界消费，光标立即更新，活动笔画保持 Down 样式。
- Base：没有样式变化时不重复配置 appearance。
- Bad：让 resolver 无条件覆盖工具 alpha，或用普通笔宽度暗中驱动 Laser。

### 6. Tests Required

- 纯逻辑测试覆盖荧光笔 `0.35`、橡皮 Hover/Contact、鼠标、倒转笔和 Touch Contact。
- 静态检查普通笔最小 DIP 直径、荧光笔实际 `6.25 × 50 px` 尺寸以及 Laser 光标/笔迹共享 helper。
- 完整 ARM64 `Debug|ARM64` Solution 构建，再运行 `InkeysHeadlessTests.exe --no-window` 和 `Inkeys.exe --draw3-hidden-test`。

### 7. Wrong vs Correct

- Wrong：`Pen sample -> opacity = 1.0`，把 Highlighter 或 Eraser Hover 当普通笔处理。
- Correct：只对原本不透明的普通 Pen/Mouse appearance 做默认归一化，部分透明工具保留自身 alpha。

## Scenario: 选择模式、当前页内容与 Clear 截断

### 1. Scope / Trigger

修改产品模式桥接、Drawpad 显隐/消息穿透、Bar 选择布局、runtime history、Clear/Undo/Redo、页面切换或高精度计时器时必须应用本合同。

### 2. Signatures

- `Bridge::ProductState::selectionMode : bool`，默认 `true`；不得从 `WS_EX_TRANSPARENT` 反推模式。
- `DrawingControllerRuntimeObserver::currentPageContentChanged(void*, bool, uint64_t)`。
- `HostRuntimeSnapshot` 包含内容、已应用选择模式、请求/就绪输出目标及 revision、`presentedContentRevision`、`auxiliaryFullFrameClean`、`runtimeRevision`；状态线程使用 `WaitForRuntimeRevision()`。
- `InkCanvas::ClearStrokes()`。
- `ResolveDrawpadPresentationSurface(selectionMode, currentPageHasContent, auxiliaryFullFrameClean)`。
- `TimerPeriodController::SetSelectionMode(bool)`。

### 3. Contracts

- 当前页内容真值唯一来自 `CanvasRuntimeHistory::LastVisibleItem().has_value()`。Pen、Highlighter、Shape 和 Eraser history 都算内容；Laser 不算；Eraser 即使把视觉画面擦空仍算；Undo 到无可见项、Clear 或切到空页才为无内容。
- DrawingController 在文档初始化、Stored Stroke 成功进入 runtime history、Undo/Redo 成功、Clear 和页面切换后检查内容布尔值；Host 只在布尔值变化时递增单调内容 revision。每次成功 Present 记录实际目标、输出 revision 和对应内容 revision。
- presentation 状态固定为：非选择只显示主 Drawpad；选择先把最终 backbuffer 全量提交到辅助 ULW，再隐藏主窗并显示辅助窗；选择无内容时只有辅助完整帧 alpha 全零才隐藏两窗。换窗前必须满足请求/就绪 target 与 revision 一致且 `presentedContentRevision == contentRevision`。
- Window Service 用批量窗口位置命令确保两窗互斥可见；失败时先隐藏两窗再收敛到唯一目标。主 Drawpad 不得动态切换 `WS_EX_TRANSPARENT`。
- Bar 仅在“选择+无内容”隐藏 Eraser/Geometry/Recall 等绘制按钮；选择+有内容与非选择均保持完整布局，选择按钮文字恒为“选择”。产品路径不再注册或读取 Pierce/`penetrate.select`。
- Clear 只截断当前页：清 Stroke，并以全新 runtime 替换 history、undo/redo、before/after raster state，再分配新 raster token；丢弃热前像、composition cache/维护、恢复计划和 trusted L2，重置 Laser/粒子/瞬态层并全量透明呈现。保留当前页 viewport 和其他页面；之后 Undo/Redo 必须为空操作。
- `timeBeginPeriod(1)` 只在进入非选择模式时幂等尝试；回到选择或绘制线程退出时，仅对成功 begin 配对 `timeEndPeriod(1)`。begin 失败后同一次绘制停留不重试，必须离开并重新进入绘制模式。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 初始选择空页 / 清空后选择 | 辅助 ULW 完整帧 clean 后两窗隐藏；Bar 精简 |
| 空选择进入绘制但未落笔，再选择 | 重新进入隐藏空页状态 |
| 选择且有 history | 主 Drawpad 隐藏，辅助穿透窗显示；Bar 保持完整 |
| 选择可见态 Clear 或 Undo 到无内容 | 当前内容帧与 full-frame clean revision 就绪后隐藏辅助窗 |
| 选择空页仍有 Laser/粒子 | 辅助窗持续呈现淡出；最后全帧 alpha 为零后隐藏 |
| Eraser 在空页形成 history 或擦到视觉空白 | `currentPageHasContent=true` |
| Clear 后 Undo/Redo | 命令可消费但内容、revision 和画面保持空；旧 Stroke 不恢复 |
| 切换空页/有内容页 | 发布对应布尔值；其他页 history 与当前页 viewport 不受 Clear 影响 |
| `timeBeginPeriod(1)` 失败 | 不调用 end；同次非选择停留不重试 |

### 5. Good / Base / Bad Cases

- Good：有一笔的选择态由辅助 ULW 穿透显示，Clear 后等当前 revision 的完整 clean 帧再隐藏；另一页的 Eraser history 仍存在。
- Base：初始选择空页两窗隐藏；进入 Pen 先预热主 presenter 再显示，未落笔返回选择再次经辅助 clean 后隐藏。
- Bad：用窗口穿透样式推断选择模式、让主 Drawpad 承担穿透、复制固定 L2、视觉像素是否为空推断内容，或把 Clear 实现成可撤回 history 项。

### 6. Tests Required

- Headless 覆盖 `Primary/Presentation/Hidden` 解析，以及 timer begin/end 幂等、失败、模式往返和析构清理。
- 隐藏 HWND 集成覆盖双窗固定样式/owner/bounds、互斥可见、输出 generation 往返、clean 握手、Stored Stroke 内容发布、页面切换、Clear 截断和 presenter recovery。
- 完整 `InkeysRepo.sln Debug|ARM64` 构建，运行 `InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-hidden-test` 与 `git diff --check`；不得启动可见窗口。

### 7. Wrong vs Correct

Wrong：`selection = current WS_EX_TRANSPARENT`、`主 Drawpad + WS_EX_TRANSPARENT`，或 `Clear -> append erase/blank history -> Undo 可恢复旧画面`。

Correct：`显式 selectionMode + LastVisibleItem 内容真值 + generation/content/clean 握手 -> 双 surface 互斥切换`；`Clear -> 新 runtime/raster token + GPU/瞬态全清 -> Undo/Redo 空操作`。

## Presenter 合同

- Draw3 自己创建 D3D11.1 hardware-first/WARP-fallback device/context、DXGI factory、swap chain 和 presenter，不注册 `Inkeys.UI.RenderPipeline` client，也不共享其 device epoch。
- presenter 模式顺序为 DComp -> DWM2 -> DWM/Win7 -> ULW；每次失败都销毁该模式的 swap chain/presenter 状态后再降级。DComp 在创建前通过能力探测决定是否预置 `WS_EX_NOREDIRECTIONBITMAP`；legacy 重建 Host 明确跳过 DComp。ULW 使用 premultiplied top-down 32-bit DIB、`1/255` CPU alpha 边界和 dirty rect，透明像素 alpha 必须保持零。
- 主 presenter 与辅助 ULW target 共用 renderer/final backbuffer，每帧只向请求目标提交；辅助初始化失败即 Host 启动失败，运行期失败必须重建并全量重试，不回退到主窗穿透。
- 只允许一个绑定主 Drawpad 的 `RealTimeStylusInput` producer；Draw2 RTS 不得初始化或重复发布。退出顺序为停止命令生产 -> 停止 RTS -> 唤醒绘制线程 -> 释放双 presenter/device -> Window Service 销毁两窗。

## 来源任务与验证状态

- 源任务历史统一保存到 `.trellis/tasks/archive/2026-08/draw3-source/`；其中 `active/` 保留源快照的 `in_progress` 状态但不进入目标 active task 列表，原 2026-07/08 归档按月份保留。
- 截至 2026-08-16，文件/资源映射、固定库 ABI、HWND/owner/Z 序、独立设备和唯一 RTS 约束已完成静态审计；ARM64 Debug/Release Solution 全量 Rebuild、`--no-window` 纯逻辑测试和隐藏 HWND 的真实 DComp/DWM2/DWM/ULW 合成测试均通过。测试按 DComp-compatible 与 legacy 两个顺序生命周期运行，并验证样式回调结果与实际样式一致；产品启动使用同样的显示前顺序重建合同。
