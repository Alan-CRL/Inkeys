> 第三轮直接续作：当前要求与验收以 [scrollbar-revision.md](scrollbar-revision.md) 为准，修复缓存到终态的滚动条与页面布局跳变；前两轮结果保留。

> 2026-10-14 第二轮已直接批准继续优化和测试。最新要求、实现边界与验收以 [submission-revision.md](submission-revision.md) 为准；首轮验证保留在 validation.md。UI共用WARP、Draw独立hardware；保留CPU帧以维护延迟动画/光标/轮询，精确相同输出跳过实际提交。

# 技术设计

复用现有 HomeMotionState 准备门禁和值键。书写完成后保持遮罩不透明，等待真实布局/动态图集稳定，把不含 foreground overlay 的正文/导航实际 DX11 输出捕获到单张 GPU texture（无需 SRV）；不经 CPU 截屏。后续 Begin 返回冻结页面，当前帧使用缓存背景加实时标题栏和 foreground。入场完成前必要的 child 暖帧在不透明或缓存遮挡下完成，避免终态 AutoResize 裁切。

缓存的 D3D11 资源由设置渲染线程独占，复用同设备、FL11.0 API 与现有 backend，不新增 device/thread/shader。资源事务成功后发布ready；key/epoch改变及Hide/终态/失败立即无效，旧texture不能提交。资源错误复用原 Retry/DeviceLost 仲裁。透明根背景避免重复混合/不当预乘，像素测试使用生产绘制并解释不可避免的UNORM二次量化。

现有无窗口夹具扩展为生产缓存路径对照；WARP为生产软件渲染代表，额外hardware独立device只提供硬件吞吐诊断。以query/完成等待明确测绘制完成，readback正确性和持续渲染吞吐分开，避免把CopyResource/Map误标纯GPU。预热+有界重复采样，固定时刻同页同字标、无Sleep/Present；记录CPU/backend/完成/总时间与几何量。