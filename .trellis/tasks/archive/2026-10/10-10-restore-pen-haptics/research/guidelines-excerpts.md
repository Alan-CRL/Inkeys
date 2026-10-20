# 本任务相关规范摘录

仅摘录本任务需要的既有规范；权威源仍为以下原始文档，实施遇到其他相关章节时直接读取原文。

## .trellis/spec/native-desktop/input-and-ink.md:1-114

# Input and Ink

本页用 `【直接确认】`记录实现事实，用 `【合理推断】`记录修改时的审查建议，用 `【待确认】`记录没有运行验证或维护者契约的内容。传统实现事实不自动成为未来新输入系统的强制设计。

## 输入归一化链

`【直接确认】` 当前产品输入主链位于 `Draw3.RealtimeStylus.*`、`Draw3.ContactInput.*` 和 `Draw3.DrawingController.*`；Draw2 输入源码保留但不参与产品编译或启动：

~~~text
主 Drawpad RealTimeStylus ─> ContactInputCoordinator mailbox
                                        │
                                        └─> DrawingController / document / runtime history / final backbuffer
~~~

`RealTimeStylusInput` 是主 Drawpad 的唯一 RTS producer，并保留 `SetAllTabletsMode(TRUE)`；辅助 DrawpadPresentation 不绑定 RTS、WndProc mailbox 或输入门禁。`CSyncEventHandlerRTS`/`TouchMode` 描述仅属于保留的 Draw2 历史实现，不应反推 Draw3 的产品选择模式。

Draw3 Host 在图形资源准备后才初始化 RTS，退出时先停止 producer，再唤醒并结束绘制线程；`DrawpadMsgCallback` 只把主 Drawpad 消息转发到唯一 Host。

`【直接确认】` `WindowController::HandleWindowMessage` 的 `WM_MOUSE*` 接受/拒绝仅控制 `PublishMouseCursorSample` 光标反馈；绘制 contact 仍由 RTS `StylusDown -> coordinator.PublishDown` 提供。不得用 `accepted-mouse` 或 `mouseContact` 证明绘制 Down 已被准入，也不得把光标兼容过滤直接当成落笔失败的根因。

`【直接确认】` 产品未定义独立 demo 的 `DRAW3_RTS_DIAGNOSTICS` 宏；旧 `RecordCallback/FlushRtsCallbackTrace` 不会因开启 ConsoleOutput 而出现。产品 `ConsoleOutput.Cursor` 的运行期开关通过现有有界队列提供 `rts-input event=Down/Up reason=arrival/published/失败原因`，Packets 每 plugin 至多每秒一次。`published=1` 是 producer mailbox 发布证据，仍须分别验证 controller 准入与 GPU 像素。

`【直接确认】` `present success=1 visuals=1` 仅证明帧提交返回成功，不能证明 cursor shader 已写像素。启用 `ConsoleOutput.Draw3` 后，`[Draw3Diag][ulw-pixels]` 在实际 dirty/full 提交时每 presenter 首次及最多每秒一次区分 `sourceAlphaNonzero/sourceMaxBgra` 与 `finalDibAlphaNonzero/finalDibMaxBgra`。Primary 命中底层能使源全透明时的最终 DIB alpha 为 1；黑墨 RGB 可以全零。需使用底层合成前的源 alpha 判断墨迹，源/最终 DIB 正常且 ULW 成功也不能等同桌面可见。`--draw3-ulw-copy-benchmark` 的 8 个统计用例覆盖非原点脏区、RowPitch、黑墨、白光标及底层前后差异。

`【合理推断】` 需要让鼠标、笔、触摸行为一致的改动，优先放在两条输入已汇合的位置；如果只改 `CSyncEventHandlerRTS` 或只改 `DrawpadMsgCallback`，需明确另一条路径为何不适用。

## Scenario: Draw3 触摸擦除与主光标归属

### 1. Scope / Trigger
修改 Draw3 的 RTS Touch 通知、Pointer/Mouse 兼容消息、Pen/Mouse 主光标或系统箭头显隐时适用。Touch 接触圆环与主光标是两条独立呈现路径。

### 2. Signatures
- `WindowController::NotifyTouchContactBegin/End`、`CursorOwner()`、`SetTouchCursorSuppressed(bool)`。
- `ResolveDrawingCursorVisualAuthority(persistentOwner,touchCursorSuppressed,touchPanActive,realMouseTakeoverDuringTouchPan)`。
- `ShouldIgnoreMouseCursorMessage(..., INPUT_MESSAGE_DEVICE_TYPE inputSource)`；Win8+ 动态获取 `GetCurrentInputMessageSource`，Win7 可为 `IMDT_UNAVAILABLE`。

