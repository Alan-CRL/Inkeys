# Commit 审计批次 1 findings（SHA 1–200）

审计基线 H0：`8b156fca59f0337a6afc6d722941666fcf143080`。此文件只记录已由 H0 当前代码确认的问题；不能以 diff 扫描替代真机验证。

覆盖：父任务冻结清单的第 1–200 行，SHA 唯一且顺序一致，**200/200 均完成逐父完整 diff 读取和静态分类**。其中 18 个 merge 检查了两个父差异与 `--remerge-diff`（均无独立冲突解决 diff）；40 个纯 Trellis 文档/任务记录简审；其余产品、测试、工作流和二进制变动按 TSV 的 `review_depth` 分层。`audit-batch-1.tsv` 记录每项 diff 指纹、变更位置、H0 映射、结论和验证边界。该 200 项属于冻结的 611 项候选全集的一段，不能据此声称全范围审计完成。此 agent 未运行构建、GUI 或性能采样，实际运行验证仍依赖主任务记录。

## AUD-B1-001 — 正式透明呈现仍可选择两种已禁用 DWM 模式

- 状态：**confirmed，未修复/待回归**。严重性：P1 发布可用性/兼容合同；尚无真 Win7 运行证据，未宣称已复现黑屏或崩溃。
- 来源：`fc9a8b865` 引入 `kTransparentPresentModes` 中的两个 DWM 项；该提交在本审计候选集合后续行，批次 1 的 Draw3 集成与此路径形成现行发布代码。`git blame -L 87,97 H0 -- Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` 复核过来源。
- 当前证据：`Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:90–95` 自动顺序为 DComp、DwmBlurBehind2、DwmBlurBehind、ULW；`:776–786` 的初始化与 `:856–864` 的运行期恢复均遍历此表。`:770–774` 和 `:842–851` 的 `requireMode` 还允许强制 DWM。`Draw3.Host.cpp:219–220` 可从 Host 模式映射到两种 DWM。
- 触发与影响：在 Win7 SP1 + KB2670838 上 DComp 不可用，自动路径会先尝试两种已禁止的 DWM 透明模式，再到 ULW；若其中一种初始化返回成功，可能停留在不受支持的正式呈现后端，违反用户明确的 DComp/ULW 发布合同。强制 DWM 也未被正式入口阻止。实际设备结果尚待验证。
- 最小修复建议：正式自动列表仅保留 DComp、ULW；正式强制入口拒绝两个 DWM 枚举或将其限制于明确的测试构建，保持 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`。覆盖初始化、运行期恢复、强制选择和模式映射的回归，不通过关闭 FLIP 规避。
- 验证：静态调用链已确认；修复后需构建并跑无窗 presenter 合同测试，Win7 SP1 仅 KB2670838 的 HARDWARE FL11.0、有硬件但低于 FL11.0、WARP 及 ULW/FLIP 真机矩阵仍列人工门禁。

## AUD-B1-002 — 桌面自动保存索引读取没有应用层大小/条目上限

- 状态：**confirmed 边界缺失，资源耗尽表现未实测**。严重性：P2 本地资源可用性；没有证据表明可远程触发或形成代码执行。
- 来源：批次 1 的 `fafa7009` 引入桌面 UInk autosave；当前实现映射为 `Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp`。
- 当前证据：`:288–306` 的 `ReadTextFile` 仅拒绝负长度或超过 `size_t` 的文件，然后按完整磁盘长度 `std::string::resize`；`:423–441` 的 `ReadIndex` 对完整文本解析并调用 `ValidateIndex`；`:370–418` 遍历全部 `entries` 建立多个 `std::set`，没有应用层最大索引字节数或条目数。主/备索引从 `:487–493` 进入该路径。
- 触发与影响：自动保存目录内一个非常大的或条目巨多的本地 `index.json` 会让应用在索引读取/解析/校验时产生与文件大小成比例的内存和 CPU 消耗；分配异常在读取阶段被捕获，但文件可耗尽可用内存或使保存线程长时间不可用。用户文档或网络可否把文件放入此目录尚未证实；只按本地用户文件边界记录。
- 最小修复建议：在 `GetFileSizeEx` 后拒绝超出合理上限的索引；解析后限制 `entries` 数量及单字段长度，保持主/备索引损坏时现有安全退路。上限应按允许的每日保存数量和发布合同确定，不擅自清理未知文件。
- 验证：静态调用链确认；应增加隔离自动保存目录的超大/高条目索引测试，确保及时返回受控错误、不覆盖最后有效索引，不在真用户目录测试。

## AUD-B1-003 — Bar 笔型裸写与 PPT 自动接管的锁/修订合同冲突

- 状态：**confirmed 可达竞争窗口，尚未动态复现**。严重性：P1 输入状态一致性；C++ 普通字段并发读写构成 data race，实际用户可见错误取决于线程调度。
- 来源：`fc9a8b865` 的激光字段裸写、`75c54c0d` 的笔型裸写和 `ChangeStateModeToPen` 调用，与批次 1 `1cd45e575` 新增的 PPT 批注接管写入组合后形成现行风险。
- 当前证据：`Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:5229,5272–5275,5319–5322,5365–5368` 在 UI 交互处理里先写 `stateMode.laserActive` / `stateMode.Pen.ModeSelect`，后调用 `ChangeStateModeToPen`。两字段在 `Inkeys/IdtState.h:69,73` 是普通 bool/enum。`Inkeys/IdtState.cpp:888–920` 的 `ChangeStateModeToPen` 和 `ChangeStateModeToPptAnnotation` 才获取 `stateModeTransitionMutex` 并更新 revision；后者由 `Inkeys/IdtPlug-in.cpp:877–892` 的 PPT 服务循环调用。
- 触发与影响：用户切换激光/软笔/硬笔/荧光笔时，PPT 自动接管在另一线程读取和修改同一状态。Bar 的锁外写入未纳入模式 revision；竞争可产生未定义行为、笔型和模式不匹配、旧 PPT 选择覆盖新用户选择，进而令光标与 Draw3 bridge 不一致。静态证据确认竞争可达，未声称真机已复现。
- 最小修复建议：由 `IdtState` 的单一语义入口在同一锁和 revision 事务内设置笔型/laser、模式、背景色与 Draw3 同步；Bar 与 PPT 都发布意图，保留 PPT `expectedRevision` 条件。避免仅在裸写外加无副作用 setter。
- 验证：交叉入口测试并发/迟到 PPT 回调、用户快速换笔型、Draw3 bridge 和 UI 高亮一致；无窗口可测状态事务，真实 PPT/输入仍需人工回归。修复前不能把仅有 `ChangeStateModeToPen` 锁误写为整项安全。

## AUD-B1-004 — 启动预览 owner 超时停止可漏掉尚未建立的消息队列

- 状态：**confirmed 停止竞态，残留 HWND 尚未动态复现**。严重性：P2 启动/退出生命周期；触发需要 owner 线程创建窗口阶段超过 2 秒并且随后仍未建立消息队列。
- 来源：`b7edaa81c` 引入 `PreviewOwner`（`git blame H0` 已核）。
- 当前证据：`Inkeys/Inkeys/UI/StartupPreview/StartupPreview.cpp:236–239` 的 `Start` 仅等 `ready` 2 秒，超时返回 false；外层 `Start` 在 `:956` 随局部 `runtime` 销毁进入 `PreviewOwner::~PreviewOwner → Stop`。`Stop` 在 `:245–254` 若 HWND 尚为空，只投递 `WM_QUIT` 到线程，未检查 `PostThreadMessageW` 成功；`:255–261` 1.5 秒后 detach 并遗失 owner 控制。owner 线程在 `:180–234` 设置 `threadId` 后仍需 RegisterClass/CreateWindow，成功后进入 `GetMessageW` 循环。
- 触发与影响：线程被长时间延迟时，`Start` 超时，`Stop` 可能在目标线程消息队列尚不存在时投递失败；线程之后继续创建 topmost layered 预览窗并进入消息循环，而控制对象已经 detach。该进程内残留窗口的可见性/点击穿透取决于最终 alpha 和窗口状态，尚未实测，不能宣称已发生用户输入失效。
- 最小修复建议：在共享 `OwnerState` 记录 stop 请求；线程在创建窗口前后以及进入消息循环前检查，并在窗口已创建时自行销毁/退出。`PostThreadMessageW` 失败不能被当作成功停止。保留有界等待但让迟到线程也能完成清理。
- 验证：隔离进程中注入 owner 创建阶段延迟超过两段超时，检查不会留下该进程的预览 HWND/线程；正常启动、失败色帧与主动 Stop 分开验证。当前无 GUI 授权，实际残留窗口行为标记未验证。

## AUD-B1-005 — 正式启动路径保留可由任意非空环境值触发的人工失败/重启

- 状态：**confirmed 入口可达，未运行 Release GUI**。严重性：P2 本地可用性/发布配置；不构成已证实的远程可利用漏洞。
- 来源：`a0835ba3f` 引入人工失败入口，`4fcd398b7` 延长模拟阶段；H0 当前 `Inkeys/IdtMain.cpp`，已用 `git blame H0` 核对。
- 当前证据：`:180–205` 的 `RunStartupPreviewRetryFailureForManualTest` 不在 `#ifndef IDT_RELEASE` 内；`:1579` 在正常启动流程直接调用。环境读取只用 `GetEnvironmentVariableW(..., enabled, 2) == 0` 判断不存在，未要求返回的值为 `1`，所以 `0` 或超长非空值也可触发。首次路径 `ShellExecuteW` 带 `-WarnTry` 重启，第二次触发 fatal 失败对话框。
- 触发与影响：在启动预览活跃且进程继承该变量时，正常用户启动会被故意中断并重试/失败。该变量名称表明人工测试意图，默认环境通常不设置；仍违反“正式默认不保留可误触发故障入口”的发布约束。
- 最小修复建议：正式 Release 构建去掉此故障注入调用/实现，或将其完整放入专用测试构建门控；若 Debug 继续保留，必须显式匹配 `1` 而非仅检查变量存在。保持正式崩溃/重启链路独立验证，不能用此模拟失败当作真正未处理异常测试。
- 验证：分别构建 Debug/Release，静态核 Release 无该故障路径；隔离测试进程用值 `0`、长值与显式测试值验证门控，不启动用户现有进程。
