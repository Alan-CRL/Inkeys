# 阶段 A 执行计划

## 分工

- **Claude**：规划、全部代码编写、构建、测试、实测与独立审查。
- **Codex**（只读，不写代码）：
  - 检查点：`gpt-6.2-sol`，`model_reasoning_effort=high`，`service_tier=fast`，`--sandbox read-only`；
  - 最终审查：`gpt-6.2-astra`，High，`service_tier=default`；
  - 联网失败时用 `HTTPS_PROXY/HTTP_PROXY=http://127.0.0.1:7890`；简报写到 `%TEMP%/inkeys-codex/gpu-latency/`，给出 diff 文件路径与“已接受偏差”清单；Codex 的严重度与“已确认”结论都要用代码或测试核实。
- 每步完成后：Claude 自审 → 构建与测试 → Codex 检查点 → 修复 → 下一步。全部完成后：Claude 完整审查 → Astra 最终审查 → 修复 → Sol 复核。
- 不提交、不 push，除非用户在当时明确授权。

## 通用约定

- 改动的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF；新文件同样带 BOM、用 CRLF；不改动文件末尾换行等无关内容；不用 Git Bash `sed -i` 改源码。
- 构建：`InkeysRepo.sln`，`Debug|ARM64`，ARM64 原生 MSBuild；在同一 PowerShell 调用里 `Remove-Item Env:PATH`（保留原 Path）、`$env:MSBUILDDISABLENODEREUSE='1'`，`/nr:false`，超时 ≥ 5 分钟，日志 `-Encoding utf8`；遇 LNK1000 删除 `Inkeys.ilk` 后重链。
- 测试：`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`（完整约 5 分钟，后台运行并落盘）；局部可用帧延迟门的单独开关；窗口类测试不并发。
- 实测：画圆测延迟工具（`%TEMP%/inkeys-codex/gpu-latency/tools/`，S4 完成后改用诊断包），压力工具 `--cpu 0 --gpu 8` 每次只运行约 10 秒；Inkeys PID 只按本仓库 `Build\` 路径且不带参数匹配，并用日志头 `pid=` 核对；需要竞争前台时用 `fg.py` 把压力工具切到前台；测试后还原 `Config/main.json`。

## 步骤

### S0 开工核对（Codex Sol，只读）

让 Codex 对照 `design.md` 逐条核对代码，矛盾即列出，不给修改：
1. L0 的全部写入点是否只有设计列出的三处；各处报告的包络是否已覆盖线宽、抗锯齿、笔尖锥形、预测段；形状构造预览的包络来源。
2. `SetOperatorTarget` / `ClearOperatorLayer` 的全部调用点中，有没有在一帧内先写 L0、再整屏清、再写的顺序会破坏记账。
3. `FrameLatencyGate::Acquire` 改为超时后解析策略，对现有调用点与测试的影响。
4. `TransparentPresentation.cpp:1884` 的普通链路径在“关闭等待”下的日志、`EffectiveState`、熔断状态是否一致。
5. 配置组、启动调用点（早于 Draw3 首帧）、设置页 `ComboRow`/`ToggleRow` 参数含义、i18n 生成流程、`setting_design_tests` 中会受影响的用例。
6. Win7 工具集下 `PROCESS_POWER_THROTTLING_STATE` 等定义的可用性。

Claude 核实报告后修订 design，再进入 S1。

### S1 熔断判据（R5）+ 关闭等待用普通链（R6）

1. `Draw3.FrameLatencyGate.h`：超时策略改为超时后解析；新增 GPU 忙超时计数。
2. `Draw3.TransparentPresentation.cpp`：`PrepareFrameLatency` 传入解析逻辑；汇总日志加 `gpuBusyTimeouts`；1884 行条件加入 `!options.enableWaitableWait`，删除不可达分支并补日志。
3. `InkeysHeadlessTests/draw3_frame_latency_gate_tests.cpp`：补充 R5 用例。
4. 构建 + 帧延迟门测试 + 完整 headless 测试。
5. 实测：
   - 关闭等待（配置 false），Inkeys 临时 REALTIME 排除饥饿，压力在前台：延迟有上限，日志为非 waitable 链；
   - 开启等待、NORMAL、压力在前台：日志出现 `gpuBusyTimeouts`，不再因 GPU 忙熔断。
6. Codex 检查点。

### S2 进程调度档位与电源节流（R1–R4）

1. 新建 `Helper.ProcessScheduling.cppm`，登记到 `Inkeys.vcxproj` 与 `.filters`。
2. `Other.Config.cppm` 新增两个键；`IdtMain.cpp` 启动调用。
3. `Setting.cpp` 性能页新增下拉框与开关（含 i 提示、高性能附加说明）；`zh-CN.jsonc` 新增文案，`pwsh Scripts/i18n.ps1 sync` 生成头文件并同步其他语言后补译。
4. 补充测试：档位到类值的映射、配置默认值；调整受影响的设置页用例。
5. 构建 + 完整 headless 测试 + 设置页测试。
6. 实测（本机）：
   - 默认配置（均衡 + 关闭节流），不借助外部工具，压力在前台：延迟 p50 ≤ 80 ms，无熔断；
   - 标准 + 开关关：复现饥饿（对照）；
   - 高性能：延迟不劣于默认；
   - OBS + 压力（三档各一次）：记录渲染掉帧；
   - WPR 一次：读回 Inkeys 上下文优先级（默认配置应为 16）。
   日志核对：启动与切换都有调度日志与读回值。
7. Codex 检查点。

### S3 L0 只清脏区（R7）

1. `Draw3.Renderer.cppm/.cpp`：记账字段、`NoteOperatorWrite`、`ClearOperatorLayerContent`、缓存 `ID3D11DeviceContext1`。
2. 三个 L0 写入点调用 `NoteOperatorWrite`；`DrawingController.cpp:23670` 改为 `ClearOperatorLayerContent`。
3. headless 用例：局部清除回读复原；未报告写入时退回整屏清除。
4. 构建 + 完整 headless 测试 + 生产无窗口探针（`--draw3-control-fence-test`、`--draw3-canvas-pan-feel-test` 等现有探针，先 grep 核实参数）。
5. 实测：笔、荧光笔、预测开/关、多指、形状构造与编辑、橡皮、激光、抬笔后立即落笔，截图比对无残影；压力下延迟不退化。可选：WPR 对比书写时每包 GPU 执行时间。
6. Codex 检查点。

### S4 诊断包（R8）

1. `Scripts/LatencyDiag/LatencyDiag.cpp`、`Start-LatencyDiag.cmd/.ps1`、`Pack-LatencyDiag.ps1`。
2. 编译 arm64 / x64 / x86。
3. 本机用 arm64 构建完整跑通一次（含压力负载与可选 WPR），检查报告、CSV、日志副本、收尾恢复。
4. 打包 x64（HD 4600 机器用）与 x86（32 位 Win7）；Inkeys 需另外构建 `Release|x64`（及 `Release|Win32`），确认 Release 构建是否输出 `[Draw3Diag][latency]`，不输出时附带 Debug 构建。
5. Codex 检查点（重点：Win7 兼容、动态加载、收尾恢复、不修改用户配置）。

### S5 收尾

1. Claude 完整审查全部 diff（编码、换行、超范围改动）。
2. Astra 最终审查；修复；Sol 复核修复。
3. 规范更新（`trellis-update-spec`）：`draw3-integration.md`（R5、R6 新约定、L0 局部清除不变量）、`settings-performance-and-drawing.md`（调度档位与节流开关、Win11 实测映射、REALTIME 对录屏的影响）。
4. research 补充阶段 A 实测汇总；PRD 验收项逐条勾选，标明未验证项（老设备、Win7）。
5. 用户授权后提交；阶段 A 完成后安排老设备诊断。

## 回退点

S1、S2、S3、S4 相互独立，建议各自形成独立提交，便于单独回退。
