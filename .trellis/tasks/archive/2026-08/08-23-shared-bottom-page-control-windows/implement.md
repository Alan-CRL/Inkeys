# 共享底部分页窗口执行计划

## Implementation

- [x] 在 `Inkeys.Window` 删除 `WhiteboardLeft/WhiteboardRight` 角色及 Window Service 中的创建、销毁、样式、最小化/恢复、capture 与隐藏集合重复项；让共享 `PptBottomLeft/PptBottomRight` 保持 Drawpad owner 与既有 Z 序。
- [x] 在 `IdtMain.cpp` 删除独立 Whiteboard 左右窗口规格，并让共享 PPT 底部窗口使用可按 Whiteboard mode 分发的窗口过程。
- [x] 在 `Inkeys.UI.Whiteboard` 把控制 surface 映射到共享 PPT 底部角色，调整 inactive、PublishActive、Shutdown 和 capture 逻辑，确保不隐藏 Presentation owner 的窗口。
- [x] 在 `Inkeys.UI.Ppt` 增加共享宿主 owner 门禁和 Whiteboard WndProc 分发；清理模式切换时的输入、drag、长按和迟到 render 请求。
- [x] 调整 `IdtState` 的 Enter/Exit 顺序：共享宿主在两种 owner 之间经过隐藏态，PPT 只在成功离开 Whiteboard window mode 后恢复。
- [x] 更新 `InkeysHeadlessTests/window_tests.cpp`、`whiteboard_ui_tests.cpp` 及必要的 PPT/RenderPipeline 测试，覆盖共享角色和 owner 决策。
- [x] 更新 `.trellis/spec/native-desktop/rendering-and-ui.md` 中“五个 PPT + 两个 Whiteboard HWND”、独立角色和白板分页尺寸的过期合同。

## Validation

- [x] 运行 `git diff --check` 并静态搜索，确认产品路径不再引用 `WindowRole::WhiteboardLeft/WhiteboardRight`，且没有意外修改 PPT 中部/退出控件。
- [x] 使用 ARM64 `MSBuild.exe` 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`，超时不少于 5 分钟。
- [x] 运行 ARM64 Debug `InkeysHeadlessTests.exe --no-window`。
- [x] 静态审查 Enter/Exit/反向重入、renderer fixed order、Shutdown 与 Window Service Stop 顺序；本会话不启动 GUI 窗口。

## Risk And Rollback Points

- 共享 HWND 最大风险是 mode 切换与已排队 RenderPipeline 回调交错；任何提交点缺少 owner 二次确认都可能造成旧帧覆盖。
- `WindowRole` 删除会影响数组大小和连续范围推导，必须依靠完整编译与 Window 测试捕获遗漏。
- `PptBottom*` 名称作为共享宿主是有意的最小改动；不要在本任务顺带重命名配置、Control 或 RenderPipeline client。
