# E02 B 无 hold 反例实际代码运行前安全复审

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。只读核对 `startup-no-hold-design.md`、实际 `git diff HEAD` 的 ShutdownSupervisor、真实 Main 四站点、MessageBox 窗口处理及原启动夹具安全报告；本 reviewer 不改产品或运行测试，只写本报告。

本次审查冻结身份：`Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp` SHA-256 `29237164675CC6E356E5939552947D9566100F026F012804F0CF92C0836A10B4`；设计 SHA-256 `B3CC38E501BED03BCB766559EBC3FA7E26BCD94D7B883707A9A8578E58201035`。后续修补必须注明增量，不能凭本报告直接覆盖改变鉴权/窗口选择/启动行为的 diff。

## 结论

**CLEAR_SAFETY。** root 可在本仓根目录、完整最新 Debug|ARM64 构建后，逐站点执行 `--shutdown-supervisor-tests --startup-no-hold-only D004|D005|D003|B002`。范围限现有 exact inherited-parent/private copied-child 信封与本次 owned 提示确认。先单站点再扩大；本审查不是自然退出结果 PASS，也不开放普通用户安装/Office/全局输入测试。

## Findings (fixed)

- File：`ShutdownSupervisor.cpp::RunStartupFailureProcessTest` 自然 exitCode 断言；真实 `IdtMain.cpp` D003 字体分支约 2291。
- Issue：首版将三个早期站点都预期 1，实际 D003 调 RenderPipeline::Shutdown 后返回 0，会把正常自然退出误报 FAIL。
- Fix：root 已改为 Com/PptCom=1、Font/BarState=0，设计也同步；Main 生产返回码保持原样。本 reviewer 未改源码。

- **证据输出补充已关闭**：root 随后将 armToDeath 计算移到输出前，把 `arm_start_tick`/`arm_to_death_ms` 加入同一 stdout 并将固定 buffer 扩到 800。本 reviewer 已核实际数值顺序/格式实参与自然断言未变，以上 hash 为此最终增量；CLEAR_SAFETY 继续有效。

## 实际安全边界

- **mode 与鉴权**：`StartupFailureObservation` 的 reserved 改 testMode，仍 128B、requestTick offset64、deadline offset88。parent 初始化为 0/1；`AuthorizeStartupFailureChild` 1035 附近拒 testMode>1，和 argc9、有限 site、magic/version/bytes/failureCode、三个继承 HANDLE、exact parent PID/文件身份、private 非 reparse root/bin/current EXE 核验一起完成后才安装全局 observation/site。`TryRunShutdownSupervisorEarly` 1820 附近在 Main 首次业务前解析；错参数/鉴权失败直接返回非零，不落普通 GUI。
- **默认/原 Hold**：`HoldAuthorizedStartupBoundary` 1765 附近仍要求已授权 observation、对应 failureCode、真实 failurePublished 和首次 gate，先发布 gate/intent/state。只有 testMode==1 返回；默认 null observation 无影响，mode0 保持 Sleep(INFINITE)。Arm、Main 四个 fatal site、成功路径和普通业务处理没有被本模式替换。原 Hold suite 仍需复验。
- **确实确认实际提示**：`IdtMain.cpp::ShowStartupMessage` 的 title 是 Inkeys Tips，MakeOkRequest 只有 Ok；Fluent class 由 `MessageBox.Window.cpp` 1452 附近生成 Inkeys.FluentMessageBox.*。其 WndProc 1349 的 WM_KEYDOWN/VK_RETURN 调 ActivateFocusedButton，后者真正 CommitResult。parent Native 分支使用 WM_COMMAND/IDOK，Fluent 使用 WM_KEYDOWN/VK_RETURN；没有 SendInput/全局光标/鼠标或 Computer Use。
- **owned 窗口与有限消息**：`ConfirmOwnedStartupPrompt` 1468 附近先查精确 child PID/可见、完整 title、Native class 或有限 Fluent prefix，发送前再查 PID/可见。仅 PostMessage 成功才记录 HWND/post tick；保存一个 prompt 后不再枚举发送。枚举可以读其它顶层 HWND 元数据，但不会向不匹配 child 的 HWND 发送消息。父始终持本次 `hProcess`，不是按名称查杀。
- **有限等待与失败清理**：`RunStartupFailureProcessTest` 的自然循环每次 WaitForSingleObject 50ms，总 25 秒；每次未 signaled 才尝试枚举。超时/等待失败保持 dead=false，之后仅 TerminateProcess 这个 child HANDLE；返回条件必须 dead=true，因此父强杀不会成为 GREEN。TestChildGuard 也只处理自己的 HANDLE。未运行时不能声称提示已收到或清理完成。
- **严格区分自然与强退**：自然断言要求 authorized/failure/gate/arm/durable、prompt PostMessage 成功、intent1、已发布 deadline、Arm→死亡<12000ms、postTick>=gateTick 和真实普通返回码 1/0。监督强退的 0xE1430015/16 不能满足普通 exitCode，15 秒退场也不满足<12秒。Hold 分支仍要求约14–21秒与专用强退码。消息仅入队成功不等于已处理；正常 exitCode/时长共同提供反例，仍需实际运行。
- **副作用隔离沿原审查**：child 继续同一 auth-only 的真实 wWinMain，各路径由 copied EXE 目录派生，跳过 update/旧 EXE、SuperTop、注册表自启、shortcut/DDB、PPT/Office/后期全局线程；四站点的既有允许调用链未扩大。错误父身份与无继承句柄负例仍执行。B002 是真实 Main 处理合成失败状态，不能称自然 Bar Register 失败。

