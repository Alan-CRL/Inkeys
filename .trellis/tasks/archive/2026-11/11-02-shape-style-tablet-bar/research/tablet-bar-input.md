# Research: 数位板主栏按钮输入准入

- Query: 为什么 20261028a 中数位板不能点击主栏，而白板/PPT 翻页正常？
- Scope: mixed（生产源码静态追踪、历史任务、微软 API 文档；无运行验证）
- Date: 2026-11-02

## Findings

### 用户事实与版本边界

用户报告设备为高漫 M6，Windows 11，测试版本 20261028a；其他驱动信息未知，用户倾向静态检查。本轮不要求切换驱动或进行真机测试。

主会话已核对本地发布候选提交 `dc8d0a13c99dcb8de8273f28d7029525cf7c7ab9`（Prepare to publish 20261028a，2026-10-28 11:11）。相对当前 HEAD，Bar.Interaction.cpp、PageControl.cpp、Message.cpp 无差异。此信息将下述静态缺口对应到发布候选源码，但不能证明用户安装包二进制与该提交逐字节相同。

### 已确认：主栏丢弃笔兼容鼠标消息，未提供同等笔业务入口

- `Inkeys/Inkeys/Message/Message.cpp:61` 的 `IsPointerGeneratedMouseMessage` 对鼠标消息匹配 `extraInfo & 0xFFFFFF00 == 0xFF515700`；`:68` 与 `:74` 再用 `0x80` 区分 Touch / Pen。过滤 Pointer 等于同时过滤这两类。
- `Inkeys/IdtMain.cpp:3547` 把栏窗口消息交给 `QueueWindowMessageInLayoutSpace`。该函数在 `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:9726` 首先返回 Discard，笔兼容 WM_LBUTTONDOWN / UP / MOVE 不会到共享交互队列。
- 同文件 `:3208` 的底层 WndProc 再次以同一 Pointer 判断拦截；注释假设 Pen 与 Touch 均由 WM_TOUCH 合成。仅放开其中一处不能构成完整修复。
- WM_TOUCH 入口在 `:3070`，其 `PublishTouch` 在 `:3084` 无条件标记 Source::Touch，随后转译成 WM_LBUTTONDOWN / MOVE / UP 入队（`:3091`、`:3101`）。这是已有非鼠标业务入口，但不能从源码得出所有数位板笔都会发 WM_TOUCH 的保证。
- WM_POINTER 在 `:2913` 仅调用 `ObserveBarHapticPointer` 被动旁观，`:296` 的实现用于笔触觉 token；不是按钮 Down / Up 分发替代入口。
- 普通主栏按钮在 `:6153` 遍历呈现按钮，`:6171` 收到 Down 后进入按压，`:6198` 必须收到明确 Up 并仍命中才提交。上游拦截即可导致无法按下/激活，不需要假设渲染命中失效。

### 已确认：PageControl 单独保留了笔兼容路径

- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:2800` 只过滤 Touch 兼容鼠标副本。
- `:2844` 对笔兼容 Down / Move / Up 单独处理，`:2850` 标记 Source::Pen；Down 建立非鼠标租约与 generation，后续仅允许同来源会话，终态释放租约。该分支明确注明笔兼容消息没有物理 Raw Mouse，不要求 Raw 配对。
- `:3099` 附近以 capture 与 Session 建立笔归属；`:3110` 仅 Mouse 拖动候选才申请 Raw。`:3116` 形成 SurfaceInputEvent，`:3133` 投递共享 worker。
- `:2459` 对真实场景做按钮 Down；`:2524` 按工作区策略调用方向动作，白板方向点击在 `:2596` Up 提交。同一 PageControl 路径服务白板和 PPT，区别在业务触发策略。

因此，**源码确定存在笔兼容消息在两个 UI 客户端准入不一致的缺口**。如果高漫 M6 此时向主栏发送带笔签名的兼容鼠标消息，而没有可用的 WM_TOUCH 替代流，这一缺口足以解释用户现象。后半句是条件性根因假设，未读取该设备实际消息流，不写作已复现或已验证的真机根因。

### 不能只改过滤函数

- `Bar.Interaction.cpp:1110` 入队时只有 Mouse Down 固化 MouseGesture 准入，只有 Touch 被打非鼠标消息标记。
- 消费端 `:1290` 根据 `IsBarTouchPointerMessage` 分流；`:1333` 仅 Touch 调用 `barPointerSession.Begin`，其余有 metadata 路径取 MouseGesture 准入，Move / Up 同样按此区分。
- `Inkeys/Inkeys/Input/UiPointerInput.cpp:87` 的 MouseGesture::Begin 拒绝非 Mouse 来源，`:54` 的 Session 已能按来源/generation 隔离非鼠标会话。
- Raw provider 在 `UiPointerInput.cpp:702` / `:720` 过滤全部提升副本，这是防重复的独立合同，不能通过放开 Raw 使笔伪装为物理鼠标。

最小修复候选：借鉴 PageControl 的已确认笔兼容入口，使 Bar 对经签名确认的 Pen 单独建立来源、generation、capture 与租约；共享 worker 以真实 source 处理 Pen 的 Down / Move / Up / Cancel，不让其走 MouseGesture 或 Raw，并保留 Touch 副本过滤。应核对取消、隐藏/销毁、来源接管和主按钮/属性连续交互，而非全局改 Message 识别函数、Hook 或 Draw3 绘制输入。若同时存在 WM_TOUCH 流，还需明确防重复归属，不应盲目并行提交两个 Down。

### 历史意图与现有测试

- `.trellis/tasks/archive/2026-10/10-17-ui-pointer-session/research/check-findings.md:8` 明确记录：PageControl 原本接纳 pen-promoted WM_MOUSE；保留其独立来源和 capture，不改变 Bar 原有笔过滤。说明差异是历史上有意保持的范围边界，不宜直接断言由 10-17 新增回归。
- `InkeysHeadlessTests/message_tests.cpp:109` / `:130` 测来源签名区分。
- `InkeysHeadlessTests/ui_pointer_tests.cpp:28` 测 Raw 过滤，`:32` 测 PageControl 笔识别，`:135` 测 Session::Pen 不被物理 Mouse/Touch Up 结束。
- `PageControl.cpp:3812` 起有生产 worker 工作区隔离测试；`:3862` 确认白板合法点击共用按钮状态机。
- 本轮未找到主栏生产入口“带笔签名 WM_MOUSE 到按钮激活”的无窗口回归。现有通用 Session / 签名测试通过，也不足以证明 Bar 入口接纳笔。

建议后续静态/无窗口验证覆盖真实 Bar 准入策略、Pen 单次点击、双击第二击、连续 Move/Up、鼠标与触摸接管、迟到终态、capture 取消、重复流；现有 PageControl / Touch / Raw 测试须保持。代码实施后按仓库规则构建 `InkeysRepo.sln Debug|ARM64` 并运行 `InkeysHeadlessTests --no-window`。本轮只写调查文档，未运行构建或测试。

## Files Found

- `Inkeys/IdtMain.cpp`：主栏 HiMsg 回调挂接。
- `Inkeys/Inkeys/Message/Message.cpp`：笔/触摸兼容鼠标来源识别。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：主栏窗口准入、共享队列、来源会话、按钮动作。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`：白板/PPT 分页窗口笔兼容会话与业务提交。
- `Inkeys/Inkeys/Input/UiPointerInput.cpp`：Session / MouseGesture 与私有 Raw 传输。
- `InkeysHeadlessTests/message_tests.cpp`、`ui_pointer_tests.cpp`：来源识别与隔离回归。

## External References

- [Microsoft: System Events and Mouse Messages](https://learn.microsoft.com/en-us/windows/win32/tablet/system-events-and-mouse-messages)（2026-11-02 查阅）：建议 UI 指向/选择使用鼠标消息；给出上述签名及 0x80 的 Pen / Touch 区分。支持保留笔兼容 UI 路径的候选方向。
- [Microsoft: WM_TOUCH](https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-touchdown)（2026-11-02 查阅）：文档描述可包含 finger 或 pen，最低 Windows 7；不能据此反向断言“所有 pen 一定发 WM_TOUCH”，也不应写成“WM_TOUCH 绝不承载 pen”。

## Related Specs

- `.trellis/spec/native-desktop/ui-pointer-session.md`：Mouse/Touch/Pen 来源隔离、普通点击不依赖 Raw、PageControl 笔兼容捕获。
- `.trellis/spec/native-desktop/index.md`：生产 UI3 主栏与 PageControl 的线程/构建边界。

## Caveats / Not Found

- 高漫 M6 的驱动版本、Windows Ink 开关及设备实际派发消息未知；未知项不影响确定的源码准入差异，也不能被静态分析补作真机证据。
- 没有启动 GUI、Computer Use、设备操作或消息采样；没有产品代码改动，没有编译/测试结论。
- 这里未检索所有历史提交来定位首次引入过滤的日期；旧任务证据只能证明 10-17 已知并保留这一差异。
