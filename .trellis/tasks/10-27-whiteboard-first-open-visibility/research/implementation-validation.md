# 实施与验证记录

## 本轮修改

- `Window.cpp`：minimized 下 Freeze SetBounds/Show 返回未提交；PPT/画布旧请求抑制保持原语义。Freeze 正常 SetBounds 读回 RECT，Show 读回 `IsWindowVisible && !IsIconic`。
- `Window.cpp`：新增 Freeze SC_MINIMIZE/SC_RESTORE、SIZE_MINIMIZED/SIZE_RESTORED 的低频非阻塞诊断，记录实际 visible/iconic/groupMinimized/RECT。
- `window_tests.cpp`：新增 minimized 下 Freeze 假成功负例、原 RECT 保持、Rejected 回执，以及恢复后的首次 bounds/visibility 重试。
- `Whiteboard.cpp` 无需修改：现有失败分支保留 damage 并 Retry；`whiteboardMinimizedChanged` 已请求 WhiteboardFreeze。
- 上一轮 IdtState 激活 guard/异步、Shell 全屏、Setting Present 解锁改动全部保留。
- 同步更新 whiteboard、rendering-and-ui、canvas-freeze-boundaries spec。

## 审查发现

初版把 minimized 下所有非 Bar/Setting Show 都改成 false，会无关改变 PPT/画布旧请求抑制语义；已收窄为仅 Freeze 返回未提交，其他角色保留原结果。最终完整构建在该修正后重新执行。

## 构建

第一次命令因 Git Bash 将 `/p:`、`/m` 转为路径而出现 MSB1008，未进入编译；使用 `MSYS2_ARG_CONV_EXCL='*'` 后解决，不是源码失败。

最终命令：

```text
MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m /nodeReuse:false
```

- ARM64 host：`C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe`
- 结果：Build succeeded，0 errors，1 个既有 `Draw3.DiagnosticOutput.h` C4702 warning。
- 日志：`Build/WhiteboardFirstOpenVisibility-solution-build.log`

## 自动验证

- `Inkeys.exe --state-mode-production-test`：PASS。
- `InkeysHeadlessTests.exe --no-window`：PASS，退出码 0；最终日志 655 行，末尾 `PASS animation correctness`。日志：`Build/WhiteboardFirstOpenVisibility-headless.log`。首次 300 秒预算不足以覆盖套件历史耗时，主动停止后以 900 秒预算完整重跑，最终结果不计入被停止的尝试。
- `git diff --check`：通过。
- Trellis task context validation：通过；两个大 spec 文件仅有注入截断警告，主会话已读取相关章节。

## GUI 边界

用户在实施计划中明确选择“仅无窗口验证”。因此新增真实 Window Service minimized/restore 回归只完成编译，未运行；冷启动第一次白板、实际可见性、任务栏恢复和 UI 卡死均未做现场验收。不得将本记录解释为真实故障已验证修复。
