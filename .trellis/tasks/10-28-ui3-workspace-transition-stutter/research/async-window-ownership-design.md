# UI 候选帧与 Window Service 异步所有权设计

2026-10-28；用户明确批准按此方向调整实现。目标不是把卡顿转移到另一个线程，而是消除 UI 渲染线程跨 owner 等待、逐帧重复窗口管理和旧命令积压。

## 1. 证据与问题边界

生产 `PageControl::RenderSurface` 当前在每个活动帧、每个可见分页窗口上无条件执行同步 `SetBounds + Show/Hide`。PPT `Show` 还会执行整组 `RefreshOverlayGroup`。这些调用经 `Window::Submit(wait=true)` 阻塞共享渲染线程，且发生在 PresentScene 的 Draw/GetDC/ULW 计时之后。

`research/native-pagecontrol-unlimited.md` 的真实屏外四 HWND 样本（818 个四窗口批次）显示：Show/Hide 占 RenderSurface 总耗时 65.66%，SetBounds 2.33%，PresentScene 23.53%。该样本没有复现现场“只剩几帧”，不能把屏外 FPS 当真实 DWM 帧率；它只支持优先处理 RenderSurface 内部原生窗口命令热点。

普通主栏展开/收起不经过 PageControl 原生提交链。只搬移函数而不做幂等、命令合并和 Z-order 事件化，会继续积压旧帧，不属于可接受方案。

## 2. 所有权目标

### UI RenderPipeline 只保留

- 读取一致业务快照，计算 UI 布局、动效和命中候选；
- D2D 资源、绘制、GetDC/ULW 等渲染必要事务；
- 发布固定数据的窗口管理候选；
- 非阻塞读取已经发布的 Window Service 回执；
- 根据动画和 pending/receipt 状态返回 `Continue / Retry / Idle`。

“UI 不阻塞”指不跨 Window owner 等待，不禁止 D2D/GetDC/ULW 自身必要的同步调用。ULW 仍属于渲染呈现；窗口策略和真实 HWND 状态裁决不属于 UI。

### Window Service 负责

- 在现有 overlay owner 线程执行真实 `SetWindowPos / ShowWindow / Hide`、显隐、Z-order 和必要回滚；
- 管理 latest-wins 候选队列、实际窗口代际、真实 RECT/visible/generation；
- 只有实际状态变化才提交 bounds 或显隐；
- 只在明确 promote、workspace/topology/band/owner 变化或失败恢复时管理 Z-order；
- 发布包含实际结果的完成/失败回执。

不新增第二套渲染线程或窗口 owner。复用现有 Window Service 消息循环、命令身份、取消和恢复责任。

## 3. Candidate / Receipt 合同

候选至少包含：`revision`、`frameSerial`、`role`、目标 `bounds`、`visible`、必要的 `promote`、`workspaceRevision`、`windowGeneration`。多窗操作还需 pair/group 身份和允许提交的 mask。

回执至少包含：对应 `revision`、实际 `bounds`、实际 `visible`、完成阶段/错误、`nativeGeneration`。多窗操作必须报告实际完成 mask，不能把半对成功当成整组成功。

使用“原子 revision + 固定快照/既定 mailbox”保证一致读取，不能只共享一个没有版本和完成状态的 RECT。写方先完整写 payload，再 release 发布 revision；读方 acquire 后复核身份。现有 seqlock/mailbox 能复用时优先复用，不另造通用框架。

### latest-wins 与 coalescing

- 每个 role/pair 只保留最新未执行候选；连续位置变化可覆盖，不能把每一帧排队后依次播放。
- 正在执行的原生命令用 revision/generation 标识；迟到完成不得覆盖更新候选或 committed 状态。
- 只有当前候选和实际回执匹配时，UI 才提交对应 damage、命中边界和 Page UI-ready。
- 回执未到、失败或身份不符时保留 pending/damage/旧命中，不阻塞等待，不提前开放输入。
- 新 revision、取消、设备/窗口代际变化按现有规则废弃旧候选；失败恢复预算和源补偿不能为提速而削弱。
- 无新像素但存在 native pending 时可用 `Retry` 让调度器后续消费回执；不得忙等、条件变量等待或在 render callback 内 `Submit(wait=true)`。

## 4. Z-order 事件化

普通 `Show` 不等于 promote，也不得默认触发完整 `RefreshOverlayGroup`。

- `SetBounds`：只在目标矩形与最后成功/在途状态不同且仍需提交时执行。
- `Show/Hide`：只在真实可见性目标改变时执行，重复状态幂等。
- PPT 内部 promote：由有效 Pointer Down 等明确交互触发；现有 `PromotePptWindow` 是独立语义入口。
- 工作区、owner/topology/band/topmost 变化和真实恢复：由 Window Service 按策略管理整组顺序。
- 不得把普通逐帧 `Show` 继续异步投递成逐帧全组重排。

## 5. 分阶段迁移范围

