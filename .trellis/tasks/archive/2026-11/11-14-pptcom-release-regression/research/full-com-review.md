# 全量 dynamic / COM 包装器复审

日期：2026-11-14。范围：PptCOM 的全部活动 C# 源码与 native 服务智能指针交接；继续使用当前 Trellis 任务。用户反馈 GC 恢复后仍复现，要求按每次取得核对显式释放、链式访问与异常分支。用户随后确认 DLL 需要手动同步。

## 逐项发现

### F1 — 已确认并补齐：新 DLL 未进入主程序资源

- `Inkeys/Inkeys.rc:174` 的 DLL 资源引用 `Inkeys/PptCOM.dll`。
- `Inkeys/IdtMain.cpp:3660–3688` 从 EXE 内嵌资源验证/写出 PptCOM.dll 后加载；只替换程序目录旁的 DLL 也可能被旧 EXE 内嵌资源覆盖。
- 上轮只生成 PptCOM/bin/Release/PptCOM.dll，没有同步 Inkeys/PptCOM.dll。两者同名/同尺寸，但 IL 不同：
  - 旧随附 DLL：MVID `10078be7-e8d8-48cc-8c19-5720784a5d9c`，FullCleanup 没有 System.GC 调用，SHA256 `1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3`。
  - 新 DLL：MVID `f5330b8a-2c58-4ee9-ae5a-1e02d9b4a0c6`，FullCleanup 的 System.GC 调用为 Collect、WaitForPendingFinalizers、Collect，SHA256 `67808600A60E84C92D65F659A43476165B826D5D233B455EE6170C568EBE046F`。
- 影响：此前主程序复测不能验证新 GC 补丁，因为主工程资源输入仍是旧 DLL。这是交付遗漏，不能据此推断新 GC 仍然无效，也不能因此断言已解释全部原始 COM 残留。
- 已按用户补充同步新 DLL。Inkeys/PptCOM.tlb 与新产物哈希完全相同，不作覆盖。

### F2 — 已确认并处理：旧时间戳使增量构建仍保留旧嵌入资源

- Copy-Item 保留源 DLL 的 08:44:28 时间戳；已有 Inkeys.res 为 08:49:09、EXE 为 08:49:13。首次增量 solution 对主程序显示成功/已最新，但 EXE 内嵌哈希检查失败。
- 不能只根据 MSBuild 输出路径认定新 DLL 已进入 EXE；需要检查实际资源内容。
- 已将本次同步的 DLL 时间戳更新为实际交接时间，触发资源重编译；不修改工程配置、资源定义或业务源码。
- 最终 Debug|ARM64 EXE 生成时间 09:08:27，现有 Assert-EmbeddedPptCom 检查通过，内嵌哈希与上述新 DLL 一致。

### F3 — 已确认的条件性保留路径：失败事件连接可继续持有应用

- `ComEventConnection.cs:148`：Unadvise 失败且并非明确断连时直接返回 false，不消费 CP 取得。
- `PptCOM.cs:490–491`：失败连接转入静态 retirement registry。
- `ComEventConnection.cs:163–189`：静态列表持续强引用 connection，闭包继续持有 ownedPoint；重试失败仍放回列表。
- `PptCOM.cs:1518–1529`：服务最终清理后最多三轮重试，持续失败会保留连接，返回 -1。这不是“全部资源已释放”。
- 新增调查探针使用现有真实 IDispatch/连接点夹具，直接进入生产 FullCleanup，而不是由测试 Scope 替生产清理 root。注入 Unadvise busy 后，FullCleanup 和 GC 执行完，观测 `PendingCount=1`、`ActiveConnections=1`、Application 原生引用数 5；三次显式注入的 busy 重试后仍保留。后续允许 Unadvise 成功，再次 Retry 后归零至夹具 owner 基线，最后所有对象销毁。
- 数字 5 包括夹具 owner 和 RCW 缓存的多个接口指针，不等于“漏了五次 C# acquisition”。这里确认的是仍有活 CP/应用引用，不是简单按托管变量数计数。
- 影响：若用户现场持续命中此分支，GC 无法解决，Office 可被客户端引用保活。当前未运行真实 Office，不能认定用户现场已命中这一 HRESULT。单次 busy 后重试成功则正常释放。
- 本轮未擅改该策略；后续如需改变最终失败路径，应明确何时结束重试并释放 CP，以及服务端迟到通知的生命周期，不能直接对共享 RCW 全局 FinalRelease。

## 审计缺口（不作为已证实泄漏）

`PresentationDescriptor.cs:182–191` 的 Release 丢弃 ReleaseComObject 返回值并吞异常，PptCOM.cs:460–463 又吞掉 accessor 异常。现有日志不能直接证明每次释放成功或最终 RCW 计数为零。这是诊断/验证缺口；不能从未记录计数推导出实际漏释放，且 temporary 释放后非零可能是同一 RCW 的合法 root 持有。

## 全部 dynamic 位置与 COM 对象账本

