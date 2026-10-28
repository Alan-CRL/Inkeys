# 当前改动提交检查点（2026-10-28）

用户明确要求记录状态并提交当前改动，不结束任务。此检查点包含本任务代码、测试、两份同步规范及调查/验证文档，不包含忽略的TestResults原始大文件。实际提交哈希以git历史为准，避免在提交内容内自引用。未授权push。

## 已验证状态

- 当前分支bugfix/mode；修复共享Scheduler粗粒度等待、主栏过大的隐藏面板容量预约与稳定段追扩，保留公共按钮/统一renderer和UI3成功事务。
- 产品修复完整Debug/Release ARM64 Solution构建、Headless及相关离屏回归通过。新增独立SVG计时另经Debug构建、真实probe合同和短pilot验证；版本与覆盖边界见final-verification.md、svg-numeric-timing.md及review.md。
- 同Debug成功提交间隔P95的组中位数37.739→20.451ms。最终Release热态24/54组达预算，R4未完成；任务维持in_progress，completedAt为空，不运行finish-work或archive。
- 剩余GetDC内部同步/WARP成本尚未分清。WPR CPU启动被Windows profiling policy拒绝0xc5585011；确认没有遗留会话，不修改系统权限/策略。

## SVG图标归因澄清

此前静态路径指向主栏Whiteboard按钮：非白板使用barWhiteboard.svg，白板激活且未最小化时切为barDismiss.svg（关闭白板/返回放映）；Selected内容色渐变经公共CacheBitmap触发解析、raster和上传。具体入口为Bar.Button.cpp的UpdateWhiteboardButtonStyle及对应公共SVG颜色推进路径。

这确认重渲染可达，不能将约17ms单次慢帧归因于某一图标：当前stage sidecar只有四段累计，没有resource/button identity。本次Debug100%/200%短pilot成功回调中四段累计约4.59%/3.03%，仍保留约17.68ms SVG离群；不能说SVG始终便宜，也不能说已证明白板图标是全部明显卡顿的原因。

## 后续工作

继续定位GetDC中真正执行/等待的来源，再选择有证据的小修；进一步收紧backing/source布局须独立具体设计及像素/负原点/DPI/反向/失败/epoch/事务门禁。不猜测修改SVG算法、不降光效或切默认后端。原生Freeze/协调链和真实显示手感仍未覆盖。
