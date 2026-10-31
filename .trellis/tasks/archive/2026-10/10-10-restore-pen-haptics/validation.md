# 验证记录

## 范围与环境
- 用户于 2026-10-11 批准实施并要求关键逻辑中文批注；不创建 commit、不 push、不进行 GUI 或真实笔设备操作。
- 分支：bugfix/vibration；修改前 HEAD：a50ad143。生产构建入口为 InkeysRepo.sln，Debug | ARM64。
- 通过 vswhere 发现 ARM64 原生 MSBuild；保持项目 SDK、工具链、依赖和 solution 结构。
- 原始文件编码/BOM/EOL 快照：.trellis/.runtime/restore-pen-haptics/baseline-formats.json（151 个既有文件）。

## 修改前基线
- 运行既有 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window；退出码 1，11 项失败均涉及配置/原子文件写入。日志：.trellis/.runtime/restore-pen-haptics/headless-baseline.log。
- 等价 Win32 I/O probe：沙箱默认临时目录下 CreateFileW、WriteFile、FlushFileBuffers 成功，MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH) 失败，GetLastError=5 (ERROR_ACCESS_DENIED)。工作区 .trellis/.runtime 下相同操作全成功。此为执行环境问题，未修改产品原子写入代码。
- 将测试进程 TEMP/TMP 定向 .trellis/.runtime/restore-pen-haptics/test-temp 后重新运行完整基线，退出码 0，PASS animation correctness；日志 headless-baseline-workspace-temp.log。

- 修改前 Inkeys.exe 的 ARM64 dumpbin /imports 退出码 0；未出现 combase.dll、api-ms-win-core-winrt、RoInitialize、RoGetActivationFactory、GetPointerInfo、GetPointerType、GetPointerCursorId 静态导入。日志 imports-baseline.log。

## 实施后验证
- 首次沙箱内完整构建退出 1：15 项 C1902，多个无关 TU 在第 1 行报告 Program database manager mismatch；未更改工具链/项目配置绕过。
- 同参数沙箱外构建消除了 PDB 错误；发现 Bar marker 上界的有符号/无符号提升问题，已局部修复并加入普通鼠标 metadata 不误认回归。
- 最终完整 InkeysRepo.sln Debug|ARM64 构建退出 0，0 errors，18 warnings，日志 build-final.log；本轮剩余为既有 Bar 坐标窄化警告，前一轮还暴露既有第三方转换及原后端调试日志 C4702，未进行无关清理。
- ARM64 dumpbin /imports 退出 0；没有新增 combase/WinRT/上述 Pointer API 静态导入。日志 imports-final.log。
- 独立 trellis-check 完整复核，并修复 TakeLeave→Reset→Observe→Take 交错导致的旧停止标记误停新反馈；回归使用生产 mailbox + 实际 backend fake。
- git diff --check 通过，15 个既有产品/测试文件 BOM/CRLF 保持；新增文件 CRLF。
- 修改后完整 --no-window 测试退出 0，PASS animation correctness；日志 headless-final.log。新增 backend/token/时序回归与原输入、光标、橡皮测试均随现有入口运行，无窗口。

## 人工设备验收边界
本次只做静态、编译、无窗口命令行验证。实际笔体感、跨驱动 Pointer/WM_TOUCH 严格关联可达性、多笔切换、真实 Win7 和硬件异常路径均不以自动测试结果替代。
