# PPT 翻页控件主栏化

## Goal

将 PPT 翻页控件迁移到 Bar UI，共享白板底部分页形态，并将结束放映迁移至主栏 A2。

## Requirements

- 使用 `Inkeys.UI.PageControl` 统一拥有四个 PPT 翻页窗口的 Scene、输入、拖动和呈现；PPT 与 Whiteboard 仅发布状态和业务回调。
- PPT 底部与两侧翻页控件改用深色 Bar UI 主题、边框和光影，按钮内容变化沿用 Bar 动画。
- PPT 底部可见外框为 `165 x 42.5 DIP`，两侧为 `42.5 x 165 DIP`；拖动条、翻页按钮、页码按钮按批准规格排布。
- PPT 翻页按钮只显示箭头；页码显示当前页加粗、总页数常规字重，按下仅有视觉反馈，不触发业务动作。
- 只有拖动条可移动成对控件；拖动条可命中和捕获，但没有悬停、按压或点击视觉。
- 保留缩放、位置记忆、长按连续翻页、滚轮翻页、DPI、最近可行碰撞回退和 `PptInfoStateBuffer` 语义。
- 主栏当前可见矩形作为最高优先级碰撞障碍；临时避让不得覆盖用户保存的位置。
- PPT 底栏首次显示时在目标位置渐显；侧栏保留既有侧向显隐行为。
- PPT 底栏已显示时进入全屏白板，连续形变为 `230 x 80 DIP` 的三枚 `70 x 70 DIP` 白板按钮；expanded 布局目标独立于 Draw3 就绪后发布的背景 active，退出时反向过渡，反向重入不得闪回或经过隐藏帧。
- 移除独立结束放映窗口和对应 RenderPipeline 客户端；主栏 A2 新增固定 `2 x 2` EndShow 按钮，复用原 `ppt3` 图标、文字和结束确认流程。
- A2 运行时布局固定为：桌面显示 Whiteboard + Freeze；PPT 显示 Whiteboard + EndShow；全屏白板仅显示 Close Whiteboard。
- A2 合法旧配置迁移时保留 Whiteboard/Freeze 相对顺序并追加 EndShow；非法配置仍回退固定区默认值。
- 设置页移除独立结束窗口显示、位置和缩放入口；旧 JSON 字段继续兼容读写但不参与运行时。
- 不修改 COM ABI、PowerPoint/WPS 支持范围、画布换页顺序、页级墨迹存储或旧图片资源。

## Acceptance Criteria

- [x] 四个分页窗口均由 Bar UI Scene 呈现，不再存在 `PptExitShow` 窗口或客户端生产路径引用。
- [x] 紧凑布局在 DPI/缩放下尺寸正确，箭头无文字，页码当前页加粗且点击无业务动作。
- [x] 拖动条是唯一拖动入口，翻页长按、滚轮、键盘闪动和位置记忆行为保持。
- [x] PPT/白板双向形变连续，过渡期间输入锁定，反向重入从当前动画值重新定向。
- [x] A2 配置迁移、三种运行时可见性矩阵及 EndShow 单次业务投递有无窗口测试覆盖。
- [x] 碰撞计算覆盖底部组、侧边组和主栏障碍，临时回退不改变保存位置。
- [x] `git diff --check` 通过。
- [x] ARM64 `MSBuild.exe` 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64` 通过。
- [x] ARM64 `InkeysHeadlessTests.exe --no-window` 通过。

## Notes

- 真实 PowerPoint/WPS、触摸拖动、光影、DPI 和连续白板切换属于设备手工验收项，本任务不启动 GUI。
