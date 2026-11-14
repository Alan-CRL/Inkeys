# PptCOM 关闭释放回归

## 目标与价值

当前交付：保存已确认的联动阻塞证据与用户决策，撤回未解决实机问题的候选修复，恢复到仅补三行GC的业务行为，保留调试诊断。未来另行实施多线程动态绑定，使旧绑定清理与新绑定建立可以并行。最初的Inkeys2对照与自然退出归因已修正；下文历史授权和交付不代表当前保留的候选行为。

## 本轮交付与状态

用户最新明确要求撤回后续候选修复，保留最初三行GC与调试输出；不在本轮实现架构优化。候选未通过实机验收，当前同步清理阻塞新发现的问题仍未解决。当前回退和验证记录见 research/rollback-to-gc-and-diagnostics.md。

## 背景与已确认事实

- 初始用户报告 POWERPNT.exe 残留且认为 Inkeys2 1019a 无此问题；最新同机复测 Inkeys2 也延迟，完全不启动 Inkeys 仍延迟，并确认进程驻留时新文稿可打开。最初的回归归属不再成立，需区分后台驻留与联动阻塞。
- 调查前基线 `e73673f7b1c91fd13cc5a4a946960d7b9934c646` 的 `PptCOM/PptCOM.cs:501` FullCleanup 只逐笔单次 Release，未保留原 final FinalRelease 或 GC 三步收尾；这是恢复 GC 前的历史状态。
- 当前回退版本的 `FullCleanup` 位于 `PptCOM/PptCOM.cs:523`，`:570–572` 已恢复 `GC.Collect()` → `GC.WaitForPendingFinalizers()` → `GC.Collect()`；没有恢复 FinalRelease，逐笔 Release 策略不变。
- `0eb1c7c5`（2026-10-18）删除了上述两项策略；`20261019a` 属另一分支，保留它们。调查前的后续清理计时提交尚未恢复它们；本轮仅恢复 GC 收尾。
- 已有最后文稿关闭检测和临时对象 finally 释放；未确认具体现场仍残留哪一笔引用。真实 Office 退出不能由已有 fake/in-proc fixture 通过推定。
- 用户后续明确确认恢复 Collect → WaitForPendingFinalizers → Collect；其他对象审计与现有原生夹具 43 项通过结果见 research/ownership-audit.md。
- 历史证据、候选原因和旧验证缺口记录在 research/release-regression.md。

## 要求

- R1：记录当前清理调用链和文件行号，并区分静态可确认缺口与运行时推断。
- R2：以源码内容和 git 历史确认释放策略变更的提交、合入时间，以及与 Inkeys2 1019a 的分支关系。
- R3：后续修复必须正确显式释放本模块取得的 dynamic/COM 引用，并在显式清理后强制 GC；不能只赋 null 或仅依赖 GC。
- R4：遵循 minimal diff，保留 dynamic 常规访问、COM ABI、PowerPoint/WPS 既有分支；保持编码/BOM/换行，在关键逻辑添加适量中文注释。
- R5：不以强退 POWERPNT.exe 或主动 Quit 用户 PowerPoint 代替引用释放；失败事件解绑必须保留明确结果。

## 验收标准

- AC1（R1/R2）：调查含可复查的文件行号、提交差异和证据等级，能解释旧测试覆盖的边界。
- AC2（R3/R4）：业务代码仅修改 PptCOM/PptCOM.cs 的 FullCleanup，恢复原有 GC 三步收尾并添加简短中文注释；既有对象释放与事件逻辑不变。
- AC3（R3/R5）：恢复的 GC 位于既有 root 释放、字段清空和页码重置后；现有托管与原生 COM 夹具通过，不能以 GC 替代活引用的显式释放。
- AC4（后续设备验收）：关闭最后窗口后进程自然退出，马上打开下一文稿仍可绑定；关闭单个文稿不影响其余文稿；事件/轮询及 WPS 对照分别记录。
- AC5（本轮）：核对最小 diff、原编码/BOM/换行；执行无 GUI 的托管构建与现有原生 COM 夹具，记录真实退出码；不宣称真实 Office 退出已验收。

## 范围外

COM ABI 改造、全量包装器重写、无关线程/渲染改动、依赖/工具链升级、自动复制发布 DLL/TLB、GUI/Computer Use、Git commit/push 和远程写入。

## 后续复审与交付补齐

用户反馈仍复现并要求逐个审阅所有 dynamic、包装器和链式访问；随后确认 DLL 需手动同步。本轮已完成全量复审、真实 RCW 进入生产 FullCleanup 的补充探针、随附 DLL 同步和完整 ARM64 solution/内嵌哈希验证。详见 research/full-com-review.md。除补齐新 DLL 外没有继续改其他业务释放策略；真实 Office 设备验收仍待进行。

## 本次已授权：控制台引用诊断

