# 发光激光笔技术设计

## Architecture

Laser 复用现有 `RuntimeStroke` 的模型输入、真实点和 prediction，但将两张全尺寸 RGBA8 资源分别作为已完成轨迹的预乘颜色层和可复用的单笔 coverage scratch。每支未烘干 Laser 独立以 MAX blend 生成无接缝 coverage，再按 Down 顺序解析材质并 source-over 到目标；同一批次最后一支抬起后，所有未烘干轨迹按相同顺序写入稳定颜色层。完成普通 `L2 + L1 + L0` 合成后，先叠加稳定颜色，再依次叠加活动批次、粒子和发光笔尖，最后绘制普通 cursor 并 Present。

coverage 四通道分别保存白芯、白色散射脊、红色实体外套和红色外部漫反射。独立 `LaserStyleConstants` 同时供 VS 按端点实体半径与固定漫反射宽度扩大 quad、PS 生成 coverage/resolve 材质，避免复用现有常量 padding。96 DPI 基准材质的实体总直径为 5px，其中白芯约 1.67px，红色实体轮廓再向外固定扩散 5px；压力只缩放实体半径，漫反射宽度只随 DPI 缩放。PS 复用实体 signed distance，以仅含乘加的平方曲线把漫反射从实体边界 alpha 1 更快地衰减到外缘 alpha 0；红粉高光只混合 RGB，不再作为第二层抬高 alpha。常量结构保持 112 bytes 并绑定 `b1`，resolve 输出 premultiplied Add/Retain，并沿用双源混合叠加到 backbuffer 或稳定颜色层。

## State And Data Flow

- `DrawingTool::Laser` 只在 Down 时锁定到 runtime；该工具跳过 reconnect 和 L1/L2 提交。
- 每个 Laser runtime 只保存所属 Down 顺序层、时间发射小数余量、上一有效 L0 切线和 seed 游标；粒子位置、固定初速度、年龄/寿命、累计/最大行程、半径、Alpha、亮度与呼吸状态只存在 GPU 缓冲。Pen 输入保留上一有效压力半径。较早结束的 contact 将最终 CPU 几何保留在所属层，直到同批次最后一个 contact 抬起。
- 全局 Laser session 保存稳定颜色 bounds、未烘干层及其旧/新 bounds、活动 contact 数、最后全部 Up QPC 和阶段 `Inactive/Active/Hold/Fade`。
- Active 阶段 opacity 为 1；最后 Up 时按 Down 顺序把未烘干层写入稳定颜色并进入 Hold；`holdSeconds` 从原子设置读取并相对最后 Up 实时计算。当前批次实际安排过 GPU 粒子时只在内部令有效 Hold 取 `max(holdSeconds, maximumLifetimeSeconds)`，默认即 `max(holdSeconds, 1.0s)`，不改写公开设置；Fade 固定 0.8s；完成后清空稳定颜色、scratch 和 bounds。
- 新 Laser Down 在 Hold/Fade 中保留稳定颜色、恢复 opacity=1 并回到 Active；新批次始终绘制在旧稳定颜色上方，完全清空后不会恢复已消失内容。
- scratch 使用矩形覆盖写零局部清理，避免每支 contact 全画布清空；resize 只复制稳定颜色交集并从 CPU 几何重建未烘干层；clear 同时清空 Laser session；Present 恢复覆盖新旧 bounds。

## Particles And Tips

粒子由独立 `draw3.laser_particles` 模块管理。Renderer 初始化时只固定创建 2048 槽粒子 UAV/SRV、Update/Emit 两个常量缓冲和两个 Compute Shader；`LaserGpuParticle` 保持 128 bytes，Update/Emit 常量分别为 32/112 bytes 并保持 16-byte 对齐。任何帧都不创建资源，不使用路径点/路径头缓冲、Append/Consume、间接绘制或 GPU 回读。CPU 通过循环写入游标覆盖最旧槽，Resize 不重建粒子资源。

控制器从本帧 `l0DrawPoints.back()` 取得出生位置，并从 L0 尾部反向查找最后一个非退化线段作为切线；重复点沿用 runtime 中上一有效切线。真实输入尚未产生首次移动且没有有效方向时不发射，也不累计随后会形成 Down 爆发的余量。prediction 可以决定当前出生位置和最后 L0 切线，但其位移不进入密度计算。

发射使用上限 `1/30s` 的时间积分：`rate=min(72, 6 + filteredRealInputSpeedDipPerSecond/2.5)` 粒/秒，全局每帧最多 96 粒，预算截断和卡顿造成的整数超额不积压。移动后的静止帧回到每秒 6 粒基线。`EmitCS` 对每粒以 50/50 概率选择正/负法线，再在该法线附近均匀偏转 `±25°`；出生点允许在白芯内沿所选法线随机偏移。