### 3. Contracts
- Touch Down 临时接管视觉归属为 Touch，隐藏系统箭头与旧 Pen/Mouse 主光标；持久 `cursorOwner_` 不写 Touch。Touch Up/Cancel 不恢复旧 Hover，直到可确认的新 Pen 样本或真实 Mouse/TouchPad 消息接管。各活动 Touch 橡皮圆环仍按自己的 runtime 生成，Up 后随 runtime 清除。
- Touch Pan 已由真实 Mouse 接管时，后续 Touch 指头不清 Mouse 样本；Pan 结束后若 Mouse 样本仍有效，保留 Mouse 归属。Pen 新 Hover/Contact 解除 Touch 抑制，Win7 不依赖新 Pointer API 才能恢复笔光标。
- Touch/Pen 提升的 `WM_MOUSE*` 先用 promoted 签名、消息时间屏障与可用的 `INPUT_MESSAGE_SOURCE.deviceType` 过滤；`IMDT_MOUSE/IMDT_TOUCHPAD` 可立即接管。Win7 来源不可用时保留 RTS Touch 与旧过滤链，不使用固定延时压制真实鼠标。
- API 成功返回 `device=IMDT_UNAVAILABLE + origin=IMO_SYSTEM` 的 `WM_MOUSEMOVE` 在触摸抑制仍有效时必须拒绝；不论按键位、触点坐标是否一致或距离 Touch Up 多久，均不创建 Mouse 样本、不改变 owner。明确 Mouse/TouchPad 来源继续走既有接管策略。
- 来源 API 缺失/失败或其它未知来源仍保留已有位置回退；API 查询失败不能被当成已确认的系统注入。Win7 的真实设备行为仍需单独实测。
- WindowControl 必须直接使用 `FilterMouseCursorMessage` 的过滤结果；来源、时间屏障、系统来源、位置回退和按键例外均位于同一个生产入口，调用处不得再按按键条件绕过其中的规则。日志拒绝原因复用该结果。

### 4. Validation & Error Matrix
| 输入 | 主光标与系统箭头 |
| --- | --- |
| 单指/多指 Touch Down、Move、最后 Up/Cancel | 主光标与箭头隐藏；活动擦除圆环仍在，最后一指终态后消失 |
| Touch/Pen 提升的 Mouse 消息 | 不发布 Mouse Hover，不抢占 Touch/Pen |
| Touch Up 后来源未知且停在末触点的 MouseMove | 不解除 Touch 抑制，不重新绘制主悬停圆环 |
| 后续真实 Mouse/TouchPad 或 Pen 样本 | 立即恢复该设备的既有光标策略 |
| Touch Pan 中真实 Mouse 已接管 | 后续 Touch 指头不清 Mouse；Pan 停止不回弹到 Touch |
| Win7 无输入来源 API | 仍用 RTS、promoted 签名及时间屏障；新 Pen Hover 可恢复 |

### 5. Good / Base / Bad Cases
- Good：当前产品选 Pen 而 Touch 入口配置为 Eraser 时，只显示触点擦除圆环，抬指后仍无小光标；之后真实鼠标移动或笔悬停正常恢复。
- Base：没有 Touch 输入时沿用 Pen/Mouse 原有光标策略。
- Bad：只在 Touch 活动计数非零时隐藏系统箭头，最后 Up 后旧 Hover/箭头立即回弹；或把 Touch 写成持久 owner 导致新设备无法接管。

### 6. Tests Required
Headless 覆盖有效视觉归属、Touch 系统箭头显隐、兼容 Mouse 消息来源过滤、真实 Mouse/Pen 恢复和 Pan 接管；另须调用生产 `FilterMouseCursorMessage` 覆盖系统按键态 Move → Touch Up → 迟到系统 Move → 真实 Mouse 接管，并检查 mailbox 与主光标解析结果。完整 ARM64 Solution 构建。隐藏 Host 测试与真实纯触摸、Pen+Mouse+Touch、Win7 设备验证分别记录结果，不能把纯逻辑通过等同硬件验收。

