# 激光笔增量覆盖技术设计

## Architecture

保留 `laserCompositedColor` 作为已烘干批次的预乘颜色层。单 contact 活动批次使用两张 RGBA8 coverage：已有 `laserStrokeCoverage` 保存稳定 L1，新建 `laserLiveCoverage` 保存每帧 L0。Pixel Shader shape 13 同时采样 t7/t9，逐通道 MAX 后调用现有 `ResolveLaserMaterial`，从数学上等价于把同一笔全部胶囊段写入一张 MAX coverage。

多 contact 不能共享一张持久 coverage 而保持每笔 coverage 并集和 Down 顺序，因此第二个 layer 出现时锁定 `FullRedraw` 到本批结束。回退路径沿用当前“逐笔 clear scratch -> 全量 coverage -> 按序 source-over”的实现。

## Resource Lifecycle

- `laserLiveCoverage` 默认不存在。绘制线程在空闲/控制唤醒后发现 `WindowController::ActiveTool() == Laser` 时调用 `EnsureLaserIncrementalCoverageResources()`。
- 创建成功后清零并用零像素 draw 预热 shape 13；切换工具不释放。失败时释放部分资源、记录一次并把本会话标为 unavailable。
- Resize 记录资源是否已启用，释放尺寸资源后按新尺寸重建；活动增量状态由 controller 标记为 rebuild，并从 CPU 点重画，不能沿用旧索引对应的空纹理。
- 所有释放/Resize 路径解绑 PS t0-t9；`ClearAllLaserCoverage()` 在资源存在时同时清理稳定、live 和已烘干颜色层。

## State And Data Flow

每个 `LaserStrokeLayer` 保存稳定提交索引、稳定/live bounds 和批次模式。稳定提交边界复用 `CommitStablePrefixToL1` 的 `liveTipDuration + predictionDuration` 时间保护逻辑：只把保护窗口之前的真实点追加到稳定 coverage，并保留一个边界点供下一段连接。live coverage 每帧先清旧 bounds，再绘制边界真实点、未稳定真实尾部和 prediction。

每帧先更新 coverage 并汇总完整 dirty，再依序执行：普通画布合成、粒子、已烘干 Laser、shape 13 合并 coverage、Laser tip、cursor、Present。shape 13 对最终 `frameDirty ∩ layer.bounds` 解析，因此粒子、cursor 或其他图层触发的底图重建不会擦除稳定 Laser。

最后 Up 时，Incremental 模式用 shape 13 把当前 coverage 一次写入 `laserCompositedColor`；FullRedraw 模式使用现有逐层 Bake。完成后清理两张活动 coverage 与增量状态。

## Diagnostics

临时 `LaserIncrementalDiagnostics` 仅驻留 controller：累计活动帧、最大可见点数、stable/live 实际提交点数、完整重绘等价点数、dirty 像素累计/峰值、coverage CPU 提交耗时和回退原因。资源创建输出一次；超过既有 8ms 渲染阈值时输出 SlowFrame；最后 Up/Bake 输出一条 Summary。D3D 调用是异步的，日志中的 coverage 时间明确表示 CPU command submission，不宣称 GPU duration。

## Compatibility And Rollback

- 不修改普通 operator layer、粒子缓冲、公开 Laser 设置或 presenter。
- 任一资源/状态不变量失败都在批次边界回退完整重绘，不允许部分增量状态继续运行。
- Resize、Clear、生命周期结束和资源创建失败是强制 reset/rebuild 点。
- 回滚本任务时删除 t9/shape 13 与新增资源，并恢复当前完整重绘；`laserCompositedColor` 和粒子模块保持可独立工作。
