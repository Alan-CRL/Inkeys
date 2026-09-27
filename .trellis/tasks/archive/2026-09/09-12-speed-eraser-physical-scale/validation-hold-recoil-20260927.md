# Draw3 保持／回缩许可验证（2026-09-27）

## 基线与复现

当前分支 `bugfix/eraser`，起始 HEAD `e6bc3c28b96c3cd6cf0911c4834af02ca3b67f6b`，短快划参数来自 `6aefa84e2b915a395dab0ce55b54ff2f84bc92ac`。起始 `git status --short --branch` 无改动；原 `09-12-speed-eraser-physical-scale` 任务在本会话用 `task.py start` 重新选中，保持 `in_progress`。不切换、重置或重放其他分支。

生产 Controller 合成回放：Surface 2880×1920、192 DPI、28×19cm、Laptop/Small、B=32、中灵敏度、面积关。屏内局部圆轨迹，1000Hz 真实输入与后移 0.4ms 的 100Hz 帧观察独立调度；先持续强清扫 2 秒到 160 DIP，再循环 40ms 高速/120ms 普通速度 6 秒。高速为 Touch 375、ScreenPen 525 mm/s；普通分别为 54、72 mm/s，它们是各模型相对激励。新增测试在未改控制器时：Touch/Pen 的证据 cap 均回到 32 DIP、actual 始终 160 DIP，headless 退出 1，仅报两条新增回缩断言失败。原始 Surface `Pasted text(9).txt` 是稀疏 Touch 快照，不用于逐点重放或宣称 Pen 实机验收。

## 修改后的控制器证据

Touch 在切换后约 2 秒为 33.7127 DIP、3 秒为 32；ScreenPen 对应 32/32。两者至 6 秒终点均为 32。100Hz trace 共各 799 帧，位于 `%TEMP%/inkeys-hold-pulse-DirectTouch.csv` 和 `...-ScreenPenHybrid.csv`，列出 raw/effective target、cap、growthGoal、actual、证据、保持剩余、回缩确认、状态、本步许可、最近真实输入速度/年龄和每次 reset 原因。Touch 在总时刻 2.2 秒仍为 160 DIP、cap 72.43、保留原有约 489ms hold；总时刻 3 秒 actual 102.89、cap 32、decrease 确认约 938ms；总时刻 4 秒 actual 33.71。ScreenPen 在 2.2 秒仍为 160、cap 95.22、原 hold 尚余约 39ms；3 秒 actual 40.73、cap 约 32.27。两条 trace 各只有一次回缩 reset，原因为到达目标后的 `settled`，100Hz 观察中没有同帧遗漏多次 reset。cap 低于 actual 时并未立即夹小或撤销短期保持。

40 组高/低占空比组合覆盖每模型高段 20/40/80/100ms、普通段 80/120/200/400/800ms。40/120ms 的后段最大证据为 Touch 62.47ms、ScreenPen 78.35ms，均低于 80ms 开启门槛，终点均 32 DIP。80/80ms 的真实高占空比仍可保持 Touch 93.43、Pen 123.94 DIP；100/80ms 为 143.00/159.71 DIP，不把所有重复快划一概禁止。纯普通运动从 160 回到标准；0.8s 高/40ms 低的短折返仍保持至少 0.9×大尺寸。既有暖状态、新 Down、精细、同位置、无Move、Up、非Touch会话、面积、尺寸/灵敏度、物理/DIP/经验回退和 60/125/240/1000Hz 输入 × 30/60/120/144fps 帧回归通过。面积普通拖擦约 39 DIP，可高于约 32 DIP 的清扫证据 cap；cap 不剪面积下限。

暖状态冷证据 100/150/200ms 强快划的末尺寸：Touch 32/32.8963/37.957，ScreenPen 32/33.0491/38.0394 DIP；500ms 为 108.05/112.005，1 秒为 155.941/157.386。Mouse 同 10 条暖状态输出与修复前基线逐行相同；其 100/150/200ms 为 32/32.0119/32.8015 DIP。既有频率矩阵 240 组最差快段末差 0.590517%，全套 sample/frame 最差 4.61275%，仍在 5% 内。教室大屏 432.376/600/800/1000/1300mm/s 持续圆轨迹实际约 33.1135/42.1586/67.1525/109.401/160 DIP，原速度场景目标未改。Mouse 离面尺寸状态新增只读诊断回归，实际回落与 `growthGoal` 一致。最终产品运行的同接触首点、光标/新增几何、尺寸断点、保存/撤销与右键/笔尾断言没有新增失败；中间两次运行有不同的额外断言，详见下节。

