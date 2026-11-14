# 用户决策与候选修复回退

## 用户要求

2026-11-14：将当前证据记录到本Trellis任务；撤回后续未解决实机问题的修复，业务回到最初仅补三行GC的版本，允许保留调试输出。未来大幅优化架构，采用多线程动态绑定，使旧清理等待时新绑定已能开始。本轮不实施该架构。

## 已确认与未确认

- 第四份日志seq1..3681完整，旧Unadvise真实耗时55.5777165秒，旧服务最终419/419配平，结束后退避约2.112秒，新服务约0.269秒进入Active。
- 第三份日志CP Release真实耗时33.4575715秒，说明Unadvise和Release均可能形成等待。
- 用户确认没有Inkeys也会后台驻留；旧POWERPNT仍在时重启Inkeys，新文稿却可立即联动。当前产品问题是旧同步清理阻塞下一绑定，不应继续以后台驻留本身作为修复成功标准。
- 两次结束放映后未响应的根因仍未定位；不能把引用最终配平或本次回退宣称为它们的修复。
- 完整证据见 fourth-user-log-and-rebinding-block.md、third-user-log-and-baseline.md，以及 validation/fourth-user-log-analysis.json。

## 回退范围

业务基线为当前HEAD `e73673f7b1c91fd13cc5a4a946960d7b9934c646`，不是复原整个工作区。保留原GC三行和调试仪表；没有恢复FinalRelease。

撤回：关闭标记字段及消费/重入fence；2025/2004关闭延后；2031直接退休；关闭中的ROT让出；0x800706BA在service/CP中的新增断连分类；ActivePresentation busy时继续探测Count的候选改动。对应新增候选用例和规范同步撤回。

保留：弱对象引用账本、每次取得/转移/单次释放日志、事件队列与原始event_qpc/event_tid、Unadvise/getter/command/Release真实耗时、服务/GC阶段日志、慢输出隔离与诊断错误隔离测试。原有事件/轮询、失败CP重试和单次Release顺序恢复基线行为。

## 后续架构任务边界

多线程动态绑定应使旧清理与新发现独立推进。旧所有者独占其COM取得，旧sink/命令/session及native页码写入必须先失效；新所有者不能被旧CP重试再次阻塞。需要明确apartment/封送、清理任务上限、重复开关文稿、同Application多文稿和Inkeys退出边界。

验收先用可控制闸门分别阻塞旧Unadvise和Release：闸门未释放时新绑定已Active，旧事件/命令/页码不能污染新session；闸门释放后所有权仍逐笔配平。这些内容当前只记录，不宣称已实现。

## 回退验证

- PptCOM.Tests.csproj Release|AnyCPU：ARM64原生MSBuild，退出码0。
- 全部托管用例及真实IDispatch夹具：退出码0，原生47 cases、0 failures；保留诊断测试，候选专用测试已撤回。首次夹具调用被要求使用绝对路径，修正命令参数后通过，未修改项目代码绕过。
- InkeysRepo.sln Debug|ARM64：退出码0，使用vswhere定位当前安装的ARM64原生MSBuild，在同一PowerShell invocation应用PATH规范化、禁node reuse。复用此前已确认的本机正常构建环境；未修改工具链、SDK、依赖或工程配置。
- 构建前已手动同步新DLL并更新时间戳触发资源编译；Assert-EmbeddedPptCom确认EXE内嵌与本次DLL一致，SHA256=AD2C5E69FFEB20CA1302DB939BD8D9FA7DAE79104FA3D5C8492016090215C70C。
- DLL模块MVID=f9290ae8-fb4c-408c-bcd8-67f8f81b9b4b；元数据探针确认FullCleanup仍为Collect,WaitForPendingFinalizers,Collect。
- TLB与Inkeys随附文件逐字节一致，SHA256=C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606，ABI未变。
- 移除GC与诊断后的PptCOM.cs控制流token与HEAD基线一致（两侧等同归一化注释/空白及单语句花括号），CP断连分类函数与HEAD逐字一致。详见validation/rollback-source-verification.json。
- 修改的生产/测试/spec文件保持原UTF-8、BOM状态和CRLF；git diff --check通过，没有大面积无关格式变更。
- 完整构建仍输出既有pwsh.exe不在PATH的附带消息，实际MSBuild退出码0，Inkeys与依赖均成功生成；未为消除该环境提示调整项目。
- 产物：Build/ARM64/Debug/Inkeys.exe、Inkeys/PptCOM.dll。本轮未启动Office/Inkeys/浏览器GUI、未用Computer Use、未commit/push。真实联动阻塞仍保留为后续架构任务，未标修复。

验证日志：validation/rollback-managed-build.log、rollback-managed-tests.log、rollback-solution-build.log，以及rollback-source-verification.json。
