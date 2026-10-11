# 笔触觉接线与输入隔离

## 1. Scope / Trigger
生产 Draw3 Host、UI3 Bar 工具/类型按钮及可选 Windows 笔触觉。历史迁移 fc9a8b86 保留后端源码却遗漏 Host 初始化/注入，使默认 nullptr 隐藏了功能缺失；不能仅检查模块是否编译。

## 2. Signatures
- `PenHapticFeedback(bool allowInputManagerFallback = true) noexcept`；生产 Host 使用 `false`。
- 后端 `Initialize()`、`AttachPointerId(uint32_t)`、`DetachPointer()`、`TickContinuous(...)`、`PlayDiscrete(...)`、`Shutdown()` 全在同一绘制线程调用。
- `ProductHapticContext() noexcept -> Haptics::Context`；`PublishProductHapticClick(const Haptics::Click&) noexcept -> bool`。
- `CanvasPointerMailbox` 是生产 Pointer 请求/RTS 重置/终态的共享边界，`Inspect()` 只读且不消费。
- `Haptics::Context { uint64_t run, canvas; }`、`ClickSource` 和 `ClickMailbox` 位于 `Draw3.HapticRequest.h`；Bar token/接受操作凭据位于 `Bar.Haptics.h`。

## 3. Contracts
- Host 创建、初始化、注入后端；局部对象析构保证正常退出、启动失败和异常路径配对清理 COM。可选初始化失败不能阻止画布启动。
- 动态加载系统目录 combase.dll/导出并探测 factory、property、controller。生产不从后台线程创建泛设备 InputHapticsManager；无法明确绑定当前笔时无反馈。
- Pen/HardPen/Shape 使用 InkContinuous，Highlighter 使用 ChiselMarkerContinuous，Eraser 使用 EraserContinuous，倒转笔尾沿用 InkContinuous 特殊处理；Laser 无连续反馈，默认强度遵从系统。
- 新 RTS 笔接触不能继承旧 hover 的设备绑定；Pointer 与 RTS 不共享物理 ID 保证，消息顺序必须显式处理。Selection、隐藏、Laser、Pan、离开、取消和失效代际均停止旧反馈。
- Bar 保持 WM_TOUCH 翻译、四种 marker 语义及兼容鼠标去重。仅被动观察 Pointer，不修改 capture、cursor 或默认消息处理，不调用画布 ForwardProductMessage。
- 工具/类型按钮必须在原业务接受点发布 Click。Mouse/Touch、取消、程序化回调无凭据；同一接触 submitted 原子去重。
- Pointer/Touch 关联要求明确 Pen、同来源、同阶段系统时间、原始屏幕点且唯一；禁止把 TOUCHINPUT.dwID 当 pointerId。驱动关联不被文档保证，失败保留原 UI 动作。
- token 与 mailbox 各 8 槽；消息、按下 session 和 Host 持有共享凭据，取消原子失效不依赖队列空位。Click 在 Up 后 250ms 内、接触总计 5000ms 内有效，并核对 Host run 与 canvas 代际。

## 4. Validation & Error Matrix
| 条件 | 合同 |
| --- | --- |
| 缺 DLL/导出/factory/controller、HRESULT 失败 | false/no-op，原输入继续 |
| 可选波形缺失 | 回退 InkContinuous/Click；基础波形缺失则无反馈 |
| 新笔绑定失败、Pointer 数字 ID 重用 | 清旧 controller/失败缓存，不向旧笔发送 |
| 来源歧义、取消、超时、满槽、Host 停止/重启 | 仅丢可选反馈，不挤画布/保存命令 |
| 连续输入被隔离或表面隐藏 | 显式停止含 hover 预设的连续反馈 |

## 5. Good / Base / Bad Cases
- Good：有效 Pen 工具按钮动作发布一次 Click；新鲜画布 Pen 绑定按实际工具反馈。
- Base：WinRT 不支持或笔无能力，绘制与按钮照常工作。
- Bad：用全局最近笔、RTS 新接触继承的旧 hover controller 或 Touch ID 猜设备；这些路径应被回归拒绝。

## 6. Tests Required
- `draw3_haptic_tests.cpp` 直接运行生产 token/ClickMailbox：取消、重复、满槽、代际与复用。
- `draw3_haptic_backend.inl` 仅测试构建启用，在后端同一 TU 用 ABI fake 验证能力/波形回退、发送失败、Stop/Detach、COM 配对；并将 CanvasPointerMailbox 与真实后端组合覆盖 Pointer/RTS 两种顺序、Reset 清旧 pending、终态迟到 Update、取消、多笔退出和工具不匹配；不把纯策略测试当硬件实测。
- 完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`；检查静态 imports 无新增 WinRT/新 Pointer 硬依赖，检查 BOM/EOL。
- 沙箱默认 Temp 若原子替换返回 ERROR_ACCESS_DENIED，可仅将测试进程 TEMP/TMP 指向工作区 runtime；不要修改产品原子写入逻辑绕过环境问题。
- 实体震感、跨驱动关联可达性、真实 Win7 和混合设备光标须另行设备验收。

## 7. Wrong vs Correct
错误：只登记 HapticFeedback 源码，依赖 DrawingController 默认 nullptr；或在 Bar 窗口线程直接调用后台所有的 COM 对象。

正确：`PenHapticFeedback haptics(false);` 在 Host 绘制线程初始化并显式注入；Bar 只发布带有效接触凭据的独立请求，由绘制线程消费并清理。
