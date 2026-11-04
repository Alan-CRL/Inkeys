# Research: 2026-11-01 COM 翻页剩余阻塞与现有诊断覆盖

- Query: 指定 PPT 第 3→4 页鼠标控件仍阻塞、键盘正常时，现有源码与日志能区分哪些阶段？
- Scope: internal；只读当前 Inkeys 源码，并读取 Inkeys-draw 对应文件作规范化文本对照。
- Date: 2026-11-01
- Task: 10-30-ppt-gif-mouse-navigation-stutter

## Findings

### 1. 当前运行工作区与被审阅工作区源码不一致（重要前提）

主会话 Windows 窗口盘点显示运行程序路径为 `D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/Inkeys.exe`。本研究未枚举进程、未读运行内存或验证实际内嵌 DLL；以下只证明两份磁盘源码不同，运行产物归属由主会话单独核验。

按 UTF-8-SIG 解码并规范化换行后，四份对照文件均仍不同（排除了单纯 BOM/EOL 差异）：

- `Inkeys-draw/PptCOM/PptCOM.cs:310-330` 的刷新直接调用完整 `ReadShow`，没有 stablePresentationTopology/session、fast 分支或 `[PptOwner]` 新诊断；`PptComService:1257-1258` 仍先刷新 descriptor 再处理命令。
- `Inkeys-draw/PptCOM/PresentationDescriptor.cs` 没有 `ReadFastShow`。
- `Inkeys-draw/PptCOM/SlideShowSession.cs` 没有诊断字段，有界排水仍是旧 `while(true)`。
- `Inkeys-draw/Inkeys/IdtPlug-in.cpp:134-160` 没有 navigationEpoch / PublishNavigationWaiting / `[PptNav]`；当前 Inkeys 对应代码有这些新增路径。

若实际运行 EXE/DLL 来自上述旧源码，旧的全文稿扫描和没有等待发布机制足以解释用户现象；但不得仅据源码推断二进制。该差异也意味着不能把 Inkeys-draw 的一次测试直接记为 `f1267e12` 修复回归。

### 2. 当前鼠标链中仍有多个同步 COM 阶段

| 阶段 | 当前源码位置 | 事实与可能等待点 |
| --- | --- | --- |
| native 入队 | `Inkeys/IdtPlug-in.cpp:149-189` | outstanding 去重；成功入队后在业务锁外 PublishNavigationWaiting，然后通知消费者 |
| native 业务 | `IdtPlug-in.cpp:636-656` | Next/Previous 同步调用服务；返回后还有 FocusPptSession |
| native COM 包装 | `IdtPlug-in.cpp:504-527` | `NextSlideShow`/`PreviousSlideShow` 为 void，没有把 managed 成败/超时结果交给 UI |
| owner 等待 | `PptCOM/PptCOM.cs:1625-1641`，`SlideShowSession.cs:359-367` | 请求进入至多 8 项邮箱，同步等 2000 ms |
| owner 执行前 | `PptCOM.cs:1681-1696` | Next 必须先 GetCurrentSlideIndex，取得 View → Slide → SlideIndex；polling==1 还会再查一次 |
| 真正命令 | `PptCOM.cs:1705-1713` | 再次取得 View，然后 dynamic Next/Previous |
| 命令结束 | `PptCOM.cs:1716-1717` | 标记需要重新观察 raw 页码并刷新 descriptor |
| descriptor | `PptCOM.cs:362-384`，`PresentationDescriptor.cs:369-448` | 同场 fast 仍同步读取 FullName、Name、View.State、View.Slide 的 index/ID、Slides.Count；fast 不是无 COM，单项仍可能阻塞 |
| 发布后附加读 | `PptCOM.cs:420-427` | descriptor 已 Publish 后，额外同步读 Application.Caption 更新 legacy 名称缓存；可延迟 owner 接受后续命令 |

