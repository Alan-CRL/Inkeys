# 发光激光笔技术设计

## Architecture

Laser 复用现有 `RuntimeStroke` 的模型输入、真实点和 prediction，但使用独立的 `laserStableCoverage` 与 `laserLiveCoverage` 全尺寸 RGBA8 资源。稳定确认前缀以 MAX blend 增量写入 stable；每帧清空 live，只重建活动真实尾部和 prediction。完成普通 `L2 + L1 + L0` 合成后，laser resolve 合并两层 coverage、应用整组 opacity，再绘制粒子和发光笔尖，最后绘制普通 cursor 并 Present。

coverage 四通道分别保存白芯、白色散射脊、红色边框和红色外晕。独立 `LaserStyleConstants` 同时供 VS 按端点半径扩大可变光晕 quad、PS 生成 coverage/resolve 材质，避免复用现有常量 padding。基准材质为 5px 白芯、15px 红边、28px 光晕和 1px 散射带，按柔光、红边、红粉外缘高亮、内侧散射、白芯顺序合成；常量增加边缘高亮颜色后为 112 bytes，仍绑定 `b1`。resolve 输出 premultiplied Add/Retain，并沿用双源混合叠加到 backbuffer。

## State And Data Flow

- `DrawingTool::Laser` 只在 Down 时锁定到 runtime；该工具跳过 reconnect 和 L1/L2 提交。
- 每个 Laser runtime 保存 stable commit 游标、live/previous live bounds、粒子槽位、累计真实弧长、segment cursor 和粒子流动相位；Pen 输入保留上一有效压力半径。
- 全局 Laser session 保存 coverage bounds、活动 contact 数、最后全部 Up QPC 和阶段 `Inactive/Active/Hold/Fade`。
- Active 阶段 opacity 为 1；最后 Up 进入 Hold；`holdSeconds` 从原子设置读取并相对最后 Up 实时计算；Fade 固定 0.8s；完成后清空两层 coverage 和 bounds。
- 新 Laser Down 在 Hold/Fade 中保留 coverage、恢复 opacity=1 并回到 Active；完全清空后不会恢复已消失内容。
- resize 复制 stable 交集并重建 live；clear 同时清空 Laser session；Present 恢复在 base dirty 区重新 resolve Laser。

## Particles And Tips

粒子只存在于 CPU runtime，并批量上传为小圆 SDF primitive。固定 seed、弧长槽位和相位保证追加点与整帧重绘不改变旧粒子身份。Down 产生 12-18 个初始槽位；移动按 8-12px 真实弧长间隔追加并限制为 48 个。粒子保存所属 segment/弧长，逐帧摊销推进 segment cursor 并按当前曲线切线/法线更新，末端停止不外推。流速为 `clamp(0.025 * smoothedSpeed, 8*dpiScale, 36*dpiScale)`；Up 后只做一次最近路径点查找，75% 收束到红边、25% 到中心线，在 220ms 内缩小淡出。

Pen/Mouse Hover 和所有活动 contact 的笔尖使用专用 LaserDot shader shape。Hover 静止不动画；活动 tip 由最新一致输入位置驱动。粒子关闭时停止推进/生成并把上一帧粒子 bounds 标脏。

每个有效 RTS Pen 样本明确取得 Pen authority；Pen leave/out-of-range 清除样本并清旧 tip dirty 区但保留 authority，只有新的非 promoted Mouse 消息或新 Pen 样本才能恢复对应光标。

## Public Contracts

- `DrawingTool::Laser` 与键 4。
- `StrokeModelConfiguration::laserParticlesEnabled = true`、`laserHoldDurationSeconds = 3.0`。
- `DrawingController::Set/GetLaserParticlesEnabled`。
- `DrawingController::SetLaserHoldDurationSeconds` 返回 `bool`，只接受有限非负秒数；`GetLaserHoldDurationSeconds` 返回当前值。设置变化发布 control wake。

## Risks And Rollback

- 两张 RGBA8 coverage 增加与画布面积线性相关的 GPU 内存；只增量更新 stable、局部重绘 live，并用 Release 指标验证 ARM64 成本。
- CPU bounds 必须覆盖 24px 光晕、旧 live、粒子和 tip；任何遗漏会产生残影。
- CPU/HLSL 结构、shape type、寄存器和常量缓冲区必须同步；构建两个 shader 和完整解决方案作为合并门槛。
- 如果动态粒子影响帧预算，可通过公开开关即时关闭，不回退主轨迹实现。
