# 发光激光笔技术设计

## Architecture

Laser 复用现有 `RuntimeStroke` 的模型输入、真实点和 prediction，但将两张全尺寸 RGBA8 资源分别作为已完成轨迹的预乘颜色层和可复用的单笔 coverage scratch。每支未烘干 Laser 独立以 MAX blend 生成无接缝 coverage，再按 Down 顺序解析材质并 source-over 到目标；同一批次最后一支抬起后，所有未烘干轨迹按相同顺序写入稳定颜色层。完成普通 `L2 + L1 + L0` 合成后，先叠加稳定颜色，再依次叠加活动批次、粒子和发光笔尖，最后绘制普通 cursor 并 Present。

coverage 四通道分别保存白芯、白色散射脊、红色实体外套和红色外部漫反射。独立 `LaserStyleConstants` 同时供 VS 按端点实体半径与固定漫反射宽度扩大 quad、PS 生成 coverage/resolve 材质，避免复用现有常量 padding。96 DPI 基准材质的实体总直径为 5px，其中白芯约 1.67px，红色实体轮廓再向外固定扩散 5px；压力只缩放实体半径，漫反射宽度只随 DPI 缩放。PS 复用实体 signed distance，以仅含乘加的平方曲线把漫反射从实体边界 alpha 1 更快地衰减到外缘 alpha 0；红粉高光只混合 RGB，不再作为第二层抬高 alpha。常量结构保持 112 bytes 并绑定 `b1`，resolve 输出 premultiplied Add/Retain，并沿用双源混合叠加到 backbuffer 或稳定颜色层。

## State And Data Flow

- `DrawingTool::Laser` 只在 Down 时锁定到 runtime；该工具跳过 reconnect 和 L1/L2 提交。
- 每个 Laser runtime 只保存所属 Down 顺序层、GPU 路径 handle、真实点上传游标、真实/已发布组合弧长、时间发射小数余量、平滑出生锚点和 seed 游标；每粒子的 segment cursor、位置/切线、亮度与呼吸相位只存在 GPU 缓冲。Pen 输入保留上一有效压力半径。较早结束的 contact 将最终 CPU 几何保留在所属层，直到同批次最后一个 contact 抬起。
- 全局 Laser session 保存稳定颜色 bounds、未烘干层及其旧/新 bounds、活动 contact 数、最后全部 Up QPC 和阶段 `Inactive/Active/Hold/Fade`。
- Active 阶段 opacity 为 1；最后 Up 时按 Down 顺序把未烘干层写入稳定颜色并进入 Hold；`holdSeconds` 从原子设置读取并相对最后 Up 实时计算。当前批次实际安排过 GPU 粒子时只在内部令有效 Hold 取 `max(holdSeconds, 3.0s)`，不改写公开设置；Fade 固定 0.8s；完成后清空稳定颜色、scratch 和 bounds。
- 新 Laser Down 在 Hold/Fade 中保留稳定颜色、恢复 opacity=1 并回到 Active；新批次始终绘制在旧稳定颜色上方，完全清空后不会恢复已消失内容。
- scratch 使用矩形覆盖写零局部清理，避免每支 contact 全画布清空；resize 只复制稳定颜色交集并从 CPU 几何重建未烘干层；clear 同时清空 Laser session；Present 恢复覆盖新旧 bounds。

## Particles And Tips

粒子由独立 `draw3.laser_particles` 模块管理。Renderer 初始化时固定创建 2048 槽粒子 UAV/SRV、32 个路径槽和每槽 16384 点的组合路径 SRV；任何帧都不创建资源、不使用 Append/Consume/间接绘制，也不做 GPU 回读。每条路径由不可变真实前缀和每帧替换的 prediction 尾组成，新真实点先覆盖旧预测尾，再从最新真实末端重写本帧预测点；真实点始终优先占用容量。路径 handle 使用 slot/generation 防止旧粒子引用复用槽，路径容量不足时只停发对应 contact。

