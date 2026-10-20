# 已批准计划与约束

用户于 2026-10-10 明确批准完整实施计划。原失败日志：
https://github.com/Alan-CRL/Inkeys/actions/runs/38013686449/job/114099243392
首个致命错误位于 Inkeys/Inkeys/UI/Bar/Bar.Presentation.Test.cpp:941，为 C1002（pass 2 heap space）；初始 LINK 使用 HostX86/x86，随后重启 HostX64/x86。独立 InkeysHeadlessTests 已构建成功。

必须保持最小 diff、原编码/BOM/CRLF及必要中文注释。用户实施阶段修正：Inkeys/PptCOM.dll 继续 Git 跟踪且不忽略，由维护者在 PptCOM.cs 变更后手动同步 DLL/TLB；本地文件内容与 RC 引用位置保持。不得 commit/push/触发远程 workflow，不启动 GUI。SDK、依赖和现有 Actions 版本无需升级。
编译、上传和下载失败必须使 job 失败。
Windows 2022 native CI 使用 PreferredToolArchitecture=x64；本机 ARM64 原生 MSBuild 动态定位。
每次 MSBuild 在同一 PowerShell invocation 内执行 Remove-Item Env:PATH -ErrorAction SilentlyContinue 与 MSBUILDDISABLENODEREUSE=1。