以上等待点只是代码支持的候选，尚无本轮实机分段数据证明某个成员实际慢。所有对象获取仍在 owner，临时 View/Slide/Slides 均有 finally 释放；没有证据据此把等待归因为新增 RCW 泄漏。

### 3. 同一 owner 的其他工作也可能阻塞“入队→开始”

`PptComService:1320-1344` 顺序为必要 ObserveBoundShow → 有界命令排水 → descriptor → 排水；500ms 维护到期后还做 RetryRetiredEventConnections、MaintainBinding、再次 Observe、descriptor、annotation。

- `ObserveBoundShow:1197-1281` 无分段计时；依次读 ActivePresentation、SlideShowWindows.Count、SlideShowWindow、Slides.Count、View.State、View.Slide/SlideIndex、HWND。若这里卡住，已入队鼠标命令暂时不能开始。
- `MaintainBinding:1114-1133` 调用 ROT 全扫描。`GetAnyActivePowerPoint:895-1082` 遍历 moniker、取得文稿 Application、候选 ActivePresentation/SlideShowWindow/Active；即使已有稳定 binding，也会按维护期限执行。扫描和每项 COM 的耗时均未单列日志。
- `RefreshSlideShowAnnotationTool:1401-1415` 又取得 View 并读可选 LaserPointerEnabled/PointerType，也没有耗时日志。
- 命令后设置 showObservationPending，先读取 descriptor，然后下一轮再观察 raw。native 必须等 raw/descriptor 对齐。唤醒事件一般允许立即继续，因此不能据此宣称必有固定 500ms 延迟；重复的 COM 读取仍应计时。

### 4. 等待标记最长只跟随 native 同步调用，不保证覆盖执行中 COM 长尾

`SlideShowExitRequest.Wait:268-272` 在 2 秒到期时，只有尚未执行的请求会 cancelled；执行中的调用不被取消，仍向请求方返回 -1。公开 `NextSlideShow` 为 void，丢弃该结果。native 的 CompleteRequest 在服务返回后（并经过 Focus）就清除 outstanding 和导航等待（`IdtPlug-in.cpp:1471-1478`）。

因此可以存在确定的语义序列：owner 已进入 Office → 等待者 2 秒超时 → native 清除三点 → Office 数秒后才完成 → 后续观察/descriptor/画布才更新。若本轮日志同时出现 `PptOwner timeout ... executing=1`、较早的 `[PptNav] complete` 和较晚的同 id owner complete，即可把后半程无三点与该路径对应。此时不能声称请求已取消，也不能自动重试导致双翻页。

### 5. 当前日志能量化的阶段及盲区

- `[PptNav] enqueue/complete`：由 `INKEYS_PPT_TIMING=1` 开启，经 IDTLogger 写日志。enqueue 位于等待状态发布之后，不是精确鼠标 Down；complete 也包含 native 取服务、COM 封送、managed 等待及后续焦点交接。
- `[PptOwner] enqueue/start/complete/timeout`：`INKEYS_PPTCOM_TRACE=1` 开启，另外必须启用 `Experimental.Inkeys3.ConsoleOutput.PptCOM`，否则 `SetConsoleOutputEnabled(false)` 已将 Console 设置为 Null（`IdtPlug-in.cpp:334`，`PptCOM.cs:226`）。此开关在静态字段初始化读取，测试应在新进程启动前设置环境变量。
- owner start→complete 的 ms 是整个 ExecuteOwnerCommandCore，包含 Next 前页面检查、View 取得、实际 Next，并不是纯 View.Next 的时间。现有日志没有 QPC/UTC 字段；重定向 stdout 与带时间戳 native 日志做跨流关联时需注意缓冲/时基。
- owner enqueue→start：可见排队空档，但不能单独区分 Observe/ROT/annotation/前一命令。缺少这些阶段的开始/结束计时。
- `[PptOwner] fast/full ms`：覆盖 reader，能确认是否仍全文稿扫描，以及 reader 自身耗时。不覆盖其之前的 Observe、ROT，也不覆盖之后 Caption。
- `[PptSync] native_observed`：native 发现 shared raw 页码改变才建立基准（`IdtPlug-in.cpp:919-928`）；当前页 descriptor 被接受后才补输出同 target 的历史 QPC（1252-1254）。点击到 raw 变化以前的等待完全不在原 descriptor/input_open 指标内。直接键盘可先让 PowerPoint 翻页；鼠标先经 COM，故只比较 native_observed→input_open 可能掩盖真正鼠标差异。
- 相关画布/UI 后段 `[PptSync] target_published / canvas_presented / page-ui-commit / input_open` 保持原身份门；`Draw3.PptTiming.h:24-39` 明确输出 qpc 与 frequency，可做精确后段差值。
- 若 owner complete 很快，而 native PptNav complete 很晚，另检查 `FocusPptSession:609-625`（进程/窗口资格读取及 SetForegroundWindow）；现有 trace 未给此段边界。

