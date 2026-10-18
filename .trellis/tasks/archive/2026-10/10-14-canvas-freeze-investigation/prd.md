# 画布卡死调查与 F1–F4 修复

## Goal

修复已取得触发证据的 F1–F4，分别消除诊断背压阻塞、PPT 冷槽暂时读取错误误分类、速率橡皮静止后的 Move 漏唤醒，以及 Settings 隐藏后鼠标捕获残留。四项独立验收并进行整体验证，保留现有墨迹、历史、输入终态和生命周期合同。用户原始画布冻结的共同根因仍待确认，四项通过不替代现场归因。

## Background

- 用户报告 UI 可操作但画布不响应，通常需重启；步骤包括软笔绘制、设置开关、Inkeys 文件管理器、选择往返，另有设置后空白画布和设置中仍能重启的反馈。
- 原始视频 `D:/Personal/Downloads/10月11日.mp4` 约 79.95 秒，1920×1080/30 FPS，重点约 0:50；视频仅是独立样本，录于 2026-10-11，不代表当前 HEAD 或所有用户。
- 2026-10-15追加要求已将调查扩大到输入、绘制、窗口、业务状态和持久化等待；随后要求深入其他窗口关联。具体新反馈明确未开启控制台输出，因此不能直接归因 F1。
- 具体反馈用户输入设备未知；总体报告触摸居多，也有鼠标，不能据此断言同一故障现场已证明两种输入一起失效。
- 代码基线为 `bugfix/draw3`、HEAD `5d319bbf5c520dec97dcb37ad2fcc7e26d029bbb`；开始调查时工作区干净，至修复规划阶段产品源码仍未修改。
- 2026-10-15用户明确提出“先把1234修复了”。原“仅调查、不修复”限制对这四项解除；具体实施方案经用户随后“批准，并开始修复”明确批准，父任务与四个子任务已执行 task.py start，进入实施。

## Requirements

### R1 / F1：诊断背压不阻塞画布及退出

已证实 Cursor 开启、stdout 活着但停止读取时，完整 HEAD Draw3 线程停在同步 WriteFile；两次触发、三次栈采样、设置页仍响应，恢复读端后同进程继续绘制。生产入口：`Draw3.Diagnostics.cpp:48–55,574–617`，`Draw3.DrawingController.cpp:12607,14454`，启动重定向合同 `IdtMain.cpp:295–325`。

修复本次绘制/输入链实际可达的 stdout/stderr 标准流背压风险；生产者有界入队、允许诊断溢出丢弃，输出消费者可停止，输出端停读时正常绘制和退出均继续。保留现有诊断开关与字段，不改变普通业务日志策略、墨迹或输入队列。相关诊断关闭且无有效继承输出端时，不分配输出队列或启动输出线程；有继承输出端时保留既有普通错误诊断。

### R2 / F2：PPT 暂时读取失败进入既有退避

真实 worker/UInk/Bridge 加当前原码 gate 实验已证明合法已保存冷槽文件暂时独占时 IoError 被误归 SourceChanged，进入 manualOnly，解锁后同目标/工具操作不自动重读。来源：`Draw3.PresentationAutoSave.cpp:1568–1575`，重试/准入：`Draw3.DrawingController.cpp:808–924,12061–12077,12262`，同目标去重 `Draw3.Bridge.cpp:75–78`、`Draw3.Host.cpp:1294–1304`。

保留暂时 IoError 并复用当前四次退避预算（250/500/1000/2000 ms）；在预算内解除文件占用后同一目标自动恢复加载与输入。真实 SourceChanged、外部身份冲突、损坏/无效数据继续保护原文件和墨迹。预算耗尽后沿用既有受保护终态；不新增“忽略旧墨迹继续写”的入口或新恢复界面。

### R3 / F3：速率橡皮静止后移动唤醒

