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

- `WindowService` 是 `WindowRole::Drawpad` HWND 的唯一创建、显示、隐藏和销毁者。Draw3 只能通过 `AttachExternal(HWND, callbacks)` 绑定已经创建的句柄。
- Draw3 不创建/销毁顶层窗口，不修改标题、owner 或 Z 序；样式变化必须通过 Window Service 的 owner-thread 回调完成。
- Drawpad 的 owner 链保持 `MagnifierHost -> Freeze -> Drawpad`，PPT/Bar 作为 Drawpad 上方的 owned popup。Draw3 presenter 不得将 Drawpad 提升为新的 topmost 根，不得调用 `SetWindowPos`、`SetParent` 或直接改 owner；bounds/Z 序更新只能经 Window Service 的 owner thread 完成。

## 设备与线程

- Draw3 独立创建 D3D11.1 hardware-first/WARP-fallback 设备、context、DXGI factory、交换链和呈现资源，禁止引用 `Inkeys.UI.RenderPipeline` 的设备。
- UI 线程只向 bridge 发布不可变快照和命令；renderer、document、history、RTS 消费只在 Draw3 绘制线程进行。
- 一个 Drawpad HWND 只允许一个 `RealTimeStylusInput` producer。退出顺序固定为停止命令生产、停止 RTS、唤醒绘制线程、释放 presenter/设备，最后由 Window Service 销毁 HWND。

## 样式与透明度

- 初始扩展样式保留 `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT`；只有 `ShouldPreconfigureNoRedirectionBitmap()` 能力探测通过时才在创建 HWND 前预置 `WS_EX_NOREDIRECTIONBITMAP`，随后按 presenter 模式切换 `WS_EX_NOREDIRECTIONBITMAP`/`WS_EX_LAYERED`。
- DComp 清除 `WS_EX_LAYERED`；DWM 清除 `WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP`；ULW 设置 `WS_EX_LAYERED` 并清除 `WS_EX_NOREDIRECTIONBITMAP`。
- ULW 必须提交 premultiplied-alpha、top-down 32-bit DIB 和 dirty rect；未绘制像素的 alpha 为零，禁止整窗不透明更新遮挡下层窗口。

Windows 对创建时带 `WS_EX_NOREDIRECTIONBITMAP` 且已经绑定过 DComp target 的 HWND 可能拒绝后续清除该位（`ERROR_INVALID_PARAMETER`）。Window Service 必须写后读取并核对真实样式，禁止报告未实际生效的 DWM/ULW。若产品 DComp 启动失败，必须在首帧显示和 Setting 初始化前停止 Draw3 与整条隐藏窗口链，再由 Window Service 顺序重建唯一的 legacy-compatible Drawpad HWND；新 Host 禁用 DComp 并从 DWM2 -> DWM -> ULW 继续。两个 Drawpad HWND 不得同时存在。

## 功能边界

桥接工具固定为 Pen、Highlighter、FixedEraser、SpeedEraser、Laser、SolidLine、DashedLine、OutlineRectangle、FilledRectangle。清屏、撤销/重做和页面切换接入已验证实现；保存、超级恢复、自动直线拉直和输入测试保留 `Unsupported/NotReady` 空接口并隐藏产品入口。保留 Draw3 速度橡皮、固定橡皮及 `SpeedEraserOcController`；仅删除旧 Draw2 压感橡皮实现和设置入口。

## Scenario: 选择模式、当前页内容与 Clear 截断

### 1. Scope / Trigger

修改产品模式桥接、Drawpad 显隐/消息穿透、Bar 选择布局、runtime history、Clear/Undo/Redo、页面切换或高精度计时器时必须应用本合同。

### 2. Signatures

- `Bridge::ProductState::selectionMode : bool`，默认 `true`；不得从 `WS_EX_TRANSPARENT` 反推模式。
- `DrawingControllerRuntimeObserver::currentPageContentChanged(void*, bool)`。
- `HostRuntimeSnapshot::{currentPageHasContent, contentRevision}` 与 `WaitForContentRevision(revision, timeoutMilliseconds)`。
- `InkCanvas::ClearStrokes()`。
- `ResolveDrawpadPresentationPlan(selectionMode, currentPageHasContent)`，固定返回两步样式/可见性操作。
- `TimerPeriodController::SetSelectionMode(bool)`。

### 3. Contracts