### 7. Wrong vs Correct
- Wrong：Touch Down 只清 Mouse mailbox，仍按旧 Pen/Mouse owner 解析主光标，并在 Touch Up 后依赖 Windows 自动隐藏。
- Correct：Touch 暂时接管视觉归属并保持到真实新设备输入；兼容 Mouse 不得伪装成接管事件。

## Scenario: Draw3 光标控制台诊断

### 1. Scope / Trigger
追查 Pen、Mouse、Touch、荧光笔、橡皮或激光笔光标的归属、重影及抬起后残留时适用。

### 2. Signatures
- 配置键 `Experimental.Inkeys3.ConsoleOutput.Cursor`，默认 `false`；实验选项“光标调试信息”在下次启动生效。
- `SetCursorDiagnosticsEnabled(bool)`、`RecordCursorDiagnostic(format, ...)`、`FlushCursorDiagnostics()` 位于 Draw3 diagnostics module。

### 3. Contracts
- 关闭时不向控制台输出光标诊断；开启时先建立 Debug 控制台，再启用记录。诊断只观察状态，不改变鼠标、笔、触摸的归属和光标显隐。
- `InitializeDebugConsole` 保留已有 stdout/stderr 文件或管道重定向；两路均重定向时不创建控制台，只有一路重定向时只初始化缺失输出。采集必须保留启动至退出的全量文件，不能用末屏替代。Win7 PowerShell 2 / 旧 CLR 的 HashAlgorithm 释放使用 public Clear()，不能假设直接 Dispose() 可见；脚本 parser 通过不等于旧 CLR 方法验证。
- RTS/窗口回调只把定长事件写入有界队列，不直接等待控制台 I/O；绘制线程输出序号、时间、线程和事件，队列溢出必须报告丢弃条数。
- 输入日志需包含 Touch 生命周期、Pointer/Mouse 来源与过滤结果、Pen/Mouse 样本、owner 和系统光标决策。绘制日志需区分主光标与逐触点光标，覆盖形状、位置、尺寸、透明度、Laser 笔尖和本帧呈现结果；移动时可限频，来源和可见性变化必须立即输出。

### 4. Validation & Error Matrix
| 条件 | 结果 |
| --- | --- |
| 开关关闭 | 不格式化或输出光标日志，原光标策略不变 |
| 开关开启且 Touch Up 后重现圆环 | 可从输入接受结果、归属变化、最终视觉来源和呈现结果追溯生产者 |
| 日志生产速度超过控制台消费 | 保留有界内存并报告丢弃数量 |
| Release 构建 | 不显示实验控制台开关，不要求用户开启 Debug 控制台 |

### 5. Good / Base / Bad Cases
- Good：错误圆环出现时有 `mouse`/`pointer`、`owner`、`visual source=primary|touch` 和 `present` 的同序列证据。
- Base：正常单指擦除仅报告一个 Touch visual，最后 Up 后报告 visual 消失。
- Bad：只打印 `SetCursor` 结果，把 Draw3 自绘的圆环误判为 Windows 系统箭头；或在每个 RTS 包中直接写控制台。

### 6. Tests Required
运行 i18n `sync/check`、完整 ARM64 Debug Solution 构建、`InkeysHeadlessTests.exe --no-window` 与 `git diff --check`；真实输入日志需由用户在设备上复现后核对。诊断构建通过不等于原光标缺陷已修复。

### 7. Wrong vs Correct
- Wrong：控制台记录单一“光标可见”布尔值，无法区分系统箭头、主自绘光标和每个 Touch 接触圆环。
- Correct：同时记录输入来源和最终视觉来源，并明确日志丢弃及呈现成功状态。

### UI3 Bar 接触消息归一化合同

`【直接确认】` Bar 注册的 `WM_TOUCH` 会把主接触（包括当前系统上报的 Pen）转换成带内部来源标记的 `ExMessage`。Windows 同时可能为同一接触派发 Pointer 兼容鼠标消息，其 `GetMessageExtraInfo()` 的来源签名为 `0xFF515700`（掩码 `0xFFFFFF00`）。

`【直接确认】` Pointer 兼容鼠标副本必须在 Bar 的窗口过程和 HiMsg 入队回调两个入口都丢弃；不带该签名的真实鼠标消息仍正常入队。低位 `0x80` 只能用于诊断 Pen/Touch 分类，不能决定一次接触的输入归属，因为同一支笔的 Down/Up 可能出现不同低位值。触摸长按右键抑制依赖这条单一 `WM_TOUCH` 路径，不得重新保留 Pen 兼容鼠标分支。

