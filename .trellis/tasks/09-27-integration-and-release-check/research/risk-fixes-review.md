# F-006 / F-008 / F-010 独立风险修复复审

审查日期：2026-09-27。范围为当前未提交的 `Draw3.TransparentPresentation.cpp/.cppm`、`Draw3.Host.cpp`、`Draw3.HiddenWindowTest.cpp`、`Draw3.AutoSave.cpp`、`StartupPreview.cpp` 与相关 Desktop 测试；追读 `Draw3.GraphicsInitialization.cpp`、`Draw3.DrawingController.cpp`、`IdtMain.cpp` 及 `TestResults/release-hardening/` 现有日志。只读审查，没有运行 HWND/GUI、构建或测试。以下行号是审查时工作区。

## 结论总览

| Finding | 当前代码结论 | 已有运行证据 | 未验证边界 |
| --- | --- | --- | --- |
| F-006 DWM 透明模式禁用 | 源码已修复：自动、强制和恢复均只可到 DComp/ULW；ULW 的 FLIP_SEQUENTIAL 保留 | `work4-risk-fixes-debug-arm64.log` 完整 Debug ARM64 Solution 构建成功、14 warning/0 error；隐藏 HWND 未运行 | Win7 SP1 仅 KB2670838 的 HARDWARE/WARP、FL11.0 有/无、ULW 透明/输入/resize/device-lost 均需目标机实测 |
| F-008 Desktop index 资源边界 | 源码已修复待更广验证：读入、entry 数和序列化输出均设上限；超限不替换索引、不删已有 UInk | `autosave-index-red-isolated-test.log` 曾失败；`autosave-index-final-isolated-test.log` 为 `Desktop UInk index bounds tests passed` | 本机完整 Desktop suite 仍失败，不能以专项通过替代备份/普通多次提交成功 |
| F-010 PreviewOwner Stop | 源码时序修正：sticky stop 标志覆盖创建前、创建后和阻塞消息循环前的窗口 | Debug ARM64 构建成功；无专门 Stop 交错运行测试 | HWND 迟到创建、Stop 超时、真实透明 topmost 窗口清理需隔离 GUI/故障注入 |

## F-006：presenter 与 Win7

