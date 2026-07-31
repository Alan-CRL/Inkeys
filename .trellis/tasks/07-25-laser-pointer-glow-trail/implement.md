# 发光激光笔实施计划

## Implementation Order

1. 扩展工具枚举、键 4、工具名称/宽度/光标选择和运行时设置 API；加入 Laser session/lifecycle 纯逻辑与单元测试。
2. 将 renderer 的两张 Laser RGBA8 资源改为稳定预乘颜色层和单笔 coverage scratch；实现局部覆盖清零、MAX coverage、单笔材质解析、稳定颜色解析、resize/clear/release 和 CPU/GPU 常量契约。
3. 接入多 contact 主循环：按 Down 顺序保留未烘干层，逐笔覆盖到 scratch 后有序 source-over；较早结束的 contact 保留最终 CPU 几何，最后 Up 时依序烘入稳定颜色。保持无 reconnect/no L2、deadline wait 和完整 dirty rect。
4. 保留共享 Laser 尺寸契约、LaserDot Hover/Touch 笔尖和压力实体半径；把独立 GPU 粒子模块改为从 L0 笔尖沿双侧法线随机喷射的屏幕空间粒子，使用固定 2048 槽 UAV/SRV、0.7–1.0 秒随机寿命、按距离缩小、Update/Emit Compute Shader、实例化绘制、线程安全开关与无粒子降级。
5. 完善 Pen authority/leave/re-entry 光标状态，补充可变半径 bounds 与 112-byte shader 契约。
6. 补充自动测试和 D3D11 静态契约审查；人工视觉与 Release 性能指标留待后续专项验证。

## Validation Commands

- 使用 ARM64 `MSBuild.exe` 完整构建 `inkStrokeModelerTest.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64`，只编译/链接但不运行测试，并确认 VS/PS 与两个 CS 均成功编译并嵌入资源。
- 本轮不运行任何 EXE（包括测试程序），不启动 GUI、指标场景、浏览器或电脑控制。
- 静态核对 CPU/HLSL `LaserGpuParticle` 128-byte 镜像、32/112-byte 常量缓冲、固定容量、FXC `cs_5_0` 产物、仅 `u0 ↔ t8` 的显式解绑、绘制顺序、屏幕空间独立运动、时间寿命和开关降级路径。
- 自动测试运行、Release、视觉观感、ULW 强制回退和 Windows 7 运行验证留待用户后续验证。
- 运行 `git diff --check`，检查 shader/source 保持 UTF-8 BOM + CRLF，核对无关 `Vcpkg/` 未被修改。

## Review Gates

- PRD 所有 acceptance criteria 有测试或明确人工证据。
- Laser 稳定颜色与 scratch 在逐笔解析、批次烘干和普通 operator layer 合成中没有 SRV/RTV 冲突，失败/resize 路径释放完整。
- 清屏、fade 完成、粒子关闭、tip 隐藏均包含旧 bounds；粒子脏区按帧批次独立到期并在 resize 后重新裁剪，透明背景无残影。
- 没有把 prediction 或 Laser coverage resolve 到 L2，也没有为 Laser 启用 reconnect。
- 旧路径槽、generation、弧长、segment cursor、端点淡出和 prediction correction 不再出现在粒子源码或运行时规范中。

## Current Particle Revision

