# Audit batch 3 — 审查结果与边界

- 分配范围：父任务 `audit-coverage.tsv` 数据行 401–611，共 211 个 SHA；终点 H0 为 `8b156fca59f0337a6afc6d722941666fcf143080`。
- 已读取任务规划、审计设计、相关 spec 索引并核对 211 行 SHA/标题和归属；**211/211 SHA 的实际 diff 已读**。其中 124 个 H0 可达产品代码节点和 5 个 H0 merge 做静态语义复核；51 个 H0 纯文档/任务状态、24 个未合入分支候选、7 个历史 Draw3 导入祖先分别分类记账。原 B3-INPUT-001 跨应用吞键假设已由编译链排除；另发现 B3-INPUT-002 颜色面板键盘 producer 静态缺口，运行待验证。未运行任何构建、GUI 或性能采样。
- 行 401–407 为 2025-11 的 Draw3 源仓历史节点；408–466 为 2026-08-10 后 H0 可达节点；467–587 为较早日期但被保守候选集合纳入的 H0 可达节点；588–611 为 H0 不可达的近期分支候选。是否应纳入最终分母须逐项结合拓扑和集成差异判定。
- 211 个 SHA 的实际 patch 均已逐行流式读取，merge 均按每个父提交单独读取，补丁 hash、改动路径、hunk 和 H0 直接路径映射写入 `audit-batch-3.tsv`。静态审查没有替代当前工作区动态测试；历史导入的产品等价映射和未合入分支未来重审仍是明确缺口。
- 已人工查看 401–466 的代码变更摘录和 467–611 的逐项摘要，重点复核当前 `Display.cpp` 订阅发布、`Window.cpp` 同步命令/隐藏窗口、`Bar.Interaction.cpp` 拖动与键盘路由、`Bar.DirtyRegion.h` 边界、SVG/PNG 缓存、UI3 设备/设置和配置迁移。唯一静态功能缺口为 B3-INPUT-002；B3-INPUT-001 已排除，尚无经实际触发证实的严重缺陷。
- merge `git show --remerge-diff` 因 `.git` 只读无法创建临时对象目录而失败；已改用每父 diff 和 `git show --cc` 查看合并解决内容。529、562 的 combined diff 为 journal 合并；444、447、574、611 的 combined diff 为空。此处没有改动 Git 状态。
- 协同期间按主 agent 要求两次暂停 Git 扫描以保证 Draw3 基准独占；两次恢复后才继续。没有与基准并发运行 Git 扫描。

## B3-INPUT-001 — “H0 颜色面板跨应用吞键”假设被排除

- 历史来源：本批 SHA `2d1223fe`（数据行 524）曾在 `Inkeys/IdtDrawpad.cpp:73-76` 的低级键盘 hook 中调用 `TryQueueColorPickerKeyboardInput`，成功后 `return 1`。仅看仍保留的源码会得出吞键结论。
- H0 编译边界反证：`Inkeys/Inkeys.vcxproj:895` 把 `IdtDrawpad.cpp` 登记为 `None`；`:905` 实际编译 `IdtDrawpadFacade.cpp`。后者 `:42-50` 的 `DrawpadHookCallback` 仅调用 `CallNextHookEx`，`DrawpadInstallHook()` 为空。对当前第一方 Inkeys 源码搜索 `WH_KEYBOARD_LL` 和 `TryQueueColorPickerKeyboardInput`，唯一安装与唯一调用均在未编译的旧文件。
- 结论：H0 正式产品不会通过该旧 hook 路径跨应用吞掉 WASD/方向键。原报告的“已确认失败/高严重度发布阻塞”已撤销，状态为**假设被排除**。没有进行 GUI 动态验证；此结论严格限定于 H0 的当前编译链。
- 审查教训：历史 diff 的调用链必须继续追到 `.vcxproj` 编译入口；保留源码不证明发布产物可达。

## B3-INPUT-002 — 颜色面板 WASD/方向键生产者缺失（静态功能缺口，运行待验证）

