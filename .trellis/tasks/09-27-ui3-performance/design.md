# UI3 性能设计

- 以生产 RenderPipeline/Bar 诊断分段，确定 callback 与成功呈现的区别；同帧时钟、raw/animation dt、Rebase、idle、Retry 都纳入分布。
- 先确认 scheduler 其他客户端、锁、GPU 同步、光影、SVG/path、cache、日志是否在关键长帧上显著，不预设光影单因。
- 优先移除无状态变化的 target/layout、无活动动画遍历、无视觉变化的绘制/Present 和无资源变化的重建；仅瓶颈证据支持时引入 dirty revision/active set。
- 光影及 SVG/path 的不变层共享与每帧层分开；cache key/容量/device epoch/失败写入约束明确，dirty 包含 glow、旧影与 AA。
- 确定性时间与视觉轨迹/真机验证分开；收益不超过噪声的复杂优化撤销。