真实 ContactInput/SpeedEraser 加原控制器等待循环已证明 Touch Move 更新 mailbox 而未 wake，idle while 重复等待同 generation；Up/Cancel/control wake 可解除，并非所有工具永久失效。入口：`Draw3.ContactInput.cpp:771–801`、`Draw3.DrawingController.cpp:14551`；隐藏注入 `Draw3.Host.cpp:2111` 的额外 ControlWake 会掩盖故障。

修复生产 Move 与 idle 消费之间的通知缺口，静止后重新移动即可被消费，不需要先 Up 或切工具。保持 mailbox 合并、command boundary、终态和空闲 CPU 行为；回归必须覆盖没有隐藏注入额外 wake 的真实生产路径以及 Touch/Pen/Mouse。

### R4 / F4：Settings 隐藏时清理自己的捕获

无诊断完整 HEAD 实测：Settings 内鼠标 Down → 按住期间 Alt+F4 → 正常 Up，隐藏设置仍捕获，Bar 橡皮点击持续无效；同进程仅 WM_CANCELMODE 即恢复点击；同场新笔仍可用。原码：`Setting.cpp:610–617,647–658,8860–8869`、`imgui_impl_win32.cpp:698–705,719–721`、`Window.cpp:1733–1741`。

在 HWND 所属线程的隐藏/会话结束边界收束仍属于 Settings 的捕获与输入状态。保护其他 HWND 已获得的新捕获、ImGui context 生命周期和既有锁序；普通 X 关闭、重开及最终退出也须正常。不把任意 capture/失焦转换为 RTS contact Cancel。

### R5：集成、变更与协作约束

- 既有任务作为父任务，四个子任务分别拥有对应需求、回归及回退；函数依赖和执行顺序显式写入各子任务。
- 授权仍包括隔离副本的原生程序化 GUI 验证；不使用 Computer Use，不修改用户安装目录、原视频或原配置。
- 最小 diff；保留目标文件编码/BOM/CRLF，关键逻辑添加适量中文注释；不顺手重构、升级依赖或更换构建系统。
- 完整 InkeysRepo.sln Debug|ARM64、原生 ARM64 MSBuild 与同 invocation PATH 规范化；构建至少允许五分钟，按首个有意义错误定位。源码变化后重新构建，不沿用旧二进制作修复证据。
- 不创建 commit、不 push、不写远程；实施/检查按 Trellis 子代理默认方式执行，主会话协调共享文件、集成、规范和验证。

## Acceptance Criteria

- [x] H1：视频形成时间线，当前源码调用链和字段含义均有研究证据，历史版本不冒充 HEAD。
- [x] H2：F1 完整产品故障/恢复；F2/F3 生产组件/原码片段实验；F4 完整产品捕获残留与同进程恢复，证据范围已交叉审阅。
- [x] H3：三轮正常完整产品鼠标窗口操作、十八组真实 OS/RTS 边界留证；完整产品合成触摸无绘制正对照，明确未通过触摸窗口验收。
- [x] AC1：F1 同一停读条件下新笔迹、工具响应和正常关闭继续；输出队列有界、过载/关闭可丢诊断，正常 sink 仍输出。
- [x] AC2：F2 暂时文件占用返回 IoError，预算内解锁后无需目标变化或重启即可加载原墨迹并准入；修改源/损坏/冲突不自动变为空白可写。
- [x] AC3：F3 idle 后 Move 能唤醒消费；覆盖入睡前/后竞争、Touch/Pen/Mouse、多个接触、command boundary、Up/Cancel/退出；静止不忙轮询。
- [x] AC4：F4 强触发后捕获已释放、Bar 点击成功；重开/X关闭/退出正常，隐藏时属于其他窗口的捕获不被释放。
- [x] AC5：完整 Solution Debug|ARM64、InkeysHeadlessTests --no-window 及匹配四项的生产回归通过；GUI 同条件复测与二进制 hash 留存。
- [x] AC6：四项独立检查及最终 diff/格式/跨层合同复核完成；测试、诊断和真实物理触摸/Win7/多屏的覆盖边界逐项报告。
- [ ] 后续现场归因：确认用户原始报告与已修触发器是否一致；保留为独立调查目标，不阻断四项已证缺陷修复交付。

