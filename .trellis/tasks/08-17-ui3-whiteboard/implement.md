# UI3 白板执行计划

## Ordered Checklist

- [x] 扩展 Draw3 workspace/page runtime、产品快照、命令与输出门控。
- [x] 扩展 WindowRole、owner/style、Whiteboard mode、NOTOPMOST、taskbar activation anchor 和窗口组最小化/恢复。
- [x] 使用 Freeze 背景与两个 Whiteboard RenderPipeline 客户端建立白板 UI。
- [x] 将分页控件迁移为三个标准 Bar `twoTwo` 按钮，并统一为 `230x80 DIP`、Bar SVG/主题/动画/光效/脏区合同。
- [x] 实现 `PageStateTransaction`，稳定追加页、Previous enabled、Add/Arrow 与首次 switching 发布。
- [x] 在所有 workspace 状态切换时收起辅助面板并撤 capture，进入白板保持面板关闭。
- [x] 完成 Freeze/Drawpad activation、taskbar、NOTOPMOST、minimize/restore 与退出 click-through/Bar/dock 恢复。
- [x] 统一 Whiteboard 红/绿调试框，并把 D2D/GDI 借用资源租约延长至 `EndDraw`。
- [x] 添加分页、窗口 style/lifecycle、Draw3 workspace、Bar dock 与 present 状态的 headless 回归测试。
- [ ] 在允许 GUI 的环境完成 50 次进入/退出、任务栏、桌面点击穿透与 D2D Debug Layer 验收。

## Validation

- `git diff --check`
- `git show --check` 审查近两天 Whiteboard 提交。
- ARM64 host `MSBuild.exe InkeysRepo.sln /m /t:Build /p:Configuration=Debug /p:Platform=ARM64`。
- x64 host `MSBuild.exe InkeysRepo.sln /m /t:Build /p:Configuration=Debug /p:Platform=x64`。
- `Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`。
- `Build\x64\Debug\InkeysHeadlessTests.exe --no-window`。
- 只执行静态或无窗口测试，不启动可见窗口。

## Risk Gates

- workspace 切换必须等待 active contact 与目标首帧，禁止 Whiteboard/Presentation 双写。
- 分页事务中不得用中间快照改变未变化按钮；`switching` 只改变 interactive。
- Whiteboard 期间任何 topmost refresh 都必须保持窗口组 `HWND_NOTOPMOST`。
- render/present 借用指针必须持有到 `EndDraw`，绿色 debug present 框不得污染业务 dirty。
- 任务保持 `in_progress`，直到可见 GUI 验收完成。
