# GC 恢复实施验证

用户授权范围：仅恢复原有 FullCleanup 最后 Collect → WaitForPendingFinalizers → Collect。

## 改动

- PptCOM/PptCOM.cs:545 起新增一条中文注释、三条 GC 调用及空行；位置为 root 显式释放、字段清空、页码重置之后，CLEAN 日志之前。与原实现相同，重绑与最终清理均执行。
- 未改其他对象的 Release/FinalRelease、事件连接/重试、dynamic、COM ABI、native 或工程配置。
- com-contract.md 仅同步两处相关约定；当前任务文档按用户最新明确范围更新。

## 验证结果

- 使用安装中的 ARM64 原生 MSBuild 构建 PptCOM.Tests.csproj Release|AnyCPU，先在同 PowerShell invocation 规范化 PATH、禁 node reuse；exit 0。
- PptCOM DLL/TLB 生成成功，PptCOM.Tests.exe 构建成功。
- 运行 PptCOM.Tests.exe --dispatch-fixture Build/pptcom-dispatch-fixture/x64/NativeDispatchFixture.dll；实际 .NET Framework CLR 为 x64，与夹具匹配；完整测试 exit 0，原生夹具 43 cases、0 failures。
- 现有测试检查对象逐笔配平、会话/owner/清理路径。未为三条调用新增镜像实现的测试。
- 字节检查：移除新增的唯一 GC 块后，生产源码与 HEAD 相同；UTF-8、原 BOM 状态、CRLF 均保持。git diff --check 通过。

日志：validation/gc-restored-managed-build.log、validation/gc-restored-managed-fixture-tests.log。

## 实际边界

未启动 GUI/Office 或 Inkeys 主窗口，没有完整构建 native（本次仅改托管清理、COM ABI 不变）。没有自动复制生成的 DLL/TLB 至 Inkeys 随附资源，没有 commit/push。真实 PowerPoint 关闭后进程自然退出和下一文稿仍待设备验收，任务保留该验收边界，不将本次构建/夹具结果等同于实机问题解决。