## 新 Down 增长形态

从新接触以各模型 1.5×large 连续清扫，按 100Hz 帧观察，四个 250ms 区间直径增量（DIP）及相对 B 的平均增长速率（B/s）：

| 模型 | 0–0.25s | 0.25–0.5s | 0.5–0.75s | 0.75–1s | 最大单帧增量 | 最大帧速率 |
| --- | --- | --- | --- | --- | --- | --- |
| Touch | 11.178 / 1.397 | 47.661 / 5.958 | 54.585 / 6.823 | 22.198 / 2.775 | 2.743 DIP | 8.572 B/s |
| ScreenPen | 13.799 / 1.725 | 65.349 / 8.169 | 36.536 / 4.567 | 9.643 / 1.205 | 3.016 DIP | 9.426 B/s |

Touch 从 16、Pen 从 32 DIP 起步。中段追赶较明显是测量事实，不据此与本轮回缩问题混为同一根因。本轮未改 tau、log rate、场景速度阈值或目标。若真人反馈特指中段追涨，可把当前版本作 A，与只调整证据已开启后 0.25–0.75s 跟随形状的 B 独立比较；B 须保持 100/150/200ms 上界和 1 秒持续清扫可达，再决定是否实施。

## 构建与产品测试

- 从本机 Visual Studio 安装定位 ARM64 原生 `MSBuild.exe`；在同一 PowerShell 调用中移除重复 `Env:PATH`、设 `MSBUILDDISABLENODEREUSE=1`，构建 `InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal`。基线完整构建退出 0。首版改动的增量构建遭 `C1041` 写 `vc143.pdb`（退出 1），没有源码错误；仅在后续当前调用追加原值后面的 `CL=/FS` 与 `_CL_=/Z7` 避免编译 PDB 争用，不修改工程或全局环境。最终完整构建退出 0，日志 `%TEMP%/inkeys-hold-ship-build-stdout.log`。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：基线新增反例退出 1（仅两条预期红灯）；最终退出 0、`[SpeedEraser] failures=0`，日志 `%TEMP%/inkeys-hold-ship-headless-stdout.log`。
- `Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test` 用隐藏窗口启动、等待并读取实际退出码：基线与最终均退出 1；DComp/ULW 各有 `drawpad and presentation remain Freeze siblings` 与 `Host stop leaves hidden Window Service HWND intact` 两条 Window Service owner 断言，共四条。最终日志 `%TEMP%/inkeys-hold-ship-hidden-stderr.log` 有 Touch/ScreenPen `[EraserFollow]` 帧级限频输出（82 行），其中 44 条无当前清扫/面积资格且 actual>B 的快照均未报告高于 actual 的增长目标。最终运行无新增橡皮断言失败。整项未通过；本轮未删除、跳过、归因环境或重构 owner。
- 另外两次中间隐藏测试各出现一条非 owner 额外失败：一次为 `right/tail override only erasing and leave ordinary drawing unchanged`，另一次为 `manual classroom ordinary local Touch remains near the selected B`（失败快照已回到 16 DIP、清扫速度 0）。它们在最终运行均未复现，原因尚未证实，不能把这些结果删除或直接归为环境故障。中间日志分别为 `%TEMP%/inkeys-hold-release-hidden-stderr.log` 与 `%TEMP%/inkeys-hold-repeat-hidden-stderr.log`；本轮未改窗口所有权或为通过测试放宽断言。
- 仅执行命令行与隐藏窗口测试，没有启动需人工关闭的产品 GUI 或 Computer Use。未做 Surface/ScreenPen、教室大屏、Win7 真人/设备验收；一份 Surface Touch 稀疏日志不能代替它们。

用户在验证后授权提交并推送本轮修改；原任务继续 `in_progress`，不 finish 或归档，人工测试稍后进行。最终 `git diff --check` 退出 0；既有修改文本的 BOM 与起始工作区一致，所有修改文件保持 CRLF，新验证记录使用 UTF-8/CRLF。完整构建生成的受跟踪 `Inkeys/PptCOM.dll` 在起始工作区干净的前提下从 HEAD blob 恢复原字节，最终无二进制 diff；未覆盖任何起始已有改动。
