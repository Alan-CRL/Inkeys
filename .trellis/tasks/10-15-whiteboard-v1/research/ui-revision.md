# 白板截图反馈 UI 修订

- 标签根因：`StandardTwoTwo` 的共享 metrics 将标签定义在 primary 槽；PageControl 错把 previous/next/More 写入 secondary，随后共享 metrics 把 secondary 字号及高度设为零。已迁回 primary，增加生产 Scene 离屏标签区域亮像素断言，同时接入三语言键。
- 左下主体保持 305×80；菜单、右侧主题面板分别绘制和命中，位于同一 HWND。Scene 新增可复用 panel/springLayout/primaryLeftAligned 属性，沿用 BarUi Back 曲线及 Pct 动画。主题九个预览均 70×70，圆角裁切、中心勾，无 selected 外圈；自绘内容复用标准按压缩放与悬停覆盖。
- 两个 SVG 图标已加入 rc；关闭复用 barCloseSmall。最小化按钮在 PPT presentationVisible 时禁用；控件显隐读取窗口组 minimized 状态，工作区不退出。
- 面板包络预留 Back 过冲的透明空间，背景命中仅对应真实矩形。关闭中的面板根据 reveal 保持命中，避免可见面板内落笔。
- 修改保留既有 UTF-8/BOM 和 CRLF。git diff --check 通过；构建及运行离屏测试由主会话统一完成。
- 离屏测试可输出 `TestResults/whiteboard-menu-revision.bmp`（无需创建 HWND）。真实 PPT 转场、系统 DPI、任务栏恢复、视觉动画仍待人工验收。
