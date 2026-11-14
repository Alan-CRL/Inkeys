> 2026-11-13 更新：用户已提供新的整体路线图。未来主线以 [双输入后端规划接纳记录](input-backend-future-plan-2026-11-13.md) 和 [原文快照](input-backend-roadmap-2026-11-12.md) 为准；本文件继续保存故障证据、历史候选与验证门槛，不再表示优先采用“每次移动模式切换 RTS/DM”。当前 PTP 仍全平台延期，本轮不实施。

# 触控板未来实现路线：DM 失败机制、系统要求与 Win10 兼容

## 1. Scope / Trigger

本文供未来重新实现 PTP 双指平移时阅读；不是当前功能的启用许可，也不表示候选方案已通过产品验收。

当前状态（2026-11-12）：

- 用户已选择暂时停用 PTP 双指及其系统惯性，优先恢复 Mouse/Touch/Pen/PenTail 的拖动与绘制。
- `kTouchpadDirectManipulationQuarantined=true`；`TouchpadNavigationOwner::Initialize` 在窗口写入、COM、CreateViewport、Activate 之前返回 false。不得绕过该门禁继续使用旧 DM 路径。
- 触控板配置和 UI 偏好保留，但当前不会产生 PTP 运动。其它输入后端、动力学和绘制路径不变。
- 完整 ARM64 Solution、专项和完整 Headless 已通过；真实设备拖动→回绘制仍由用户现场验收。详见 `multi-input-regression-validation.md`。
- 用户补充未来兼容要求：重新考虑该功能时至少要评估并覆盖 Win10；Win11-only 方案不能单独宣称满足这一要求。

## 2. Signatures

当前被停用的关键调用：

```cpp
RealTimeStylusInput::Initialize(hwnd, ...);       // RTS 绑定主画布
manager_->CreateViewport(nullptr, hwnd, ...);    // 旧 DM viewport
manager_->Activate(hwnd);                        // 已复现的冲突点
viewport_->SetContact(pointerId);                // 只关联触点，不负责解决注册冲突
```

Win11 Pointer / Interaction Context 候选使用以下公开接口：

```cpp
RegisterTouchpadCapableWindow(hwnd, TRUE);
GetPointerFrameTouchpadInfoHistory(pointerId, &frames, &pointers, data);
ProcessPointerFramesInteractionContext2(context, frames, pointers, typeInfo);
ProcessInertiaInteractionContext(context);
ReportWindowContentInertia(hwnd, isInertial);
RegisterTouchpadCapableWindow(hwnd, FALSE);      // owner 退休时恢复
```

这些触控板专用接口的公开最低版本为 **Windows 11**；部分页面仍带预发布说明。若未来采用，必须在产品兼容边界内动态解析全部所需入口，不得新增静态系统导入，也不得把当前开发机存在导出推导为 Win10 可用。

Win10 完整 PTP 候选仍需 DM，但必须更改同窗口输入所有权，例如：

```cpp
// 仅表示生命周期顺序，不是可直接复制的实现。
CloseContactAdmissionAndDrainRts();
rts->put_Enabled(FALSE);
rts->RemoveStylusSyncPlugin(...);
// 证明 RTS 已退休后，才能激活 DM 和拖动期替代 contact producer。

// 回绘制前反向执行：
touchpad.Stop/Disable/RemoveHandler/Abandon/Deactivate();
// 新建并启用 RTS，重建 tablet context/decoder；旧 contact 不得跨代复活。
```

## 3. Contracts

### 3.1 为什么旧 DM 方案在当前接线中不可行

旧方案不是“DM 本身不能识别触控板”，而是 **RTS 与 DM 同时占用同一个主 HWND 的输入委托**。

本机独立探针的直接结果：

