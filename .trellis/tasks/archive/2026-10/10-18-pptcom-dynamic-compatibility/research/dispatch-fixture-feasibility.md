# Research: 无注册真实 IDispatch 测试夹具可行性

- Query: 如何用现有工具链验证生产 dynamic adapter 在 typed QI 拒绝、IDispatch Invoke 可用，以及 ITypeInfo 缺失时的实际行为，并追踪引用释放。
- Scope: mixed；只读工程/工具链/运行时证据，未构建夹具、未激活 Office、未开 GUI、未进行 Git 操作。
- Date: 2026-10-18

## Findings

### 可立即实施的推荐

在现有 `PptCOM.Tests` 增加一个 C# 测试文件和一个 native C++ DLL 源文件。DLL 只实现手写 IDispatch 和导出工厂/账本，不实现注册入口，不依赖 ATL、Office、PIA 或 COM 注册表。通过绝对路径 LoadLibraryExW + GetProcAddress 加载工厂；工厂返回真实 native IDispatch 指针，C# 用 Marshal.GetObjectForIUnknown 生成真实 RCW，再调用生产 MarshalLateBoundComAccessor。继续使用 .NET Framework 4.0 项目和现有 MSVC/Windows SDK，不新增 CMake、NuGet、独立 solution 或新版 SDK。

推荐文件与接入：

| 路径 | 建议责任 |
| --- | --- |
| `PptCOM.Tests/Fixtures/DispatchFixture.cpp` | native IDispatch 图、TypeInfo 模式、HRESULT 注入、导出工厂/账本 |
| `PptCOM.Tests/NativeDispatchFixtureTests.cs` | .NET4 兼容加载器、RCW 所有权、生产 adapter/reader 测试、运行时信息输出 |
| `PptCOM.Tests/PptCOM.Tests.csproj` | 在现有显式 Compile ItemGroup 添加 `<Compile Include="NativeDispatchFixtureTests.cs" />`；native cpp 可列为 None；不改变 TargetFrameworkVersion/PlatformTarget |
| `PptCOM.Tests/Program.cs` | `Main(string[] args)`；保留默认纯逻辑测试；显式 `--dispatch-fixture <absolute-dll>` 执行真实 COM 组，缺 DLL/架构不匹配必须失败，不能静默 skip 后报全绿 |

不建议把 CLR 托管接入 InkeysHeadlessTests。它是既有 native Application 工程、包含很多 UI/绘图模块；为了检验 C# binder 还需额外 CLR hosting。直接复用现有 .NET4 console test 更小、更直达生产 adapter。

### 现有工程证据