`EmitCS` 在当前组合路径末端与 CPU 平滑出生锚点创建粒子，并写入 CPU 管理的循环粒子槽；`UpdateCS` 保存并推进每粒子的组合弧长、segment cursor、平滑位置/切线、横向偏移、速度、3 秒寿命和呼吸状态。路径采样优先使用 cursor，跨越过多段时用固定次数二分查找；预测尾缩短时弧长立即钳制，但持久位置/切线以正常 40ms、异常修正 120ms 的响应靠近新目标，丢弃的位移不追赶。出生锚点正常跟随 L0 前端，超过 `max(6*dpiScale, 2*filteredInputSpeed*dt + 2*dpiScale)` 的跳变才用 120ms 响应。

发射使用上限 `1/30s` 的时间积分：`rate=min(48, 6 + max(组合弧长增量, 0)/dt/4)` 粒/秒，全局每帧最多 96 粒，超额整数不积压。Down 只建立累计基线；静止也以每秒 6 粒生成，预测回缩只保留该基线。粒子从白芯内出生，120ms 内向随机左右扩散并最多越出实体红边 4.5 DIP。

活动期间基础流速为 `clamp(12*dpiScale + 0.08*filteredInputSpeed, 12*dpiScale, 96*dpiScale)`，以 80ms 时间常数追随并带固定 `±12%` 粒子差异。固定寿命 3.0s，实际推进速度与 Alpha 同乘 `1-smoothstep(0,1,age/3)`；不再用最大行程终止。Up 只停止发射和路径追加、冻结最后输入速度，存量粒子继续沿最终路径减速淡出，路径槽在 Up 后保留 3 秒再回收。

每粒基础亮度随机为 0.68–1.0，0.8–1.4Hz 的独立随机相位呼吸以 0.12 振幅在出生后 0.2s 渐入，并只乘 RGB。现有 VS 的 shape `10` 通过 `SV_InstanceID` 从 VS `t8` 读取粒子并固定 `DrawInstanced(6, 2048)`；PS 输出纯白亮核与 `(1.0, 0.55, 0.62)`、峰值 Alpha 0.24 的 3 DIP 浅粉红辉光，继续使用 premultiplied Add/Retain。绘制顺序仍为稳定 Laser、活动逐笔层、粒子、Laser tip，因此粒子穿过红色实体时保持可见。

Pen/Mouse Hover 和所有活动 contact 的笔尖使用专用 LaserDot shader shape。`LaserDot.radius` 表示实际参与材质计算的实体外半径，Hover、Touch 和固定宽度真实轨迹复用同一套尺寸契约。Hover 静止不动画；活动 tip 由最新一致输入位置驱动。粒子关闭时停止推进/生成并把上一帧粒子 bounds 标脏。

每个有效 RTS Pen 样本明确取得 Pen authority；Pen leave/out-of-range 清除样本并清旧 tip dirty 区但保留 authority，只有新的非 promoted Mouse 消息或新 Pen 样本才能恢复对应光标。

## Public Contracts

- `DrawingTool::Laser` 与键 4。
- `StrokeModelConfiguration::laserParticlesEnabled = true`、`laserParticleConfig`、`laserHoldDurationSeconds = 3.0`。
- `DrawingController::Set/GetLaserParticlesEnabled`。
- `DrawingController::SetLaserHoldDurationSeconds` 返回 `bool`，只接受有限非负秒数；`GetLaserHoldDurationSeconds` 返回当前值。设置变化发布 control wake。

## Risks And Rollback

- 两张 RGBA8 资源增加与画布面积线性相关的 GPU 内存；稳定颜色只在批次结束时写入，单笔 scratch 局部清理，并用 Release 指标验证 ARM64 多 contact 成本。
- GPU 粒子不允许回读；CPU 保守脏区按路径合并，覆盖旧/新 prediction 前端及修正区，并按减速曲线积分的最大推进、实体半径、4.5 DIP 横向扩散、粒子半径、3 DIP 辉光和 AA 扩展，保留到最后一次发射后 3 秒。
- CPU/HLSL 结构、shape type、寄存器和常量缓冲区必须同步；构建 VS、PS、UpdateCS、EmitCS 四个 shader 和完整解决方案作为合并门槛。
- 如果 Compute Shader 或任一固定资源创建失败，粒子模块只记录一次并保持不可用；公开开关仍可即时关闭，主轨迹和透明呈现回退不受影响。
