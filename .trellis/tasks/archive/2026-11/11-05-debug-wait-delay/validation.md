# 验证记录

- Trellis implement/check 代理完成实现与独立复核；复核修复原生隐藏/销毁退休及派发前初始化/生命周期检查。
- Scripts/i18n.ps1 check：全部 20 语言结构检查通过（19 个非源语言）。
- git diff --check --ignore-submodules：通过；修改保持 BOM 与原工作区 CRLF，无无关格式化。
- ARM64 原生 MSBuild 路径由 vswhere 定位；同 invocation 规范化 PATH、禁用节点复用。
- 初次受限环境构建创建无效子节点并等待，已核对主进程后终止本次构建树；未改源码/项目配置。
- 正常环境构建源码通过，增量链接器 LNK1000 / IncrBuildImage 内部崩溃；用命令参数 /p:LinkIncremental=false 重试，InkeysRepo.sln Debug|ARM64 完整构建退出 0，0 errors（67 warnings），日志 Build/debug-wait-build-nonincremental.log。工程文件未修改。输出存在 pwsh.exe 未在子进程 PATH 的提示，未影响构建退出结果。
- Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test：退出 0，[PageControlUI3] failures=0，包含新增延迟生产路径断言。日志 Build/debug-wait-offscreen.stdout.log / .stderr.log。
- Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window --startup-preview-only：退出 0，[StartupPreviewOnly] failures=0。
- 未进行 GUI 观感测试，遵守仓库默认禁止启动交互式界面的要求；未 commit/push。

- 用户验收：确认改动生效，授权结束归档并提交。
