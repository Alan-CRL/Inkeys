# Final Quality Check

审查时间：2026-09-29 20:10 +08:00。范围冻结于当前工作树：分支 `chore/publish`，`HEAD=8b156fca59f0337a6afc6d722941666fcf143080`，工作区仍有未提交改动。本次只读核对任务上下文、当前 diff、启动/退出调用链、已有日志和规范；没有修改产品代码、spec、共享账本或构建输出，也没有重跑大型构建、GUI、PowerPoint 或性能测试。

## Findings

### P1-01：监督器双重建立失败时仍没有 15 秒保证（confirmed）

- 当前 `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp` blob 为 `8301a55fbcd3bbeddfeb83f51303364bbe83512b`。`ArmCore()` 先创建进程内 `FallbackDeadlineThread`，再创建并握手 helper；若 `CreateThread` 失败且 helper 也失败，最终状态为 `3`，返回 `ArmResult::Failed`，没有截止线程。
- 当前 `Inkeys/IdtMain.cpp` 的 `SetOffSignal()` 在 `ArmResult::Failed` 时只记录错误并继续清理。首次 CAS 已经阻止后续重试，清理一旦卡在窗口、Draw3 或保存 worker，旧进程可以超过用户确认的 15 秒仍存活。
- 现有红绿用例覆盖 helper 失败后 fallback 线程仍能建立，以及各自报告/强退路径；没有覆盖两个监督均无法建立的注入。

这是用户“任何结束和重启 15 秒无条件强制”的发布阻塞。需要不依赖线程创建的有界兜底，或明确把 `Failed` 作为不可发布结果并提供可重复故障注入证据。

### P1-02：Draw3 Host 停止与保存 worker 排空没有内部 deadline（confirmed conditional）

启动失败分支的排序修补已完成并由主任务在该 patch 后验证：D101、D201、D301、D401、D202、D102 均先调用 `SetOffSignal(1)`，再进入 fatal 提示和资源清理；Debug ARM64 以及 Release ARM64/x64/Win32 Solution Build、Headless、PptCOM.Tests、shutdown-supervisor suites 均报告 exit0。该证据关闭了“这些已列启动失败分支在清理前完全没有监督”的原问题，但没有覆盖每个故障注入分支的独立运行时触发。

当前 `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` blob 为 `63f6518d3aa1f419fb0835e954038df8579ea72f`。`Host::Impl::Stop()` 在约 1308 行后：

- 先等待 `exitAutoSavePrepared || !running`，该 condition wait 无超时；
- 然后调用 `autoSave.CloseAndDrain()` 与 `presentationAutoSave.CloseAndDrain()`，worker I/O 同样无界；
- `running` 若因 Controller/Present/Contact 卡死不变为 false，或最终保存屏障没有回执，调用者会一直停在退出链。

正常入口在监督成功时通常由外部 15 秒看门狗兜底，但 P1-01 的 `Failed` 情形没有该保证。用户报告的“画布卡死”没有真实永久阻塞 worker/绘制线程的动态复现，因此本条是静态 confirmed conditional，不应写成已确认现场根因。

保留一个独立的启动回退边界：`Inkeys/IdtMain.cpp:2527-2546` 的首次 DComp 启动失败重建路径先执行 `Draw3::StopProduct()` 和 `windowService.StopAndJoin()`，只有重建失败后进入 D201 分支才调用 `SetOffSignal(1)`。本次排序修补和上述 exit0 验证没有故障注入该回退路径；若停止/重建期间阻塞，仍可能在监督建立前等待，需单独决定是否纳入 15 秒合同。

### P1-03：正式发布宏门仍未满足（confirmed release gate）

`Inkeys/IdtMain.h:20` 仍为注释 `// #define IDT_RELEASE`；`.github/workflows/build-windows.yml:52-69` 在默认 `validate_release=true` 时要求精确的未注释宏。`CL=/DIDT_RELEASE` 的隔离重建不能替代源码宏和 CI 门，因此当前候选不能称为正式发布构建通过。

### P1-04：Win7 SP1 仅 KB2670838 的真实图形矩阵未验证（manual gate）

