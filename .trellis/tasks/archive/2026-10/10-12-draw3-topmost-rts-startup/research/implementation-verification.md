# RTS 前置顶修复实施与验证

日期：2026-10-12；用户批准规划后实施。

## 产品修改

仅 Inkeys/IdtMain.cpp 新增 86 行：私有 PrepareDraw3WindowTopmost 以及 initial/rebuilt-ulw 两处 gate。UTF-8 BOM 和 CRLF 保持。

每代窗口启动前复用同步根刷新，读取 root/Mag/Freeze/Presentation/Drawpad 有效性、GW_OWNER 和 WS_EX_TOPMOST，复核句柄与策略仍同一代。Mag 缺失使用 Freeze 根。准备失败直接结束，不触发 DComp 回退；ULW 新链准备发生在旧 cleanup 完成且新窗口服务成功以后，新 Host cleanup scope 创建以前。

没有直接 SetWindowPos、Show、公共接口、逐窗置顶或周期刷新变化。首帧后的 Preview observer/milestone、whiteboard NOTOPMOST 保留。日志提供 phase、cause、role、HWND/owner/exstyle/read error；根命令错误由 Window Service owner thread 捕获。

## 编译：通过

已安装 Visual Studio ARM64 原生 MSBuild，InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false /v:minimal /nologo；同一 PowerShell 先规范化 Env:PATH 并禁用 node reuse。允许 10 分钟超时，正常完成，退出码 0。生成 Inkeys.exe、InkeysHeadlessTests.exe 和 PptCOM/PptCOM.Tests。

本次使用正常本机权限执行构建（此前沙箱构建曾超时）。既有 hashlib C4267 与原有 IdtMain 浮点转整型 C4244 不在新增代码中，未修改无关警告。pwsh.exe 找不到来自 vcpkg applocal 先探测 pwsh 的命令；已核对安装的 vcpkg.targets:245-266，其随后回退到系统 PowerShell；日志没有二者均失败警告。

本地构建日志：.trellis/.runtime/build-logs/topmost-rts-debug-arm64.log（runtime 文件，不进入产品提交）。

## 无窗口测试：通过

Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window，退出码 0，末尾 PASS animation correctness；existing 纯逻辑回归通过。此入口明确跳过 HWND 测试，不能证明实际 Z 序/RTS 前置 gate 的运行状态。

## 差异与审查

实现代理静态审查、编码检查与 git diff --ignore-submodules=all --check 通过；独立 trellis-check 未改源码，最终结论：无代码或规范待修问题，审查通过。

## 验证边界

未添加假启动排序或源码匹配测试：现有私有 helper 无合适无 HWND 生产测试 seam，不扩大公共 API。未启动可见窗口、真实 RTS/PPT/白板或 Computer Use；故障机器实际视觉置顶仍需验证。未 commit/push，用户于 2026-10-12 确认任务核验通过，要求结束任务并提交 commit。

## 用户验收与归档

2026-10-12 用户确认当前任务核验通过，授权结束和提交。本次归档以用户验收、编译/无窗口测试与独立审查通过为依据。AI 未运行 GUI，不将用户反馈扩写为已采集实际完整 Z 序或 RTS 内部机制证据。不执行 push。
