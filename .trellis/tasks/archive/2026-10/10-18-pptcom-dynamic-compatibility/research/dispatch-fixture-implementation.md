# 无注册原生 IDispatch 夹具实施与运行证据

- 日期：2026-10-18。
- 范围：仅测试夹具、构建脚本和本报告；不启动 Office/GUI，不注册 COM，不修改系统或 Office 注册，不执行 commit/push。
- 责任文件：`PptCOM.Tests/NativeDispatchFixture.cpp`、`DispatchFixtureTests.cs`、`Build-DispatchFixture.ps1`。Main/Compile 接入由托管实现方修改现有工程完成。

## 实现

原生 DLL 手写十个 IDispatch 节点：Application、Presentation、SlideShowWindow、View、Slides、三个 Slide、SlideNavigation 和 SlideShowWindows。前三种访问 profile 只有 IUnknown 和 IDispatch 的 QI 成功；PowerPoint SlideShowWindow IID `91493453-5a91-11cf-8700-00aa0060263b` 明确返回 E_NOINTERFACE，C# 用真实 Marshal.QueryInterface 和原生 typedRejected 账本同时验证。没有 class factory、注册入口或 Office 依赖。

每个节点分别记录 QI/拒绝、AddRef/Release、当前引用、创建/销毁、Invoke、GetIDsOfNames、GetTypeInfoCount/GetTypeInfo、对象返回、注入异常和命令副作用计数。导出接口只返回定宽整数结构。每个 fixture 独立保存账本，终态必须同时满足 live objects=0、refs=0、每个节点 destroyed=created=1、Release=AddRef+created，才能 Forget 账本。ReleaseOwner 只交还夹具自身的一笔引用，不替调用方清除引用。

有效 ITypeInfo 使用系统 OleAut32 的 CreateTypeLib2/CreateTypeInfo 在内存构造 TKIND_DISPATCH，LayOut 后返回；不 SaveAllChanges/RegisterTypeLib，也不写临时 TLB。每个有效 fixture/node 使用不同 GUID，避免 DLR 类型描述缓存掩盖不同布局。HWND 单独标记 restricted，不据此放宽其他属性访问。

三种 profile：

| profile | GetTypeInfoCount | GetTypeInfo | 原始 GetIDsOfNames/Invoke |
| --- | --- | --- | --- |
| WithTypeInfo | 1 | S_OK，有效内存类型信息 | 可用 |
| NoTypeInfo | 0 | DISP_E_BADINDEX，0x8002000B | 可用 |
| TypeInfoFailure | 1 | TYPE_E_CANTLOADLIBRARY，0x80029C4A | 可用 |

后续按主会话批准新增独立 `WithEvents=3`，沿用有效 TypeInfo，只在 Application 对 CPC/CP QI 成功；原三种访问 profile 和原39个用例保持。Application/CPC/CP 共用同一个 COM identity，FindConnectionPoint 每次仍是独立 acquisition；生产包装器不能以同 identity 为由省掉释放，也不能额外释放 borrowed container。

WithEvents 实现真实 Advise/Unadvise/cookie、事件 sink QI 和逐次 sink 引用；纯值 event ledger 记录活动连接、订阅/取消、sink AddRef/Release、参数 AddRef/VariantClear、返回 HRESULT 和取消标量。触发器把同一 Window/Presentation 的 IDispatch 指针作为两份 VARIANT 参数，并可增加 byref cancel；生产 raw sink 只接收 IntPtr，不物化 RCW。ReleaseOwner 在连接仍 active 时返回错误，防止通过销毁 source 隐藏漏 Unadvise。

事件 source IID `914934c2-5a91-11cf-8700-00aa0060263b`、DISPID Begin=2011、NextSlide=2013、End=2014、BeforeClose=2025 均由仓库 PIA 静态 metadata 核实，不激活提供方。

C# 通过 .NET4 支持的 LoadLibraryEx/GetProcAddress 和 Winapi delegates 加载绝对 DLL 路径；Marshal.GetObjectForIUnknown 建立真实 RCW，原始 factory 输出指针在 finally 配平；RCW 在终态各手工 ReleaseComObject 一次。无 NativeLibrary、FinalRelease、Release 循环、GC、延迟等待或杀进程。加载器不 FreeLibrary：CLR 可能仍缓存类型描述/RCW，不用卸载 DLL 隐藏 ledger，也不让后续 CLR 清理落到已卸载的代码；DLL 由测试进程退出时 OS 卸载。

