# 实现与验证记录（2026-10-19）

## 授权与交付状态

用户明确批准规划并要求实现；代码、设置、三语和无窗口验证已完成。用户随后确认改动生效，并明确授权结束任务、提交commit。按实现提交→归档→会话记录收尾，不push。代理未启动GUI/Computer Use或实际系统Hook验收；用户未逐项报告的Win7/设备覆盖仍不作扩大声明。

## 已实现

- MouseHook 专用受管线程保留；回调只做固定原子鼠标键缓存、一次稳定路由读取、复制标量和有界 SPSC 提交，普通移动立即链传。原100ms业务和任意收起 callback 已移到 Bar owner。满队列、未续租等无法接纳时放行，提交后才抑制 PPT wheel。
- Bar 原有顶层和嵌套轮询消费全局事件，PageControl 本地轮另用有界 mailbox；唯一 owner 分别累计120单位，保留本地 Selection 门禁。PPT 捕获 token 原样到最终业务队列；同场 EndShow 有界。不恢复旧键盘接管/长按循环，不添加第二套 HiMsg 翻页队列。
- 四项 Inkeys3 Config 独立整数策略及默认 Drawing=1，其余0；旧 Inkeys2 bool 不迁移、不双写。两个样式均有四行下拉，三语说明及尺寸测量已同步。
- 真实 accepted stroke 通知覆盖画笔、形状、激光与实际临时橡皮；逻辑场景/Host/PPT身份退休旧事件，短笔完成不丢起笔。原聚合 activity 通知保留。
- 主按钮越过原阈值且策略允许后单次 fold；内部收起更新当前手势基线与呈现 barrier，保留绝对抓取点。栏外点击以100ms候选、栏内 Down 与场景/策略版本取消。

## 编译

使用现有 InkeysRepo.sln、Debug|ARM64，由 vswhere 查找当前 Visual Studio 的 ARM64 原生 MSBuild；同一个 PowerShell invocation 先 Remove-Item Env:PATH，并设置 MSBUILDDISABLENODEREUSE=1。

1. 受限 runner 首次运行约5分钟结束，exit1但0errors/0warnings；日志没有源码诊断，与既有记录的受限 MSBuild 通信问题一致。没有依据该失败改源码/依赖。
2. 正常宿主给出实际编译诊断：新增配置测试需要既有 config_metadata_tests 相同的单文件 CRT 定义；根会话新增辅助函数访问 Bar protected 状态。已分别按既有测试约定补编译定义、把消费/命中改成 Bar 成员入口，未扩大字段可见性。独立检查复核了修正。
3. 源码编译完成后，ARM64 link.exe 的增量链接出现 LNK1000 / C0000005 内部崩溃。仅本次命令 /p:LinkIncremental=false 执行完整链接；未编辑项目默认链接选项或更换工具链。
4. 最终完整 Solution **exit0，0errors，1warning，00:00:08.55**。唯一最终warning是Headless旧EDITANDCONTINUE与命令行INCREMENTAL:NO的LNK4075；此前重编译还输出既有第三方/Setting等warning，未为清零修改无关源码。最终日志 `build-debug-arm64-complete.log`。后置脚本有pwsh查找提示，MSBuild最终成功退出。

## 执行验证

全部正常宿主以 ProcessStartInfo UseShellExecute=false、CreateNoWindow=true、Hidden 执行，等待实际 ExitCode并保存stdout/stderr；Headless上限300秒、两探针各60秒。

| 命令 | 结果 |
| --- | --- |
| InkeysHeadlessTests.exe --no-window | exit0；Auto-collapse tests 0 failures，PASS animation correctness |
| Inkeys.exe --state-mode-production-test | exit0；状态/条件版本/FineDial/取消等生产探针PASS |
| Inkeys.exe --pen-width-production-test | exit0；preferences/state/FineDial PASS |
| Scripts/i18n.ps1 sync/check | PASS；en-US与zh-TW均369/369 |
| task.py validate | PASS；implement/check context均13项（新增本次生产规范后14项） |
| git diff --check | PASS |
| 原编码/BOM/CRLF | 33个已跟踪变更文件全部保持；详见research/format-check.json |
| 独立fresh check | 无已确认阻塞缺陷；详见research/check-implementation.md |

新增测试直接调用生产 Bridge/RoutePublisher/Accumulator/EndShowGate、HostAcceptedStrokeState、Config codec及Drag/Outside策略；覆盖满队列无幽灵副本、并发快照、细分delta、旧token、短笔/场景ABA/Clear/重启、四字段严格读写与旧配置隔离。既有 Raw/Touch/状态/笔宽回归同批通过。

## 传输入口测量（本机本次运行）

| 条件（各30,000次） | P50 | P95 | P99 | 最大 | 放行数 |
| --- | --- | --- | --- | --- | --- |
| consumer暂停 | 500ns | 800ns | 900ns | 636100ns | 29745 |
| 并发consumer | 600ns | 1000ns | 1100ns | 837300ns | 1805 |

暂停时有效容量255，之后29745次全部放行；所有已接纳事件恰好消费一次。测量仅为本应用生产桥入口，含计时/调度影响，不包括系统Hook投递、CallNext下游、Office或完整GUI手感，不能作为真实光标零卡顿保证。

## 待人工验收

真实鼠标/触摸板、PPT/WPS普通页/末页/黑页、管理员焦点/UIPI、Win7、触摸/笔、设置缩放与长文本，以及拖动内自动收起的真实动画/抓点连续性未运行GUI验收。源码审查和无窗口测试不能代替这些硬件/窗口行为。当前编译产物为Build/ARM64/Debug/Inkeys.exe。

## 人工确认与关闭
2026-10-19 用户确认改动生效，要求结束相关任务并提交commit。本次仅更新验收和收尾记录，产品代码自上轮通过验证后未变；沿用上述实际验证，不重复构建或启动GUI。

原始构建与测试日志保存在本机Build/ARM64/Debug/mouse-hook-autofold-logs/（保留原始输出，不纳入源码提交）；本文中的日志文件名均相对此目录。
