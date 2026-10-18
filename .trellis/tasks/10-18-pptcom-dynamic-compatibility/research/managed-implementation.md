# 托管实施记录（2026-10-18）

本文件只记录托管实现；主会话负责 native、规范、同次 DLL/TLB 交接和完整 solution 验证。没有启动 Office/GUI、修改系统 COM 注册、commit 或 push。

## 实现边界

- `PresentationDescriptor.cs` 保留内部可注入 accessor，普通属性、集合 Item、命令及 setter 使用真正的 C# dynamic。仅 HWND 保留已有 typed / 受限成员反射例外。名称缺失继续读取 fresh 页码，但不生成墨迹身份；合法结束状态和总数已确认、拓扑普通失败时保留基础结束页 DTO；busy 始终为 TransientBusy / Unknown。
- `PptCOM.cs` 把绑定、观测、命令、解绑与释放收束到既有 owner。公开页名/HWND/工具 getter 只读纯值缓存。ROT 列表独占取得，best 是选择别名；相同 identity 的新 ROT 返回、比较 helper 和轮询返回仍各配平。实际转移后清空临时槽，命令 View/SlideNavigation 具名 finally 释放。
- 轮询每次用 fresh show identity，替换可读但过期的旧对象；明确 Count0 清空旧 show；Count 缺失时用本次 show.Presentation 和 View/页码证据。失效 RCW、明确服务器断连或独立 Presentations.Count0 结束本次 binding，避免永久持有已关闭文稿。busy/普通缺成员只作 Unknown。
- descriptor 和 session getter 发布同一候选的失败语义。短暂 busy 不把旧稳定页重新标 Valid，独立 100ms retry deadline 不受 3 秒复核挡住，生命周期不活动时清理 retry deadline，避免空转。
- 同场 HWND 暂为 0 不拒绝对象定向 Exit；保留旧锚供 native 失效检查。本次得到不同非零 HWND 仍拒绝。对象取得前后均核对 owner 的 session/binding/generation。
- HWND 后备只在 Application.HWND 类型标量给出独立进程锚、COM Count1、show.Presentation identity 一致时尝试。选择同 PID 唯一 `screenClass` 放映候选并核对文稿名，返回前再次核对 PID/class/title。未知 WPP 类或证据不足维持 HWND0，不靠标题唯一猜测。

## 事件包装与确定性释放

现有 PIA EventProvider 的 `remove_SlideShowBegin` IL 显示先 `ArrayList.RemoveAt` 后 `IConnectionPoint.Unadvise`，因此保留 `-=` Action 不能证明失败后能再次解除。实现已删除旧 Action helper，改用 `ComEventConnection.cs` 的一个显式连接点。

raw sink 使用 EApplication IID `914934C2-5A91-11CF-8700-00AA0060263B`，IUnknown 后为四个 IDispatch 槽。内部接口声明为 ComImport，ICustomQueryInterface 显式映射事件 IID 和 IID_IDispatch。Invoke 的 DISPPARAMS、返回值和异常均为 IntPtr，只把 DISPIDs 2011/2013/2014/2025 发给纯值提示；不把事件 VARIANT 转成 WnObj RCW，不保存或盲目 Release 入站参数。普通 COM 访问不依赖事件接口。

`FindConnectionPoint` 返回是独立的 CP acquisition；`Advise` 成功后显式保存 CP、cookie、sink。解除成功或明确无连接/已断连才单次释放 CP。不能解除时先停通知，再将完整 CP/cookie 所有权转给静态 `ComEventRetirementRegistry`，后续 owner 可在旧 server 被 native reset 后继续重试。锁内只搬移 lease，锁外执行 COM。旧应用 root 可以按其自身取得配平，因为后续解除使用 CP 自己的独立取得。

每个事件提示捕获 binding revision。旧 binding 的回调忽略；同 binding 的 Begin/End 只使在途结果过期并唤醒，不直接猜测活动/退出或重新创建场次。owner 以 fresh Count/show/View 认定生命周期。

停止阶段执行本次解除及最多 3 次显式重试，持续拒绝返回 `-1` 并记录数值错误，registry 保留清晰待办 lease。下一 owner 继续重试。不会强制 GC、FinalRelease、杀进程或启动后台无主线程来伪造成功。已进入外部 COM 的 Unadvise/业务调用无法被本地 timeout 撤销；外部持续拒绝/阻塞是明确的未完成边界，不能宣称该路径已全部释放。

