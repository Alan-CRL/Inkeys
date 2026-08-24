# 实施计划：非白板覆盖层置顶与呈现失败收敛

## Ordered Checklist

- [x] 1. 在 `Inkeys/Inkeys/UI/Ppt/Ppt.cpp` 记录 `presentationVisible` 的 `false -> true` 边沿和内部 refresh pending；发布状态后请求根刷新，成功/隐藏时清除，失败时由后续发布重试。
- [x] 2. 在 `Inkeys/Inkeys/UI/PageControl/PageControl.cpp` 检查 `SetBounds/Show/Hide` 的 bool 结果；失败返回 `FrameResult::Retry`，并添加仅失败时的提交诊断。
- [x] 3. 在 `Inkeys/IdtState.cpp` 将 Draw3 surface reconciliation 改为 Waiting/Applied/Retry 三态，在状态线程中维持 pending，并在失败时记录完整 Draw3/窗口快照。
- [x] 4. 在 `Inkeys/Inkeys/Window/Window.cpp` 的现有根刷新失败点捕获 owner thread 的 Win32 error/context，但不改变 `HWND_TOPMOST/HWND_NOTOPMOST` 选择或 owner 关系。
- [x] 5. 在 `Inkeys/Inkeys/Window/Window.Legacy.cpp` 消费周期刷新失败并输出 overlay owner 树关键状态；保持现有刷新间隔与停止逻辑。
- [x] 6. 扩展 `InkeysHeadlessTests/window_tests.cpp`：验证隐藏根抬升、外部 topmost 竞争窗口、整树连续性、Bar/PPT sibling 顺序、非根无独立 topmost 和 no-activate。
- [x] 7. 未扩大生产公共 API；真实 Win32 失败不做不稳定注入，依赖现有 RenderPipeline Retry 合同、生产分支静态审查和隐藏 HWND 集成覆盖。
- [x] 8. 检查修改文件编码与 CRLF、运行静态 diff 检查、完整 ARM64 构建和无窗口测试。

## Validation

1. 定位 Visual Studio 的 ARM64-host MSBuild，并确认路径包含 `MSBuild/Current/Bin/arm64/MSBuild.exe`。
2. 构建完整 solution（超时至少 5 分钟）：

```powershell
& '<ARM64-host-MSBuild.exe>' InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64
```

3. 运行无窗口测试：

```powershell
.\Build\ARM64\Debug\InkeysHeadlessTests.exe
```

4. 静态检查：

```powershell
git diff --check
git diff --stat
git status --short
```

5. 验收输出需明确包含新增 z-order 测试结果；不得启动 Inkeys 主程序或任何可见 GUI。

## Risky Files And Rollback Points

- `Inkeys/Inkeys/Window/Window.cpp`: owner-thread Win32 核心；只允许增加失败上下文，不得改根置顶语义。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp`: 共享渲染线程；日志和错误处理不得位于会导致反向状态发布死锁的锁序之外。
- `Inkeys/IdtState.cpp`: 状态线程；pending 只能在失败时维持，成功后必须清除，避免固定 250ms 重复 HWND 命令。
- `Inkeys/Inkeys/UI/Ppt/Ppt.cpp`: 500ms 重复发布路径；边沿成功后必须去重。

## Pre-start Gate

- [x] Goal、范围、非目标和验收标准已明确。
- [x] 根置顶 Win32 行为已用本机隐藏 HWND 探针验证。
- [x] 没有未解决的产品/兼容性决策。
- [x] `prd.md` 已完成收敛重写。
- [x] `design.md` 与 `implement.md` 已创建。
- [x] 用户在最终规划摘要之后明确批准实施。
- [x] 批准后运行 `task.py start` 并加载 `trellis-before-dev` Phase 2 上下文。

## Validation Results

- `2026-08-24`: ARM64-host MSBuild 完整构建 `InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64` 成功，0 errors。
- `2026-08-24`: `Build/ARM64/Debug/InkeysHeadlessTests.exe` 通过，输出 `PASS animation correctness`；新增隐藏 HWND z-order 用例已执行。
- `2026-08-24`: `git diff --check` 通过；任务源文件保持各自原有 UTF-8 BOM 策略与 CRLF。
