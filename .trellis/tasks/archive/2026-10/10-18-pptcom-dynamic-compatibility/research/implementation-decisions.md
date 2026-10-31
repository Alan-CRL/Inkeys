# 实施决定与阶段验证

- 日期：2026-10-18
- 用户已批准实施；无GUI/Office激活、无commit/push。

## 批准补充与最小边界

1. 用户确认此前dynamic不能取得HWND。保留该标量的包装例外，不再强制先dynamic重试；其他常规成员使用真正dynamic。
2. 应用HWND可作同一标量例外的窄进程锚：只借用绑定Application接口读标量，不新增RCW取得；再以同PID、已关联show.Presentation、单show、受验证放映类和唯一候选筛选Win32窗口。没有锚/类依据仍返回0，保留基础模式，不猜测WPP窗口类。
3. native将基础COM控制与墨迹/OS窗口能力分开；同场旧可信窗口只供销毁/复用检测，当前窗口能力未知时不Focus/不自动接管Office笔。
4. native审核发现Pending→InkReady同页/同target的迟到旧UI帧无法由原四参回执区分；主会话批准局部增加PageControl内部pageAckEpoch。必须随真实frame snapshot/commit传递，Ppt校验后仍向native转发原四参，不改COM/Draw3协议，不重做UI。
5. Bar同场窗口锚提升只请求重绘，不重放场次/位置恢复。
6. 真正IDispatch fixture复用现有MSVC与.NET4 console工程；不注册COM、不启动Office。当前AnyCPU exe已实测加载Framework64/x64 CLR；双架构夹具允许另在ARM64 CLR测试，不能根据OS/PowerShell直接猜DLL架构。

## 已运行红验证

- ARM64原生MSBuild，PptCOM.Tests.csproj Release|AnyCPU，PATH规范化与禁node reuse：build exit0。
- 实施前新增回归：tests exit1，恰为3项：
  - production dynamic-only graph survives typed interface absence
  - production dynamic-only adapter: MissingMethodException
  - same COM show can exit without HWND
- 日志：Build/Validation/pptcom-dynamic-20261018/red-managed-build.stdout.log、red-managed-build.stderr.log、red-managed-tests.log。
- 红验证用于证明新测试能捕获原访问/准入问题；不是旧版Office实机结果。

## 其他证据

- HEAD字节/BOM/CRLF及初始随附DLL/TLB哈希记录在implementation-baseline.json。
- 其余构建、fixture、headless与审核结果随实施追加；最终以verification.md为准。


## 事件包装的实施复核补充

- 独立检查发现 .NET Framework 的 ComEventsSink 将入站 VARIANT 参数物化为 RCW 后不自行逐笔释放；handler不保存参数不能证明这部分引用已配平。ComEventsHelper/PIA包装的取消还存在先删本地订阅、Unadvise抛错后无法可靠重试的边界。
- 为落实用户已批准的确定性释放要求，事件例外改用内部 raw IDispatch sink 和显式 IConnectionPoint/cookie：Invoke只读DISPID并发纯值提示，不物化WnObj；owner显式Unadvise后释放连接点。没有常规属性反射回退，也不改变公开组件ABI/依赖。
- 这属于既有事件包装例外和释放修复范围内的实施选择；新增真实连接点夹具，验证重复同identity参数、订阅、解绑失败后重试及引用归零。不能以纯Action rollback测试代替。
- 普通暂忙/缺成员保持Unknown；已确认RPC断连/已失效RCW要结束旧binding并清理，防止最后文稿关闭后旧root永久保留。
- 持续外部COM拒绝解绑或已经阻塞的调用不能由本地超时撤销。最终清理有界显式重试，未完成要报告并保留可重试所有权，不以GC、FinalRelease、吞异常或新增无主线程伪造清理成功。

依据（本轮检查者读取的.NET Framework第一方源码）：
- https://github.com/microsoft/referencesource/blob/main/mscorlib/system/runtime/interopservices/ComEventsSink.cs
- https://github.com/microsoft/referencesource/blob/main/mscorlib/system/runtime/interopservices/ComEventsHelper.cs

## 本轮诊断的IErrorInfo副作用回归

- x64真实fixture曾出现native Invoke/Fault=1、原始HRESULT=8001010A，但CLR重抛前序BindEvents留下的InvalidCastException（Connection point unavailable），生产reader因此得到Unavailable。
- 日志证据：Build/Validation/pptcom-dynamic-20261018/managed-errorinfo-before-x64.stderr.log。该例属于本次实施新增日志被独立验证捕获的问题，不冒充用户20261014a历史故障的既有实测。
- 根因是Marshal.GetHRForException会设置线程IErrorInfo。产品统一改为纯读取ExternalException.ErrorCode，其他managed异常明确未知；不对Office加反射、不改变busy预期。保留原native故障发出行为，以修前失败/修后通过闭合回归。
