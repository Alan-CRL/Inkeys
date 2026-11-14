# 输入规范定向摘录

来源：.trellis/spec/native-desktop/input-and-ink.md，基线 42f4b9a8。仅截取本任务需要的启动回退与面积合同，避免上下文注入的32KB文件截断；旧面积公式由本任务需求定向替代。实施前若源规范有变化，重新核对。

## 输入后端启动回退（原文件116–156行）

产品 Draw3 启动输入初始化适用；历史 `IdtRts.cpp::SafeRTSInit` / `useMouseInput` 不参与产品编译，不能作为当前兜底已接线的证明。

### 2. Signatures

- `RealTimeStylusInput::Initialize / UsesBasicPacketDescription / EnableContactPublication / Shutdown`；生产共享编排和 callback lease 位于 `Draw3.RtsStartup.h`。
- `HostInputMode::{Disabled,RtsExtended,RtsBasicXY,Win32Mouse}`；`HostStartupStage::InputReady` 对应 `Milestone::Draw3InputReady`，保持原进度槽/权重。
- `WindowController::SetMouseCompatibilityEnabled / CloseMouseInputAdmission` 和 `Draw3.MouseInput.h` 的生产鼠标状态；只向现有 ContactInputCoordinator 发布。

### 3. Contracts

- 按完整RTS→基础X/Y→Win32Mouse串行选择，每档最多一次。基础档覆盖扩展SetDesired失败及设置成功后Enable失败，使用新RTS实例；保留all-tablets/多点设置，不枚举额外开关或固定延时重试。
- 每次可恢复RTS尝试有独立Dormant失败清理scope，停止/排空并真实Complete/join后才能下一档；成功实例恢复Host signal。Disable失败时必须成功get_Enabled读回明确FALSE，才可继续Remove及排空；已加入插件但仍启用或状态未知、Remove失败、排空未证明或deadline过期均不能启用下一producer。
- 同步插件所有业务回调先取单原子关闭位+在途计数的lease，Shutdown先关门，再无内部锁Disable/Remove，等最后lease的event通知后才能终止contact和释放借用对象；不能只看计数零越过最后通知尾部。
- 候选RTS可建立decoder但不发布业务Down，选定输入后才开放；就绪前按下必须抬起后重新Down。不能在drawing consumer存活时ResetForNextRun清失败候选队列。
- 每代只有一个绘制producer。RTS模式WM_MOUSE仍只处理原光标，不重复落笔；Win32Mouse从正常RTS的promoted/Touch barrier过滤之前接收真实鼠标及系统已提升的单点鼠标消息，Pointer不再提供第二条绘制流。
- 鼠标左右键复用MouseLeft/MouseRight语义；单活动流、另一键先结束旧流，迟到旧Up不结束新流。保留有符号客户区坐标和未知压力/面积；Move不能凭空创建Down。
- 捕获由owner线程设置/释放并读回。CaptureChanged/CancelMode/隐藏/销毁/Stop取消活动contact；MouseLeave不是Up。停止须owner串行确认和转发排空，之后才能撤Coordinator/Reset；RTS Shutdown不能代替鼠标停产证明。
- 鼠标降级记录warning和真实input_mode，不冒报rts_ready成功，不新增阻塞提示或设置项。基础档不承诺原压感；鼠标档不承诺压感、多点、笔尾/触觉，系统不提升鼠标消息时不保证对应Pen/Touch可用。

### 4. Validation & Error Matrix

| 情形 | 必须结果 |
|---|---|
| 完整RTS成功 | 不试X/Y，不启用WM_MOUSE绘制 |
| 扩展描述失败，或扩展Enable=0x80070057 | 安全清理旧attempt后用新实例试X/Y一次 |
| RTS不可用且安全停产 | owner确认Win32Mouse后才打开输入就绪门 |
| Disable失败但成功读回Enabled=FALSE | 继续必需的Remove和drain，全部完成后才可降级，与publisher是否存在无关 |
| 已加入插件且Disable失败读回仍启用/状态未知、Remove/drain失败或Expired | 不启用新输入，不释放仍借用对象，受控致命收口 |
| 重复/迟到Up、捕获丢失、Stop交错 | 恰好一个终态，新代次不继承旧contact或mode |

### 5. Good / Base / Bad Cases

Good：扩展Enable失败→X/Y成功；或两档失败→鼠标就绪并经原Controller绘制。Base：完整RTS保持原能力。Bad：只重试SetDesired、吞停产错误、RTS和WM_MOUSE同时发布、把光标移动当作绘制兜底。

### 6. Tests Required

生产共享RTS编排/清理scope/lease/publication gate故障注入；生产鼠标状态→真实Coordinator；完整Solution Debug|ARM64与Headless `--no-window`。这些不能证明真实COM/驱动接受X/Y、实际首帧、owner捕获或系统Pen/Touch提升，现场/授权窗口验收单列。

### 7. Wrong vs Correct