静态代码仍只选 DComp/ULW，拒绝两种 DWM 透明方案，并保持 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`；Hardware FL11.0 失败后有 WARP FL11.0 分支。当前机器是 Win11 ARM64，没有证据证明下列目标组合真实成功：Hardware FL11.0、无 FL11.0 时 Hardware→WARP、DComp 不可用→ULW 的 FLIP 创建/成功 Present、透明 alpha/dirty/resize/device-lost 和真实输入。用户提供的 Win7 FLIP 实测已写入 spec，本审查没有把微软通用文档冲突当作回退理由。

### P1-05：真实输入、用户退出入口和可比性能仍未关闭（manual gate）

当前无窗/隐藏 Host 测试证明生产逻辑的有限路径和成功 Present，但没有真实笔/Touch 的 Down→成功 Present 尾延迟、设置页真实按钮命中后 Close/Restart、HC/H2 同设备同效果对照，也没有光学端到端测量。它们不能升级为“体验不低于 Canary/Inkeys2”或“用户画布卡死已解决”。

## Stale or incomplete evidence

- `research/final-hf-independent-review.md` 和 `research/spec-final-alignment-review.md` 仍引用 `IdtMain.cpp` blob `fb5b1bf222d44e1a63668a73e099a8803fcdb4e6`；当前 blob 是 `a45440114603e1d6a9ba3629cb4e3178706b5837`。最新 D101/D201/D301/D401/D202/D102 重排因此没有被这两份报告的调用链指纹覆盖。
- `TestResults/release-hardening/hf-startup-order-final-build-release-{ARM64,x64,Win32}.log` 是当前顺序修补后的编译日志，证明 `IdtMain.cpp` 被编译并生成三架构产物；主任务随后补报的 Debug ARM64、Release 三架构 Build/Headless/PptCOM/shutdown-supervisor 均 exit0。仍没有针对每个 D101/D201/D301/D401/D202/D102 分支的独立故障注入运行证据。`hf-startup-failure-arm64-headless.*` 本身只是通用 HeadlessTests，未触达这些 `wWinMain` 分支。
- 更早的 `hf-code-freeze-*`、`hf-final-integrated-*`、UEF/HiddenWindow/PptCOM 和 CLI 日志早于当前 `IdtMain.cpp` blob；它们只能作为历史证据，最终报告应引用上述 patch 后的 Build/Headless/PptCOM/supervisor 结果，并把来源 blob 或构建指纹写入日志/账本。
- 当前没有交付后的 `HEAD + 非忽略工作区内容` HF 指纹；旧 `hf-independent-review.md` 的 `831b...` 指纹属于更早快照。
- 当前 `git status` 仍列出四个未跟踪 shader 生成物：`inkStrokeModelerTest/{inkPixelShader,inkVertexShader,laserParticleEmitCS,laserParticleUpdateCS}.cso`。它们不能进入源码交付；自动审批服务此前拒绝清理命令，需在允许的清理通道处理或明确排除在 HF 指纹外。

## Verified in this review

- `python .trellis/scripts/task.py validate .trellis/tasks/09-27-integration-and-release-check`：通过，`implement.jsonl` 4 项、`check.jsonl` 3 项。
- `git diff --check`：通过。
- 当前关键源码 blob：`IdtMain.cpp=a45440114603e1d6a9ba3629cb4e3178706b5837`、`ShutdownSupervisor.cpp=8301a55fbcd3bbeddfeb83f51303364bbe83512b`、`Draw3.Host.cpp=63f6518d3aa1f419fb0835e954038df8579ea72f`、`Draw3.ContactInput.cpp=e9b1792417d7cb40c14ddca969ae84ab09a83777`。选查源码保持原有 BOM/CRLF；spec 文件保持 CRLF，未发现由本次代码 diff 造成的格式污染。
- 主任务交接的 patch 后动态证据：Debug ARM64 与 Release ARM64/x64/Win32 的 Solution Build、Headless、PptCOM.Tests、shutdown-supervisor suites 均 exit0；本 reviewer 没有重新运行这些命令，故将其标为交接证据而非本轮独立动态 PASS。
- `task.json` 仍为 `in_progress`，工作区未提交；本报告不改变任务状态。

## Remaining gates

1. 设计并验证双重监督建立失败时的无条件 15 秒退场，以及 DComp fallback/Host Stop 的有界合同。
2. 继续用当前 `a454401...` 源码为 DComp fallback、绘制线程卡住和保存 worker 卡住边界建立故障注入；启动排序的普通 Build/Headless/PptCOM/supervisor 验证已完成，但每个启动失败分支的触发证据仍可补强。
3. 取得 Win7 SP1+KB2670838 Hardware/WARP、DComp 缺失→ULW FLIP、真实笔/Touch/resize/device-lost 的现场证据。
4. 满足官方 `IDT_RELEASE` CI 宏门、清理或排除生成 `.cso`，计算新的 HF 内容指纹，再做一次针对最终 blob 的全 diff 独立复审。
5. 在上述门禁完成前保持“不可以发布”；自动更新来源认证仍按用户决定保留为明确的 accepted residual risk，HTTP/HTTPS 与旧 `智绘教.exe` 回退不可删除。

## Verification limits

本审查没有运行大型构建、GUI/Computer Use、PowerPoint、真实异常注入或性能采样；报告中的编译/运行结论仅引用任务目录中已有日志，并按日志时间和当前源码 blob 区分了可复用证据与过期证据。