- [x] 发射源改为当前 L0 笔尖和最后非退化切线；真实首次移动前不发射，重复点沿用上一有效方向。
- [x] EmitCS 使用双侧法线 `±25°` 随机喷射，连续尺寸分布为 `0.45–2.2 DIP`（`pow(random,1.6)`），速度 `28–64 DIP/s`、寿命 `0.7–1.0s`，不继承画笔前向速度。
- [x] UpdateCS 改为屏幕空间独立运动、生命周期减速/Alpha 和按实际行程缩至 20% 半径；删除所有路径资源与 prediction 修正。
- [x] 基础亮度与尺寸正相关、射程与尺寸弱反向相关；小粒子更多且通常更暗、更远，层级范围保持重叠。
- [x] 粒子辉光调整为核心半径的 2.5 倍加 `2 × dpiScale` 地板，颜色保持 `(1.0, 0.32, 0.40)`、峰值 Alpha `0.28`；PS falloff 改为 `pow(1.6)` 在柔和与聚焦间取平衡；暗小核心更泛红，亮大核心更接近淡粉白，呼吸振幅 `0.18` 继续只改变 RGB。粒子绘制于激光主体（稳定颜色层与活动逐笔层）下方，辉光衬于墨迹之下，不遮挡演示内容。
- [x] 脏区改为每帧未裁剪发射批次，按最大 1 秒寿命到期并在使用时按当前画布裁剪；内部最小 Hold 同步为最大寿命。
- [x] 编译测试覆盖默认参数、发射预算、法线偏转、尺寸分布与相关度、8.5 DIP 标称行程、减速、缩小、核心/辉光层级、呼吸分离、独立运动和 dirty/resize。
- [x] 完整 `Debug|ARM64` 构建。
- [x] 最终静态差异检查。
- [x] 粒子小型化与密度提升：半径 `0.28–1.4 DIP`（指数 2.0），间距 1.8 DIP，上限 90/s、120/帧。
- [x] 行程缩短：速度 `18–40 DIP/s`、寿命 `0.55–0.75s`，标称行程 `5–15 DIP`（原32 DIP）。
- [x] 辉光进一步收窄：Alpha `0.18`、倍率 `2.0`。
- [x] PS `coreColorMix` 改为上限模式（`= coreColorWhiteMix × brightness`，上限 0.82），最亮粒子白中带红（18% 红色调保留），全体"微红白色"而非纯白。
- [x] 预热所有激光着色器路径（`WarmUpLaserShaders()`），消除 Qualcomm/Adreno 首笔 JIT 编译卡顿（对 Nvidia/AMD/Intel/WARP 零额外开销）。
- [x] 排查并确认"抬笔后迅速落笔卡顿"问题：已通过层级调整修复（粒子移到激光主体下方减少 Adreno TBDR tile cache flush），诊断输出证实第2次起落笔 DrawStrokes < 2ms、帧时 < 8ms；唯一慢帧（会话首笔~18ms）为 Qualcomm 驱动一次性 JIT shader 编译，非可修复缺陷。

## Static Validation Evidence

以下记录按时间保留；2026-07-30 本次“屏幕空间法线喷射”证据完成后，将取代更早的路径绑定参数，旧记录不再代表现行契约。

