# F-026 活动笔迹 fatal CPU 封口实施记录

- 范围：`Draw3.DrawingController.cpp` 的正常 Up CPU 提交、两个显式 graphics fatal 分支、`Run` 局部 C++ 异常边界和既有无窗口 CLI 测试入口。
- 初次封口只修改 controller 实现及本记录；后续为稳定 Closing 无窗口夹具，仅在 ContactInput 接入默认关闭的测试 hook。始终未触碰 Host 的析构后局部状态、Storage worker、Idt/Window 或工程文件。夹具最终证据见 [fatal-closing-test-stabilization.md](fatal-closing-test-stabilization.md)。

## 行为边界

Draw3 绘制线程独占 `RuntimeStroke`、active contact、活动文档和 page history。RTS 生产线程只更新 mailbox。正常 Up 原先在帧尾把实时 `realPoints` 或 shape primitive Finalize，随后向 Canvas 与 history 追加，GPU L2/Preimage 在其后；退出快照只读取可见 history。显式 graphics fatal 在帧首发生，活动 Down/Move 尚未走正常 Up 提交，因而原快照漏掉活动笔迹。Host 的 `Run` 异常处理发生在局部 active/history 析构后，不能在 Host 补救。

本次把 CPU Finalize→tile preflight→Canvas/history 追加抽为同一生产 helper。正常 Up 与 fatal 共用该 helper；只有 `history.AppendStroke` 后的 render item 仍可见且指向刚追加的 Canvas stroke，才返回成功。Canvas 成功而 history 失败时可能留下不可见孤儿 stroke；标记该 runtime 已尝试，禁止重复 fatal 再写同一 contact，也不把它计入快照/Presentation mutation。没有修改 InkCanvas/History 的底层事务 API。

fatal 先关闭新 Down admission，然后只处理绘制线程已消费的 `lastInputSnapshot` 或实际 `deferredUpSnapshot`。以 handle generation、Down admission revision、消费 sequence、workspace/page GUID 与 index 同时验证归属；shape 端点覆盖为真实 raw 终点，普通工具末端只追加 raw fallback，已有 `FinalizeStoredStroke` 自身只读取 `realPoints`。封口前稳定探测一次 mailbox：已到达 Cancel 则跳过，该探测不消费迟到 Move。Laser、Cancelled 和 Whiteboard 不持久化。调用既有 Desktop/PPT Exit snapshot 后，本绘制线程直接 break 或重抛，**fatal 专用路径不等待旧 route Closing**；正常页切换/完成仍保留 `DiscardUntilTerminal` 合同。`queued` 仍表示 worker 已接受，不能说已 durable。`Run` 的 `std::exception` 仅在这些局部仍存活的 while 边界、活动文档页数与 history/state 数量一致时，尽力调用一次上述路径，随后重新抛给 Host 受控退出；数量不一致、`bad_alloc` 与未知异常不在可能损坏状态上继续分配保存。

## 红测证据

- ARM64 原生 MSBuild 构建 `InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64`，退出码 0。日志：`TestResults/release-hardening/f026-active-ink-red-build.log`。
- 早期无窗口 `--draw3-parked-desktop-exit-test` 使用 `Start-Process -Wait -PassThru -WindowStyle Hidden`，退出码 **1**；原 `[Draw3DesktopExit] PASS`，新增 `[Draw3FatalActiveInk]` 在“活动真实 Move 应进 Exit snapshot”断言失败。stderr：`TestResults/release-hardening/f026-active-ink-red.stderr.log`。红版生产 helper 是临时 no-op，故失败点精确落在尚未存在的活动 CPU 封口。
- 测试使用真实 `ContactInputCoordinator` Down/Move mailbox、`RuntimeStroke`、`InkCanvasCollection`、`CanvasRuntimeHistory`、生产 Desktop snapshot builder；旧已完成笔存在、活动笔原先不在快照。新增断言覆盖工具、Down-only、deferred Up、PPT builder，以及 Cancelled、Laser、重复 seal、跨页/track 和非法终点。

## 绿测证据