所有普通读取、Item、命令和工具 setter 都调用生产 MarshalLateBoundComAccessor，不在测试里用反射/dynamic 的替代实现。reader 外层 TrackedAccessor 仅记录 object-valued acquisition/Release，调用仍转发生产适配器；它另断言 borrowed roots 零次额外 Release。原生图不创建窗口，reader 明确注入 HWND=0 resolver，不提供假 HWND/PID。

## 覆盖

- 生产 dynamic 在 typed QI 拒绝时读取 Name/FullName、Slides.Item、完整 descriptor。
- 连续十二次真实 COM 全拓扑扫描；每次 temporary 都回到 fixture-owner 单引用基线。
- 同 identity 两次取得同 RCW：第一次 Release 后第二次取得仍可读取，最终两笔分别配平。
- 生产 Next/Previous/Exit/Activate 及 PointerType/LaserPointerEnabled/Visible setter；每个副作用恰一次；结束页只枚举真实拓扑。
- 十六个逐取得后异常位置，每处普通成员失败与 busy 各一次：View、State、当前 Slide/SlideIndex/SlideID、Slides/Count、Item 第1/2/3次、每个拓扑 SlideIndex/SlideID（当前 Slide2 与 Item2 分别按 occurrence 区分）。busy 三个既有 HRESULT 轮流覆盖。
- busy 命令保留 0x8001010A，原生 Invoke=1、副作用=0，证明没有反射或重复命令补救。
- NoTypeInfo/TypeInfoFailure 各自通过原生 probe 证明 IDispatch 属性、Item、方法本来可用，再独立用生产属性、直接集合 RCW 的 Item 和 Activate 测出 .NET binder 实际行为，不因前置 getter 失败掩盖 Item 结果。
- 四个独立 raw 事件用例：104次重复同 identity 参数/取消参数、Deactivate 后迟到通知、一次 Unadvise busy 后保留同 cookie/CP 显式重试、一次 Advise 失败后释放独立 CP 并重新注册、CPC 缺失时核心 dynamic reader 仍可用。事件组使用生产 ComEventConnection，不用 FakeAction 代替真实连接点。

## 构建与运行

脚本从 vswhere 定位当前安装，读取现有 `Inkeys.vcxproj` 的 v143/SDK，使用其 `Microsoft.VCToolsVersion.v143.default.txt` 定位具体 MSVC。没有新增工程、构建系统、SDK/包版本或 VS 硬编码。默认 x64，支持 `-Architecture arm64`；始终使用当前 ARM64 本机的 Hostarm64 compiler。

本次实际工具链为 v143/MSVC 14.44.35207、Windows SDK 10.0.26100.0。最终两次构建都退出 0、无警告：

```powershell
& .\PptCOM.Tests\Build-DispatchFixture.ps1 -Architecture x64
& .\PptCOM.Tests\Build-DispatchFixture.ps1 -Architecture arm64
```

- `Build/pptcom-dispatch-fixture/x64/NativeDispatchFixture.dll`，PE Machine=0x8664。
- `Build/pptcom-dispatch-fixture/arm64/NativeDispatchFixture.dll`，PE Machine=0xAA64。

主会话统一 MSBuild 生成的测试 EXE 接入显式 `--dispatch-fixture <absolute DLL>`；默认无参数测试不加载 native DLL。夹具代码本身不调用 MSBuild，也不修改其项目配置。

x64 实跑命令：

```powershell
& .\PptCOM.Tests\bin\Release\PptCOM.Tests.exe --dispatch-fixture `
    ([IO.Path]::GetFullPath('Build\pptcom-dispatch-fixture\x64\NativeDispatchFixture.dll'))
