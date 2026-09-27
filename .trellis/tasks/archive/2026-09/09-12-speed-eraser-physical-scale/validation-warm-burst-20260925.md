# Touch/ScreenPen 暖状态短快划增长（2026-09-25）

## 基线与实现

原 `speed-eraser-physical-scale` 任务保持 `in_progress`，本轮在干净 `bugfix/eraser` HEAD `4d1727916a7d2474841156de753ee3d24d2ad7c7` 上继续，不切换/重放旧 Git 状态。教室大屏尚未实测本次及上一轮场景曲线。初始代码与旧控制器 240Hz 回放均证实短快划偏大并非最新提交缩短了时间常数。

本轮只更改 `ResolveConfig` 的 DirectTouch 和 ScreenPenHybrid 标准以上证据/增长参数：Touch start/full/decay=80/180/300ms、tau=220/180ms、log rate=4/6每秒；ScreenPen 对应80/200/350ms、tau=200/160ms、原 log rate4/6不变。Mouse/间接模型、ScreenPen 的速度目标与 beta、Touch 小/中/大屏场景权重和全部 DIP/经验回退、精细平台/迟滞/面积转换/尺寸会话均未调整。普通 Touch 面积下限单独保持原120/100ms、6/8每秒增长；诊断的 `evidenceCapDIP` 由现有证据只读推导，不引入新的许可状态。

## 已执行：同一生产控制器的无PDB ARM64 探针

用本机 ARM64 `cl` 直接编译仓库 `Draw3.SpeedEraser.cpp` 与有界命令行探针，未修改工程配置或输入测量。Surface 合成标尺28×19cm、2880×1920、192DPI，B=32、中灵敏度、面积关闭；先0.6×enter普通移动2秒至B、证据0，再以1.5×large快动，240Hz输入/120fps帧独立调度。旧源码的100/150/200ms结果与用户提供的 Mouse 32/32.01/32.80、ScreenPen 32.27/37.11/45.39、SmallTouch 43.10/58.50/80.31 DIP 对齐。

| 模型 | 100ms | 150ms | 200ms | 500ms | 1000ms | 0.9×最大目标首次到达 |
|---|---:|---:|---:|---:|---:|---:|
| Mouse，旧/新均相同 | 32.00 | 32.0119 | 32.8015 | 82.0935 | 153.322 | 见CSV |
| ScreenPen，旧 | 32.2693 | 37.1074 | 45.3882 | 132.688 | 160 | — |
| ScreenPen，候选 | 32.00 | 33.0491 | 38.0394 | 112.005 | 157.386 | 704ms |
| Touch，旧 | 43.0987 | 58.4983 | 80.3089 | 154.013 | 160 | — |
| Touch，候选 | 32.00 | 32.8963 | 37.9570 | 108.050 | 155.941 | 746ms |

同一探针在50/80/120/250/300ms及快段后1.5s继续普通移动的全程峰值、峰值时刻、越过1.25B/2B/0.9目标时间和证据见 [warm-burst-comparison-20260925.csv](research/warm-burst-comparison-20260925.csv)。各候选回放的快段后峰值未再超过快段内峰值；Mouse 行不变。240Hz请求80ms实际为79.167ms，不能写成精确80ms。上述三种强速度分别是2850 DIP/s、525 mm/s、375 mm/s，不是同一真人动作。

独立频率探针的 Surface Touch/ScreenPen 共96组（60/125/240/1000Hz输入 × 30/60/120/144fps × 80/150/500ms），按量化后的真实快段时长与1000Hz对照，最大快段末相对差0.591%。60Hz请求80ms实际83.333ms。局部往返1秒使 ScreenPen/Touch 增长至约157/155 DIP；四次80ms孤立快划各自不超过32 DIP，0.8s普通/同位置间隔后证据约0.004–0.007s，未连续预充。完全无Move时快段后不再增大并最终回到16 DIP、休眠。面积辅助普通拖擦在0.15/0.25/0.40/0.80/1.20s与旧面积增长逐点一致，最大差0 DIP，清扫证据始终0。

合成教室大屏139×78cm、1920×1080、144DPI的3秒局部圆轨迹仍保持原场景目标：100–322mm/s=32 DIP，400/432.376≈32.415/33.113 DIP，600/800/1000/1300≈42.155/67.133/109.347/160 DIP。其间没有提高物理速度阈值来掩盖短快划。以上为公式/控制器合成证据，不是产品入口或实机手感。

Surface/教室/经验 Touch 与物理/DIP ScreenPen 的临界速度（刚过 enter、目标2B、近large、超large）共20组3秒局部圆回放，实际与合法目标最大差0.0775 DIP，稳态证据尺寸上限均覆盖目标，见 [可达性 CSV](research/warm-burst-reachability-20260925.csv)。本轮在测试源码新增面积开关、暖状态和完整频率矩阵；原有非 Touch 跨段与产品光标/几何回归保留。以下先记录初期构建阻塞，再列最终可执行结果，不把初期的语法编译冒充 headless 通过。

