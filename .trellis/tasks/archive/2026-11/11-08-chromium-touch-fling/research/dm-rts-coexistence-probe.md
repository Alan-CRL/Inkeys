# DM 与 RTS 同 HWND 共存探针：静态结论与动态对照

## 范围

本轮以静态判断为主，补充了一个独立的无窗口动态探针。探针位于 Build/Investigations/dm-rts-20261112/coexist-probe.cpp，不修改产品代码，不启动产品主窗口，不注入鼠标、触摸或笔输入；每次运行只创建自动销毁的隐藏 HWND，在同一线程按阶段初始化 COM、RTS 和 DirectManipulation，并读取 HRESULT、RTS Enabled、tablet context 数量及插件通知计数。

## 静态确认

1. 产品把 RTS 和 DM 绑定到同一个主画布 HWND。RTS 由 Draw3.Host.cpp:2464 传入该 HWND；DM 在 Draw3.WindowControl.cpp:387 对同一 HWND 创建 viewport，并在 :399 调用 manager_->Activate(window)。
2. 本机 DirectManipulation.dll 10.0.26100.9818 配套微软符号显示，CManagerImpl::Activate 会进入 DelegateInput，实参包括 HWND、flags=0x1000；_ClearWindow 会进入 UndelegateInput。CreateViewport 本身没有直接调用 DelegateInput。0x1000 的精确 private 语义未知，不将其命名为“禁用 RTS”。
3. DM 的 Activate 是 HWND 级输入委托；viewport 的 Enable/Disable 是另一层控制。产品离开移动模式时通常只执行 Stop/Disable，完整 UndelegateInput 只在 owner Shutdown 中执行。因此业务上的“关闭触控板导航”不等于撤销 HWND 级 DM 输入委托。
4. RTS 的公开合同只说明 put_HWND 指定采集窗口、SetAllTabletsMode 要在 Enabled 前设置，以及多个 RTS 在同 HWND 重叠时行为可能不确定；没有找到 DM 与 RTS 同 HWND 必然互斥或必然共存的公开合同。
5. DM 文档保证的是窗口消息与 DM delegate/UI 的处理边界，并没有承诺原有 RTS/WISP 同步插件在 DM delegate 建立后继续收到 StylusDown/Packets/Up。因此“DM 使用 INPUT 和显式命中”不能当作 RTS callback 仍可达的证明。
6. 更关键的是 DM 自身的失败回滚：Activate 在 DelegateInput 失败后，不仅返回 HRESULT，还调用自身 Deactivate。后者在窗口引用计数减到零后执行 _ClearWindow，进而执行 UndelegateInput(HWND, 2)。这发生在产品看到 Activate 失败之前；因此产品的 managerActivated=false 不能证明系统层从未执行撤销委托。

匹配二进制锚点：Activate RVA 0xDFE0；DelegateInput 调用 RVA 0xE278；失败分支经 0xE3E0，在 0xE3EC 取 vtable+0x20、0xE3F8 调用。文件内 CManagerImpl vtable RVA 0xE9290 的 +0x20 项确为 Deactivate RVA 0xF090。Deactivate 在 0xF1FC 调用 _ClearWindow RVA 0xFC98，后者 0xFCAC 设 w1=2、0xFCB0 调用 UndelegateInput。导入槽经 user32 匹配 PDB 确认为 NtUserDelegateInput/NtUserUndelegateInput；没有为探针调用这些 private API。

## 动态对照结果