按钮点击完成必须等待明确的 `WM_LBUTTONUP`；`WM_MOUSEMOVE` 的 `MK_LBUTTON` 缺失不能被解释为抬起，否则会把接触或窗口激活阶段的移动误判为第二次点击。

## RTS 初始化与回退

`【直接确认】`：

- `IdtRts.cpp` 的不同初始化路径包含关闭 flicks、启用多点触摸、注册 stylus plugin 等操作；

## .trellis/spec/native-desktop/draw3-integration.md:1-78

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
- owner 层级保持 `MagnifierHost -> Freeze -> DrawpadPresentation -> Drawpad -> PPT/Bar`，使 PPT/Bar 通过传递 Owner 关系始终高于两套画布表面。Drawpad 仍是顶层 owned popup，不是 `WS_CHILD`。Draw3 presenter 不得调用 `SetWindowPos`、`SetParent` 或直接改 owner；`SetBounds(Drawpad)` 必须同步两窗。

## 设备与线程

- Draw3 独立创建 D3D11.1 hardware-first/WARP-fallback 设备、context、DXGI factory、交换链和呈现资源，禁止引用 `Inkeys.UI.RenderPipeline` 的设备。
- Hardware 与 WARP 都可落在 FL11.0；这不保证动态 SRV 的 `WRITE_NO_OVERWRITE`。生产 renderer 必须在当前设备上查 `D3D11_OPTIONS::MapNoOverwriteOnDynamicBufferSRV`，不支持或查询失败时普通笔与 Shape 逐批 DISCARD 且 buffer offset 为零；支持设备保留环形路径。此上传门禁与 ULW 的 `FLIP_SEQUENTIAL` 独立，不能为修一项而替换交换链模式。
- UI 线程只向 bridge 发布不可变快照和命令；renderer、document、history、RTS 消费只在 Draw3 绘制线程进行。
- 进程只允许一套 Draw3 Host、绘制线程和 `RealTimeStylusInput` producer；Desktop、Whiteboard 及每个 `PresentationKey` 的 document/history 是该绘制线程独占的独立 slot。辅助 HWND 仅是同一最终 backbuffer 的 presentation target。退出顺序固定为停止命令生产、停止 RTS、唤醒绘制线程、执行最终保存屏障并排空 worker、释放双 presenter/设备，最后由 Window Service 销毁两窗。
- `DrawingControllerRuntimeObserver::drawingActivityChanged` 只按全部 physical contact 的聚合值发布 `0→1/1→0`；每个命令消费边界与帧末复核，避免 Down 后提前 `continue` 漏报。Host 通过独立 `HostRuntimeCallbacks` 注入产品通知、再次去重，并在 Run 正常返回、异常或 stop/join 后补发一次 false；Draw3 核心不得直接依赖 Bar。

## 样式与透明度

- 主 Drawpad 固定不带选择语义的 `WS_EX_TRANSPARENT`；只有 `ShouldPreconfigureNoRedirectionBitmap()` 能力探测通过时才在创建前预置 `WS_EX_NOREDIRECTIONBITMAP`，随后按主 presenter 模式切换 `WS_EX_NOREDIRECTIONBITMAP`/`WS_EX_LAYERED`。
- `DrawpadPresentation` 出生即固定包含 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，运行期不得清除或借其承载输入。
- DComp 清除 `WS_EX_LAYERED`；ULW 设置 `WS_EX_LAYERED` 并清除 `WS_EX_NOREDIRECTIONBITMAP`。两种 DWM 实现属于禁用的历史路径。
- ULW 必须提交 premultiplied-alpha、top-down 32-bit DIB 和 dirty rect；未绘制像素的 alpha 为零，禁止整窗不透明更新遮挡下层窗口。

Windows 对创建时带 `WS_EX_NOREDIRECTIONBITMAP` 且已经绑定过 DComp target 的主 HWND 可能拒绝后续清除该位（`ERROR_INVALID_PARAMETER`）。Window Service 必须写后读取并核对真实样式。若产品 DComp 启动失败，必须在首帧显示和 Setting 初始化前停止 Draw3 与整条隐藏窗口链，再顺序重建 legacy-compatible 主 Drawpad 及其 presentation-only 前置表面；新 Host 禁用 DComp 并只尝试 ULW。两个主 Drawpad generation 不得同时存在，但同一 generation 必须包含长期待命的 presentation-only 前置表面。

## 功能边界
