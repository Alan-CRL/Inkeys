# 笔触觉接线与输入隔离

## 1. Scope / Trigger
生产 Draw3 Host、UI3 主栏与属性面板按钮及可选 Windows 笔触觉。历史迁移 fc9a8b86 保留后端源码却遗漏 Host 初始化/注入，使默认 nullptr 隐藏了功能缺失；不能仅检查模块是否编译。

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
- Pointer 范围身份与 RTS 笔划生命周期分开维护：动态 GetPointerCursorId 成功取得 tablet cursor 后核对 RTS 来源；不把 RTS contactId 当 pointerId。匹配的普通 Down/Up 可复用仍有效的同笔范围，Up 停止本笔反馈；离开、取消、来源冲突/歧义和失效代际清理绑定，Selection、隐藏、Laser、Pan/隔离禁止连续反馈。
- Bar 保持 WM_TOUCH 翻译、Pointer 来源会话及兼容鼠标去重；Click 凭据随共享有界输入 FIFO 的按值样本传递，不恢复旧 metadata token 环，也不另占 wheel token。仅被动观察 Pointer，不修改 capture、cursor 或默认消息处理，不调用画布 ForwardProductMessage。
- 主栏与属性按钮必须在原业务接受点发布 Click；表驱动主栏/形状/粗细按钮共用接受入口。Mouse/Touch、取消、程序化回调无凭据；同一接触 submitted 原子去重。
- Pointer/Touch 关联要求明确 Pen、同来源、同阶段精确时间值、原始屏幕点且唯一；TIMEFROMSYSTEM 缺失时允许提供方时间参与精确关联，可信新鲜度与有效期仍由本进程单调时钟判断。禁止把 TOUCHINPUT.dwID 当 pointerId。跨 API 关联是应用策略，驱动不匹配时保留原 UI 动作。
- token 与 mailbox 各 8 槽；消息、按下 session 和 Host 持有共享凭据，取消原子失效不依赖队列空位。Click 在 Up 后 250ms 内、接触总计 5000ms 内有效，并核对独立按钮 run 代际（Host 启停和按钮开关变化均失效）；画布范围代际仅约束连续反馈，不能让切换模式的按钮因自身动作失效。

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
- Bad：用全局最近笔、已离开/取消/身份不匹配的旧 hover controller 或 Touch ID 猜设备；这些路径应被回归拒绝。

