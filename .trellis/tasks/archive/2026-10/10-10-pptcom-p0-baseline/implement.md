# P0 执行与复核

1. 固定基线和现有构建入口；研究仅在research/写入，生产源码只读。
2. 新增research/probes/下.NET4命名管道探针及PowerShell运行脚本，输出放TestResults/pptcom-p0/等忽略目录。用框架引用程序集实际编译；用当前安装CLR运行，记录差别。复用现有C#工具链，避免增加csproj/solution或包。
3. 并行核查COM/线程/活动证据和官方IPC兼容资料；根agent独占PptCOM.Tests构建，探针agent不要同时构建该工程。
4. 在旧DLL/TLB不复制的前提下构建并执行PptCOM.Tests；其他完整native构建非本阶段必需。已有headless二进制仅在能证明其源码/构建基线时才作为结果，不能把旧产物冒充本次验证。
5. 汇总首发传输决策、全部命令/结果/退出码、采样与未验证清单；Trellis check子代理独立复核探针和证据，必要时在本任务边界内修复。
6. 核查git diff/编码/无生产变更，记录阶段完成，更新父任务P0状态；不执行P1，不commit/push。

## 所有权

- 根agent：本任务元数据、IPC选型/验证报告、现有测试构建，父任务状态及收尾。
- trellis-implement：research/probes/源文件与探针执行结果，独立输出目录；不碰生产代码/项目文件。
- trellis-research：research/com-baseline.md及基线散列/活动采样证据；research以外只读。
- trellis-check：实现结束后审查本任务probe/报告，按发现最小修复；不递归派发。

## 后续门禁

Win7 x86/x64 + .NET4、两个权限方向与不同凭据、安全ACL/本机peer身份、真实PowerPoint/WPS损坏COM/多show/退出、事件到UI的延迟与资源趋势均待真实环境与GUI测试授权。P0静态与同权限探针不替代这些门禁。