- 当前页内容真值唯一来自 `CanvasRuntimeHistory::LastVisibleItem().has_value()`。Pen、Highlighter、Shape 和 Eraser history 都算内容；Laser 不算；Eraser 即使把视觉画面擦空仍算；Undo 到无可见项、Clear 或切到空页才为无内容。
- DrawingController 在文档初始化、Stored Stroke 成功进入 runtime history、Undo/Redo 成功、Clear 和页面切换后发布内容布尔值。Host 只在布尔值变化时递增单调 `contentRevision`，并通过条件变量唤醒消费者。
- presentation 状态固定为：`选择+无内容 = DisableClickThrough -> Hide`；`选择+有内容 = EnableClickThrough -> Show`；`非选择 = DisableClickThrough -> Show`。模式切换必须发布状态后立即 reconcile，不能只等待内容 revision。
- Bar 仅在“选择+无内容”隐藏 Eraser/Geometry/Recall 等绘制按钮；选择+有内容与非选择均保持完整布局，选择按钮文字恒为“选择”。产品路径不再注册或读取 Pierce/`penetrate.select`。
- Clear 只截断当前页：清 Stroke，并以全新 runtime 替换 history、undo/redo、before/after raster state，再分配新 raster token；丢弃热前像、composition cache/维护、恢复计划和 trusted L2，重置 Laser/粒子/瞬态层并全量透明呈现。保留当前页 viewport 和其他页面；之后 Undo/Redo 必须为空操作。
- `timeBeginPeriod(1)` 只在进入非选择模式时幂等尝试；回到选择或绘制线程退出时，仅对成功 begin 配对 `timeEndPeriod(1)`。begin 失败后同一次绘制停留不重试，必须离开并重新进入绘制模式。

### 4. Validation & Error Matrix

| 条件 | 必需结果 |
|---|---|
| 初始选择空页 / 清空后选择 | 先清穿透，再隐藏 Drawpad；Bar 精简 |
| 空选择进入绘制但未落笔，再选择 | 重新进入隐藏空页状态 |
| 选择且有 history | Drawpad 可见且穿透；Bar 保持完整 |
| 选择可见态 Clear 或 Undo 到无内容 | revision 变化后立即清样式并隐藏 |
| Eraser 在空页形成 history 或擦到视觉空白 | `currentPageHasContent=true` |
| Clear 后 Undo/Redo | 命令可消费但内容、revision 和画面保持空；旧 Stroke 不恢复 |
| 切换空页/有内容页 | 发布对应布尔值；其他页 history 与当前页 viewport 不受 Clear 影响 |
| `timeBeginPeriod(1)` 失败 | 不调用 end；同次非选择停留不重试 |

### 5. Good / Base / Bad Cases

- Good：有一笔的选择态保持穿透可见，Clear 后同一 content revision 通知使样式先清除、窗口再隐藏；另一页的 Eraser history 仍存在。
- Base：初始选择空页不显示 Drawpad；进入 Pen 后立即清穿透并显示，未落笔返回选择再次隐藏。
- Bad：用窗口穿透样式推断选择模式、视觉像素是否为空推断内容，或把 Clear 实现成一条可撤回的普通 history 项。

### 6. Tests Required

- Headless 覆盖三种 presentation 两步顺序，以及 timer begin/end 幂等、失败、模式往返和析构清理。
- 隐藏 HWND 集成覆盖 Stored Stroke 内容发布、空页/有内容页切换、空页 Eraser history、Clear 后 Undo/Redo 不恢复、其他页内容保留和 content revision 单调变化。
- 完整 `InkeysRepo.sln Debug|ARM64` 构建，运行 `InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-hidden-test` 与 `git diff --check`；不得启动可见窗口。

### 7. Wrong vs Correct

Wrong：`selection = current WS_EX_TRANSPARENT`，或 `Clear -> append erase/blank history -> Undo 可恢复旧画面`。

Correct：`显式 selectionMode + LastVisibleItem 内容真值 -> 有序 presentation plan`；`Clear -> 新 runtime/raster token + GPU/瞬态全清 -> Undo/Redo 空操作`。

## Presenter 合同

- Draw3 自己创建 D3D11.1 hardware-first/WARP-fallback device/context、DXGI factory、swap chain 和 presenter，不注册 `Inkeys.UI.RenderPipeline` client，也不共享其 device epoch。
- presenter 模式顺序为 DComp -> DWM2 -> DWM/Win7 -> ULW；每次失败都销毁该模式的 swap chain/presenter 状态后再降级。DComp 在创建前通过能力探测决定是否预置 `WS_EX_NOREDIRECTIONBITMAP`；legacy 重建 Host 明确跳过 DComp。ULW 使用 premultiplied top-down 32-bit DIB、`1/255` CPU alpha 边界和 dirty rect，透明像素 alpha 必须保持零。
- 同一 Drawpad HWND 只允许一个 `RealTimeStylusInput` producer；Draw2 RTS 不得初始化或重复发布。退出顺序为停止命令生产 -> 停止 RTS -> 唤醒绘制线程 -> 释放 presenter/device -> Window Service 销毁 HWND。

## 来源任务与验证状态

- 源任务历史统一保存到 `.trellis/tasks/archive/2026-08/draw3-source/`；其中 `active/` 保留源快照的 `in_progress` 状态但不进入目标 active task 列表，原 2026-07/08 归档按月份保留。
- 截至 2026-08-16，文件/资源映射、固定库 ABI、HWND/owner/Z 序、独立设备和唯一 RTS 约束已完成静态审计；ARM64 Debug/Release Solution 全量 Rebuild、`--no-window` 纯逻辑测试和隐藏 HWND 的真实 DComp/DWM2/DWM/ULW 合成测试均通过。测试按 DComp-compatible 与 legacy 两个顺序生命周期运行，并验证样式回调结果与实际样式一致；产品启动使用同样的显示前顺序重建合同。