- 生产修补版 `InkeysRepo.sln Debug|ARM64` 完整构建退出 0，日志 `TestResults/release-hardening/f026-active-ink-green-build-1.log`；只有现存第三方头 warning，0 error。`--draw3-parked-desktop-exit-test` 退出 0，原 Desktop 与新增 fatal 子测分别打印 PASS。新增子测逐项触发生产 CPU helper：旧已完成 stroke、Pen 的 Down→Move、HardPen、Highlighter、固定/速度 Eraser、四种 Shape、Down-only Pen、actual deferred Up、正常 Up 共用提交；快照断言没有 500/600 的预测点。Cancelled、Laser、重复 seal、跨页 GUID、跨 Desktop/PPT workspace、非法浮点终点拒绝；PPT 生产 builder 断言活动页与 EndScreen 身份。stderr `f026-active-ink-green-1.stderr.log`。
- 关联早期无窗口产品 CLI：renderer commit failure、Laser raster failure、PPT retained source、loaded retained install、pending topology load 五项均退出 0，日志 `f026-active-ink-regression-*`。`InkeysHeadlessTests.exe --no-window` 退出 0，日志 `f026-active-ink-headless.*`。
- `inkStrokeModelerTest.sln Debug|ARM64` standalone 完整构建退出 0，日志 `f026-active-ink-standalone-build.log`。完整 `inkStrokeModelerTestTests.exe` 在受限沙箱内退出 1，首个有意义错误为 Desktop `index-commit`，后续 PPT/UInk 文件替换连锁失败，共 62 个断言；日志 `f026-active-ink-standalone-full.*`。同一新构建、同一无 GUI 命令在沙箱外用测试自身的独立临时目录重跑退出 0，stdout 最后为 `All draw3 contact input tests passed.`，stderr 的 `source_changed`/`io_error` 为既有故障注入预期；日志 `f026-active-ink-standalone-full-unsandboxed.*`。这一区别是本机/Codex 文件替换限制，未通过改源码绕过。
- 最后一次静态复核把 `DiscardUntilTerminal` 移到 fatal snapshot 入队之后，以免 producer Closing 等待挡住保存。修改后再次完整 `InkeysRepo.sln Debug|ARM64` 构建退出 0、产品 Desktop/fatal 双子测退出 0、`InkeysHeadlessTests.exe --no-window` 退出 0；日志 `f026-active-ink-final-build.log`、`f026-active-ink-final-cli.*`、`f026-active-ink-final-headless.*`。两次主 Solution 构建均 0 error；controller 源保持 UTF-8 BOM/CRLF，`git diff --check` 通过。
- 冻结源码前再补一项 `Run` 异常入口的只读 history/state 数量预检，防止 CPU 追加中途抛错后导出半事务；最终完整主 Solution 构建、产品双子测及 Headless no-window 再次全数退出 0，日志 `f026-active-ink-final2-build.log`、`f026-active-ink-final2-cli.*`、`f026-active-ink-final2-headless.*`。该一致性分支没有可控异常注入，只有编译与源码边界证据。
- 独立复审发现 `DiscardUntilTerminal` 即使放在 snapshot 入队后，遇 `ProducerState::Closing` 仍会无限 `YieldProcessor`，阻住 `Run` 返回与 Host 的后续 drain。初次红绿使用随机 `SuspendThread` 暂停 Move writer，ARM64 Debug 得到红 exit1→绿 exit0，但后续 x64 Release 曾非确定性挂住，且主线程 `PublishMove` 不能严格证明 route 已到 Closing。该夹具现已被 CAS 后安全点 hook 完全替换；当前有效的确定性 Release|x64 红绿与三架构验证见 [fatal-closing-test-stabilization.md](fatal-closing-test-stabilization.md)。生产修补仍只在 fatal 专用处理取消同步 Discard，改为封闭新 admission 并让 Host/RTS 接管旧 route；正常页切换合同不变。
- 复审指出原 Pen 测试手工把 `realPoints` 推到 raw Move x55，未验证补点。绿版已把模型真实尾固定在 Down x40，仅 mailbox 消费 Move x55；生产 helper 封口后快照尾点仍断言为 x55，并检查预测 x500/y600 未入盘。此调整与 Closing 绿版同一 CLI 执行。

## fatal 路由所有权

两个显式 graphics fatal 分支在 `captureGraphicsFatalExit` 后立即 `RequestExit`/`break`；`Run` 普通 C++ 异常在局部 catch 尝试一次保存后立即重抛，Host catch 请求退出。后续不再有 Draw3 消费者读取旧 route，因此 fatal 不需让绘制线程同步等 producer Closing。Host `Stop()` 在 `Draw3.Host.cpp:1325-1363` 先停 Bridge，`RealTimeStylusInput::Shutdown()` 先 disable RTS/plugin、再 `CloseAllProducerContacts`，随后等待保存屏障/worker drain、join 绘制线程，coordinator 在下一次 Start 的 `ResetForNextRun` 清旧 slot。此顺序依赖 Host 受控停止；如果真正的 RTS/COM shutdown 本身卡死，仍由进程级 15 秒监督器处理，不能保证本次新请求 durable。没有更改全局 `DiscardUntilTerminal`：其正常页边界与正常完成若在 Closing 时不等 producer 转为 ConsumerOwned，调用者丢 handle 会造成 slot 泄漏。

## 验证边界与发布门禁

无窗口测试调用真实生产 CPU helper、ContactInput 及 Desktop/PPT 快照 builder，但未调用实际 `DrawingController::Run` 图形 fatal 分支；两分支的调用顺序由源码静态核对。当前确定性夹具在真实 route CAS 到 Closing 后证明 fatal helper 不等待 producer，**未**注入 GPU 设备永久失败、绘图中 C++ 异常、真实 RTS/COM Shutdown 卡死或迟到 Cancel 的并发交错。history append 失败后的孤儿 stroke 不进入可见快照由代码边界保证，但没有可控 `CanvasRuntimeHistory::AppendStroke` 失败注入；双 contact 的实际同时封口也未动态覆盖。真实 Office 多文稿、关机 worker 是否在 15 秒前 durable、GPU Debug Layer、Win7 真机、人工绘制与跨进程可见恢复均未验证；Release|x64/Win32 在当前 Windows ARM64 机器的编译和早期无窗口运行证据单列于夹具报告，不能替代 Win7/真实图形故障。`Run` 早期初始化、`bad_alloc`、未知异常、内存损坏、SEH/FailFast、硬终止与断电不承诺活动墨迹或尚未入队的已完成内存文档保存；已 durable 的旧恢复点只受既有 worker/UInk 原子协议保护，本单元没有证明强退时新请求已 durable。
