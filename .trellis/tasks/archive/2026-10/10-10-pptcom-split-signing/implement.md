# 实施及验证
只编辑自身workflow/辅助脚本，工程文件他人负责；规范由主会话更新。
读取已批准parent研究及PPT构建规范，保持原有编码CRLF与中文注释。
静态解析YAML与每个PowerShell块，验证产物选择、缺失输入和清理。
无远程Actions触发，不进行GUI；大构建由主会话串行进行。

## 完成清单
- [x] 对应范围实现、中文关键注释、原编码/BOM/CRLF与最小diff检查。
- [x] trellis-check 独立审查与产物校验/清理模拟通过。
- [x] PptCOM独立构建、Win32 Release off/on、ARM64 Debug solution与headless --no-window通过。
- [x] DLL/TLB继续跟踪、不忽略、本地哈希不变；已更新中英文编译说明及Trellis规范。
- [x] 用户禁止commit/push/触发workflow/GUI的约束保留，归档和session记录使用--no-commit。
