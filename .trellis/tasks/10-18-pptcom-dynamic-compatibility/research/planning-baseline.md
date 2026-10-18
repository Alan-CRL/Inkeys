# 调查基线与已复现机制

- 日期：2026-10-18（Asia/Shanghai）
- 范围：20261014a 源码与仓库随附 PptCOM.dll；前轮只读调查的延续。
- 证据级别：源码事实、故障注入、历史实机记录及尚未验证项分别标识。
- 当前阶段：仅 planning；用户尚未批准产品修复。

## 1. 版本基线

- 工作分支 bugfix/pptcom；任务创建前 HEAD=5d319bbf（Revent 20261014a）。
- origin/canary=e7b4e214，其 20261014a 的 PptCOM/ 与 Inkeys/IdtPlug-in.cpp 与当前 HEAD 比较无差异。
- 本地未找到独立 0813a tag，因此不宣称完成了旧安装包逐字节对比。2026-08-11 的 043b495b 与 2026-08-13 的 b7e23e87 内 PptCOM.cs blob 同为 baf935a7f7602680990c771d1e9c2f5009583896，作为 0813 附近托管源码基线。
- 657f0954（2026-09-05）新增 PresentationDescriptor.cs、GetPresentationDescriptor 和 native 文稿目标消费。
- 10ea1e0d（2026-09-25）新增 IPptCOMSessionState、会话缓存/owner 与 native 准入门。
- 9d432cd6（同日）修复类型接口可读 HWND 而 InvokeMember 失败的实机回归。
- 1cd45e57（2026-09-27）调整原生工具接管，也是仓库随附 DLL 的最后修改提交。
- 724f28f4（2026-10-10）拆分 DLL/TLB 构建交接，取消自动复制；本次没有证据把用户问题归咎为产物错配。

## 2. 无窗口生产函数故障注入（本会话前轮已执行）

使用 Windows PowerShell/.NET Framework 反射调用现有私有生产刷新函数，仅为测试注入；不是 Office 访问采用反射的产品设计。加载仓库现有 DLL 与 PptCOM.Tests 里的 FakeGraph/FakeAccessor，不激活 Office，也不修改文件。

步骤：
1. 把服务器 reader 换成使用 FakeAccessor 的实际 PresentationDescriptorReader；设置 application/presentation/window 三个 fake 根，binding=7，Observe(7,true,100)。
2. 调用 RefreshPresentationDescriptorIfNeeded(true)，得到稳定同页快照。
3. 在 application.Name 注入 COMException 0x8001010A，再次强制刷新。
4. 撤销故障后调用非强制刷新。
5. 移除 fake HWND 属性，强制刷新。

对 Inkeys/PptCOM.dll 实际输出：

~~~text
Tracked DLL busy reproduction: oldCache=StableSlideIds newCache=TransientBusy pageStatus=Unknown
After recovery without forced refresh: status=TransientBusy, sameRevision=True
Tracked DLL missing HWND: status=StableSlideIds, pageStatus=Valid, hwnd=0
~~~

结论边界：
- 已证明新旧缓存对一次 busy 的发布不一致，以及恢复后稳定复核 deadline 会挡住非强制刷新。
- 已证明 reader 能发布 Valid/StableSlideIds 但 HWND=0；native 强门禁随后会拒绝此目标。
- Fake 不是真实 PowerPoint/WPS/损坏 COM，不证明旧版本支持，也不逐台解释用户反馈。
- 前轮故障注入未保存为可执行脚本；此文保存步骤、输入和结果。实施阶段应补到正式 managed 回归测试，避免依赖聊天或临时反射脚本。

## 3. 对应生产路径

- PptCOM.cs:269-312：keepStable 仅保护 presentationDescriptorCache，随后 session.Publish 仍传失败 candidate；失败也设置 next refresh=3 秒。
- PptCOM.cs:524-559：旧 busy helper 新增发布 Unknown 会话；需要合并恢复策略，不能只修 descriptor 分支。
- IdtPlug-in.cpp:663-667：有效页要求 raw 与 descriptor 页码/总数一致。
- IdtPlug-in.cpp:693-713：sessionApi 存在时仅依赖 descriptor HWND；GetPptShow 回退仅 !sessionApi 执行。
- IdtPlug-in.cpp:800-808：不可信目标暂停写入或撤销目标；这也是防串页保护，不能直接删除。
- PptCOM.cs:1643-1654：既有 GetPptHwnd 有 Win32 后备。
- PptCOM.cs:1805-1905：既有 Win32 后备只做可见性、标题文件名+应用关键词和唯一结果过滤，没有完整的窗口类/COM提供方进程关联，不能未经增强就当强身份。
- PresentationDescriptor.cs:89-126：属性/集合/方法采用 InvokeMember；HWND 特判 typed 接口优先。
- PresentationDescriptor.cs:347-423：完整拓扑扫描随刷新执行；forcePolling 时需评估调用负担，但未测出性能结论。

## 4. 历史实机证据与限制

9 月 25 日的 research/recognition-regression-state.md 记录过同一实际 PowerPoint 窗口：
- typed HWND=10227496、PID=37764；
- InvokeMember("HWND") 报 0x80020003；
- reader 可得到 StableSlideIds / Valid / 1 of 4，却 HWND/PID=0；
- 随后的修复改为 typed 标量读取。
这不是 dynamic 路径失败的对照证据，不能据此把其他成员统一改成反射或把 typed 固定成默认。
9 月 29 日的历史隔离 Office 测试后来证实某个本机 Office16 会话的 target/ready 链路可用，不能外推到 Office2010、WPS2013、所有位数或类型接口损坏设备。

## 5. 已有验证与程序集事实

- 前轮运行现有 PptCOM.Tests.exe：PASS，进程退出码 0；其主要 fake ledger 不能覆盖真实提供方访问差异。
- 随附 DLL 与本地测试 DLL 不同 SHA/MVID，但 RefreshPresentationDescriptorIfNeeded 的 IL SHA-256 相同：67C9E15EE52E3C3C325514998307DEDEBE494F526B894A51FCEA745015C62F80。
- 本轮只读加载随附 DLL：CLR image runtime v4.0.30319；AssemblyRef 为 mscorlib、System.Core、System、Microsoft.CSharp、System.Web.Extensions，均 Version4.0.0.0，没有外部 Office15 AssemblyRef。
- 内嵌 SlideShowWindow IID=91493453-5a91-11cf-8700-00aa0060263b。只证明当前内嵌元数据，不证明 MSO14/WPS2013 对该接口支持。
- PptCOM.csproj 的 .NET4、PIA15 引用及 EmbedInteropTypes=True 不是本轮新增加的最低版本要求。
- 产品 zh-CN/en-US/zh-TW 文案当前声明 PowerPoint2007 与 WPS2013+，任务保留该既有范围。

## 6. 规范冲突与执行环境

- 用户已明确 dynamic 优先、确定性释放。当前 com-contract.md:51 与旧 design.md:140 的统一 InvokeMember 规定冲突；本任务审批后需同步更新规范，当前仅记录，不改全局 spec。
- 当前 .codex/skills 目录为空，未发现 trellis-start/trellis-brainstorm 的 SKILL.md；按已提供 workflow.md、get_context.py 的 Phase1.1 指引和 trellis-research 定义执行规划，不自行补装或修改流程。
- PATH 中 python.exe 指向无法启动的 WindowsApps alias；任务脚本改用已有 bundled Python 的绝对路径执行，不改 PATH/系统配置。