## 测试与当前结果

- 主会话记录的第一轮红测试：MSBuild exit0，测试 exit1，三个新增失败分别为生产 dynamic-only 图、dynamic 命令和 HWND0 Exit。
- 第二轮编译 exit0；六个已有结束页断言发现 identity 降级范围过宽，已修正。
- 第三轮托管编译 exit0、core tests exit0；随后独立检查补入生命周期、raw 事件和关闭路径回归。
- raw 接口第一版的真实 Advise 发现内部接口未以 ComImport 声明时事件 IID QI 失败；已修为内部 ComImport 加显式双 IID 映射，真实事件连接与参数账本在 x64、ARM64 均通过。没有公开新类型或静默扩大 TLB。
- 完整 TLB SHA256 已由主会话核对，与原产物完全相同：`C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606`。保留四个旧委托的私有 typeof 元数据锚，仅用于保留旧导出，不进入事件订阅路径。
- 修诊断前 ARM64 CLR 的 43 个真实 IDispatch/事件 cases 全部通过。x64 仅 Window.View#1 busy 失败，主会话取得 native HRESULT=8001010A、Invoke=1/Fault=1，但 CLR 实际重抛更早 `Connect/BindEvents` 的 InvalidCastException。证据为 `managed-errorinfo-before-x64.stderr.log`，确认日志 `Marshal.GetHRForException` 设置线程 IErrorInfo 造成污染。
- 生产日志已全部改为纯读取 `ExternalException.ErrorCode`（可读的真实 HRESULT），一般托管异常返回 0 明确表示未知，不写 IErrorInfo。补入回归验证既有 IErrorInfo 不被替换/清除、空状态不被创建；测试 finally 恢复原状态并逐个释放原始指针。修后同 x64 CLR 的 core 与全部 43 cases 通过，exit0；证据 `managed-errorinfo-after-x64.*`，native fixture 的原始故障发出行为未更改。无需改 busy 判定或放宽预期。
- 独立 checker 已补清理日志 IOException 保护、先恢复 retired lease 所有权后输出及 throwing TextWriter 回归，并冻结相关文件；最终双 CLR、DLL/TLB、完整 solution 和格式检查由主会话按同次冻结快照执行，本角色不并发运行 MSBuild。
- `ManagedCompatibilityTests.cs` 直接调用生产 owner/cleanup，借用测试自有页码槽，覆盖：同 identity 临时返回、旧 show 可读的新 identity 重开、Count 缺失/0、busy deadline、metadata 基础页码、命令 finally、原 Next(check) 末页边界、迟到事件、执行中 timeout、断连/最后文稿关闭、CP 单次释放、失败 cookie 由新 owner 重试及停止持续拒绝后明确未完成。
- `Program.cs` 使用没有 CLR 可反射属性的 DynamicObject 图走生产 accessor；原 reader 逐异常点 ledger、ABI/schema、annotation 和 session tests 保留并扩展。
- `git diff --check -- PptCOM PptCOM.Tests` 通过。C# 保持原 UTF-8 BOM / CRLF（原带 BOM 的 PptCOM.cs、PresentationDescriptor.cs 保留）；PptCOM.csproj 原 BOM 已恢复，diff 仅必要 Compile 项。

## 未验证与可观测性边界

PowerPoint 2010/MSO14、PowerPoint 2007、WPP/WPS2013 实机及真实用户损坏环境、Office 退出及立即打开下一文稿均 NOT VERIFIED；没有 GUI 授权或目标旧版提供方。无注册 IDispatch fixture 证明生产访问与 COM 引用/事件 ABI，不等于旧版 Office 实机兼容通过。

当真正结束与重开均发生在两次观测之间，且 COM show identity、HWND 和其他可见状态完全相同，不能仅凭可能迟到的事件证明已经换场，保留此提供方可观测性限制。未知 WPP 窗口类也不作未经证实的可靠窗口承诺。

## 本角色修改文件

`PptCOM/PptCOM.cs`、`PresentationDescriptor.cs`、`SlideShowSession.cs`、新增 `ComEventConnection.cs`，产品 csproj 只新增 Compile 项；`PptCOM.Tests/Program.cs`、`SlideShowSessionTests.cs`、新增 `ManagedCompatibilityTests.cs`，测试 csproj 只新增 Compile 项。`DispatchFixtureTests.cs` 与原生 fixture/脚本由独立实施者拥有；本角色只接入 Compile 和显式命令行入口。