### 6. 建议本轮实机记录方式（未擅自实施）

1. 先固定 EXE 路径、EXE 内嵌 DLL 哈希、源码 worktree。旧源码重现与最新产物回归分开。
2. 同一已确认产物启动前设置两环境变量，临时启用 PptCOM Console，收集 stdout/stderr/native文件日志；记录开始前配置值并恢复。
3. 每次给鼠标 Down 的时间、PowerPoint 新页首次可见时间、三点首次可见/消失、页码提交、画布可写时间。冷/暖、同场与新场至少分开，指定 3→4 与 4→5；键盘对照用相同状态。
4. 若现有日志只把长尾定位到 owner start→complete，下一步最小诊断应在 GetCurrentSlideIndex、View 取得、Invoke Next 前后分别记 QPC；若是 enqueue→start，则给 Observe/ROT/annotation 分段。不用关闭输入门或改成发键盘掩盖。
5. 若没有 native等待发布（旧产物）或发生 2 秒 timeout executing=1，则先按对应事实解释 UI。新产物的等待像素路径由另一研究代理核对，不能仅凭结构体测试宣称已显示。

## Files Found

- `PptCOM/PptCOM.cs`：唯一 owner、绑定维护、刷新、命令入口及聚合计时。
- `PptCOM/PresentationDescriptor.cs`：dynamic accessor、标量边界、fast/full reader 与临时 COM 释放。
- `PptCOM/SlideShowSession.cs`：有界邮箱、2000ms等待、执行中不可取消与完成通知。
- `Inkeys/IdtPlug-in.cpp`：native入队、业务执行、焦点交接、等待状态和 raw/descriptor 关联时序。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PptTiming.h`：QPC trace 字段定义。
- `Inkeys-draw` 对应四份文件：只读比较产物工作区差异的源码证据，不代表进程已加载版本。

## External References

本轮没有使用外部资料或引入版本/兼容性结论；使用仓库中既有可执行合同和当前源码。未改 COM 公寓、支持矩阵、依赖或项目配置。

## Related Specs

- `.trellis/spec/ppt-interop/index.md`
- `.trellis/spec/ppt-interop/com-contract.md`
- `.trellis/spec/ppt-interop/native-session-ui3.md`
- 既有 `research/investigation.md` 与 `research/validation.md` 的旧冷暖样本仅作历史，不当作本轮实测。

## Caveats / Not Found

- 未执行任何 GUI、Office、COM runtime、构建、测试或 git 操作；只写本研究文件。
- 当前源码支持多个剩余同步等待位置；没有本轮实测数据前不能选择一个属性作为根因，不能把整个 start→complete 命名为 Next 调用耗时。
- 另一工作区源码缺少新修复这一事实已通知主会话；实际运行 DLL/EXE 仍须由主会话核验。
- 真实 Office 退出与下一文稿、WPS/旧 Office、全量性能分布仍不能由本报告证明。