探针使用本机现有 Visual Studio ARM64 原生 MSVC 编译，结果保存在 Build/Investigations/dm-rts-20261112/*.stdout.log。各 case 都是独立进程、独立隐藏窗口；复用产品 Tablet flags=0x01010219、MTA、AllTablets、RTS3 MultiTouch、聚合 FTM 的同步插件、实际 DM 配置和 handler。没有调用 EnableMouseInPointer 或修改桌面级输入设置；记录中 mouseInPointer=0、touchWindow=0。

| case | 顺序/窗口 | 结果 |
| --- | --- | --- |
| baseline | 只启用 RTS | RTS.Enable=0；5 个 tablet contexts；RTS.Disable=0，收到 Enabled/Disabled 各一次 |
| same-create-only | 先启用 RTS；同 HWND 只执行 DM CreateViewport、配置、handler，不 Activate | 所有步骤成功；RTS 仍 Enabled，随后 RTS.Disable=0 |
| same | 先启用 RTS；同 HWND 再 DM Activate | DM.CreateViewport/Configuration/Options/Rect/Content/Handler 均成功，DM.Activate=0x80070005；RTS 读取仍显示 Enabled、5 contexts，但随后 RTS.Disable=0x80070057，没有收到 Disabled 回调 |
| dm-first | 先在 HWND 上 DM Activate/Enable，再启用 RTS | DM 初始化成功；RTS.Enable=0x80070005 |
| dm-first-retired | 先完整 DM Stop/Disable/RemoveHandler/Abandon/Deactivate，再启用 RTS | RTS Enable/Disable 均成功，回调计数正常 |
| separate | RTS 在 HWND-A，DM 在同线程的 HWND-B | 两边初始化、启用和关闭均成功 |
| same-retry | 先启用 RTS，同 HWND 第一次 DM Activate 失败后立即重试 | 首次 0x80070005，第二次成功；RTS 始终显示 Enabled，但随后首次 Disable 仍为 0x80070057 |

same-disable-early 对照在任何探针侧 DM cleanup 前立即调用 RTS put_Enabled(FALSE)，仍返回 0x80070057，排除了“只在探针清理时才发生”的解释。追加 same-disable-twice 在 DM cleanup 前连续关闭 RTS：第一次 0x80070057，第二次成功并收到 Disabled 回调。因此不能将 same-disable-early 中后一次关闭成功归因于 DM cleanup，不能宣称永久损坏或只能重启，也不据此向产品加入未评估的盲目重试。

## 当前判断

动态结果与现场 touchpad-init result=0x80070005 高度吻合：在“RTS 已占用主 HWND、随后 DM 试图建立 HWND 输入委托”的顺序下，本机稳定得到同一个 HRESULT；交换顺序则由 RTS Enable 报同一个 HRESULT。把 DM 放到另一个 HWND 或先完整撤销 DM 委托，冲突消失。

本机同 HWND 的初始化冲突已经直接复现；结合静态失败回滚，可以形成更具体的强推断：既有 RTS 输入委托使 DM Activate 被拒绝，DM 随即回滚执行按 HWND 的 UndelegateInput，可能把原 RTS 的系统委托一并撤销；RTS 对象自己的 Enabled/decoder 状态没有随之通知更新。第二次 DM Activate 因原委托已不再占用而成功，仍不能让原 RTS 自动恢复。

这条链同时吻合现场的“首次 DM 失败后 RTS 发布消失”“后一次 DM 初始化成功”“未碰触控板即可触发”“回画笔依旧无接触输入”。但静态存在 UndelegateInput 路径不等于已 hook 观测到该次调用，探针没有直接枚举系统 delegate owner；具体 private slot 语义与真实硬件 Packets 断流仍保留证据边界。

get_Enabled()==TRUE 不能作为健康证明：同 HWND DM.Activate 失败后 RTS 仍返回 Enabled 和旧 contexts，但关闭操作失败且没有 Disabled 回调。后续诊断应记录 RTS 的 Enable/Disable 返回值、Disabled/Error 回调、DM Activate/Deactivate/Shutdown 返回值，不能只记录一个“Enabled”布尔值。

## 对下一步的约束

- 首先确定避免同 HWND 委托竞争的接线方案：保留 RTS 时研究 DM 独立输入 HWND 的命中/转发合同；或在同 HWND 上对 RTS/DM 做明确的互斥生命周期切换。独立 HWND 的初始化成功只证明隔离注册有效，还没证明触控板真实命中能送到它，不能直接交付为修复。
- 同步验证首次 Activate 失败后的原 RTS 退休/重建流程，覆盖 callback、context generation、活动 contact 和输入后端选择；只 Disable viewport、交换初始化顺序、追加 INPUT/EXPLICITHITTEST 都不足以解决已复现冲突。
- 未来产品诊断需按阶段记录 DM HRESULT 及 RTS 的 Enabled/Disabled/Error、真实 Down/Packets/Up 到达和发布；硬件验证用最少的画线→拖动→回画笔操作补齐。当前调查未修改产品诊断。
- 不要把 RegisterPointerInputTarget 的 UIAccess/E_ACCESSDENIED 规则套到本问题；当前 DM DLL 走的是 private DelegateInput，不是该公开 API。
- 不要仅用 fake viewport、数值 probe 或 headless PublishDown/Move/Up 测试宣称 RTS/DM 已兼容；这些测试绕过了系统输入委托层。

## 限制

探针没有物理硬件输入，也没有附加到产品进程；因此它证明的是系统初始化/注册冲突的可复现性，而不是完整的真实设备消息丢失链。产品用外部已附着 HWND、具有自己的线程、窗口样式、入队/发布生命周期；探针用自建隐藏 WS_POPUP，所有初始化在同线程 MTA 中完成，并未镜像产品跨线程 owner 编排，也未调用 SetDesiredPacketDescription/SetInputRectangle 或运行产品 decoder。separate 的两个 HWND 都设置与产品相同的 Tablet property/手势 flags；补齐第二个 HWND 的属性后结果仍全部成功。相同 case 的逐阶段结果足以定位当前系统组合的初始化冲突，不能外推所有 Windows 版本、无条件 DM/RTS 互斥、真实输入已经恢复，或产品退出一定与探针相同。

本轮只新增研究文档和忽略目录内的调查工具；未修改产品源码、测试、工程配置或现有 spec。独立探针编译成功，九种 case 的退出码/阶段结果均已检查；非零 4/5 表示预期观测到的 COM API 失败，不是编译失败。未构建主 solution，因为没有产品改动；未执行 GUI/Computer Use。工具与系统 DLL 的 SHA256 在 binary-sha256.json，匹配系统 PDB/反汇编/原始日志保存在同一调查目录。


## 11-12 追加：真实合成接触与独立 DM 窗口否决证据

经用户明确授权，本轮新增独立调查工具 `Build/Investigations/dm-rts-20261112/manual-contact-probe.cpp` 与 `manual-touchpad-probe.cpp`，显示短时小测试窗口并注入合成输入；没有启动或操作正式产品。RTS 在独立 MTA worker 绑定可见主 HWND，DM 在 UI STA；RTS 就绪握手和关闭均保持 UI 消息泵。工具、构建脚本及原始 stdout/stderr 均保存在同一忽略目录。

### 普通 Touch 对照

`manual-contact-probe` 使用 `InitializeTouchInjection`/`InjectTouchInput`。首轮 pointerId=37 的注入被系统以 ERROR_INVALID_PARAMETER 拒绝，未作为行为证据；改为既有探针使用的 id=0 后注入成功。三次接触均产生真实 RTS StylusDown，至少有后续 Packets/Up；但主 HWND 没有对应 WM_POINTERDOWN，只有 WM_POINTERCAPTURECHANGED。该结果只说明不能用普通 Touch 验证“RTS 活跃时主窗口 WM_POINTERDOWN→DM SetContact”的假设，不外推 PT_TOUCHPAD。DM 独立 HWND Activate 成功，RTS 最终 Disable/Disabled 回调正常。

### 双指 PT_TOUCHPAD 对照

`manual-touchpad-probe` 动态解析 `CreateSyntheticPointerDevice2`、`InjectSyntheticPointerInput` 和 `DestroySyntheticPointerDevice`，创建带物理尺寸及 `SDCO_TOUCHPAD_GESTURE_ONLY` 的真实 PT_TOUCHPAD 合成设备，每次输入包含两个触点；这些 API 只用于获准的独立探针，不进入产品或改变项目兼容基线。触控板输入按鼠标位置映射，探针把光标置于测试窗口并在结束后恢复原位置。

- 未调用 `RegisterTouchpadCapableWindow`：RTS 主 HWND + 独立 message-only DM HWND 均初始化成功；三次双指注入全部成功，但主 HWND 没有 WM_POINTERDOWN/DM_POINTERHITTEST，DM 没有 Running/Content，RTS 最终 Disable 正常。仅换 DM HWND 不会让手势到达主画布。
- 动态调用 `RegisterTouchpadCapableWindow(canvas,TRUE)` 的对照：主 HWND 收到两个真实 type=5 的 WM_POINTERDOWN；对独立 message-only DM viewport 分别 SetContact，两个调用均返回 `0x80070490`，仍无 Running/Content。第一次和第三次未关联手势、第二次尝试关联的输入均有完整 Pointer Up；RTS 最终 Disable/Disabled 回调正常，无 RTS Error。
- 因此“独立 DM HWND + 主 HWND 手动 SetContact”已被实际系统结果否决，不能作为修复。注册 API 的公开合同为 Windows 11，且任务原设计明确不把预发布触控板注册 API 作为基线；本轮没有把它接入产品。

原始日志：`manual-contact-probe.stdout.log`、`manual-touchpad-probe.stdout.log`、`manual-touchpad-registered-probe.stdout.log`。首次 RTS 握手曾因探针主线程阻塞等待而没有泵消息，修正为带消息泵后重跑；该失败只属于探针，不是产品根因证据。

### 修复裁决

用户在三路线中明确选择“先禁用触控板扩展”。产品修复因此不尝试跨 HWND DM、不切换 RTS 后端、不引入 Win11-only Pointer/Interaction Context 新前端；而是在 WindowController 统一隔离 DM owner 创建和 hit-test 关联，使主 RTS HWND 不再执行 DM Activate。PTP 双指及系统惯性暂时不可用，其它设备、配置和绘制路径保持。真实设备恢复仍需用户用新产物验证，不能由上述探针直接宣称。