- `Draw3.TransparentPresentation.cpp:90–93` 的自动数组只含 DComp 和 ULW；`Initialize:777–795`、`RecoverFromRuntimeFailure:843–875` 都用该数组。`allowDirectComposition=false` 直接从 ULW 开始。`TryInitialize:668–680` 还在样式回调和 swap-chain 创建前拒绝两种 DWM/未知 enum，阻止直接调用控制器强制模式及恢复绕过。`Host::Start:998–1004` 又在 `bridge.Reset()`/`AttachExternal()` 前拒绝强制 DWM；因此被拒绝的 Host 启动不触碰 HWND tablet property、样式或 AutoSave worker。`HiddenWindowTest:2178–2207` 新增样式、tablet property、运行状态不变断言，但未运行。
- `ShouldPreconfigureNoRedirectionBitmap()` 使用运行时 `dcomp.dll`/导出探测 (`TransparentPresentation.cpp:109–117,466–470`)。无 DComp API 时创建 legacy 主 HWND，自动模式跳过 DComp 后尝试 ULW；隐藏套件现在按该能力分支，仍保留 legacy ULW 项。`CreateSwapChain:614–641` 的 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`、双缓冲和 `DXGI_SCALING_STRETCH` 未变；未出现 bitblt 回退。`GraphicsInitialization.cpp:53–99` 分别对 HARDWARE、WARP 尝试 11.1/11.0，`E_INVALIDARG` 时用仅 11.0 重试。上述是源码可达性，不是 Win7 设备/Preset/Present 成功证据。
- **保留的条件性发布风险，非本次 diff 新引入**：DComp 在运行期持续失败且主 HWND 创建期 `WS_EX_NOREDIRECTIONBITMAP` 不可清除时，`RecoverFromRuntimeFailure:820–875` 在同一 HWND 上重试 DComp→ULW；ULW 样式提交可能失败。`Draw3.DrawingController.cpp:6309–6320` 在恢复失败后 `RequestExit()`，`IdtMain.cpp:1910–1928` 只在**启动**失败时重建 HWND 链；正常运行期无等价重建。故该条件一旦发生，Draw3 输入可能退出。这一触发尚无实机/故障注入证据，不能宣称当前用户已遭遇；也不能据本次 DWM 禁用宣称 device-lost 全链路已修。原独立分析见 `09-27-commit-and-security-audit/research/dwm-disable-review.md` 的 R-DWM-01。
- 历史 DWM enum/presenter 实现仍保留在源文件，但当前正常入口不可到达；这遵循最小 diff。隐藏测试含 HWND/合成，未获 GUI 授权时不得将其静态存在或构建成功记作运行通过。

## F-008：index 边界、备份与异常

- `AutoSave.cpp:55–56,290–306` 在分配前拒绝超过 16 MiB 的 index；`ValidateIndex:376–381` 拒绝超过 32768 entries；`CommitIndex:521–541` 在 append 前检查 entry 上限、序列化后检查字节上限。原有顺序仍是 UInk durable commit 后写 index；超限时返回 Failed、保留旧 index/backup 和已提交 UInk，不将截断列表替换为有效索引。相同请求若已在有效索引中，可在上限检查前通过幂等身份核验，避免误伤既有条目。
- `ReadIndex:427–452` 给 JsonCpp 设 `stackLimit=64` 并将 `Json::Exception` 视为 Invalid；其他异常由外层 `ProcessRequest:627–695` 的 catch-all 收敛为 Failed。主索引 Invalid 时 `CommitIndex:499–511` 仍验证 backup，只有有效备份或主备都 Missing 才继续；两者均 Invalid 不发布新 index。新深度测试 (`desktop_autosave_tests.cpp:649–702`) 覆盖无备份拒绝和有效备份幂等请求；大文件测试 (`607–647`) 确认旧 index/UInk 保留。
- **验证范围要分开**：隔离 bounds 用例 red→green，最终日志显示 16 MiB/深度拒绝、备份有效时可按既有请求报告 committed。该备份用例提交的是与备份已有记录同一个 request，`CommitIndex` 在幂等匹配后返回，测试明确断言损坏主索引仍保持原字节；它不验证“从备份新增另一 request 并原子替换主索引”。完整 `--desktop-autosave-only` 日志有 27 项失败，首次发生在第二次保存的 `ReplaceFileW`；父任务 validation 记录修改前后同样失败，隔离 API 探针返回 `ACCESS_DENIED`，归因仍待定。不能把这个完整 suite 写为 PASS，也不能把其失败直接判作 F-008 新代码缺陷。需要可执行替换语义的隔离环境复验备份恢复/普通多次提交。
- 新容量上限意味着单日达到 32768 条或 16 MiB index 后，新的 UInk 可能成为孤儿并报告 Failed；旧有效 index 和文件保持。这是明确的容量取舍，应在最终报告列出，不将“拒绝异常输入”说成无限容量的自动保存。

## F-010：PreviewOwner 停止时序

- `StartupPreview.cpp:84–85,197–252` 以同一个 `OwnerState::mutex` 发布并读取持续有效的 `stopRequested`：创建前已停止就不建 HWND；若停止落在创建期间，`stopAfterCreate` 使窗口不发布给其他线程，并由 owner 自己隐藏/销毁；若停止落在发布与 `GetMessageW` 之间，`stopBeforeWait` 再读一次。`RequestStop:317–336` 先置标志再投递消息。若它在最后一次检查后发生，HWND 已创建且已发布，投递窗口消息可唤醒队列；消息泵因 `WM_QUIT` 退出时还会销毁仍有效的 HWND。`Start:258–261` 也不会在 stop 已请求后误报创建成功。检查未发现这组新增锁与 `presentationMutex` 的倒置路径。
- `PreviewOwner::Stop:264–278` 在 1.5 秒等待失败时仍 detach；共享 `OwnerState` 被线程捕获，避免立即悬空，但真实 owner 迟到退出与 HWND 不残留尚无运行证据。`RequestStop` 忽略 `PostMessageW`/`PostThreadMessageW` 失败；sticky flag 保护创建交错，而异常情况下已经运行并阻塞的消息泵若窗口消息投递失败，仍依赖 Stop 超时/进程收尾。这是现有有界停止能力的限制，不能将此补丁称为所有 Win32 消息故障下必然关闭。
- **R-F010-01 [P2，条件性；未复现]**：若已发布 HWND 的 owner 消息循环正在 `GetMessageW` 中，`RequestStop()` 设置标志后窗口停止消息投递失败，owner 不会再次主动读取 `stopRequested`；`Stop()` 超时后 detach 可留下 topmost layered HWND。代码可见该失败路径，触发概率与实际输入拦截尚未测得。最小补强是 `PostMessageW` 失败后尝试向已有队列投 `WM_QUIT`，并让 owner 每次处理消息后检查 sticky stop；不能在 render/UI 锁内做无界等待。该风险不同于本次已经修正的“创建前尚无队列”竞态。
- 公共 `StartupPreview::Start/Stop` 的 `currentRuntime` 发布/摘除在 `runtimeMutex` 下；本次主要修 Owner 的创建/停止窗口。主程序通常串行调用 Start/Stop；没有针对并发 Public Start 与 Stop 或真实 HWND 的测试证据，报告应限于已修路径。

## 审查结论与后续验证

当前 diff 的 DWM 禁用、index 有界失败与 Preview sticky-stop 改动范围合理；本审查没有确认由这些改动新引入的严重崩溃、死锁或数据覆盖。`git diff --check` 本次退出码 0。已有 Debug|ARM64 完整 Solution 构建与独立 index bounds 测试可证明对应目标编译和专项逻辑运行；完整 Desktop suite 失败、hidden HWND、Win7 真机和 Preview stop 交错均保持**未通过/未验证**的各自状态。R-DWM-01 可能影响发布输入可用性，须由总门禁明确处理，不能以源码构建通过降级为无风险。