- 当前路径：`Bar.Interaction.cpp:6779-6805` 仍导出键盘入队函数，`:2006-2057` 仍实现颜色面板 2 DIP 步进和最后 KeyUp 持久化，`:2233-2249` 消费 Bar 的 `EM_KEY`；但当前编译的 `IdtDrawpadFacade.cpp:42-50` 不再安装键盘 hook，也不调用入队函数。第一方代码中未找到另一个编译中的调用点。
- 正常窗口合同：`IdtMain.cpp:1718-1719,1760-1770,1832-1833` 给 Bar `WS_EX_NOACTIVATE`；`Bar.Interaction.cpp:505-507` 返回 `MA_NOACTIVATE`；`Window.cpp:1001-1003,1397-1402` 用 `SW_SHOWNOACTIVATE` 显示普通 overlay。普通用户点开同窗颜色面板并不会让 Bar 获得键盘焦点。外部程序强行聚焦或其他未发现的事件注入不计作正常产品入口。
- 影响与状态：原颜色选择器任务的方向键/WASD 调节设计，在 H0 编译产品中缺少可确认的键盘 producer；归类**中等严重性静态功能缺口，实际键盘操作需要人工/隔离 GUI 验证**。这不是当前可利用的跨应用拦截。
- 最小修复建议：若首发仍要求该键盘能力，在唯一编译中的输入 producer 中接入 Bar 队列，并在决定抑制原按键前核对 Bar 交互归属。Bar 本身 non-activating，因此不能只检查 Bar 前台；需要可交互区域的指针/明确会话归属。已接纳的 Down 后，即使指针或焦点转移，其 Up 仍须归还同一会话以清除 key mask，且只在成功入队时抑制原事件。不要直接把旧 `IdtDrawpad.cpp` 加回编译。
- 回归建议：headless 测试调用生产判定和真实队列/处理逻辑，覆盖面板关闭、模式不符、外部应用前台且指针离 Bar、Bar 指针归属、重复 Down、最后 Up 恰好一次提交、Down 后焦点转移的 Up 闭合、队列拒绝及橡皮/PPT 优先级。静态门禁检查唯一编译中的 producer 确有调用；GUI 授权后在隔离进程复验“外部编辑器输入不丢，回到 Bar 可用键盘改色”。
- 处理状态：静态缺口已确认、运行效果未验证；本审查 agent 只拥有 research 文件，已交主 agent 处理。

## 未来恢复 hook 的条件风险

- `Bar.Interaction.cpp:6783-6790` 的橡皮分支有前台/指针门禁，`:6791-6805` 的颜色 movement 分支没有。若未来把旧全局 hook 原样重新启用，颜色面板保持打开时可重新出现跨应用吞键；这是**条件风险**，不是 H0 实际缺陷。新的 producer 应统一归属判定并保留 KeyUp 会话对称性。

## 高风险提交的当前实现追踪摘要

下表是**静态补丁与 H0 当前实现的语义审查**；不把这些条目当作 GUI、Win7 或性能实测通过。完整的每父补丁 hash、变更文件、hunk 和简短增量在 `audit-batch-3.tsv` 各 SHA 行。

