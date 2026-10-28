# 上一轮改动与当前调查证据

## 已提交改动

`2f551487 fix(whiteboard): activate window after initial presentation`：

- 新增 `Window::Service::RequestWhiteboardActivation` 和 `CommandType::ActivateWhiteboard`。
- 在 Draw3 首帧、背景和窗口状态被认为就绪后调用 `SetForegroundWindow`、`SetActiveWindow`、`SetFocus`。
- 窗口测试只覆盖请求门禁和焦点状态，没有覆盖首次启动真实 GUI。

局限：激活位于可见性提交下游；如果窗口实际仍隐藏或最小化，补激活不能修复画面。旧同步等待还可能让 StateMonitoring 等住窗口 owner。

## 当前工作树已有改动（必须保留）

- `IdtState.cpp`：先发布 Active 再请求激活；激活 guard 接受同 epoch 的 Entering→Active，拒绝退出、新 epoch 和关闭意图。
- `Window.cpp/.cppm`：激活改为异步排队；系统拒绝前台时不再对其他应用调用 SetActiveWindow/SetFocus；增加 Shell 全屏状态发布点。
- `Window.ShellFullscreen.h`：普通 root 与白板 Freeze/Drawpad 的固定目标批次、去重、revision 失效和最小化 FALSE 补偿。
- `Setting.cpp/.SessionState.h`：实际 `IDXGISwapChain::Present` 期间释放 ImGui 输入锁，返回或异常后重新取得，降低焦点消息与渲染互等风险。
- 对应 headless 测试和 spec 已修改；两个 Draw3 CSO 也有无关二进制改动，本任务不得重写。

## 当前已证实的可见性缺陷

1. `Whiteboard.cpp::RenderBackground` 以 `SetBounds(Freeze)` 和 `Show(Freeze)` 的布尔结果设置 `committedBackgroundActive=true`。
2. `Window.cpp::Execute(SetBounds)` 在白板组 minimized 且 role=Freeze 时直接返回 true，未提交几何。
3. `Window.cpp::Execute(Show)` 在白板组 minimized 且非 Bar/Setting 时直接返回 true，未执行 ShowWindow。
4. 因而一次最小化竞态可被上层误确认为“背景已显示”，随后 StateMonitoring 发布 Active；激活入口又会因 minimized/IsIconic 拒绝请求。手动任务栏恢复后状态消失，符合用户现象。
5. 普通 Freeze 的 ShowWindow 后没有读回 `IsWindowVisible`；SetBounds 也只对 PPT 读回 RECT。即使不是 minimized 分支，仍缺少可见性和几何提交证明。
6. 现有窗口测试把 minimized 下 Show 当作“stale show safely consumed”并要求 true，固化了假成功语义；激活测试还允许 `GetForegroundWindow()!=Freeze` 通过，不能作为首次可见验收。
7. `Build/WhiteboardBackgroundEntryValidation/final-validation.json` 明确 `guiExecuted:false`；此前无窗口测试通过不能证明本故障修复。

## 卡死相关证据与边界

- overlay owner 负责 Freeze、Drawpad、PageControl 和 Bar；同步 Win32 激活一旦等待焦点消息，可能使整组 UI 无响应。
- 旧 Setting 渲染在持 `settingImguiMutex` 时 Present，而 Setting WM_SETFOCUS/WM_KILLFOCUS 需要同锁，存在跨线程锁互等路径。
- 当前改动只解除 StateMonitoring 的等待并在 Present 解锁；overlay 内 Win32 调用仍是同步系统调用。若真实 GUI 仍卡死，需要线程栈，不得仅凭异步改动宣称全部卡死消失。

## 待查明

- 首次进入过程中由谁触发实际 `SIZE_MINIMIZED`，以及它发生在背景 Show 前还是 Show 后。
- 故障现场 Freeze 的 `IsIconic`、`IsWindowVisible`、`GetWindowRect`、`whiteboardGroupMinimized_` 和命令回执。
- 修复假成功后，是否仍存在独立的首次激活拒绝或 Present/焦点锁互等。

这些待项不能反向否定已证实的“未执行却成功”缺陷；修复应先关闭该确定性漏洞，再用真实 GUI 判断是否还有第二根因。
