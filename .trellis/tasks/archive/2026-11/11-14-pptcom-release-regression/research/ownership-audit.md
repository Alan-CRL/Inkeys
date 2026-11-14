# 补充调查：其他 COM 对象是否正确释放

用户补充：明确确认需要恢复 Collect → WaitForPendingFinalizers → Collect；继续核查其他对象是否都正确手动释放。此轮继续调查，没有修改业务源码。

## 当前结论

正常可达路径中，已枚举的常规 COM 临时对象有匹配的 finally 释放或明确所有权转移，未找到新的确定漏释放点。已有真实 IDispatch 原生夹具 43 个用例通过，支持这些已覆盖路径的配平；不能据此声明用户现场所有引用都已释放。

需修正可能造成误解的表述：**单次 ReleaseComObject 不等于错误**。每次取得正确配平且根对象只剩一笔持有时，单次 Release 可以归零。当前 FullCleanup 不区分 final 的事实和旧强制 GC 被移除是确认的；“当前一定存在未归零 root”仍需实测，不能从没有 FinalRelease 推导出来。

C# 没有 C++ 式的 delete。手动释放的是包装器内部持有的 COM 引用；纯托管包装对象自身由 GC 回收。仅赋 null 不替代 COM Release，也不能对 borrowed alias 多 Release 一次。

## 对象逐类核对

| 对象/取得来源 | 当前释放位置/转移 | 判断 |
| --- | --- | --- |
| ROT、枚举器 | PptCOM.cs:1186–1187 | 扫描外层 finally 释放 |
| 每轮 IMoniker、IBindCtx、ROT GetObject 返回 | PptCOM.cs:1159 的 CleanUpLoopObjects → :454–458 | 本轮 finally 释放；application moniker 转移时先清空 comObject，避免双释放 |
| ROT 候选 Application | PptCOM.cs:1147–1183 | 非保活候选在本轮 finally 释放；去重列表持有者在扫描结束释放；最终 best 只转移一笔 |
| 调用方 bestApp | PptCOM.cs:1222–1228 | 转给 pptApplication 后置 null，否则无条件 finally 释放；不是按 identity 相同就跳过 |
| ActivePresentation/SlideShowWindow 比较临时对象 | PptCOM.cs:671、697–698 | 不论 identity 是否等于 root，都释放本次取得；入参/root 本身不额外释放 |
| 每次观测的 View、freshWindow、activePresentation、showPresentation | PptCOM.cs:1394、1413–1416、1448–1450 | View 局部 finally；窗口转移后清空局部；剩余对象外层 finally |
| descriptor 的 View、current Slide | PresentationDescriptor.cs:363–364 | 按 slide → view 释放，普通异常/busy/提前返回均经过 finally |
| descriptor 的 Slides、每个 Slides.Item | PresentationDescriptor.cs:428、564、610 | for 循环逐个释放 Item，集合外层 finally；没有 COM foreach 隐式枚举器 |
| 常规页码/集合 helper | PptCOM.cs:1738、1748、1760、1772、1785 | Slide、View、Slides、SlideShowWindows、Presentations 均 finally 释放 |
| 翻页/总览/批注操作 | PptCOM.cs:1570、1633、1910 | View/SlideNavigation 均具名临时对象，命令失败或 session 过期仍释放 |
| 退出放映操作 | SlideShowSession.cs:462–463 | View 在 finally 释放；畸形 COM-valued HWND 也释放；借用 window 不释放 |
| HWND typed 接口别名 | PresentationDescriptor.cs:83–87；PptCOM.cs:1666–1668 | as/cast 不是另一笔 COM 对象返回，不该单独 Release |
| identity 比较原生 IUnknown 指针 | PptCOM.cs:623–631 | GetIUnknownForObject 对应 Marshal.Release，不能改成 ReleaseComObject |
| 事件连接点包装 | ComEventConnection.cs:103–121、136–158 | FindConnectionPoint 返回由 connection 接管；Advise 失败走 finally；解绑成功/明确断连后调用持有的 release 委托 |
| raw sink 的 QueryInterface 返回指针 | ComEventConnection.cs:52–54 | 返回引用交给 COM 调用方，调用方负责 Release；不能在返回前由 sink 再 Release |

## 尚不能确认清干净的两处

1. **最终根对象**：PptCOM.cs:525–527 → PresentationDescriptor.cs:182–191 不检查 ReleaseComObject 返回值，且吞掉失败。没有直接证据说明真实 PowerPoint 的 root 在此均归零。原生夹具 Scope.Dispose 自行释放测试 root（DispatchFixtureTests.cs:193–225），生产 FullCleanup 的既有 owner 测试使用 FakeNode，这仍是覆盖缺口。后续应补原生 RCW 进入生产 FullCleanup 的测试，分别核对普通重绑和最终独占清理。
2. **持续失败的事件解绑**：PptCOM.cs:490–491 和 ComEventRetirementRegistry 的静态列表有意保留 CP/cookie/sink，后续重试。TryDisconnect 在 Unadvise 持续 busy/失败时不会释放 point；最终三次仍失败返回 -1。此时对象确实还活着，GC 无法把它清掉。现有测试证明一次 busy 后恢复和失败结果保留，并未证明用户现场不进入该持续失败分支。不能直接丢掉 registry 或强行 FinalRelease 所有 CP 来伪造成功。

包装器闭包保留 RCW 托管对象，不自动意味着底层 COM 仍活着；ReleaseComObject 到零后它即已断开。当前没有证据支持将整个 accessor 或 raw sink 重写为另一套包装器。

## 本轮实际验证

- 构建：使用 vswhere 定位安装中的 ARM64 原生 MSBuild；同一 PowerShell invocation 先规范化 PATH 并关闭 node reuse。构建现有 PptCOM.Tests.csproj Release|AnyCPU，由 ProjectReference 构建 PptCOM，DLL/TLB 导出成功，exit 0。
- 原生夹具：运行既有 Build-DispatchFixture.ps1 -Architecture x64，使用 Hostarm64/x64 cl、项目 v143 与 SDK 10.0.26100.0，exit 0。
- 测试：PptCOM.Tests.exe --dispatch-fixture <absolute-path>，实际 CLR 为 .NET Framework 4.0.30319.42000、PE 0x8664，与夹具匹配；43 cases、0 failures，完整控制台测试 exit 0。
- 日志中的 event-cleanup-pending/busy 是测试主动注入的失败场景，不是本轮观察到用户 PowerPoint 的故障。
- 没有运行 Office、GUI 或 Computer Use，没有编译/启动 Inkeys 主程序，没有改 PptCOM/PptCOM.Tests/Inkeys 源码或随附发布 DLL/TLB，没有 commit/push。

日志位于本目录 validation/managed-build.log、fixture-build.log、managed-fixture-tests.log。它们验证当前已有路径，不代表恢复 GC 的修改已经实现或用户故障已修复。

## 后续最小范围

恢复强制 GC 是已确认要求。实施前补上述真实 RCW/生产 FullCleanup 验证，决定最终根对象清空所需的最小局部修改；保留正确的临时对象配平和借用规则。事件清理结果必须单独检查，不把 GC 当作 Unadvise 的替代。
