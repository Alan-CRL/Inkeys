# 分段诊断版准备验证（2026-11-01）

## 已完成

- managed Release|AnyCPU，ARM64原生MSBuild构建/全部coretests退出0。新增覆盖enabled/disabled、实际request/maintenance、精确mailbox入出队/stop-drain/queued-cancel、2秒超时后owner晚成功、writer异常、IErrorInfo和既有RCW释放账本。
- 独立.NET Framework rawstderr探针：Console.SetOut/Error(Null)后，开关1仍有2条完整成对事件，开关0为0字节；证据Build/Validation/ppt-perf-managed/raw-*。
- 完整InkeysRepo.sln Debug|ARM64最终退出0，9.08秒，0warning/0error。首轮产品编译链接通过，但新增测试局部small与WindowsRPC宏冲突，C2632；只将测试变量改名truncatedBuffer后重跑通过。这是本轮测试源码错误，未修改工具链或工程选项绕过。
- InkeysHeadlessTests.exe --ppt-session-only退出0，约2.56秒，无HWND/GUI。覆盖禁用零时钟/输出、producer延迟与精确队列驻留分离、跨线程乱序、异常闭合、TLS恢复、格式截断和写入异常。
- 分析器10项自测通过，含长队列/焦点、精确邮箱/维护嵌套去重、超时晚完成、重复/乱序/缺失、PID/clock/身份冲突、无控件样本与异owner线程排除。真实managed探针也能按协议解析，正确标no-native-navigation，不冒充控件样本。
- Start-PptNavigationDiagnostic.ps1 -PrepareOnly通过，native_perf_marker_present=true；未启动GUI，前后main.json SHA一致。既有PowerShell7/RemoteSigned可运行；系统PS5仍Restricted，仅作AST检查，没有修改/绕过策略。
- EXE内嵌DLL只读核对通过：52465C7A0779F6E8CBF4AB72E083ED69DB5F9382893C02021BBCAC663A921526。TLB字节未变C652B5DD...，公开COM ABI保持原样。
- 独立trellis-check最终无未修findings；审查纠正了native queue取时边界、分析器无样本/身份校验/owner线程过滤以及测试宏重名。git diff --check与原文件BOM/CRLF通过。

## 验证范围

本轮只准备诊断，不操控桌面、不启动Office/产品GUI、不自动commit/push。未再次运行完整headless的无关WARP/设置矩阵，因为实际变更只涉及诊断与采集，已用完整solution+managed+PPT定向+解析器/脚本验证。真实1.23秒复现和最终归因待用户按navigation-perf-handoff-20261101.md操作。

原始构建日志：Build/Validation/ppt-navigation-perf-20261101/solution*.log。PrepareOnly元数据：Build/Validation/ppt-navigation-20261101-114611-700-b699d7f5/manifest.json；该目录仅为准备检查，不含人工测试样本。
