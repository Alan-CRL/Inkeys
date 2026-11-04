# Research: 原生设置窗口正常业务入口

- Query: 在用户允许启动窗口测试、明确禁止 Computer Use 的情况下，如何让隔离副本经正常业务 Show 打开设置，而不是只显示空 HWND。
- Scope: internal；只读源码、规范及既有测试脚本，未启动进程、发送 UI 输入或修改产品源码。
- Date: 2026-10-11
- Active task: .trellis/tasks/10-10-setting-home-design

## Findings

### 正常入口与业务状态

- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:649`：设置按钮唯一搜索命中的正常产品入口调用 `Inkeys::UI::Setting::Toggle()`。
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp:1277`：设置按钮强制加入 `moreSnapshot.explicitMore`，因此当前正常入口为主栏 More 展开后的设置 tile。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3742`：More 浮层优先处理设置 tile。按下后等待后续输入；指针仍在相同按钮且释放时，收起 More 并调用按钮 `clickFunc`。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:4305`：Toggle 根据业务可见性以及前台/焦点判断 Hide 或 ShowAndActivate。Show 调用不能由窗口物理可见性代替。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:4278`：Show 检查初始化完成，在状态锁内 `settingSessionState.SetVisible(true)`，入队 ShowWindow 业务命令并请求 Settings render client。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp:768`：业务 FIFO 将 ShowWindow 委托给 Window Service 的 Setting 角色线程。
- `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h:163`：业务 SetVisible 的真实 false→true 变化递增 ShowSerial，供入口动效消费。裸 Win32 ShowWindow 只改变 HWND 可见性，没有这一步，不能当成该动效或 renderer 初始化/呈现测试。

### 普通消息与真实鼠标输入的差异

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:7339`：`QueueWindowMessageInLayoutSpace` 在窗口线程固化屏幕点，转换到 Bar 布局空间后写入输入队列。
- 同文件 `:7357`：除 wheel 外，鼠标事件点来自 `GetMessagePos()`，并不采用测试脚本传入 lParam 中的 client 坐标；wheel 才直接读取 lParam 屏幕坐标。简单 PostMessage 仅构造 client lParam，不保证排队位置与预期点击点一致。该差异是脚本消息点击不等价真实鼠标点击的源码依据，不证明所有环境下都必然失败。
- `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h:264`：屏幕到布局点扣除 monitor origin 与成功呈现/直移的 translation；扩展面板动画改变 viewport 和客户区，因此不应沿用收起栏客户坐标点击展开后的 tile。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3941`：普通主栏按钮必须收到明确 WM_LBUTTONUP 且未按住，Move 缺少 MK_LBUTTON 不能代替释放。

### 主会话已验证的打开方法

以下为主会话直接报告的现场证据，本研究代理没有重复操作：

1. 隔离实例 PID 42384，可执行文件 `Build/SettingHomeRuntimeReview20261011/app/Inkeys.exe`；保留用户实例不操作。
2. 使用父任务既有命令行脚本 `.trellis/tasks/09-06-setting-winui3-design-rebuild/research/window-review.py` 的真实鼠标分支；本轮未使用 Computer Use。
3. 在收起工具栏以 client `(710, 80)` 点击 More；等待并捕获展开栏，重新确认设置标签位置后以 client `(700, 138)` 点击 Setting。
4. 主会话已捕获实际设置页面及入场归位中间帧；这说明正常 Toggle→Show→业务状态→渲染路径已执行，超出了裸 HWND 可见的证据。
5. 此前 `--message` 未触发 More，单独 ShowWindow(HWND) 得到空窗口；二者不计为正常入口验收。

上述坐标只对本次已确认布局有效，不能硬编码成全部 DPI/窗口尺寸的入口。后续每次动作都应读取当前 PID 窗口及客户区；More 动画/viewport 变化后重新观察布局，再操作当前设置 tile。

### 既有脚本及验收范围

- `window-review.py:182` 起的 click/move/wheel 分支先验证目标 HWND 属于显式 PID、client 点在范围内、WindowFromPoint 仍属于目标，放置指针后再次验证。真实 click 使用 mouse_event 按下/释放；`--message` 分支只用 PostMessage。
- 本轮只需复用已有工具与入口，不需要新增产品命令行参数、调试热键、私有消息或修改业务 Show。
- 捕获页面及入场中间帧证明正常业务打开/有可见呈现；还不能单独证明设备移除恢复、反复 Hide/Show、全部尺寸/倍率或 Win7 实机通过。崩溃修复的已有 WARP 离屏/故障注入证据仍见 `texture-crash-validation.md`。

## Files Found

- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`：设置按钮构建、正常 Toggle 回调及 More 强制注册。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：窗口输入坐标固化、More 与主栏按下/释放业务仲裁。
- `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h`：屏幕、客户区、布局空间转换及直移 translation。
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`：Show/Hide/Toggle、FIFO 与 RenderPipeline 请求。
- `Inkeys/Inkeys/UI/Setting/Setting.SessionState.h`：真实显隐事件序号与业务可见性。
- `.trellis/tasks/09-06-setting-winui3-design-rebuild/research/window-review.py`：既有原生命令行输入/窗口观察脚本，非 Computer Use。
- `.trellis/tasks/10-10-setting-home-design/research/texture-crash-validation.md`：上一轮崩溃缺陷、真实 WARP 与模拟故障的证据边界。

## Related Specs

- `.trellis/workflow.md`：研究落盘与任务持续 in_progress；不因为一次窗口打开结束整个任务。
- `.trellis/spec/native-desktop/index.md`：唯一当前设置实现为 ImGui+DX11 普通 Win32 窗口，复用共享 WARP epoch。
- `.trellis/spec/native-desktop/rendering-and-ui.md:1451`：Setting 统一渲染与业务队列合同，Show/Hide 与常驻资源/呈现资源分离。
- 同规范 `:2095`：主页入场依赖真实 ShowSerial；Hide 复位主页/顶部，resize/主题切换不能冒充新 Show。
- 同规范 `:2033` 的历史 Computer Use 许可已被本轮用户“不要 Computer Use”的明确要求覆盖，不能沿用旧许可。

## External References

未调用外部检索。本轮只核对仓库的实际入口、输入和脚本实现，不扩大 Windows API 或平台兼容承诺。

## Caveats / Not Found

- research.jsonl 不存在；研究角色未读取 implement.jsonl/check.jsonl，以原生 hook 提供的 Active task、研究隔离要求和 PRD/design/implement 文档加载范围为准。
- 定向搜索当前已编译产品区域只找到 Bar Setting::Toggle 正常调用；未发现可直接开启设置的现成命令行入口。本结论是该次搜索范围内的 not-found，不保证不存在所有间接调用。
- 本研究代理没有启动/关闭任何实例，也没有操作 UI；原生运行结果由主会话单独记录。
- 设备移除的最初触发源仍未确认；成功打开一次不应宣称真实设备恢复或原问题全部触发条件消除。