| 数据行 / SHA | 静态追踪重点 | 结论边界 |
| --- | --- | --- |
| 413 `1a5aa5c8`、415 `47acefc5` | 显示快照串行发布、代次递增、订阅卸载；当前 `Display.cpp:482-555,754-840` 中回调在锁外执行，订阅代次和 activeCalls 在锁内处理。 | 未见该两笔差异中的确认死锁；多屏/DPI 仍需真机。 |
| 417 `7acc74c6`、427 `df11bcd1`、429 `d3250e05` | UI3 共享设备/RenderScheduler、device epoch 与 PPT 客户端；当前由 `RenderPipeline` 统一所有权，Bar 在 epoch 变化时重建。 | 构建和 device-lost/Win7 运行由集成与兼容专项验证，静态不宣称成功。 |
| 420 `6ded57c3`、430 `12a3be1a`、431 `ba7ee145` | 直接拖动相位、动态 HWND viewport、入队坐标冻结；当前 `Bar.Interaction.cpp:6028-6075` 使用 phase CAS，`IdtMain.cpp:1793-1804` 经窗口服务把 Bar 消息转布局坐标。 | 拖动、快速反向和呈现事务需 GUI；静态未确认另一严重缺陷。 |
| 422 `908a85ed`、423 `48f3e721` | 退出时隐藏窗口、offSignal 唤醒、等待循环可中断。`CloseProgram/RestartProgram` 当前先 `HideAllUserWindows`，后设置退出信号；`Window.cpp:1121-1142` 的 `Submit` 是同步 future 等待。 | 若 owner 线程阻塞，关闭可等待；仅为风险假设，尚无可复现死锁证据。崩溃/重启专项应覆盖。 |
| 437 `664ab92e`、440 `41535eaf` | 系统触摸生成鼠标副本过滤、HiMsg/Window Service 迁移和生产窗口入口；当前 `IdtMain.cpp` 为 Bar/PPT 绑定消息，`Message.cpp` 区分触摸与笔/真鼠标。 | 真笔、触摸/鼠标混合输入无 GUI 证据；大量第三方搬迁不计为新业务逻辑通过。 |
| 453 `59cc6ee9`、454 `506dd263`、455 `963db5e5` | UI3 dirty 旧/新区、灯光包络、提交失败后的保留；当前 `Bar.DirtyRegion.h:27-250,301-365` 保留事务式 pending/commit。 | 静态结构和已有 headless 入口存在；局部 ULW 实际像素边界需真机。 |
| 466 `7fbf5720`、506 `e86b64f0`、517 `d91868ed` | 大型 UI3 性能迁移、SVG 几何/位图缓存、PNG 预乘上传；`Bar.UI.cpp:225-230,365-489` 在内容变化时 ResetCache，`Bar.Rendering.cpp:2723-2787` 按尺寸/颜色/动态放大决定重栅格，PNG 只从内嵌资源初始化。 | 不能由“存在缓存”推断性能胜出；GPU/ULW 和长跑资源趋势另测。未确认可远程喂入 PNG 的安全边界。 |
| 524 `2d1223fe` | 历史低级 hook → 颜色选择器键盘路由；H0 的旧 hook 文件不编译。 | 跨应用吞键假设被排除；B3-INPUT-002 为键盘 producer 静态缺口，运行待验证。 |
| 529 `b34ec8b0`、562 `79c3afba`、574 `8c43a6da` | 每父 diff 已读取；`git show --cc` 显示 529、562 的 combined 解决仅为 Trellis journal 合并，574 无 combined 内容。 | 合并引入的各支线代码按其自身 SHA 审；无独立产品冲突解决改动证据。 |
| 532 `b5e4f45c`、534 `c41e3727`、542 `8d172fea` | Bar 启动时 divider 列表、A1/B/A2 配置迁移与荧光笔粗细预设；`Other.Config.cppm` 使用锁保护配置序列快照，当前按钮最终经 `SetPenWidth` 语义入口。 | 配置迁移、异常 JSON 与跨入口等价须由统一状态专项测试；静态未确认第二个数据损坏问题。 |
| 559 `2a64f9c9`、582 `cde5627c` | 582 曾以极高 speedRate 结束全部 UI 动画，559 后续 diff 明确撤销该策略，保留仅在落笔时暂停第三鼠标光；H0 `Bar.RenderLoop.cpp:989,1216,5327` 从配置读取动画速率。 | 历史已修，不能把 582 的旧全 UI 静默当作 H0 的性能措施；真正光影耗时仍待测。 |
| 577 `88091b2f` | 设置窗 D3D9→D3D11、shader 资源和 WARP；当前设置客户端共享 UI3 WARP epoch，`.vcxproj` 有三架构 ShaderModel 5.0 项。 | Win7 SP1+KB2670838 的 WARP/FL11.0 和产物资源必须单独验证。 |

## 历史导入、未合入分支与工具限制

- 行 415、429、440、447、582、611 的补丁或分支差异包含旧 `IdtDrawpad.cpp`；本轮复核 H0 `.vcxproj` 后已在逐项 TSV 标出该文件为 `None`，实际产品使用 `IdtDrawpadFacade.cpp`。这些行中与旧键盘 hook/RTS 相关的改动不能直接映射为 H0 运行行为。
- 行 401–407 是 2025-11 的 Draw3 独立 demo/第三方历史祖先。各实际补丁已读；404、406 分别涉及 967、1610 个路径与大量第三方/源代码，不把旧 demo 运行结果外推到 `Inkeys/Inkeys/Drawing/Draw3`。主产品导入点和当前生产链路需与其他审计批次交叉核对。
- 行 588–611 均 H0 不可达。588–592 属 `theme`，593–606 属 `fluent`，607–610 属 `feature/recognition`，611 为 `origin/canary` merge。各差异已审为**分支候选**；不计作当前发布线代码，未来合入前须按最终基线重审。610 引入 OpenCV/vcpkg，仅分支风险，不能说 H0 已引入该依赖。
- `.git` 对本 runner 只读，`git show --remerge-diff` 需要创建临时对象而失败。已逐父运行 `git diff parent merge` 并运行只读 `git show --cc` 查看合并特有差异；失败不视为 merge 审查通过证据。