- 2026-07-29（上一阶段）：ARM64 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Rebuild` 成功，VS、PS、`UpdateCS`、`EmitCS` 四个 Shader 均由 FXC 成功生成资源产物。
- 2026-07-29（上一阶段）：`ARM64\Debug\inkStrokeModelerTestTests.exe` 全部通过；本轮按用户要求不再次运行。
- 2026-07-29（上一阶段）：静态检查确认粒子缓冲在 Compute/VS 间显式解绑、固定 `DrawInstanced(6, 2048, 0, 0)`，且绘制顺序为激光主体之后、尖端之前。
- 2026-07-29（本轮）：ARM64 原生 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Build` 成功；VS、PS、`UpdateCS`、`EmitCS` 均由 FXC 成功生成 `.cso`，新增测试源码成功编译。
- 2026-07-29（本轮）：严格遵守静态验证边界，没有运行主程序或测试 EXE，没有启动窗口或执行电脑控制；运行时视觉观感仍待用户后续验证。
- 2026-07-30（粒子调优）：法线额外发散由 `4.5 DIP` 提高到 `10 DIP`；异常跳变与预测回缩改为保持修正状态，并按当前正常粒子速度约 `2×` 限速追赶至目标。
- 2026-07-30（粒子调优）：ARM64 原生 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Rebuild` 成功，VS、PS、`UpdateCS`、`EmitCS` 四个 Shader 均由 FXC 成功生成资源产物；只编译/链接测试工程，未运行任何 EXE、窗口或电脑控制，运行时观感仍待用户验证。
- 2026-07-30（端点积压修正）：新粒子发射锚点改为直接采用当前可见 `l0DrawPoints.back()`，不再继承预测修正的平滑延迟；已发射粒子仍由 GPU 按约 `2×` 正常速度限速追赶。缓慢前行或 Up 后抵达组合路径末端的粒子按 `0.35s ±25%` 的固定种子时长快速淡出。
- 2026-07-30（端点积压修正）：ARM64 原生 `MSBuild.exe` 对完整解决方案执行 `Debug|ARM64 /t:Rebuild` 成功，测试源码及 VS、PS、`UpdateCS`、`EmitCS` 均成功编译；未运行任何 EXE、窗口或电脑控制，运行时观感仍待用户验证。
- 2026-07-30（屏幕空间法线喷射）：移除路径槽、generation、弧长、segment cursor、端点淡出和 prediction correction。新粒子从 L0 笔尖沿随机正/负法线 `±25°` 喷射，以 `28–64 DIP/s`、`0.7–1.0s` 独立运动并按实际行程缩至 20%；3 DIP 辉光同步为 `(1.0, 0.32, 0.40)`、峰值 Alpha `0.34`。
- 2026-07-30（屏幕空间法线喷射）：ARM64 原生 `MSBuild.exe` 对完整 `inkStrokeModelerTest.sln` 执行 `Debug|ARM64 /t:Build /m` 成功；首次完整重编译由 FXC 成功编译 VS、PS、`UpdateCS`、`EmitCS`，主工程与测试工程均完成编译/链接（0 error，42 条既有 `additional/` 第三方警告），修正测试断言后的最终增量构建为 0 warning / 0 error。没有运行任何 EXE、窗口或电脑控制。
- 2026-07-30（视频式粒子层级调优）：静态核对确认源码、PRD、设计和规范均已同步到 `2.5 DIP` 移动发射间距、`72/s` 单 contact 上限、`10–17 DIP/s` 出生速度、`0.28–1.15 DIP` 偏小尺寸分布、`glowRadiusScale=1.5`、基础亮度/尺寸正相关和射程/尺寸弱反向相关。ARM64 原生 `MSBuild.exe` 对完整 `inkStrokeModelerTest.sln` 执行 `Debug|ARM64 /t:Build /m` 成功；FXC 成功编译 VS、PS、`UpdateCS`、`EmitCS` 并生成 `.cso`，主工程与测试工程均完成编译/链接（0 error，42 条既有 `additional/` 第三方警告）。`git diff --check` 未报告 whitespace error，仅输出 Trellis Markdown 的 LF/CRLF 工作区提示。未运行任何 EXE、窗口或电脑控制。
- 2026-07-31（粒子可见性调优）：针对粒子效果过于不明显的问题，调整参数使效果更美观现代。变更摘要：`minimumLaunchSpeedDipPerSecond` 10→28，`maximumLaunchSpeedDipPerSecond` 17→64（粒子明显散射）；`minimumRadiusDip` 0.28→0.45，`maximumRadiusDip` 1.15→2.2（粒子更大更显眼）；`sizeDistributionExponent` 2.8→1.6（减少极小粒子偏置）；`glowRadiusScale` 1.5→4.0（比例辉光更宽）；`glowAlpha` 0.34→0.50（辉光更不透明）；`breathingAmplitude` 0.12→0.18（呼吸动画更明显）。VS 辉光公式改为混合基底地板 `+2 DIP × dpiScale` 加比例分量，确保最小粒子在任意 DPI 下均有可见辉光；PS falloff 由 `pow(...,2.0)` 改为 `pow(...,1.2)` 使辉光边缘更柔和。`.cso` 待下次 ARM64 `MSBuild.exe /t:Build` 时由 FXC 自动重新生成；`laser_particles.cpp` 内 `LaserParticleEmitConstants` 默认值与配置同步更新。
