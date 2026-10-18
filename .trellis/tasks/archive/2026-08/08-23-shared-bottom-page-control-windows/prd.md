# 合并白板与 PPT 底部翻页窗口

## Goal

让 Presentation 与 Whiteboard 复用屏幕左右下方同一对分页 HWND，删除只为白板重复创建的两个覆盖窗口，同时保持两种工作区各自既有的视觉、输入和业务语义。

## Background

- 当前 `WindowRole::WhiteboardLeft/WhiteboardRight` 与 `WindowRole::PptBottomLeft/PptBottomRight` 都是 Drawpad owned layered popup，并位于画布之上；窗口创建证据位于 `Inkeys/IdtMain.cpp:1370-1444`。
- Whiteboard 通过 `BarSurfaceScene` 呈现固定左右分页栏，PPT 底部控件使用独立 PPT renderer；两者当前分别绑定独立 RenderPipeline 客户端，证据位于 `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.cpp:55-63` 与 `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:45-59`。
- 白板事务已经在进入时关闭 PPT 可见性，并在退出后按 COM 状态恢复，证据位于 `Inkeys/IdtState.cpp:420-446`、`Inkeys/IdtState.cpp:565-622` 与 `Inkeys/IdtPlug-in.cpp:525-533`。

## Requirements

- `PptBottomLeft/PptBottomRight` 成为 Presentation 与 Whiteboard 共用的左右下方分页宿主；产品启动时不再创建 `WhiteboardLeft/WhiteboardRight` HWND。
- Presentation 模式继续使用现有 PPT renderer、页面命令、拖动、缩放、位置记忆、长按和滚轮行为，不改变 PPT 中部控件与结束放映控件。
- Whiteboard 模式继续使用现有 `BarSurfaceScene`、固定左右锚点、页码事务、Previous/Add/Arrow 语义和白板业务回调；不得继承 PPT 的拖动、缩放、位置记忆、长按或 COM 页码。
- 同一时刻只有当前工作区的 renderer 和窗口过程可以消费共享宿主的输入或向其提交 ULW；进入、退出和反向重入期间共享宿主保持隐藏且不接受迟到点击。
- 进入白板前撤销 PPT/白板 capture 并停止 PPT 底部呈现；白板首帧就绪后才显示共享宿主。退出时先同步停止白板呈现并隐藏共享宿主，窗口模式恢复为 Presentation 后才重新发布 PPT 可见性。
- 共享宿主继续作为 Drawpad owned popup 位于画布之上、Bar 之下；Whiteboard 的 NOTOPMOST、任务栏锚点、窗口组最小化/恢复和退出恢复合同保持不变。
- 删除 Window Service、窗口创建、测试和规范中对独立 Whiteboard 左右 HWND 的依赖；保留 Whiteboard 自己的渲染客户端和 Scene 资源，不强行合并两套视觉实现。

## Acceptance Criteria

- [ ] 运行时只创建一对左右下方分页宿主 HWND；不存在 `WhiteboardLeft/WhiteboardRight` 窗口角色、窗口规格或句柄。
- [ ] PPT 放映中的底部翻页视觉、页码、拖动、缩放、长按、滚轮、位置记忆和 Bar 下方 Z 序与改动前一致。
- [ ] 进入白板后，共享宿主显示白板分页视觉并执行独立白板上一页、下一页和末页加页；PPT 页面状态与配置不改变白板行为。
- [ ] 连续进入、退出和中途反向切换不会出现 PPT/白板旧帧互相覆盖、重复窗口、迟到点击、capture 残留或共享宿主无法恢复。
- [ ] 白板窗口组最小化/恢复只记录一次共享左右宿主，并仅恢复最小化前可见的成员。
- [ ] Headless 测试覆盖共享角色、模式门禁及既有白板页码合同；`InkeysRepo.sln` 的 `Debug|ARM64` 完整构建和 ARM64 `InkeysHeadlessTests.exe --no-window` 通过。

## Out Of Scope

- 不统一 PPT 与 Whiteboard 的视觉绘制实现、页面状态或业务命令。
- 不修改 PPT 中部左右控件、结束放映控件及其配置界面。
- 不改变 Whiteboard 的固定布局尺寸、Bar 样式来源、Draw3 文档或 Freeze 背景。
- 不增加新的用户配置、持久化迁移或兼容旧窗口句柄的外部 API。