## Findings (not fixed)

- **P2，动态未验证**：未由本 reviewer 构建或运行确认，不能写四站点自然 PASS、普通 Hold 无回归或 Win7 可用。自有提示暂时可见/置顶属于这些明确授权的启动反例，不等于无窗口测试；严格 Headless 结果须分开。
- **P2，恢复范围**：durable sentinel 只证明私有已 Flush 文件未改变；没有实际 Host/AutoSave/UInk fresh Load，不能升级墨迹恢复或现场卡死结论。C lifetime、真实设备/provider故障和用户手动确认体验保留各自门禁。

## Verification

- Scoped `git diff --check HEAD -- Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp`：exit0。
- Lint：未执行独立 linter；只读源码，无产品修改。
- TypeCheck / Build：未运行；root 需完整 InkeysRepo.sln Debug|ARM64、原生 ARM64 MSBuild/同调用 PATH 规范及至少 5 分钟预算。
- Tests / GUI：未运行。准许范围是运行前安全结论，输出结果按 root 后续最新 EXE/hash/命令/自然退出码记录。

## 2026-09-30 root 自然反例结果与后续观察字段修补

本 reviewer 已读取四个 `e02-startup-<site>-natural-debug-arm64.{status.txt,stdout.log,stderr.log}`，没有自行运行。四个父 suite 自然 exit0，实际 child 结果如下；每轮 wrong-parent/无继承句柄拒绝也PASS，stderr为空。

| site | parent PID | child PID | child普通exit | Arm→死亡 | 自有提示 |
| --- | ---: | ---: | ---: | ---: | --- |
| D004 | 24048 | 12280 | 1 | 875ms | Post成功，HWND 0x160D98，prompt tick6173250 >= gate6173140 |
| D005 | 27572 | 756 | 1 | 828ms | Post成功，HWND 0xC0D3A，prompt tick6226000 >= gate6225562 |
| D003 | 10896 | 9528 | 0 | 782ms | Post成功，HWND 0xD0D3A，prompt tick6227437 >= gate6227015 |
| B002 | 12380 | 14660 | 0 | 547ms | Post成功，HWND 0x50DEA，prompt tick6229031 >= gate6228937 |

四轮都有 authorized/failure/arm/gate/dead/durable=1、intent1、armState2，普通exitCode且Arm→death远小于12秒。它们支持本次旧候选的真实提示确认/自然退出反例，父强杀未计GREEN。B002仍是Main处理合成状态，sentinel仍不是UInk恢复。

D005旧raw的 `real_result=0x8007001F` 来自合成 ERROR_GEN_FAILURE 后观察，不是DLL/activation自然失败的证据。root随后只修 `IdtMain.cpp` 2203 附近观察字段：在调用合成 RecordPptComFailure之前，锁存 `actualPptComResult`，真实 activation+module均成功为S_OK，否则用先前已锁存实际错误；只交给授权观察包。`originalError`、正式Arm/error日志/提示、模块/COM清理和return1仍走原变量/顺序。当前Main SHA-256 `60589FE137376746A8AA25BF69A18F0A3EA1CFEF9D5D87DF2E4CCEB80220984C`。

该增量静态核对通过，**没有新Build/复验，不能把修后的D005字段记动态PASS**。旧自然退场证据保留，字段真实性需在新冻结EXE的D005逐站点输出上重新确认；后续C/UI3改动仍需各自审查与最后完整构建。