用户确认新 ARM64 Debug EXE 仍有问题，要求添加 PptCOM 调试控制台诊断，再由用户实机测试。本轮实现获取/转移/释放计数、剩余 RCW 计数、关闭观测、事件连接点及服务退出诊断，不改释放策略或 ABI。COM 中间对象必须逐级命名、在 finally 先子后父释放；禁止 COM 链式属性访问。构建 native EXE 前先手动同步本次 DLL，再验证内嵌哈希。诊断跟随既有 ConsoleOutput.PptCOM 开关；不得额外 COM 获取/Release、不得强引用保活 RCW，不以日志通过宣称现场已修复。

## 实机回传后的结论与下一步（尚未实施）

已确认 PowerPoint 在 SlideShowWindows getter 前后日志边界存在约 30.708 秒空档（尚未剔除输出耗时），随后 0x800706BA 被当作 Unknown 重试而未清理；WPS 通过 Count=0 正常清理。第三方 busy 补丁不覆盖这条实际路径。下一步拟补一致的 RPC 不可用分类和关闭通知/旧观测查询的时序回归与最小修复；本轮只分析、核对远程资料和更新任务证据，没有改业务代码。详见 research/user-log-analysis-and-next-step.md。

## 本轮修复授权

用户已同意执行上一轮提出的断连与关闭观测修复，并要求在需要实机测试时交付。当前实施范围、非目标和验证步骤见最新 design.md/implement.md；本轮不由代理启动 GUI。

## 本轮候选修复交付

已按最新设计实现并完成 RED13→GREEN、47 项原生 COM 夹具及完整 Debug|ARM64 构建验证。DLL 已在主程序编译前同步，内嵌哈希一致。实机等待由用户按 research/close-fix-test.md 复测，技术验证见 research/close-fix-verification.md；当前任务不据自动化测试通过宣称真实关闭问题已解决。

## 第二次实机后交付

已确认关闭期间读取 Presentations 真实耗时28.142秒。最新候选改为2031最终通知驱动的绑定释放，之前的关闭提示不查询模型；其他文稿不被关闭但可短暂重连。RED4→GREEN、47项原生COM及完整ARM64编译/内嵌校验通过，等待用户按 research/final-event-test.md 复测；详见 final-event-fix-verification.md。

## 最新基线修正

第三份日志的等待已定位在 CP ReleaseComObject 内部33.458秒，Unadvise成功且最终配平。用户确认无Inkeys也驻留、新文稿可打开。后续第四次反馈已确认新联动受旧清理阻塞，见下节；第三份现场及基线修正见 research/third-user-log-and-baseline.md。

## 第四次实机：联动阻塞已确认

用户确认旧进程存在时新文稿可打开但联动等待，重启Inkeys则可立即联动。新日志唯一超过1秒的已计时调用为旧Unadvise 55.5777165秒，最终419/419配平；旧服务结束后2.112秒重启，新服务约0.269秒进入Active。当前目标是旧清理与新发现的进度隔离；此前2031保护只避免关闭后模型查询，未完成该目标。已记录下一阶段所有权/线程/共享页码边界和闸门验收方案，尚未实施；见 research/fourth-user-log-and-rebinding-block.md。两次结束放映后未响应仍待对应现场。

## 用户最终决策：先回退，架构优化后续实施

- 已确认问题：旧绑定Unadvise/CP Release等待阻塞单一服务owner，导致新文稿联动延迟；旧POWERPNT存在时重启Inkeys仍能立即绑定新文稿。结束放映后的两次未响应尚未定位。
- 当前要求：回到仅补三行GC的业务版本，保留调试输出；关闭延后/直接退休/ROT关闭让出/RPC分类扩展及对应候选测试一并撤回。
- 后续要求：采用多线程动态绑定，让旧清理等待期间新绑定已可工作；本轮只保存方向和验收条件，不提前实施。
- 当前验收：业务控制流相对HEAD仅保留GC变更，诊断不新增COM取得/重复命令；托管和原生夹具通过，DLL先同步后完整Debug|ARM64构建，EXE内嵌一致；保持编码/BOM/CRLF，不启动GUI，不commit/push。

## 收尾与归档授权

用户明确要求归档此次调查/回退任务，保存全部发现、未来多线程动态绑定方向及修改PptCOM后必须手动同步DLL的规范；完成commit并push、保持工作区干净。本次收尾范围不包括架构实施，不将仍存在的联动阻塞或结束放映未响应标为已修复。最终复盘见research/closure-retrospective.md。

## PR #270 审阅跟进

- [历史文案建议](https://github.com/Alan-CRL/Inkeys/pull/270#discussion_r4571375143)成立：已明确区分调查前基线与当前恢复GC的回退版本，并保留未恢复FinalRelease的事实。
- [日志归因建议](https://github.com/Alan-CRL/Inkeys/pull/270#discussion_r4571375147)成立：Advise失败只在实际point.Advise异常边界记录，FindConnectionPoint或缺少容器不再被重复误报为Advise失败。没有改变清理、绑定或回退策略。
- 复用既有测试：托管与47项原生COM夹具通过，完整Debug|ARM64 solution通过；DLL已手动同步，EXE内嵌哈希一致，TLB未变化。此跟进不涉及旧清理阻塞或多线程架构修复，未GUI测试。