Wrong：依赖旧Facade转发或把RTS false改为true放行。Correct：选择真实输入后端、完成旧producer停产，并通过同一Contact/Controller/历史保存链。通用进程自动重启不是本合同的替代。

## RTS面积元数据（原文件295–341行）

## Scenario: RTS Touch contact-area metadata and relative-length conversion

### 1. Scope / Trigger
Applies when reading RTS WIDTH/HEIGHT, interpreting PROPERTY_METRICS or logging TouchArea diagnostics. In this 2026-09-14 metadata scope, speed response, size curves, DIP defaults, area multiplier/ceiling, input position collection and EDID semantics were frozen. The 2026-09-23 Touch physical-scene curve above supersedes only that historical speed-response freeze.

### 2. Signatures
- `ResolveContactLengthTransform(axis, span, positionScale)` returns per-axis status, span-to-axis and span-to-canvas factors.
- `ConvertContactArea(rawWidth, rawHeight, widthTransform, heightTransform)` preserves raw values; unknown/invalid metadata leaves pixel values unknown.
- `RealTimeStylusInput::TraceTouchAreaDiagnostics(source)` reads cached contexts only; an empty source lists Touch contexts, a supplied source requires matching context and generation.

### 3. Contracts
- Actual returned packet properties determine GUID, index, units, resolution and declared range. Requested properties are not proof of returned support.
- For supported inch/centimeter units: `spanToAxis = axisResolution / spanResolution * spanUnitCm / axisUnitCm`; `spanToCanvas = spanToAxis * abs(positionScale)`. PositionScale is the existing packet-XY-to-canvas linear mapping.
- Different resolutions and convertible length units are valid relationships, not failures. DEFAULT is unknown; angular or undocumented extended unit semantics are not guessed.
- Width/height are lengths, never translated positions. Do not subtract logical minima or multiply the context ink-to-digitizer factor again. The existing position path is untouched.
- CanvasPixels is set only for an explained conversion and in-range finite packet values. DIP conversion occurs once in the existing controller. Bounds/aspect/outlier/startup/idle/history policies remain unchanged.
- Conversion status crosses the existing contact snapshot with raw/converted values. Unknown units, invalid resolutions, invalid declared ranges, unsupported units and bad transforms have separate diagnostic reasons.
- Runtime tracing copies the existing bounded RTS cache under its reader gate and a short plugin-lifetime guard, then formats outside both locks. No per-packet hardware query or log. Enabling diagnostics replays cached Touch metadata; source/generation changes refresh it at most once a second.
- A metadata dump without a known cursor explicitly says mapping is pending. It must not substitute Mouse/Pen metadata or the primary display for the active Touch source.

- 20261007-233715 全面现场：WARP87终态完整、matrix69绑定全PASS；CursorWithDefaultInk(mask0/15)全零，而FullWithoutDefaultInk(mask0/15)恢复848像素，末尾原PS仍零/CursorOnly恢复。此双向分界定位DefaultInk源码群触发共享PS执行异常，不等于唯一opcode已证；其未guard的导数/discard尾部、薄线helper和SV_Position声明仍关联。已测“非默认类型提前返回”仍失败，正向包裹输出块须核实际DXBC；去薄线只作诊断，不作降画质修复。Fields读取/校验SV_Position.xyzw成功，不能称Win7没有该语义。修复验收须另测真实type0/3、非零Destination上的Retain/擦除及全工具。

### 4. Validation / Error Matrix
| Case | Required result |
| --- | --- |
| Same units, different positive resolutions | Apply resolution ratio |
| Inch/centimeter units | Apply one physical-unit ratio |
| Missing property/unit | Retain raw values; explicit missing status |
| Invalid resolution/range or unsupported units | No usable canvas dimensions; exact metadata reason |
| Packet outside declared range or nonfinite | Reject, not clamp into an acceptable finger |
| Nonzero logical origin / reflected position axis | Relative length ignores translation and uses absolute linear scale |
| 96/144/192 DPI / anisotropic axes | One per-axis DIP conversion; unchanged auxiliary floor formula |
| Diagnostic enabled after context creation | Dump cached Touch properties without rebuilding/querying hardware |
| Source context/generation no longer cached | Report no matching Touch, not another device |

### 5. Good / Base / Bad Cases
- Good: an explicitly declared tenfold resolution difference produces a tenfold ratio, independently of observed sample magnitude.
- Base: metadata remains unknown, so area assistance stays off while speed erasing and raw diagnostics still work.
- Bad: divide by 10/100 because a value looks too large, assume all packet properties share the context scaling factor, or relabel unverified numbers as pixels.

### 6. Tests Required
Use the production converter in headless tests for unit/resolution/range/DPI cases and pass its synthetic output through the existing hidden eraser ingress tests. Preserve no-Move expiry, point erasing, old radii, new geometry, undo/redo and persistence checks. Synthetic success is not Surface packet acceptance.

### 7. Wrong vs Correct
- Wrong: `rawWidth * contextScaleX` followed by an equality-only metrics check.
- Correct: resolve the width-to-X length relationship from returned metrics, then apply the cached position linear mapping exactly once.