| 接线 | 观测 |
| --- | --- |
| 只启用 RTS | Enable/Disable 和 Enabled/Disabled 回调正常 |
| RTS 后在同 HWND 创建 DM，但不 Activate | 不触发本次 Activate 冲突 |
| RTS 后在同 HWND 执行 DM Activate | `0x80070005`；随后原 RTS 首次 Disable 为 `0x80070057` |
| 先在同 HWND 激活 DM，再启用 RTS | RTS Enable 为 `0x80070005` |
| 完整退休 DM 后再启用 RTS | RTS 恢复正常 |
| 第一次 DM Activate 失败后立即重试 | 第二次可成功，但原 RTS 不会因此恢复 |
| RTS 与 DM 分别绑定两个 HWND | 两边初始化成功，但这只是注册隔离，不是触控板功能证明 |

匹配系统 DLL/PDB 的静态链还显示：DM Activate 进入 HWND `DelegateInput`；失败回滚会进入 Deactivate→`_ClearWindow`→`UndelegateInput`。因此“第一次失败后 RTS 委托也被撤销，第二次 DM 才成功”是与现场和探针一致的强推断；没有动态 hook 私有槽，不能把它写成已在产品进程直接观测的事实。

这解释了全部症状：

1. 进入移动模式即尝试 DM Activate，不要求用户先碰触控板。
2. 原 RTS 是 Mouse/Touch/Pen/PenTail 的共同接触 producer；其系统委托受损后，拖动和绘制都会失去 Down/Packets/Up。
3. Pen hover 可由独立 WM_POINTER 光标路径维持，所以“hover 活着”不证明 RTS 接触仍活着。
4. 退出移动模式只 Stop/Disable viewport，不会撤销失败回滚已经造成的影响，也不会自动重建 RTS。

`INPUT`、`EXPLICITHITTEST`、`MANUALUPDATE` 和 Running 状态修复都位于这层注册之后，不能消除 Activate/Enable 的委托冲突。不得再次把它们当作共存修复。

### 3.2 为什么“把 DM 放到另一个 HWND”仍然不可行

真实合成双指 PT_TOUCHPAD 对照已经否决简单换窗：

- RTS 主 HWND + 独立 message-only DM HWND 可以初始化，但未注册主窗口时，三次双指注入均没有主窗口 WM_POINTERDOWN/DM_POINTERHITTEST，也没有 DM Running/Content。
- 动态注册主窗口为 touchpad-capable 后，主窗口能收到两个 `type=5` 的 WM_POINTERDOWN；但对独立 DM viewport 调用 SetContact，两个触点均返回 `0x80070490`，仍无手势输出。
- 两次对照中 RTS 最终 Disable/Disabled 均正常，说明分窗确实避开注册冲突，却没有建立可用的触点路由。

因此不能只创建一个隐藏 HWND 就恢复功能。若未来要 DM 在另一窗口直接接收输入，必须重新设计实际命中、焦点、坐标和其他设备的穿透；这与已失败的“主窗口收 Pointer、另一个 manager SetContact”不是同一方案，需独立原型，不能默认可行。

### 3.3 Win11 候选：主窗口 Pointer + Interaction Context 为什么能避开冲突

这条路线 **不创建 DM manager，也不在 RTS HWND 上调用 DM Activate**：

1. 主窗口通过 `RegisterTouchpadCapableWindow` 主动选择接收 PT_TOUCHPAD 的 WM_POINTER 帧。
2. RTS 继续负责 Mouse/Touch/Pen/PenTail；只有明确 `PT_TOUCHPAD` 的 Pointer 帧进入触控板适配器。
3. `GetPointerFrameTouchpadInfoHistory` 保留同帧全部触点及历史；必须转成 `POINTER_TYPE_INFO` 并调用 `ProcessPointerFramesInteractionContext2`，不能退化成旧 `ProcessPointerFramesInteractionContext` 而丢失触控板扩展负载。
4. Interaction Context 在独立对象中识别双指平移和系统惯性，不需要 DM 的 HWND 输入委托，因此不触发本轮 DM/RTS Activate 冲突。
5. 输出只转换为数值差量，仍由现有绘制线程和唯一 Canvas viewport 应用；不建立第二绘制 producer、不直接操作 D3D/DComp。

系统要求与行为边界：