| 位置/对象 | 取得与去向 | 审核结论 |
| --- | --- | --- |
| PptCOM.cs:117–119 三个 dynamic root | Application 从 ROT best 转入；Presentation 从属性取得；Window 从 freshWindow 转入；FullCleanup:525–533 逐项 Release 并置空 | 成功清理路径的真实 RCW 已用生产 FullCleanup 验证 |
| PptCOM.cs:726 的 ssw | 对入参 sswObj 的托管别名，且无对象值属性读取 | 不新增取得，不应再 Release 一个别名 |
| PptCOM.cs:975、978、979 三个 ROT dynamic 局部 | candidateApp、activePres、ssWindow 分别进入本轮 finally、列表/返回值转移 | 无按 identity 跳过新取得释放的问题，无额外列表 AddRef |
| PresentationDescriptor.cs:118 的 value | 对 target 的借用；单层属性返回交给调用方 | 逐个 getter 调用点核对了临时对象 finally |
| PresentationDescriptor.cs:147 dynamic Item | 每次 Slides.Item 返回一笔取得 | reader 的每轮 finally:564 释放，集合 finally:610 释放 |
| PresentationDescriptor.cs:152、166 的 instance/value | 借用目标，仅设置标量或调用固定 void 命令 | 没有新 COM 结果被丢弃的已确认路径 |
| ActivePresentation、SlideShowWindow 比较 helper | 本次取得即使等于 root 也 finally 释放 | 已在真实生产 helper 重复调用中验证 |
| ObserveBoundShow 的 View、freshWindow、activePresentation、showPresentation | root 转移后清空局部，其余 finally:1399、1453–1455 | 生产观测重复运行及最终清理配平 |
| Descriptor 的 View/Slide/Slides/Item | 显式命名，子先父后释放 | 既有原生夹具成功/异常覆盖通过，未改动此路径 |
| Next/Previous/总览/批注/Exit | View 或 SlideNavigation 具名，finally 释放 | 既有真实 COM 夹具覆盖命令与失败路径，源码无活动链式中间对象 |
| ROT、EnumMoniker、每轮 Moniker/BindCtx/GetObject | 每轮和外层 finally 清理；获胜 Application 只转移一笔 | 静态路径与既有注入 ROT 测试未发现计数不配平；新探针不枚举用户 ROT |
| HWND typed 接口 | as/cast 只是现有 RCW 别名；读取标量 | 不能因为多一个接口变量就额外 Release |
| IUnknown identity 比较 | GetIUnknownForObject → finally Marshal.Release | 原生指针取得与释放对应 |
| RawApplicationEventSink | 直接处理 DISPID，不把事件参数变成 RCW；QI 返回指针交给 COM 调用方 | 没有入站 WnObj RCW 待本模块逐笔清理的问题；不能在返回 QI 指针前自行 Release |
| Native Get/Set/ResetPptComSnapshot 和 QueryPptSessionApi | IPptCOMServerPtr/IPptCOMSessionStatePtr 管理取得与销毁 | 无裸指针转移后遗漏 Release 的已确认路径 |

这里的配平单位是每次 COM 返回/转入 CLR 的取得；托管赋值、列表 Add、as/cast 和借用传参不等于再创建一个 COM 对象。

## xx.yy.zz 检查

逐个检查活动成员访问、getter 和命令。未发现活动源码存在 COM 根对象 `.中间COM属性.下一成员` 的未命名链式访问。

`PptCOM.cs:2033、2047` 确有 `pptActivePresentation.SlideShowSettings.AdvanceMode`，但整段位于 `/* ... */` 注释中，不参与编译，不是当前泄漏路径。`target.GetType().InvokeMember` 的中间对象是托管 Type，不是另一次 COM 取得；不能误报为相同问题。

## 非 COM 资源补充

SlideShowOwnerMailbox 在 SlideShowSession.cs:294 创建 AutoResetEvent，Stop 没有 Dispose，依赖 SafeHandle/GC 终结。这是确定性的内核句柄清理缺口，未发现它直接持有 Office COM 对象的证据；不作为 POWERPNT.exe 残留根因，也未在本次擅改。

## 新验证证据与边界

- 调查代码：AuditComLifetime.cs，独立 console 探针，复用既有 NativeDispatchFixture 和生产 DLL，不修改产品/测试工程或系统 COM 注册。
- 正常 final=false/true 两路，每路 100 次生产 helper/观测循环，真实事件绑定后由生产 FullCleanup 消费 root；测试 Scope 的 root 字段事先清空以确保它不能代劳。
- 每个节点在夹具 owner 释放前为 1 个基线引用；释放 owner 后 10 个对象全部销毁，逐节点 AddRef/Release 账本满足原夹具断言。
- busy 三轮保留与后续成功回收，属于显式故障注入，不是用户 PowerPoint 实测。
- 生产清理探针 exit 0。日志 validation/production-cleanup-owner-audit.log；复制前 IL 差异日志 validation/production-cleanup-audit.log。
- 完整 InkeysRepo.sln Debug|ARM64：第一次沙箱内在 Headless C++ 编译遇到 C1902；核对 cl/mspdbsrv 版本匹配，在允许 PDB 服务的环境使用同一 ARM64 MSBuild 命令重试后 exit 0。没有更换 SDK、关闭 PDB 或修改工程来绕过。保留既有无关警告。
- 使用现有 .github/scripts/release-artifacts.ps1 的 Assert-EmbeddedPptCom 检查新 EXE，结果为新 DLL 的 SHA256。没有启动 EXE/GUI/Office。

## 最终判断

已修正的交付链断点足以使上轮 GC 补丁没有进入实际主程序；目前的新 ARM64 Debug EXE 已包含该补丁。普通对象配平审查和新增生产清理探针没有证明另一处正常路径漏 Release。事件解绑持续失败的 CP 保留路径已用真实 COM 夹具复现；现场是否命中仍需新 EXE 的设备结果/错误记录。不能把测试通过或 GC 的存在当作实际 Office 进程退出已经验证。