## 6. Tests Required
- `draw3_haptic_tests.cpp` 直接运行生产 token/ClickMailbox：取消、重复、满槽、代际与复用。
- `draw3_haptic_backend.inl` 仅测试构建启用，在后端同一 TU 用 ABI fake 验证能力/波形回退、发送失败、Stop/Detach、COM 配对；并将 CanvasPointerMailbox 与真实后端组合覆盖 Pointer/RTS 两种顺序、同范围连续多笔、Reset 清旧 pending、终态迟到、取消、多笔退出和工具切换；不把纯策略测试当硬件实测。
- 完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`；检查静态 imports 无新增 WinRT/新 Pointer 硬依赖，检查 BOM/EOL。
- 沙箱默认 Temp 若原子替换返回 ERROR_ACCESS_DENIED，可仅将测试进程 TEMP/TMP 指向工作区 runtime；不要修改产品原子写入逻辑绕过环境问题。
- 实体震感、跨驱动关联可达性、真实 Win7 和混合设备光标须另行设备验收。

## 7. Wrong vs Correct
错误：只登记 HapticFeedback 源码，依赖 DrawingController 默认 nullptr；或在 Bar 窗口线程直接调用后台所有的 COM 对象。

正确：`PenHapticFeedback haptics(false);` 在 Host 绘制线程初始化并显式注入；Bar 只发布带有效接触凭据的独立请求，由绘制线程消费并清理。

## 设备实测诊断开关（2026-10-11）

### 1. Scope / Trigger
实体笔绘制和工具按钮均无震感时，先在设置→实验选项打开“测试触控反馈”，观察输入到 WinRT 发送各阶段，再决定是否修改触觉策略。

### 2. Signatures
- `Experimental.Inkeys3.ConsoleOutput.Haptics: IdtAtomic<bool>` 默认 `false`，仅本机配置、不上传。
- `HapticDiagnostics::SetEnabled(bool)` 在启动读取配置后调用；`Enabled()`、`Log(event, format, ...)` 由相关输入与绘制线程读取；`PenHapticFeedback::LogDiagnosticsSnapshot()` 仅由绘制线程调用。
- ClickMailbox::Publish 的可选 rejection 输出、Take 的 ClickTakeDiagnostics 只回传静态原因或丢弃计数；Host/Controller 释放 mailbox 锁后输出。

### 3. Contracts
- 设置页仅写入持久化开关，不运行中创建控制台或切换日志；重启时读取配置并在 Host 初始化前准备现有控制台。原有标准输出重定向需保留。
- 同一 `[PenHaptics]` 前缀记录 Bar Pointer/Touch 关联、按钮请求发布与队列拒绝/丢弃、Host/画布准入、WinRT 能力、绑定、波形与发送结果，拒绝路径给出可区分的阶段或原因。关闭时不进行高频字符串格式化。
- 后端初始化快照由 Host owner 线程输出，不从设置线程访问 COM 或重试激活。连续失败按状态或时间限频，避免在输入锁内写控制台。
- Bar Down/Up 记录原始 flags/mask、来源、时间、坐标及 token 前后值，Move 只记录首次凭据失效；逐项候选统计只是诊断，不改变精确关联或唯一来源校验。
- RTS 接触日志保留已有 contactId/generation、source、位置与源 QPC；画布 Pointer 的首次/状态变化拒绝也须可见，才能区分未收到与收到后未准入。相近坐标/时间仅辅助读日志，不构成跨输入源的身份换算。
- 后端发送输出 pointerId、请求与实际选中的缓存波形、HRESULT；API 成功不等同实体震感。重复失败按状态变化或有限间隔输出，不改变发送重试次数。
- 输出须在所有外层锁释放后执行；Host 在产品调用期间只回传诊断快照，由 Product 退出 `productCallMutex` 后记录，不能只检查局部 mailbox 锁。
- 诊断只是旁路；不能放宽 Pen 身份匹配、改变 RTS、鼠标去重、cursor/capture、波形回退或静默失败行为。

### 4. Validation & Error Matrix
| 条件 | 诊断与业务合同 |
| --- | --- |
| 开关关闭 / 平台无触觉 API | 无高频诊断；绘制、点击与光标继续正常工作 |
| 运行中切换开关 | 只保存配置；当前控制台与诊断状态保持至下次启动 |
| Pointer/Touch 不匹配、请求重复/过期/取消/满槽或绑定失败 | 日志说明在哪一层被丢弃，原 UI 动作继续 |
| 波形缺失或发送 HRESULT 失败 | 报告请求、回退及结果；维持原兼容回退 |

### 5. Good / Base / Bad Cases
- Good：一次笔接触可沿 Bar→Product/Host→Controller→WinRT 找到成功或第一个拒绝点。
- Base：设备不支持时日志给出能力探测结果，应用仍可绘制和切换工具。
- Bad：开关运行中创建控制台或切换诊断；每帧刷屏；为了诊断改变 Pointer/Touch 关联或光标行为。

### 6. Tests Required
- `Scripts/i18n.ps1 check` 确认中英繁键与生成物一致；检查配置默认值和设置回调的持久化与运行时接线。
- `InkeysRepo.sln Debug|ARM64` 与 `InkeysHeadlessTests.exe --no-window` 通过，`git diff --check`、BOM/CRLF 无无关变更。
- 实体笔人工验收：切换开关并重启后分别绘制和点击按钮，采集 `[PenHaptics]` 日志，核对首次拒绝阶段、限频和原交互；无硬件时不得声称震感已验证。

### 7. Wrong vs Correct
错误：设置页运行中创建控制台或改变诊断状态，导致启动日志与输出流状态混乱。

正确：设置页只持久化开关；下次启动先启用诊断并统一初始化控制台，由绘制线程输出初始化快照及后续绑定与发送结果。

## 2026-10-20 真实事件回归要求

- 一次 Pointer Enter 后没有 Pointer Down/Update，连续多次 RTS Down/Up 仍可在核对同笔后启动正确波形；普通 Up 停止本笔，Leave/Cancel 后不得复活。
- 两种线程到达顺序、未知/冲突 cursor、多笔、终态迟到、工具切换与隔离，验证共享生产生命周期与真实后端 fake 的组合。旧的 RTS Down 必然销毁 hover 并等待接触 Pointer 断言应删除。
- Bar 使用真实 mask=0x4 测试完整 Down/Up/接受/单次消费；提供方时间与系统时间都保留来源、阶段、点、唯一性、取消和单调时钟有效期反例。

## 2026-10-20 独立偏好与新增按钮接入

### Scope / Trigger
新增或修改主栏按钮注册项、More 菜单、绘制/形状/橡皮/撤销重做属性面板、颜色和粗细快捷按钮时，必须同时核对本合同。任务关系：当前 restore-pen-haptics 延续此前修复；dev 同步包含的 UI Pointer session、taskbar、shortcut、cursor 能力作为依赖基线，不能为触觉回退这些改动。

### 配置与强度
- Inkeys3 Config/main.json：Drawing.Haptics.Drawing、Drawing.Haptics.Buttons 均默认 true，旧配置缺键自动使用 schema 默认值；两个开关独立、即时生效，走设置页原有 QueueConfigWrite。
- 运行时通过 Host 控制唤醒由绘制 owner 停止/恢复反馈；设置线程不接触 COM。关闭绘制反馈不得清除可核验范围、改变输入准入或连带禁用按钮。
- 按钮使用 ButtonContext(run)，不绑定 canvas 范围 revision；画布 Leave/Reset 仅停止画布绑定，不能中断刚被接受的独立按钮 Click。关闭按钮反馈清空待处理请求并使旧凭据代际失效；快速关闭再开启、Host 重启都不能重放旧点击。
- 按钮统一 Click + kButtonHapticIntensity=0.5，支持时优先单次强度 API；缺能力走已有系统默认/基础波形回退。发送失败不重试更强的反馈；书写继续遵从系统默认强度。数值仅是产品初值，体感仍须实体笔验收。
- 实验选项 Haptics 只控制诊断，默认 false、重启生效；不能与这两个功能开关混用。

### 新控件接入规则
- 注册到公共主栏、形状或粗细按钮表的项使用共享业务接受点，不能在每个 callback 再发一次。新增独立属性控件必须显式报告“操作已接受”；Consumed/命中/按下/悬停均不代表有效点击。
- 只接受同一真实 Pen 的 Down/Up 凭据；不能根据全局最近笔、RTS contactId、TOUCHINPUT.dwID 或键盘/程序化更新猜测设备。
- 取消、拖出、禁用项、动作被拒、拖动及连续 Move 不触发按钮确认；颜色快捷按钮跨色块拖选不发送确认，连续选色板仅正常轻点收尾确认一次，不能每个颜色变化重复发送。同一接触的 submitted 原子去重保留。
- UI 消息仅传递凭据与接受结果；设备绑定/发送/Stop 仍只在 Draw3 owner。失败只省略可选反馈，业务动作、光标、capture 和默认消息处理照常。
- 新增按钮时至少检查：真实 Pen 一次确认、Mouse/Touch/键盘无反馈、取消/拖出不发、禁用不发、重复回调不重发；影响画布模式/可见性的按钮还须检查动作自身不会使其合法点击被错误丢弃。

### 验证
完整 solution Debug|ARM64、现有 --no-window、i18n check、BOM/EOL/diff 检查。后端 fake 覆盖强度/播放次数/时长/无可选能力和失败不加力度重试；队列覆盖独立启停和代际；UI 用例须落在生产共享接受策略或真实状态机，不用仅复制公式的测试替代接线。无 GUI 验证不能声称全按钮实体震感已验收。

### 按钮点击与笔尖微移（2026-10-20 实测回归）
- 普通按钮反馈遵从控件实际接受结果，不得额外用 SM_CXDRAG/SM_CYDRAG 等鼠标像素门限否决有效 Pen 点击。真实日志中 5–15px 微移仍是控件接受的点击，重复判定导致间歇无反馈。
- 连续控件自行区分确认与拖动：色块跨项切换记为拖选，返回也不补发；连续选色板可使用局部轻点门限。不能将连续控件的门限推广到普通按钮。
- 保留同一接触的 Down/Up、真实 Up、取消、有效期、代际和 submitted 去重；来源失效必须记录具体边沿，不能为补反馈放宽身份。

- Bar 的普通 Pointer Leave 仅失效未完成接触；已记录 Pointer Up 的凭据保留至业务确认/绘制消费，仍须精确匹配 Touch Up 并遵守有效期、代际和去重。捕获变化、取消、隐藏、来源冲突及新接触仍执行原失效规则。
