# 单 CMD 采集器实现与验证（2026-10-05）

本分工仅实现单入口采集编排及局部诊断启用，不改输入、shader、后端选择或持久化配置。GPU 探针由另一实现者负责，完整 Solution 构建、最终候选身份和包验证由主会话负责。任务维持 in_progress，无 commit/push/archive。

## 文件与行为

- `Inkeys/IdtMain.cpp`：仅在既有 `#ifndef IDT_RELEASE` 诊断初始化处读取 `INKEYS_DRAW3_WIN7_DIAGNOSTICS`，严格要求长度为 1 且值为 `1`；与局部 Draw3/Cursor 开关 OR，不修改配置对象。共用控制台初始化后输出一次激活记录。原 UTF-8 BOM/CRLF 保留。
- `research/Collect-Win7.ps1`：增加可选 `CaptureRoot`、`SkipProbe`；未传新选项仍先探针后正常应用，`ProbeOnly` 仍仅探针。拒绝 ProbeOnly/SkipProbe 同时使用及已存在采集目录。整体探针等待 150 秒，单个 GPU 步骤等待由产品测试控制。正常应用改为保持自有 Process.Handle → WaitForExit → ExitCode，移除 Refresh；异常仅尝试结束该确切子进程。输出、身份、文件版本及本轮 idt 日志保持采集，非零正常退出传回失败状态。
- 新 `research/Run-Win7-Diagnostics.cmd`：`setlocal` 包装 Windows PowerShell，可靠引用 `%~dp0`，保存并传回 PowerShell 退出码，最后 pause。最终包由主会话只保留此 CMD。
- 新 `research/Run-Win7-Diagnostics.ps1`：一次生成时间汇总目录，按 `01-pixel-probe` → `02-normal-ulw` → `03-full-ulw` 顺序执行同一 EXE。一次探针，后两轮 SkipProbe；显式启用局部诊断，普通轮清除 full override，最后一轮仅令 full override=`1`。finally 恢复调用进程原环境，包括原来继承的 full=`1`。不强制 GPU 后端。
- 汇总保留 EXE SHA、每阶段时间/路径/控制环境、子采集器完整双流、实际退出/不完整状态；阶段原始双流与 identity 保留在子目录。自检失败仍继续两轮应用。各正常轮退出后 Read-Host 记录观察；未提供观察明确写出，自动成功状态不代表视觉恢复。
- 中文步骤要求在新的空白测试页先普通笔画约 5 秒、等待 3 秒，再橡皮及激光各移动至少 3 秒，正常退出后填写出墨时机与光标可见现象。不要求清空既有页面，不要求手动开设置。

## 冻结身份

| 文件 | SHA-256 |
| --- | --- |
| IdtMain.cpp | 340A40BC16C0A79B861477E79F4D763D26DF5C4BAFAC819E5CCD21266BCEF4E8 |
| Collect-Win7.ps1 | D953566323033132B004511DABF1116728968D7FE6AAA865C98D9DC2039FA20E |
| Run-Win7-Diagnostics.ps1 | 9FB7B6C2CC3F82B37B574654FB5B52D62EF51B31433131D585B53024980E2607 |
| Run-Win7-Diagnostics.cmd | F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D |

## 实际验证

证据根：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-one-click-20261005/collector-tests/`。

- 实际 Windows PowerShell 5.1，通过编译生成的无 GUI console mock EXE 验证，不启动正常产品窗口。
- `final-validation/results.json`：冻结脚本的空格路径三阶段、继承 full=`1` 时 probe/普通轮实际 unset、末轮 `1`、全部 trace=`1`、准确仅三次调用、两条观察记录均 PASS。
- mock probe 退出 1，普通轮退出 5，末轮退出 0：三轮仍全部执行，身份保留真实退出码，最终 completed-with-failures/退出 1，PASS。
- 每阶段 stdout/stderr 各超过 512 KiB：六份原始流完整且无等待死锁，PASS。
- 故意 malformed mock collector 缺输出：最终 incomplete/退出 1；mock collector 启动异常：stderr 完整保留且仍执行 full 轮；缺 EXE：建立诚实 incomplete 汇总，均 PASS。此项只替换测试副本，不修改正式采集器。
- 默认入口、ProbeOnly、SkipProbe 分别实际验证 mock 调用数为 2/1/1；冲突开关拒绝且未启动 EXE，PASS。
- `restore-result.json`：同一 WinPS host 调用正式编排脚本后，原 trace=`original`/full=`1` 均恢复，PASS。
- 实际 WinPS parser 及 PS2 API/语法子集静态审查通过。Collect 保持 ASCII/CRLF，新 PS 保持 UTF-8 BOM/CRLF，CMD 为 ASCII/CRLF。`git diff --check` 通过。
- `cmd-results.json` 保存最终单 CMD 的实际 mock 验证，正常/失败两情形的入口、空格路径及退出传播结果。

## 验证边界

- 本分工不构建 Solution、不运行正常产品 GUI、不进行真实光学验收。CPP 编译和独立 GPU 自检由主会话验证。
- 本机 WinPS 5.1/mock 成功不等同 Win7 PowerShell 2.0 实测；PS2 子集仅静态核查，Win7 两设备本候选三阶段均待用户实测。
- 未人为等待探针实际 150 秒超时；150000 参数、identity 标记和原有确切子进程清理路径已静态核查。未模拟机器断电或 host 被强行终止；可执行 finally 的中断以完成阶段计数标记 incomplete。
- module 文件版本不是完整补丁清单或实际已加载模块身份；摘要不推断 WARP/Hardware。真实 driver/backend、像素和可见性仍依原始日志与用户观察分析。
