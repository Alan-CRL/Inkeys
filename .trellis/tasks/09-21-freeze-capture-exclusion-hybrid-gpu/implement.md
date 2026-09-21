# Implementation Order

1. 在 `IdtMagnification.cpp` 定义固定的 Window Service 排除角色集合。
2. 实现局部候选 HWND 校验、去重和 `MagSetWindowFilterList` 提交函数，复用既有失败日志。
3. 用统一提交函数替换 `MagnifierThread()` 中混合 legacy HWND 的一次性列表构建。
4. 在每次 `UpdateMagWindow()` 调用 `MagSetWindowSource` 前刷新过滤列表；失败则取消本次更新。
5. 静态搜索确认生产列表不再读取 legacy HWND，且没有改动 owner/显示/显卡路径。
6. 使用 ARM64 原生 MSBuild 构建 `InkeysRepo.sln` 的 `Debug | ARM64`，运行 `InkeysHeadlessTests.exe --no-window`，执行 `git diff --check`。
7. 执行 Trellis 检查并交付用户人工测试；不自动 commit、push 或归档任务。

## Verification

- `InkeysRepo.sln` `Debug | ARM64`：通过；仅有既存 hashlib++ C4267 warning。
- `Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`：退出码 0，输出 `PASS animation correctness`。
- `git diff --check`、UTF-8 BOM、纯 CRLF：通过。
- 独立 Trellis 检查：无未修复问题。
- 实际 Magnification 排除画面：等待用户人工验收。
