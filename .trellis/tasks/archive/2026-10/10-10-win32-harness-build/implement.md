# 实施与验证
读取parent研究及native/PPT规范，检查真实调用边界，保留编码/CRLF，添加中文注释。
修改native工程及必要Harness连接；写有意义的无GUI回归检查。
完成静态检查后交主会话串行Win32 off/on与ARM64 solution构建，自己不要同时运行大型构建或启动主程序。

## 完成清单
- [x] 对应范围实现、中文关键注释、原编码/BOM/CRLF与最小diff检查。
- [x] trellis-check 独立审查与产物校验/清理模拟通过。
- [x] PptCOM独立构建、Win32 Release off/on、ARM64 Debug solution与headless --no-window通过。
- [x] DLL/TLB继续跟踪、不忽略、本地哈希不变；已更新中英文编译说明及Trellis规范。
- [x] 用户禁止commit/push/触发workflow/GUI的约束保留，归档和session记录使用--no-commit。