- `PptCOM.Tests/PptCOM.Tests.csproj:8`：console Exe；`:11`：TargetFrameworkVersion=v4.0；`:16`：AnyCPU；`:22` 起显式列出 Compile，新增 C# 文件必须列入。
- `PptCOM.Tests/Program.cs:15`：FakeAccessor 替代了生产访问器，不能证明 COM binder；`:100` FakeGraph 是普通 CLR 图；调查开始时 `:311` 为无参数 Main。
- `PptCOM/PresentationDescriptor.cs:74`：生产 MarshalLateBoundComAccessor 构造入口；`:129` IsComObject；`:134` Release。实施者正在修改普通成员为显式 dynamic，名称/接口可继续复用。
- `PptCOM/Properties/AssemblyInfo.cs`：已有 InternalsVisibleTo("PptCOM.Tests")，无需扩大生产 API。
- `PptCOM/PptCOM.csproj`：.NET4、AnyCPU、Microsoft.CSharp 已存在；测试只调用 adapter 时不必额外加 Microsoft.CSharp。若测试代码自己写 dynamic 才需要该 Reference。
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj:36` 起：现有 Windows SDK=10.0.26100.0、v143、Console Application；不应为了 DLL 夹具重写这个工程。

### 本机架构：不能根据 OS 或 PowerShell 决定夹具架构

只读证据：

1. .NET Framework 注册 Release=533509，Version=4.8.09221，实际 CLR 属于 4.8.1 产品线。
2. `PptCOM.Tests/bin/Release/PptCOM.Tests.exe` 的 ReflectionOnlyLoad/GetPEKind 返回 ILOnly、I386、runtime=v4.0.30319；这只证明 AnyCPU IL，不能当作 x86 运行证据。
3. 原生 Windows PowerShell（System32 下 powershell.exe，PE Machine=AA64）进程报告 ProcessArchitecture=Arm64，实际加载 `C:\Windows\Microsoft.NET\FrameworkArm64\v4.0.30319\clr.dll`。
4. 直接启动当前 PptCOM.Tests.exe，并在其存活期间用 Toolhelp32 模块快照观察，实际加载：

   - `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\clr.dll`
   - 同目录 `clrjit.dll`、`mscoreei.dll`

   该 Framework64 clr.dll 文件 PE Machine=8664（AMD64）。所以当前测试 EXE 的 inproc fixture 应先构建 **x64**，而非 ARM64。
5. 同次 IsWow64Process2 返回 processMachine=0 / nativeMachine=AA64；单独使用这个结果会误选 ARM64，不能覆盖已观察到的 CLR 模块证据。
6. 官方文档解释 .NET Framework AnyCPU EXE 在 Arm64 默认使用 x64 模拟；声明新 supportedArchitectures/Prefer native ARM64 会改变选择。本任务无需为了测试修改 manifest/全局设置。

测试入口应输出 CLR 版本、IntPtr.Size 和实际 clr.dll 路径/文件架构；夹具也导出机器标识，加载失败要报告 DLL 架构与 host CLR。不能只打印 IntPtr.Size=8 就声称 x64/ARM64 已匹配。生产 ARM64 native host 加载 AnyCPU PptCOM 时是另一条宿主路径，现有 console 夹具成功不能代替该路径验收。

本机已确认存在：

- VS Installer vswhere：`C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe`。
- 当前安装：`C:\Program Files\Microsoft Visual Studio\18\Community`，安装版本 18.10.12217.157；正式命令用 vswhere 定位，不硬编码该版本。
- MSVC 14.44.35207 与 14.51.36231；现有 v143 可选 14.44.35207，勿无意采用最新默认 14.51。
- 14.44 的 `bin\Hostarm64\x64\cl.exe` 和 `Hostarm64\arm64\cl.exe`；最新工具链相同两架构 cl/link 也存在。
- Windows SDK 10.0.26100.0 两架构 um OleAut32.lib，SDK arm64/x64 midl.exe 都存在。

### 确切 native ABI 建议

所有导出 `extern "C" __declspec(dllexport)`，使用固定宽度整数和 WINAPI，HRESULT 显式返回；C# delegate 标记 CallingConvention.Winapi，不使用 .NET Core NativeLibrary API（.NET4 没有）。

```cpp
HRESULT WINAPI FixtureCreate(UINT mode, UINT* fixtureId,
    IDispatch** application, IDispatch** presentation, IDispatch** window);
