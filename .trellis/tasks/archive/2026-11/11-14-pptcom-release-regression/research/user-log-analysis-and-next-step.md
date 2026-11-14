# PowerPoint / WPS 实机日志对照与下一步

日期：2026-11-14。阶段：分析用户回传日志与 GitHub 补丁，本轮没有修改业务代码、运行 GUI 或编译新修复。

## 输入与可信范围

- PowerPoint 附件：6444c10e-43fa-4b9e-b4f0-7795c0a0df46/已粘贴的文本.txt，4051 行，PptComLife 记录 3929 条，pid 12240。
- WPS 附件：1bbfa634-1e69-4c8e-901f-b905d5dfe2e1/已粘贴的文本.txt，2826 行，PptComLife 记录 2709 条，pid 34688。
- 两者模块 MVID 都是 060eae06-2aec-4f85-a990-7ea1d7ea1753，与当前已构建的诊断 DLL 匹配；可排除前次 DLL 未同步的问题。
- 两份 seq 均从 1 连续到末尾，没有缺号；结构化分析、源文件 SHA256 与引用原行号见 validation/user-ppt-wps-log-analysis.json。
- 原日志没有记录 QPC frequency。本机只读 Stopwatch.Frequency 为 10,000,000，下述秒数按同机该频率计算；ticks 与原日志行号才是原始证据。

## 确认事实一：PowerPoint 首次空档在 SlideShowWindows 读取边界，不在 Release/GC/Unadvise

| 日志行 | qpc | 事实 |
| --- | --- | --- |
| 3245 | 59079898296 | 收到 DISPID 2025，即 PresentationBeforeClose 提示 |
| 3246 | 59079901811 | 开始 ActivePresentation 读取 |
| 3247 | 59079966947 | 成功返回旧 presentation RCW 42，临时取得增至 2 |
| 3248 | 59079978582 | 收到 DISPID 2004，PresentationClose 通知 |
| 3251 | 59080059555 | 开始 Application.SlideShowWindows getter |
| 3252 | 59387142343 | 同一个 call=3150 返回窗口集合 RCW 566；相隔 307082788 ticks，约 30.708 秒 |
| 3254 | 59387155779 | 对该集合读取 Count，返回 0x800706BA |
| 3256–3259 | 59387158264–59387173390 | 释放本次集合与临时 presentation，前者 remaining=0，后者留下长期 root 的一笔 |

这段空档没有 release.begin 或 cp.unadvise.begin，不能归因为 ReleaseComObject、GC 或事件解绑卡住。现有日志的 qpc 位于前后输出边界，间隔还包含 call.begin 的 Console 写入及返回后的诊断锁/记账时间，不能严格等同于纯 getter/RPC 执行时间。PowerPoint 关闭、RPC/Automation 内部等待和控制台输出等待目前还不能完全区分；不能称已证明 COM 内部死锁。下一版应在 begin 输出返回后、真正调用 getter 前取时钟，并在 getter 返回后、end 输出前锁存耗时，以排除此混淆。

## 确认事实二：0x800706BA 未被识别为旧绑定不可用

- 此错误是 HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE)，对应 Win32 1722（0x6BA）；它不是现有三个 Office busy HRESULT。
- PowerPoint 后续 22 次 ActivePresentation 和 22 次 Presentations 读取都返回 0x800706BA，owner.maintenance 持续报告 lifecycle=Unknown、app=32、presentation=42、events=1、pending_cp=0。
- 故障绑定自关闭以后没有 service.stop/service.finally/cleanup.begin，也没有 cp.unadvise.begin。pending_cp=0 只表示没有退休连接，不表示当前事件已解绑。
- 当前位置：PptCOM.cs:1302–1314 的 IsDisconnectedComObject 没有 0x800706BA，外层 ObserveBoundShow:1473–1484 将其归入 Unknown 并重试。ComEventConnection.cs:141–147 的 IsDisconnected 也没有该错误；仅修前者，清理时后者仍可能把失效 CP 保留进 registry。
- 根据全部记录复算，PowerPoint 末尾 acquired=914、released=911；仅剩 application 32、presentation 42、connection point 43 各一笔已跟踪取得。已记录的临时对象没有额外不配平。它们是仍未退出的 binding 所保留的长期引用，不能把这三笔直接归因于某个 xx.yy.zz 遗失 temporary。

## WPS 对照

- WPS 在日志 2757 行 ActivePresentation 报 0x80004005 后，独立 Presentations.Count 成功返回 0（2761 行）。
- 随后 binding.disconnected → service.stop → FullCleanup → Unadvise 成功 → CP、presentation、application 释放，root 的 remaining 均为 0。
- 60843970721 已汇总 acquired=612/released=612、outstanding=0。用户标记的 60865216853 是后续一次无绑定轮询的汇总，622/622、outstanding=0；原绑定在这之前已经清理。
- 因此同一套显式释放逻辑在 WPS 上能完成；当前差异落在进入清理前的关闭观测/错误分类。

