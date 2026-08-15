# 运行时重做与分支丢弃执行计划

1. 扩展 `CanvasRuntimeHistory`：redo 栈、查询、事务提交、分支丢弃和深度；补齐连续 Undo/Redo、错误 expected、Tile 引用、generation 与新分支测试。
2. 在窗口命令末尾追加 Redo，并映射数字键/小键盘 `6`，复用现有自动重复过滤和 FIFO wake。
3. 在 Controller 增加 `redoCurrentPage`：状态令牌校验、不可信 L2 背景恢复、Stored Stroke 局部直接绘制、热前像重新捕获、visibility 后提交和失败回滚。
4. 在新 Stored Stroke 写入成功后、RenderItem/GPU 步骤前丢弃 redo 分支；Laser、Cancelled 和导航不触发。
5. 更新 README 与 native runtime spec，静态核对接口引用、日志字段、无 readback/wait 和原编码/换行。
6. 运行 `git diff --check`，使用 ARM64 MSBuild 构建完整 Debug/Release solution，并运行两套无窗口测试；记录未执行的可见窗口和 D3D Debug Layer 验证。

Rollback point：CPU redo 栈、窗口命令和 Controller 接入可分别回退；GPU 只复用现有公开能力，不改变资源格式。

## Validation Results

- ARM64 `Debug|ARM64` 与 `Release|ARM64` 完整 solution 构建通过；主工程和测试工程均成功链接，Shader/资源目标纳入构建且为最新状态。
- Debug、Release 两套 `inkStrokeModelerTestTests.exe` 无窗口运行通过，包括新增 redo 顺序、空栈、错误 expected、分支丢弃、Tile/generation 与 per-Canvas 隔离断言。
- `git diff --check` 通过；C++/cppm 测试文件均为 UTF-8 BOM + CRLF，README/spec/task Markdown 保持原有 LF。
- 静态核对 `6`/`VK_NUMPAD6` 自动重复过滤、FIFO/active-contact 延迟、状态令牌、GPU 后提交、热前像取消/提交、隐藏 history Tile 回滚，以及无 postimage/readback/wait。
- 未人工验证：真实键盘 `6`/小键盘 `6`、D3D Debug Layer、桌面显示效果；按项目约束未启动可见窗口。
