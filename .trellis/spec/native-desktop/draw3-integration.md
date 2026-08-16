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

## Presenter 合同

- Draw3 自己创建 D3D11.1 hardware-first/WARP-fallback device/context、DXGI factory、swap chain 和 presenter，不注册 `Inkeys.UI.RenderPipeline` client，也不共享其 device epoch。
- presenter 模式顺序为 DComp -> DWM2 -> DWM/Win7 -> ULW；每次失败都销毁该模式的 swap chain/presenter 状态后再降级。DComp 在创建前通过能力探测决定是否预置 `WS_EX_NOREDIRECTIONBITMAP`；legacy 重建 Host 明确跳过 DComp。ULW 使用 premultiplied top-down 32-bit DIB、`1/255` CPU alpha 边界和 dirty rect，透明像素 alpha 必须保持零。
- 同一 Drawpad HWND 只允许一个 `RealTimeStylusInput` producer；Draw2 RTS 不得初始化或重复发布。退出顺序为停止命令生产 -> 停止 RTS -> 唤醒绘制线程 -> 释放 presenter/device -> Window Service 销毁 HWND。

## 来源任务与验证状态

- 源任务历史统一保存到 `.trellis/tasks/archive/2026-08/draw3-source/`；其中 `active/` 保留源快照的 `in_progress` 状态但不进入目标 active task 列表，原 2026-07/08 归档按月份保留。
- 截至 2026-08-16，文件/资源映射、固定库 ABI、HWND/owner/Z 序、独立设备和唯一 RTS 约束已完成静态审计；ARM64 Debug/Release Solution 全量 Rebuild、`--no-window` 纯逻辑测试和隐藏 HWND 的真实 DComp/DWM2/DWM/ULW 合成测试均通过。测试按 DComp-compatible 与 legacy 两个顺序生命周期运行，并验证样式回调结果与实际样式一致；产品启动使用同样的显示前顺序重建合同。