## GitHub 核查

提交：[YU322142/Inkeys@997dc24e](https://github.com/YU322142/Inkeys/commit/997dc24ecb0748446645bac9963e8de491beec75)。实际生产改动是从 ActivePresentation catch 的提前 throw 条件中移除 TryGetBusyComErrorCode，使 busy 也有机会读取独立 Presentations.Count；Count 非零或不可读时仍保留绑定。另有测试、随附二进制、说明和构建/嵌入脚本。

当前 [issue #269](https://github.com/Alan-CRL/Inkeys/issues/269) 标题和正文确实已撤回，称重新确认官方版本已无此前问题。这只能界定该报告的当前状态，不能替代本次现场日志，也不能单据撤回判断补丁对错。

**该补丁修复一个可成立的 busy 边界，但单独应用不会改变本次已观测到的分支：**

1. 30 秒等待之前 ActivePresentation 成功，不进入它修改的 catch。
2. 等待之后 0x800706BA 本来就不是 busy；当前代码已经尝试 Presentations，也得到相同 RPC 错误。移除 busy 提前跳出对这条分支没有效果。

可借鉴 Count=0 才确认关闭以及取消关闭/仍有文稿/Count 不可读的回归矩阵，不整体引入 fork 的二进制或工具脚本。

## 下一步建议：先回归，再做两部分最小修复

### A. 确定的旧绑定错误分类缺口

在 service 的 IsDisconnectedComObject 与事件连接点的 IsDisconnected 中一致处理本次观察到的 0x800706BA，使失效绑定经原 owner finally 清理，并释放已不可用的 CP 取得。不要扩大为“任意 COMException 都断连”，也不要将正常 WPS 的 E_FAIL 一概归为断连。

此项能修复 RPC 失败后无限重试/保留旧 roots 的问题，但不能消除此前已经进入 getter 的 30 秒等待，必须单独验收。

### B. 关闭通知与旧图查询的时序

- 当前 DispatchBoundEvent（PptCOM.cs:903 起）对 2025 发轻量提示，对 2004 没有状态处理；观测对 stamp 的有效性复核又在窗口/页面读取之后。
- 日志恰好展示 ActivePresentation 返回旧对象后，2004 到达，owner 随后仍查询 SlideShowWindows。应让关闭通知进入现有 owner 的观测失效/关闭复核机制，在后续窗口/View 查询之前复核是否已有新关闭提示，并优先使用必要的独立文稿数量证据。
- 成功 Count=0 才确认最后文稿关闭；Count>0、取消关闭和数量不可读保持可恢复路径。关闭事件不是“所有文稿已关闭”的证据，不能直接在 callback 中 FullCleanup/Release。
- PresentationClose 本身也发生在文稿移出集合的阶段，不能误称为整个 PowerPoint 进程已退出。既有在途 COM 调用仍由 owner 完成/返回后清理；本地加超时或另起线程再强制 Release 不能安全撤销已经发出的 getter。
- 具体最少新增的状态/检查点应由下列时序回归约束；不能只改错误码后就声称 30 秒问题已解决。

### 补足一次关键计时

为 getter 添加不包含控制台输出的真实调用耗时；保持现有 qpc 作为时间线，但不要直接用两个日志 qpc 的差给 RPC 下结论。若真实调用耗时远小于记录空档，再单独排查 Console/输出等待，避免诊断本身影响关闭行为。

### 必须覆盖的回归

1. ActivePresentation 成功返回旧对象，但在返回与 SlideShowWindows 读取之间收到关闭通知：不能继续用旧观测调用已关闭对象图。
2. SlideShowWindows 或 Count/ActivePresentation 返回 0x800706BA：服务退出并且 CP/root 每笔清理，不再无限 Unknown。
3. Unadvise 返回同一 RPC 不可用错误：不留下只能永久重试的失效 CP lease。
4. 三个 busy HRESULT + Count=0/非零/不可读；取消关闭后仍能恢复，不误关其他文稿。
5. WPS 现有成功路径、事件缺失轮询回退、逐级具名访问与子先父后释放均保持。

随后按既有流程：托管与原生夹具 → 手动同步本次 DLL/TLB（TLB 变化时）→ 完整 Debug|ARM64 solution → EXE 内嵌哈希 → 用户分别复测 PowerPoint/WPS。实机验收需同时满足关闭阶段不再出现长空档、原 binding 真正进入清理、对应 service-end 配平且 pending_cp=0；不只看进程在等待后最终消失。

## 官方参考

- [RPC_S_SERVER_UNAVAILABLE](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1700-3999-)：Win32 1722，不能混为 Office busy。
- [PresentationClose](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.application.presentationclose)：文稿从集合中移出时的通知。
- [PresentationBeforeClose](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.application.presentationbeforeclose)：具有 Cancel 参数，不能凭该事件直接判最后文稿已关闭。
