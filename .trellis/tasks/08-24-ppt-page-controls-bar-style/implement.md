# PPT 翻页控件主栏化：实施计划

## 1. Bar Scene 基础能力

- [x] 增加 Button/DragHandle 类型、外部按压状态和稳定 ID 几何动画。
- [x] 调整命中与内容动画，补充无窗口单元测试。

## 2. PageControl 统一模块

- [x] 新增模块接口、PPT/Whiteboard 状态快照、四客户端生命周期和共享主题。
- [x] 实现底部/侧边紧凑布局、页码混合字重、拖动条、长按和滚轮输入。
- [x] 实现 PPT/Whiteboard 双向布局状态机及首次渐显路径。

## 3. 业务接入与旧路径移除

- [x] 将 PPT 四窗口渲染与输入切换至 PageControl，保留命令和配置语义。
- [x] 将 Whiteboard 分页发布至 PageControl，删除旧独立 Scene 客户端。
- [x] 删除 `PptExitShow` WindowRole、RenderPipeline client、窗口创建和绘制路径。

## 4. 主栏与配置

- [x] 新增 A2 EndShow 固定按钮、配置迁移和三态可见性布局。
- [x] 接入原结束放映命令与确认流程。
- [x] 删除设置页中独立结束窗口入口，保留 JSON 兼容字段。
- [x] 将主栏可见矩形纳入分页控件运行时碰撞求解。

## 5. 验证

- [x] 更新 PPT、Whiteboard、RenderPipeline、Window 和 Bar headless 测试。
- [x] 执行生产路径静态引用检查和 `git diff --check`。
- [x] 使用 ARM64 MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`。
- [x] 运行 ARM64 `InkeysHeadlessTests.exe --no-window`。