### P1：PageControl RenderSurface

保留动画、布局、ConfigureSurface、PresentScene 和 ULW。把其后的同步 `SetBounds + Show/Hide` 改为 candidate/receipt；成功回执前不得推进对应命中、ConsumeDamage 或 Page UI-ready。普通 Show 与 PPT promote 分离。

屏外 hidden performance fixture 必须继续走生产 RenderSurface 和真实 Window Service，用于同口径 A/B，不得退回 `CreatePageControlOffscreenRenderCallback` 代替生产路径。

### P2：Whiteboard Freeze

`RenderBackground` 的同步 Hide/SetBounds/Show 和真实身份查询迁移到同一机制。`BackgroundPrepared` 不得再由 UI/协调器直接以 `IsWindowVisible/GetWindowRect` 作为 ready 判定；使用 Window Service 的真实回执。首背景、inactive ACK、最终显示及源补偿合同保持不变。

### P3：direct move 与主动 promote

- PageControl `MovePairWindowsDirect`：输入侧只发布 latest-wins pair 候选；Window Service 原子提交并回报 pair mask。
- PPT Pointer Down `PromotePptWindow`：改为非阻塞 promote 请求，保留角色/代际和顺序语义。
- Bar 主按钮直移：保留现有原子 translation、快速续接和像素/位置一致性，逐步迁移直接 `SetWindowPos`；不能出现旧位图在新位置或新命中先于实际呈现。

### P4：StateMonitoring 协调

`PrepareWorkspaceWindowPolicy / CompleteWorkspaceWindowPolicy` 最终改为事件/回执驱动：request → native prepared → target/background ready → native completed → Committed。不得把 desired 当 committed，不得跳过 Draw3 首帧、输入 fence、PPT session/target identity 或背景 ACK。

P1/P2 应先形成可独立验收的小阶段；P3/P4 不得为了扩大一次 diff 而破坏已验证的 P1/P2。生命周期初始化/销毁可保留必要同步，但不能把热路径同步等待视为合理常态。

## 6. 必须保持的正确性

- 公共按钮、统一 renderer、单一 UI render owner 和现有 Window owner 不变；默认 WARP、光效、动画曲线、字体和 DPI 行为不降级。
- pending/desired 不冒充 Committed；旧 ACK、旧 HWND/generation 和过期 token 不提交。
- 真实像素、bounds、visible 和必要 group 完成前，不发布对应命中、输入开放或 UI-ready。
- 快速反向从当前视觉值续接，不播放积压旧帧，不重启动画。
- 失败保留旧成功快照和 damage，恢复不吞错、不减少现有失败预算，不绕过现有 recovery barrier。
- 原生窗口失败需要回滚时仍由 Window owner 执行；UI 不直接恢复其它窗口。

## 7. 验证与证据门禁

1. 先为 candidate coalescing、迟到回执、失败、取消、代际变化、pair 半完成和无阻塞建立确定性红测，再实现。
2. 可控延迟 Window owner 时，其它 RenderPipeline 客户端仍持续推进；同 revision 不重复投递；完整 ACK 前不提交命中/UI-ready。
3. 复用 `native-pagecontrol-unlimited` 做同机同口径 A/B：报告 batch/unlimited FPS、RenderSurface 阶段、Window Service 队列深度、合并/过期命令数、candidate→receipt 延迟和真实成功呈现间隔。不得只报 callback 变快而忽略实际呈现落后。
4. 六向、翻页、PPT/白板布局 morph、任务栏自动升起、快速反向、拖动/吸附、Pointer Down promote、最小化恢复、DPI/显示器变化均回归。
5. 故障矩阵覆盖 owner 延迟/失败、ULW 成功但 native 失败、pair 第二窗失败回滚、取消、迟到 ACK、设备/窗口代际变化。
6. 完整 `InkeysRepo.sln Debug|ARM64`、相关 hidden/offscreen/state/headless 测试通过；保留原始退出码和 EXE 哈希。真实 Office/WPS/DWM/触控不由屏外结果外推。
7. 更新 `.trellis/spec/native-desktop/rendering-and-ui.md` 中允许 render 同步等待和 UI 直接 pair SetWindowPos 的旧合同，并同步 whiteboard/canvas/ppt 相关规范。

## 8. 禁止的实现捷径

- 仅把整个 RenderSurface 或全部窗口命令扔到另一线程，仍逐帧执行完整 Z-order；
- fire-and-forget 后直接当成原生成功、提前 ConsumeDamage/开放命中；
- 只删除 RefreshOverlayGroup 或所有 Show，不保留真正 promote/workspace 顺序；
- 每帧排队全部历史 bounds，或以最新状态覆盖正在执行的旧命令而没有代际回执；
- 新增平行窗口管理器、第二套三态状态机或额外渲染 owner；
- 通过禁动画、禁光效、降低帧率、跳过节流或放宽失败预算制造表面收益。