```

本轮实跑退出 **0**，输出 `Native dispatch fixture: 39 cases, 0 failures`，stderr 为空；core tests 同进程也通过。实际 CLR `4.0.30319.42000`，模块 `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\clr.dll`，PE Machine=0x8664，pointerBytes=8。目标框架仍是 .NET4，但实跑为本机安装的 .NET Framework 4.8.1 CLR，不等同 .NET4 RTM/MSO14 实机。

实际边界：NoTypeInfo 的生产 Name/Item/Activate 全部成功、reader StableSlideIds/Valid；TypeInfoFailure 三类生产操作都原样抛 0x80029C4A，reader Unavailable/Unknown，命令无隐藏副作用。两个边界的原始 IDispatch probe 均成功，所有引用终态归零。

x64 调试过程/实跑日志在 `Build/pptcom-dispatch-fixture/x64/first-debug.stdout.log` 和 `.stderr.log`；主会话最终统一验证日志优先。首次 metadata 错误均仅属于夹具：TKIND_DISPATCH 设置 FOLEAUTOMATION 返回 0x800288BD；同 property 对 getter/put 重复 SetFuncAndParamNames 返回 0x8002802B。分别删除不适用 flag、只对 getter 设置名称后通过，没有改生产 adapter 绕过错误。[微软 SetFuncAndParamNames 合同](https://learn.microsoft.com/en-us/windows/win32/api/oaidl/nf-oaidl-icreatetypeinfo-setfuncandparamnames)

原39用例 ARM64 基线由主会话实际运行通过：WindowsPowerShell 宿主加载 `C:\Windows\Microsoft.NET\FrameworkArm64\v4.0.30319\clr.dll`（PE=0xAA64），DLL 同为0xAA64，39 cases/0 failures，宿主退出0。日志 `Build/Validation/pptcom-dynamic-20261018/managed-arm64-core-dispatch.stdout.log`；stderr 仅 PowerShell 首次加载进度 CLIXML。

四个新增事件用例已经由主会话真实运行：x64 的三个 raw 事件组与 CPC 缺失组均通过；ARM64 完整43 cases/0 failures、core和宿主退出0，日志 `managed-event-fixed-arm64.stdout.log`。初次 source IID QI 返回 E_NOINTERFACE，由生产包装器内部 ComImport 接口和 ICustomQueryInterface 显式 source IID/IDispatch 映射修复；夹具没有放宽其 QI 要求。只读 native ledger 曾确认已进入 Advise（Advises=1、SinkQueries=1、PointAcquisitions=1、LastAdviseHresult=0x80004002），没有 sink acquisition/活动连接，排除在进入 native 前的参数封送失败。

集成 x64 单个 busy 用例额外揭示了产品诊断副作用：`managed-errorinfo-before-x64.stderr.log` 中 nativeLastHR=0x8001010A、Invokes=1、Faults=1，生产 adapter 实际收到的是先前 BindEvents 的 InvalidCastException（Connection point unavailable），reader 因而发布 Unavailable。Marshal.GetHRForException 会设置线程 IErrorInfo，下一次失败的 inproc COM 调用可被旧托管错误信息污染。主会话/托管实现方已将产品日志改为纯值 HRESULT 读取并补无污染回归；native fixture 不清 IErrorInfo、不改 HRESULT 或断言预期，以保留修前失败→修后通过证据。夹具只在状态失配时记录实际状态、原生 HRESULT/计数和原始异常。

x64 在仅修复产品诊断、仍使用相同 native 故障行为后已经通过 **43 cases/0 failures、core PASS、进程退出0**，日志 `Build/Validation/pptcom-dynamic-20261018/managed-errorinfo-after-x64.stdout.log` 和 `.stderr.log`。这是同一故障注入的修前失败→修后通过证据，不通过清 ErrorInfo 或放宽测试隐藏问题。

检查阶段 cleanup 修正与最终源码冻结后，主会话已完成最后一次统一托管构建及双 CLR 复验：`managed-verified-build` 退出0、无 warning；`managed-verified-x64.stdout.log` 和 `managed-verified-arm64.stdout.log` 都是 **43 cases/0 failures、core PASS、进程退出0**，ARM64 宿主另外明确记录 EntryPoint=0。对应 stderr 同名保留。最终结果以 `Build/Validation/pptcom-dynamic-20261018/managed-verified-*` 为准。完整 `InkeysRepo.sln Debug|ARM64` 构建由主会话独立执行，不计作本夹具自身已完成的验证。

本职责四个新文件均为 UTF-8 无 BOM、CRLF，无 lone LF/尾随空白；owned diff/check 通过。只修改分配的三个夹具源码/脚本和本报告，不修改产品、Program.cs、工程、规范或 DLL/TLB。

## 明确边界

夹具证明受控的 typed-QI 失败而 IDispatch 可用、类型信息差异和确定性引用配平。它不证明每一种 Office/WPS 注册损坏都能被 dynamic 绕过；TypeInfoFailure 的实际失败已经说明严重损坏边界。

没有启动 PowerPoint/WPP 或目标旧版，没有验证真实进程退出与下一文稿打开。PowerPoint2007、PowerPoint2010/MSO14、WPP2013 的具体类型信息/成员、事件/轮询、窗口和真实关闭行为仍为 NOT VERIFIED，须在有授权的提供方实机验收。x64 console/ARM64 CLR 夹具不能代替旧版本验收。