HRESULT WINAPI FixtureSetFault(UINT fixtureId, UINT stage, HRESULT failure);
HRESULT WINAPI FixtureGetLedger(UINT fixtureId, FixtureLedger* ledger, UINT bytes);
HRESULT WINAPI FixtureReleaseOwner(UINT fixtureId);
UINT WINAPI FixtureMachine(); // IMAGE_FILE_MACHINE_AMD64/ARM64
```

`FixtureLedger` 用固定布局 POD（例如 UINT32 + INT64，C# 显式同一 Pack），至少含 node live count、AddRef/Release、Invoke、GetIDsOfNames、GetTypeInfo、拒绝 typed QI、每个阶段取得子对象、Next/Previous/Exit 次数及最后 HRESULT。记录按 fixtureId 独立，不能只用无法区分测试的全局合计。

fixture 图只保留自身明确的引用基线：工厂对每个输出 IDispatch 交付一个 caller-owned 引用；每次 VT_DISPATCH 返回前恰好 AddRef；VARIANT/BSTR/错误结构按标准清理。FixtureReleaseOwner 只释放夹具自身持有引用，并在 live count 为零时才销毁账本；不得强制释放 caller 的 COM 引用使测试通过。

C# 取得 root 的固定形状：factory 导出返回 IntPtr；用 GetObjectForIUnknown 得到 RCW；finally 对每个 raw pointer Marshal.Release 配平原始引用；root RCW 只在测试最终 finally ReleaseComObject 一次。reader 临时 View/Slide/Slides/Item 的取得/释放由生产 adapter 处理。不要把类型转换、普通赋值或同 identity alias 再当一笔取得。不要强制 GC，不循环 ReleaseComObject，不 FinalReleaseComObject。

### 夹具的三类模式

1. **TypedQIRejectedWithTypeInfo**：只对 IUnknown/IDispatch 返回成功；实际 Office typed IID（至少 SlideShowWindow IID）返回 E_NOINTERFACE 并计数；GetTypeInfo 提供有效 dispatch typeinfo；GetIDsOfNames/Invoke 返回真实图。使用真实 Office IID 的 Marshal.QueryInterface 验证拒绝，不能只在名称中声称拒绝。
2. **NoTypeInfo**：GetTypeInfoCount=0，GetTypeInfo=DISP_E_BADINDEX 或 E_NOTIMPL；GetIDsOfNames 和手写 Invoke 确实仍可工作。分别运行生产 GetProperty、GetItem、InvokeMethod 并记录实际结果。不能预设 dynamic 一定通过，也不能为了通过偷偷恢复普通属性反射。若目标 .NET4 binder 无法支持该形状，应记录能力边界并验证安全降级。
3. **TypeInfoFailure**：GetTypeInfoCount=1 但 GetTypeInfo 返回 TYPE_E_CANTLOADLIBRARY（或测试指定失败）；与“自愿无 typeinfo”区分，观察是不是在到达 Invoke 之前失败。不得混成所有 COM 损坏都可修复。

有效图至少实现 Name/Caption、FullName、Slides、Count、Item(index)、View、Slide、SlideIndex、SlideID、State；Next/Previous/Exit/Activate 支持 0 参数方法；按生产需求增补。将 HWND 标为 restricted/不存在作为单独窄例外场景，不用它决定普通属性 dynamic 是否成功。

有效 ITypeInfo 推荐先用 **OleAut32 CreateDispTypeInfo** 和 METHODDATA/INTERFACEDATA 构造最小无注册类型信息，由手写 Invoke 执行图访问，不需要 CreateStdDispatch 的 vtable 调度。该 API 的简化 typeinfo 是否足够供本机 .NET4 binder 扫描仍须实际试跑。若缺少 binder 需要的元数据，使用 **CreateTypeLib2 + ICreateTypeInfo(AddFuncDesc/SetFuncAndParamNames/LayOut)** 构造内存 TKIND_DISPATCH 类型信息；不调用 SaveAllChanges/RegisterTypeLib。若已有实现者熟悉 MIDL，也可用现有 SDK 编译一个测试 .idl，并以 LoadTypeLibEx(REGKIND_NONE) 加载，产物放 Build 目录、不注册；这不应扩展到生产项目/TLB。

类型信息应给不同 node/layout/profile 分配不同 GUID，避免 C# COM binder 的类型描述缓存把有 typeinfo 的成功规则复用到缺失/损坏场景。最强隔离方式是每种 profile 由显式测试参数启动独立 console 进程。

### 最小验证与判定

- Assert Marshal.IsComObject(root)，以及 typed QI 确实返回 E_NOINTERFACE；拒绝应有 native ledger 证据。
- 用生产 adapter 逐项验证属性、Item(1)、Next/Previous/Exit/Activate；不要绕过 adapter 只调用测试自写 dynamic。
- 用生产 PresentationDescriptorReader/SlideShowStateReader 执行完整和重复扫描；注入每个 temporary 取得后的异常/busy；检查 live refs 回到基线。
- 检查真实 native ledger 的获取/释放平衡；QI/CLR 缓存可能有额外 AddRef/Release，不能把“总数不同”直接等同一次业务引用泄漏；以结束后 live roots/temporaries 和 fixture 自身持有基线相配平为准。
- 退出顺序：生产临时释放 → 测试 root RCW 释放 → fixture owner release → ledger live=0；随后才 FreeLibrary。若失败且仍有 RCW，不能卸载 DLL 造成稍后 CLR 清理落入已卸载代码；报告失败并让进程退出，不用 GC/强制清理隐藏问题。
- no-TypeInfo/TypeInfoFailure 既要证明原始 native GetIDsOfNames/Invoke 可用，也要记录生产路径是否成功；成功与失败都要独立断言已批准的预期合同，不能 catch 后全部打印 PASS。

### 可复用命令形状（供主会话执行；本研究未编译）

现有 managed 工程仍用当前 ARM64 原生 MSBuild，先在同一 PowerShell invocation 处理 PATH；将 `$fixtureMsbuild` 从 vswhere 结果 `MSBuild\Current\Bin\arm64\MSBuild.exe` 解析：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $fixtureMsbuild 'PptCOM.Tests\PptCOM.Tests.csproj' /p:Configuration=Release /p:Platform=AnyCPU /nr:false /v:minimal
if ($LASTEXITCODE -ne 0) { throw "managed build failed: $LASTEXITCODE" }
```

native 小 DLL 可直接使用已存在 MSVC 编译，无需增加 vcxproj。以下路径由 vswhere、现有 v143 工具目录、现有 SDK 发现，不能用安装/升级补齐失败：