## 初期构建阻塞与局部验证

按仓库要求从当前 VS 安装定位 ARM64 原生 MSBuild，单次 PowerShell 内保持原 Path 并移除重复 PATH，设置 `MSBUILDDISABLENODEREUSE=1`，运行完整 `InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal`。普通与沙箱外构建均在主项目 `IdtPlug-in.cpp` 和 `speed_eraser_tests.cpp` 的 `vc143.pdb` 报 `C1041`，退出1；没有出现有意义的源码错误。两个 PDB 文件可独占打开、磁盘空间充足；临时备份/重新生成仍复现，原文件已恢复。检测到 Visual Studio `devenv.exe` 与 `mspdbsrv` 持续运行，但尚未证实它们正占用本仓库 PDB 或构成唯一根因；主会话未关闭它们，也未修改工程、编译器或全局配置。此为初期状态，后续缓解与最终完整构建结果见下节。

替代验证明确限界：仓库 `Draw3.SpeedEraser.cpp` 与独立 ARM64 探针以 `/Z7` 无PDB编译/运行退出0；新增 `speed_eraser_tests.cpp` 及修改的 `Draw3.Host.cpp`、`Draw3.DrawingController.cpp`、`Draw3.HiddenWindowTest.cpp` 用现有 v143 模块 IFC 和 `/Z7` 分别语法编译退出0。它们在此阶段不能代替完整 solution、headless 和产品隐藏验证。此前基线隐藏测试整项退出1，DComp/ULW 各有 Freeze siblings 与 Host stop HWND 两条 owner 断言，本轮不删除或归因环境。

## 最终本机验证

微软的 [/FS 文档](https://learn.microsoft.com/en-us/cpp/build/reference/fs-force-synchronous-pdb-writes?view=msvc-170)说明该参数经 `MSPDBSRV.EXE` 串行化 PDB 写入。仅在一次 PowerShell 进程的 `CL` 环境变量保留原值并追加 `/FS`，其余仍为完整 `InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal`：最终构建退出0，没有修改工程/全局设置。原命令的 C1041 与这次缓解后通过分开记录，不能声称原环境问题的唯一根因已经确定。最终日志 `%TEMP%/inkeys-warm-burst-full-build-fs-drain.log`。

`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 在新增完整测试下退出0：精细区量化/稀疏1728组0失败，暖状态频率矩阵240组最大快段末差0.590517%，既有 sample/frame 最坏4.61275%（仍低于5%）；Mouse暖状态100/150/200ms为32/32.0119/32.8015 DIP，Touch/ScreenPen短段与持续清扫符合前表，20组临界目标可达。初次 headless 只失败旧“40mm平滑折返在0.6s就须>64DIP”时间断言：新响应0.6/1.0/1.4s约36.0/73.1/87.4 DIP。仅将此已授权替代的时间断言改为0.6s受控、1.0/1.4s可达中间尺寸；其他测试与容差未放宽。最终日志 `%TEMP%/inkeys-warm-burst-headless-final.log`。

`Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test` 最终仍退出1，且仅出现既有四条 Window Service owner 断言：DComp/ULW 分别的 `drawpad and presentation remain Freeze siblings`、`Host stop leaves hidden Window Service HWND intact`。本轮新增 Touch/ScreenPen 参数诊断断言和原有橡皮接入/首点/光标/当前半径、Undo/Redo/UInk及尺寸断点断言未报失败。DComp/ULW 的合成手动大屏普通 Touch 约32/31.818 DIP，光标与实际新增几何直径相等；面积辅助稳定为39 DIP，停止Move后回到16 DIP；Mouse持续清扫光标达到160 DIP。ULW 初次因早于待消费模型输出取休眠快照出现额外测试失败，测试仅等待旧输出排空后再执行原帧数断言，最终该断言通过；没有改变生产增长、面积或窗口所有权。最终日志 `%TEMP%/inkeys-warm-burst-hidden-drain-stderr.log`。整项不能记为通过，也不把 owner 问题未经证实归为环境故障。

真实 Surface、教室 Touch/ScreenPen 的手感及不同接触面积仍待人工验收。启用现有输入与橡皮诊断后应记录来源/场景/单位/B/增益、短窗速度、清扫用速度、证据/尺寸上限、目标/实际DIP和同接触光标/几何，并为普通/清扫片段加标签；250ms控制台快照不能还原100ms脉冲。构建生成的 `PptCOM.dll` 差异在初始工作区干净的前提下恢复原字节。本轮由主会话顺序完成，无子Agent、后台Agent、Computer Use 或交互式 GUI；用户后续授权 commit/push，任务不 finish 或 archive。
