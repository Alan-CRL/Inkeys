# Implementation Order

1. 在 Freeze 状态字中同步发布单调版本，以只发布状态的 observer 通知 Magnification 协调器，并提供“仅当版本仍匹配时关闭”的失败回退。
2. 将请求合并、过期检查、分阶段执行和 applied/visible 提交抽取到生产实际使用的小型协调边界。
3. 以 `MagnifierThread()` 作为唯一执行入口：保持 Host 透明、显示 Host/Child、重建并提交排除列表、提交一次 source、invalidate/redraw，最后在版本与 HWND 仍有效时揭开并提交成功状态。
4. 从 `FreezeFrameWindow()` 移除 Magnifier source、显隐、alpha 和二次请求；保留恢复、PPT 和 Freeze Surface 既有绘制职责。
5. 检查过滤、source、invalidate、redraw、reveal 和清理结果；失败只回退对应版本，后续新请求可再次尝试。
6. 删除错误的 D3D9/WDDM 推断，日志仅记录阶段、请求版本和目标 HWND，对连续同阶段失败去重并记录一次恢复。
7. 扩展 Headless 测试，覆盖单次 source、各失败阶段、失败后恢复、开启→关闭→开启、准备中取消、迟到结果、停止、生命周期重建、排除候选校验和 HWND 刷新。
8. 使用 ARM64 原生 MSBuild 构建 `InkeysRepo.sln` 的 `Debug | ARM64`，运行 `InkeysHeadlessTests.exe --no-window`，执行 `git diff --check`和编码/换行检查。
9. 执行 Trellis 检查并交付用户人工测试；不自动 commit、push 或归档任务。

## Verification

- ARM64 原生 MSBuild 完整构建 `InkeysRepo.sln` `Debug | ARM64`：通过；仅观察到既存 C4244/C4267 warning。
- `Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`：退出码 0，输出 `PASS animation correctness`。
- Trellis task validate、`git diff --check` 与编码/换行检查：通过。
- `native-desktop/rendering-and-ui.md` 已同步唯一 Magnifier coordinator、版本化取消、失败矩阵与测试合同。
- 基线动态排除列表已由用户初步人工确认改善主栏残影；本轮快速开关、工作区切换、焦点/任务栏/置顶/一像素行为仍等待实机视觉验收。