```powershell
# $fixtureVc = <VS>\VC\Tools\MSVC\14.44.35207，实际按已安装 v143 发现
# $fixtureKit = C:\Program Files (x86)\Windows Kits\10
# $fixtureSdk = 10.0.26100.0，复用当前 native 工程
$fixtureOut = [IO.Path]::GetFullPath('Build\pptcom-dispatch-fixture\x64')
New-Item -ItemType Directory -Force -Path $fixtureOut | Out-Null
$fixtureCl = Join-Path $fixtureVc 'bin\Hostarm64\x64\cl.exe'
& $fixtureCl /nologo /std:c++17 /EHsc /W4 /MT /LD /DUNICODE /D_UNICODE `
  ("/I" + (Join-Path $fixtureVc 'include')) `
  ("/I" + (Join-Path $fixtureKit "Include\$fixtureSdk\shared")) `
  ("/I" + (Join-Path $fixtureKit "Include\$fixtureSdk\um")) `
  ("/I" + (Join-Path $fixtureKit "Include\$fixtureSdk\ucrt")) `
  ("/Fo" + (Join-Path $fixtureOut 'DispatchFixture.obj')) `
  ("/Fe" + (Join-Path $fixtureOut 'DispatchFixture.dll')) `
  'PptCOM.Tests\Fixtures\DispatchFixture.cpp' /link /MACHINE:X64 `
  ("/LIBPATH:" + (Join-Path $fixtureVc 'lib\x64')) `
  ("/LIBPATH:" + (Join-Path $fixtureKit "Lib\$fixtureSdk\um\x64")) `
  ("/LIBPATH:" + (Join-Path $fixtureKit "Lib\$fixtureSdk\ucrt\x64")) `
  OleAut32.lib Ole32.lib Uuid.lib Kernel32.lib
if ($LASTEXITCODE -ne 0) { throw "fixture build failed: $LASTEXITCODE" }
& '.\PptCOM.Tests\bin\Release\PptCOM.Tests.exe' --dispatch-fixture (Join-Path $fixtureOut 'DispatchFixture.dll')
if ($LASTEXITCODE -ne 0) { throw "real IDispatch test failed: $LASTEXITCODE" }
```

如实现 native fixture 单独脚本，可以只封装上述现有编译器发现/调用；没有必要改生产 sln、引入新构建系统或改 SDK。x86/ARM64 追加运行须有实际宿主；不要只改 DLL /MACHINE 参数假装覆盖其他 CLR 架构。

### External references / versions

- [微软：AnyCPU .NET Framework 在 ARM64 上的默认与显式 ARM64 选择](https://devblogs.microsoft.com/visualstudio/how-to-run-net-apps-natively-on-arm64-devices/)（2024-09-17）。与本机实际 Framework64/FrameworkArm64 模块证据一致。
- [CreateDispTypeInfo](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-createdisptypeinfo)：运行时构造简化类型信息，OleAut32.lib。
- [CreateTypeLib2](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-createtypelib2) 与 [LoadTypeLibEx](https://learn.microsoft.com/en-us/windows/win32/api/oleauto/nf-oleauto-loadtypelibex)：需要完整类型信息时的系统 API；REGKIND_NONE 为不注册加载。
- [微软当前 COM binder 源码](https://github.com/dotnet/runtime/blob/main/src/libraries/Microsoft.CSharp/src/Microsoft/CSharp/RuntimeBinder/ComInterop/IDispatchComObject.cs)：存在 TypeInfo 扫描、restricted 成员过滤、按 GUID 缓存与显式 GetIDsOfNames fallback；这是当前 runtime 的参考，**不能代替目标 .NET Framework binder 的实际运行结果**。

### Related specs

- `.trellis/workflow.md`：研究结果持久化和角色隔离。
- `.trellis/spec/ppt-interop/index.md`、`com-contract.md`：现有 .NET4 工程、COM 生命周期、fake 与真实提供方验收边界。
- 当前任务 `design.md:20,138`、`implement.md:18`：真实 IDispatch/typed QI/ITypeInfo 夹具已在批准范围内。

## Caveats / Not Found

- 本调查只写本报告。未实现、编译或运行 native 夹具，所以不能宣称 TypeInfo 创建方式或 no-TypeInfo binder 行为已通过。
- 为确认实际 CLR 路径，运行了已有 console 测试 EXE并捕获模块：第一次退出0；第二次与托管实施者并行加入的生产 dynamic 红测试相交，退出1、3个预期红测试。不是修复后验证，不据此修改代码。
- Python PATH 命中了不可执行 WindowsApps alias；后用现有 bundled Python `C:\Users\alan-\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe` 完成只读探测，无环境/项目修改。
- fixture 证明受控的 COM 能力形状和显式释放，不证明所有 COM 注册损坏，也不证明 Office 2010/WPP2013 的具体类型信息、窗口和退出行为。旧版实机验收仍须单列。
