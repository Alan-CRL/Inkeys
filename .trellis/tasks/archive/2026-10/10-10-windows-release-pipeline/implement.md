# 实施顺序
1. 子任务Artifacts及JSONL已准备，用户已批准最新完整计划；start后分派两个实现代理，互不覆盖所有权。
2. 主会话改solution、PptCOM导出目标，保留DLL/TLB跟踪并核本地哈希，同步中英文编译流程。
3. 集成后Trellis check代理静态审查及产物处理模拟，主会话串行真实构建，避免输出/缓存冲突。
4. 独立PptCOM Release/AnyCPU；Win32 Release Harness off与on；ARM64原生MSBuild完整solution Debug/ARM64；headless --no-window。
5. 校验PE资源DLL哈希；不能运行的跨架构CLI明确记为外部验收。
6. 同步build/PPT规范并记录session（禁止自动commit）；完成可验证项后归档完成的任务，无远程操作。

## 完成清单
- [x] 对应范围实现、中文关键注释、原编码/BOM/CRLF与最小diff检查。
- [x] trellis-check 独立审查与产物校验/清理模拟通过。
- [x] PptCOM独立构建、Win32 Release off/on、ARM64 Debug solution与headless --no-window通过。
- [x] DLL/TLB继续跟踪、不忽略、本地哈希不变；已更新中英文编译说明及Trellis规范。
- [x] 用户禁止commit/push/触发workflow/GUI的约束保留，归档和session记录使用--no-commit。
