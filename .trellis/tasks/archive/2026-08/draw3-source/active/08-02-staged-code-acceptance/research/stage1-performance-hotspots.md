# 阶段 1 性能热点研究

## Validation Boundary

- 当前允许的动态验证仅限无窗口控制台测试；主程序和测试程序的 `--benchmark` 会查找并操作窗口，禁止运行。
- 优化前的 `ARM64\\Debug\\inkStrokeModelerTestTests.exe` 与 `ARM64\\Release\\inkStrokeModelerTestTests.exe` 已通过，覆盖 Highlighter geometry、Laser incremental coverage、Pen cursor 和 contact input。
- 测试工程已有全局 `operator new` 计数器 `gAllocationCount`，可沿用 `TestPublishDownDoesNotAllocate` 的模式建立确定性分配断言。

## CPU And Allocation Findings

- renderer、dirty rect 和四类绘制提交接口大量使用 `const std::vector<InkPoint>&`，调用者为子区间构造临时 vector；dot fallback 也会为两个重复点分配容器。
- `CommitStablePrefixToL1`、Eraser 实点提交、稳定前缀绘制和 Laser coverage 提交都存在可避免的范围复制。
- Highlighter geometry 先构造去重点 vector，再构造 primitive vector；实时重建使用赋空值方式丢弃容量，完成路径还会合并 committed/live primitive。
- Laser coverage 的稳定/实时子区间每帧复制到复用 vector，仍产生与点数成比例的内存写入。

## Multi-Contact Laser Findings

- 第二个 Laser contact 会把当前批次锁定为完整重绘，这是保持不同 contact Down 顺序和 source-over 的正确回退边界。
- 回退模式当前每帧重新计算完整 layer bounds、清除完整 scratch、上传完整几何并在无 scissor 情况下光栅化，`frameDirty` 还会持续包含大范围稳定轨迹。
- 可在保留完整几何绘制和逐 layer resolve 顺序的同时，只把稳定 delta、旧 live、新 live 的并集加入 frame dirty；每层再与最终 frame dirty 相交，按交集清 scratch 并设置 scissor。
- 该方案不增加每 contact 的全尺寸 coverage 纹理，也不改变自交、连接点和不同 contact 的合成数学。

## Renderer And Shader Findings

- Laser 矩形 clear/resolve pass 当前为矩形上传两个点到 ink buffer，随后再上传全局常量；相关 shape 仅需要矩形坐标。
- 可把矩形坐标放入这些 shape 未使用的 `globalColor`，由 VS 直接生成 quad，省去 ink buffer Map/Unmap 和 t0 绑定。
- `UpdateLaserStyleConstants(1.0f)` 在 coverage、dot、矩形和粒子路径重复调用；可按配置 generation 与 opacity 缓存最近上传状态，并在 Configure/设备重建后失效。

## Particle Findings

- 粒子开关开启后，只要任意绘制产生 frame dirty，controller 就可能调用粒子绘制，即使尚未发射或所有粒子已经死亡；绘制固定提交 2048 实例。
- simulation 也可能在活动 Laser 尚未发射时更新全部 2048 槽位。
- 512 项 CPU dirty tracker 的 `ActiveBounds()` 与 `HasActive()` 会在一帧内分别重复扫描；可合并为一次 prune/snapshot 并复用活动标记和 bounds。
- 多 contact 发射当前对每个请求执行完整 UAV/CB/CS bind-dispatch-unbind；可在一次批处理内保持请求顺序、spawn cursor 和 seed，只绑定一次 UAV。
- `LaserGpuParticle` stride 为 128 字节，实际字段加一个必要 padding 可压缩为 80 字节；2048 容量节省 96 KiB，降幅 37.5%。
- 粒子默认缓冲初始化会分配 256 KiB CPU 零 vector；资源创建后可复用 compute reset 清零。
- 当前粒子 PS 不读取 VS 计算的 white-mix 值，相关 hash 与传递字段对最终像素无贡献，可在性能阶段删除运行时计算，同时把更广的 API 清理留到阶段 2。

## Recommended Headless Evidence

- 为纯 CPU 几何、范围规划、脏区规划和粒子请求资格增加确定性等价测试。
- 增加无窗口 `--drawing-perf` 模式或等价 focused 测试入口，报告固定工作负载的分配次数、处理点数/primitive 数和多次运行中位耗时。
- 增加静态 shader 契约测试，覆盖矩形 shape 不读取 t0、粒子 stride/字段偏移、寄存器和资源绑定。
- 墙钟数据仅报告前后趋势，不设易抖动的严格时间阈值；分配、复制和命令资格使用硬断言。