- 公开合同为 Windows 11；Win10 不能以此方案作保证。
- Interaction Context 会处理系统平移方向、手势开关等触控板设置；不要另写启发式复制系统惯性。
- 必须显式设计输出单位。官方说明 screen 输出随显示缩放，himetric 输出与显示无关但带系统速度响应；不能把 DM 的 client-pixel matrix 语义直接套过来。
- 已被消费的 PT_TOUCHPAD 帧须 `SkipPointerFrameMessages` 并按生产消息边界返回，避免 DefWindowProc 再生成 wheel 而重复平移；非 PTP Pointer 和 RTS 消息保持原路径。
- 惯性期间沿现有 owner/帧泵调用 `ProcessInertiaInteractionContext`，用 `ReportWindowContentInertia` 报告状态，并处理 WM_STOPINERTIA/WM_ENDINERTIA；模式、页面、工作区、尺寸和停止代次必须作废旧输出。
- 当前只证明主窗口能收到合成 PT_TOUCHPAD 且 RTS 保持健康；尚未完成 Interaction Context 输出、方向、单位、惯性及产品集成验收。不得把该候选写成已验证修复。

官方资料：

- [RegisterTouchpadCapableWindow / Thread](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/registertouchpadcapable)
- [GetPointerTouchpadInfo 及 frame/history 变体](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/getpointertouchpadinfo)
- [ProcessPointerFramesInteractionContext2](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/processpointerframesinteractioncontext2)
- [Precision Touchpad Programming Guide](https://learn.microsoft.com/en-us/windows/win32/input-precisiontouchpad/precision-touchpad-guide)

### 3.4 至少支持 Win10 的可行路线及代价

若“支持 Win10”要求真正的 PTP 双指识别和系统惯性，而不是普通滚轮降级，目前最可解释的方向是 **保留 DM，但消除它与 RTS 的同时占用**：

1. 进入白板移动模式前关闭 contact 准入，排空 RTS 回调和活动接触，证明 RTS Disable/RemovePlugin/退休完成。
2. 在同一 HWND 激活 DM；触控板继续走 PT_TOUCHPAD→SetContact→DM。
3. 拖动期间 Mouse/Touch/Pen/PenTail 不能再假定由已停用的 RTS 提供，需要由统一拖动期 Pointer/Win32 输入适配器发布到现有 ContactInput/CanvasPan；不能只支持触控板而弄坏其它设备。
4. 切回绘制时完整停止并退休 DM，再创建新 RTS 代次，重建 tablet context、packet decoder 和 contact 状态；不得延续旧笔迹或让旧 Up 关闭新接触。
5. 验证快速反复切换、拖动中切换、活动笔接触、窗口隐藏/销毁、失败回滚、RTS/PTP 同时到达和退出停止证明。

优点是 DM 本身覆盖 Win10；缺点是这是一次跨 Host、WindowController、RTS 和接触生产链的架构变更，远非本轮局部修复。现有探针只证明“完整退休 DM 后可重新启用 RTS”；**干净 RTS→DM 正序切换、拖动期替代 producer 和真实 Win10 PTP 尚未验证**，必须先做独立原型，不能直接解除 quarantine。

另一种更大范围的方案是像 Chromium/Flutter 一样统一使用窗口 Pointer 输入并让 DM 负责触控板，不再使用 RTS。它能从架构上避开冲突，但必须重新覆盖压感、笔尾、hover、多点、系统兼容消息和 Win7/Win10 回退，不应作为局部修复悄然替换。

### 3.5 兼容性较好但不等价的后备方案

- **保留系统转 mouse/wheel**：所有旧平台继续原有行为，本轮即如此。它不提供可靠的 PT_TOUCHPAD 身份、双指阶段或系统惯性。
- **未来若产品允许，可在移动模式提供普通 vertical/horizontal wheel 平移**：这是通用滚轮功能，不是 PTP 支持；无法可靠区分鼠标和触控板，不能按 Touchpad 设置单独控制惯性，也不伪造 Running/Up。当前任务范围未启用该功能。
- **RegisterPointerInputTarget 不是通用答案**：它涉及特定类型的全局重定向及 UIAccess/窗口所有权约束，不能当作普通 Win10 窗口的无冲突注册替代。
- **直接读 HID/驱动私有数据**不属于通用 PTP 方案，会引入设备依赖、系统手势竞争和驱动支持成本，不建议采用。

若未来接受“Win11 使用 Pointer/IC，Win10 保留 wheel 降级”，实现范围较小；若要求 Win10 也具备完整 PTP，则需选择上述 DM 输入所有权方案，不能宣称两条路线的成本和验收等价。

## 4. Validation & Error Matrix

| 条件 | 必须结果 |
| --- | --- |
| 当前 quarantine | 不执行 DM COM/Activate；RTS 接触链保持，PTP 不产生运动 |
| 未来 Win11 Pointer/IC 可用 | 动态解析、注册、双指帧历史、IC 输出和 RTS 后续绘制均通过；不是只收到 Down |
| Win11 任一所需 API 缺失/失败 | 撤销本次注册并回退既有输入，不写坏 RTS，不伪称支持 |
| 消费 PT_TOUCHPAD Pointer | 不再经 DefWindowProc 生成重复 wheel；其它类型保持原路径 |
| 未来 Win10 DM 切换 | RTS 已真实停产才 Activate DM；DM 完整退休后才启用新 RTS 代次 |
| 模式切换/失败/退出 | 无活动 contact 跨代、旧惯性、旧 Up 或迟到回调复活 |
| 真实 Win10 PTP | 必须在实际目标系统/硬件验证；Win11 合成设备 API 不能替代 Win10 证据 |

## 5. Good / Base / Bad Cases

- Good：未来先用独立系统探针证明注册、双指输出、RTS 后续绘制和完整退休，再接入现有数值 Bridge。
- Base：Win11 Pointer/IC 与 Win10 wheel 降级被明确标注为不同能力；或 Win10 使用经过完整验证的 DM 输入切换。
- Bad：只改 flags、只换隐藏 HWND、第二次 Activate 成功就宣布恢复、盲目重试 RTS Disable、把普通 wheel 冒充 PTP、用 Win11 结果外推 Win10。

## 6. Tests Required

未来重新启用前至少需要：

1. 独立 OS 探针：初始化正反序、真实双指 Down/Move/Up、系统状态/位移、RTS 后续 StylusDown/Packets/Up、Disable/Disabled 和错误回调。
2. 生产前端测试：实际 Register/Pointer/History/IC 或 DM 切换入口，不只调用 `PublishHiddenTestTouchpadNavigation`。
3. 坐标与方向：双轴慢移/快滑、首帧不跳、DPI/显示缩放、系统滚动方向、惯性开关、READY/取消不重复或回跳。
4. 仲裁：Mouse/Touch/Pen/PenTail 与 PTP 轮换、真实 Down 接手、hover 不急停、旧来源不释放到新来源。
5. 生命周期：页面/工作区/模式/Resize/隐藏/销毁/停止、嵌套 COM 回调、迟到事件、失败清理和快速反复切换。
6. 平台：至少真实 Win10 + PTP 和 Win11 + PTP；若采用不同前端，分别验证，不能共享一个 PASS 结论。
7. 回归：完整原生 Solution、完整 Headless、control-fence、pan-feel 和真实用户拖动→绘制；通过后才允许移除 quarantine。

## 7. Wrong vs Correct

Wrong：

```text
DM Activate 失败 -> 修改 INPUT/EXPLICITHITTEST -> 重试成功 -> 宣称 RTS 已恢复
```

Correct：

```text
先确认 HWND 输入委托只被当前前端持有
-> 用真实双指接触验证手势输出和 RTS 后续绘制
-> 按 Win10/Win11 实际能力选择并分别验收
-> 再接入现有 CanvasPan 数值消费链
```

本轮只实施 quarantine 和记录；上述候选均不构成当前产品已支持声明。