## Out of Scope

- 未证实的 DXGI/owner 死锁、显示上下文等待或系统隐藏恢复方案；定时重启 RTS、自动清队列等猜测性修复。
- 合成触摸在完整产品 Pointer→RTS 的环境差异归因；这项现象在窗口操作前就存在，另行调查。
- 调整 PPT 自动重试次数/时长或新增手动恢复 UX；改变持久化身份、文件格式、COM 接口及平台支持承诺。
- 全仓日志框架或输入/窗口架构改造，未经请求的格式整理、依赖升级、commit/push。
- `INKEYS_DRAW3_TAIL_TRACE` 专用轨迹文件分支（`Draw3.StrokeGeometry.cpp:54–216`）的同步存储协议；它默认关闭，独立于本次标准流停读触发器，不把本轮F1修复宣称为全部磁盘诊断异步化。

## Evidence and Deferred Coverage

权威调查分别为 `research/expanded-investigation.md` 与 `research/window-interaction-investigation.md`；所有原始实验和构建输出位于忽略的 `Build/CanvasFreezeInvestigation/`。本机 Windows 11 ARM64，真实物理触摸/笔、Win7 和多屏硬件验收未完成，不能由合成触摸、无窗口测试或本机鼠标结果外推。

## 最终修复验收状态（2026-10-15）

F1–F4 实现及本机针对性验收完成。AC1/AC4 的完整产品范围为本机 ARM64 鼠标；F1 stdout 满管在 GUI 验证，stderr 独立满管由生产 CLI 验证。AC3 的 Touch/Pen/Mouse 为真实 coordinator 的逻辑设备枚举，32 组调用与 Run 共用 idle helper，不能外推物理触摸/笔。F4 精确会话交错由生产 SessionState 确定性测试覆盖。完整默认 HWND 套件仍 exit1/11 fail，未作为 AC5 中四项针对性回归的通过证据；预置顶 A/B 不替全部失败归因。详见 research/fix-validation.md。

## 2026-10-17 后续调查优先级

用户已授权并完成 F1–F4 commit（68cd297d），owner/Z序 11 项失败暂缓。优先调查新反馈“PPT 翻页后画布卡死、UI 仍活”；软件类型和能否继续翻页未知。本轮仅调查，已有冷槽 IoError 回归不替代此现场；新证据与后续修复建议见 research/ppt-turn-investigation.md。


## 2026-10-17 本轮修复授权（覆盖先前调查限定）

用户明确要求继续修复原 11 项窗口断言失败及 PPT 翻页后画布卡死，并审计画布拒绝输入的恢复路径。窗口问题不再暂缓；已提交 F1–F4 的 commit 为 68cd297da63642d58fd755da20ec4c8bac42bd1a，本轮无新增 commit/push 授权。

本轮严格禁止操控电脑、启动应用和创建测试窗口（包括不可见 HWND）。只允许源码分析、编译、无窗口测试；旧 GUI 通过记录不作为新补丁运行验收。目标是修复有证据的无法收敛状态，保留身份、未加载墨迹和保存安全保护；不能用无条件开放输入掩盖失败。

验收：PPT 原始 raw/descriptor 交错在生产共享逻辑中收敛；窗口 owner/topmost 缺陷定位并最小修复、编译/纯逻辑验证，原 11 项实际窗口回归明确待授权；逐项列出输入拒绝入口/恢复条件和剩余外部阻塞风险。新增确定性缺陷有针对性无窗口红绿测试。


## 2026-10-18 收尾授权更新

用户现已开放电脑操控，要求完成剩余任务并收尾。覆盖先前禁止窗口的限制；允许完整HWND测试、隔离产品GUI、真实PowerPoint测试。无新增commit/push授权。窗口测试/GUI串行，由根会话统一操作；沿用现有补丁和证据，不复写用户改动。最终验收以重新构建产物为准。