`EmitCS` 先以 `sizeRatio=pow(random, 1.6)` 生成连续尺寸层级，并把 `0.45–2.2 DIP` 半径映射到该层级。速度样本使用 `lerp(randomSpeed, 1-sizeRatio, 0.30)` 映射到 `28–64 DIP/s`，因此射程随机但与尺寸弱反向相关；寿命继续独立均匀采样 `0.7–1.0s`，完整减速曲线下标称行程为 `10–32 DIP`。粒子不继承笔尖前向速度。

`UpdateCS` 只读取粒子自身状态和 `b0`：先用 wall time 累计年龄，再以不超过 `1/30s` 的 motion dt 把固定速度乘 `1-smoothstep(0,1,age/lifetime)` 推进屏幕位置；同一曲线直接作为 Alpha。实际飞行距离逐步累计，前 10% 最大行程保持出生半径，之后平滑缩至 20%。Up/Cancel 只停止新请求；prediction 回缩、急弯和最终路径变化都不能重定位存量粒子，也没有端点阻塞淡出、路径追赶或 prediction correction。

基础亮度使用 `lerp(randomBrightness, sizeRatio, 0.72)` 映射到 0.42–1.0，使大粒子通常更亮、小粒子通常更暗，同时保留随机重叠。0.8–1.4Hz 的独立随机相位呼吸以 `0.18` 振幅在出生后 0.2s 渐入，并只乘 RGB。现有 VS 的 shape `10` 通过 `SV_InstanceID` 从 VS `t8` 读取粒子并固定 `DrawInstanced(6, 2048)`；VS 以 `currentRadius * glowRadiusScale + 2.0 * dpiScale` 的混合公式计算逐粒辉光半径（比例分量保持视觉与核心成比例，地板项确保极小粒子在任意 DPI 下均有可见辉光），并把出生基础亮度传给 PS。PS 根据该基础亮度在 `(1.0, 0.32, 0.40)` 深红粉与现有淡粉白散射色之间确定稳定核心色相，亮大粒子接近淡粉白、暗小粒子更泛红；当前呼吸亮度只调制 RGB，Alpha 仍只使用生命周期曲线。辉光峰值 Alpha 为 `0.50`，使用 `pow(..., 1.2)` 柔化距离衰减（边缘更柔和，取代旧 `pow(2.0)` 二次衰减），并保留 premultiplied Add/Retain。绘制顺序仍为稳定 Laser、活动逐笔层、粒子、Laser tip。

脏区不再附着整条路径。控制器把同一帧实际发射请求的未裁剪保守包络合成一个批次，按最大减速弹道行程、白芯出生偏移、`maximumRadius * (1 + glowRadiusScale)` 和 AA 扩展，并以最大寿命 1 秒为独立到期时间。Tracker 保存原始包络，使用时才按当前画布裁剪，因此 resize 后仍能覆盖尚存粒子；固定槽极端耗尽时只允许保守合并，不得漏清像素。

Pen/Mouse Hover 和所有活动 contact 的笔尖使用专用 LaserDot shader shape。`LaserDot.radius` 表示实际参与材质计算的实体外半径，Hover、Touch 和固定宽度真实轨迹复用同一套尺寸契约。Hover 静止不动画；活动 tip 由最新一致输入位置驱动。粒子关闭时停止推进/生成并把上一帧粒子 bounds 标脏。

每个有效 RTS Pen 样本明确取得 Pen authority；Pen leave/out-of-range 清除样本并清旧 tip dirty 区但保留 authority，只有新的非 promoted Mouse 消息或新 Pen 样本才能恢复对应光标。

## Public Contracts

- `DrawingTool::Laser` 与键 4。
- `StrokeModelConfiguration::laserParticlesEnabled = true`、`laserParticleConfig`、`laserHoldDurationSeconds = 3.0`。
- `LaserParticleConfig` 导出尺寸分布指数、尺寸/亮度相关度、尺寸/射程相关度、核心白色混合基线和核心半径辉光比例；不再导出固定 DIP 辉光范围。
- `DrawingController::Set/GetLaserParticlesEnabled`。
- `DrawingController::SetLaserHoldDurationSeconds` 返回 `bool`，只接受有限非负秒数；`GetLaserHoldDurationSeconds` 返回当前值。设置变化发布 control wake。

## Risks And Rollback

- 两张 RGBA8 资源增加与画布面积线性相关的 GPU 内存；稳定颜色只在批次结束时写入，单笔 scratch 局部清理，并用 Release 指标验证 ARM64 多 contact 成本。
- GPU 粒子不允许回读；CPU 仅按每帧发射批次保存未裁剪保守包络，覆盖最大弹道、白芯出生偏移、最大核心及其比例辉光和 AA，并在该批次发射后 1 秒到期。该包络可能过绘，但不能依赖当前画布裁剪后的矩形作为长期状态。
- CPU/HLSL 结构、shape type、寄存器和常量缓冲区必须同步；构建 VS、PS、UpdateCS、EmitCS 四个 shader 和完整解决方案作为合并门槛。
- 如果 Compute Shader 或任一固定资源创建失败，粒子模块只记录一次并保持不可用；公开开关仍可即时关闭，主轨迹和透明呈现回退不受影响。
