# 全页迁移的实现边界

基线为 `0ffd0341`，最新范围以 `approved-full-migration.md` 为准。先等待共享视觉层定稿与阶段一构建，再接入全部剩余设置页。

- 最小缺口：首批的 Shell/Home/General 已采用测量布局，其余页面仍使用固定 34% 控件列、原文字框和临时卡片堆叠。
- 修改 `Setting.cpp` 的页面 switch：标准设置使用 `Design::ToggleRow/ComboRow/SliderRow/SliderIntRow/ButtonRow`；插件入口使用 `NavigationRow`；提示使用自动测量的 `Notice`；版本、构建、支持和调试的特殊内容使用统一 `Card/Text`。
- PPT 三组缩放保留专用复合 action，内部布局使用同一控件字体、32 DIP 控件高度及 8 DIP 间距；尺寸测量不执行操作，实际 action 每帧仅提交一次。
- 移除 `Setting.Widgets` 旧页头/34%设置卡/Combo 兼容包装。Color/Dip/原标题栏相关入口保持，因为窗口 chrome 不在本轮范围。
- 最终为所有实际页面绑定校准字体，取消 Home/General 的特殊字体条件；共享D3D、epoch、resident session与标题栏字体仍保持原有责任边界。
- 业务继续保留在 coroutine 内 FIFO 宏作用域，不搬到其他 TU。对照 `full-page-binding-audit.md` 保留全部字段、判断、命令参数、保存时机和运行时同步；不新增配置字段或功能。
- 需保留的条件：预设自动宽度隐藏两个手动滑块、边缘光影控制动态子项、脏区调试控制帧率子项。其他原可用控件不擅自禁用。
- 拟修改文件仅 `Setting.cpp`、`Setting.Widgets.cpp`、`Setting.Widgets.cppm`；确需新文案时走原 i18n 源和生成链。公共视觉层由阶段一代理负责。
- 主会话统一执行 ARM64 完整 Solution、无窗口测试和实际窗口脚本截图，实施代理不重复构建、不自行启动窗口、不 commit。

后续审计以稳定 row ID 与字段/命令为锚点；变更行号不影响基线追踪。页面覆盖包括18个实际视图，`tabPerformance` 没有实际页面，保持现状。
