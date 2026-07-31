# 激光笔增量覆盖与深度优化基础

## Goal

在保持现有激光笔材质、辉光、压力、prediction、多 contact 顺序、粒子层级和 Hold/Fade 行为不变的前提下，为常见的单 contact Laser 建立专用 L1/L0 coverage 增量快路，使长笔画每帧不再上传和光栅化整条路径，并为后续深度优化保留可验证的基础与临时诊断。

## Background

- 关联任务 `.trellis/tasks/07-25-laser-pointer-glow-trail/` 已实现独立稳定颜色层、单笔 coverage scratch 和固定 GPU 粒子池。
- 提交 `989b443` 曾尝试把一张共享 scratch 跨帧当作所有 contact 的持久 coverage，并用固定 32 点保护尾部；粒子 dirty 重建底图后没有重放稳定 Laser，造成上一帧轨迹消失，已由 `c241e4d` 回滚。
- coverage 的通道 MAX 只保证同一 coverage 内的并集幂等；材质 resolve 使用 source-over，必须先重建目标 dirty，再把完整稳定/实时 coverage 在该区域解析一次。

## Requirements

- 新增一张可选的全画布 `R8G8B8A8_UNORM` Laser live coverage；已有 `laserStrokeCoverage` 在快路中保存稳定前缀，新资源保存当前真实尾部与 prediction。
- 绘制线程首次观察到选择 Laser 时创建新资源并预热新 shader 路径；创建后保留到 renderer 释放。窗口回调不得创建 D3D 资源。
- 单个有效 Laser layer 且资源可用时使用增量快路；出现第二个 layer、取消/状态异常或资源不可用时，当前批次锁定回退现有完整重绘直到烘干结束。
- 稳定边界沿用现有基于时间的 prediction 保护语义，只提交已确认 `realPoints`，不得使用固定点数窗口。L1/L0 在边界共享一个点。
- 新 resolve 路径逐通道取 `max(stableCoverage, liveCoverage)` 后只调用一次现有 Laser 材质解析，避免连接处、自交和重复帧产生辉光叠加。
- 所有旧/新 live、稳定 delta、粒子、cursor、fade 和其他图层 dirty 必须在 backbuffer 基础合成前确定。粒子或 cursor dirty 与稳定 Laser 相交时必须重放该交集。
- 最后一根 Laser Up 时，单 contact 快路把合并 coverage 烘入现有 `laserCompositedColor`；回退路径继续按 Down 顺序完整烘干。任何 Laser 内容均不得进入 L2。
- Resize 后若新资源曾创建则重新创建为空，并从 CPU 几何重建活动快路；Clear、生命周期失效和 Present 恢复不得保留无效增量游标。
- 粒子 GPU 池、Compute Shader、发射参数、512 区域 tracker 和 presenter 单矩形接口本轮保持不变；粒子开启时只共享主体 coverage 快路。
- 增加临时 `[LaserPerf]` 输出：资源创建一次、慢帧阈值输出，以及每批最后 Up/Bake 的单行汇总。不得扩展长期 metrics JSON。
- Pen、Highlighter、Eraser 的共享 L1/L0/L2 路径与视觉不得改变。未选择 Laser 的会话不分配新增 coverage。

## Out Of Scope

- 多 contact 的独立增量 coverage、纹理数组、atlas 或 tiled resources。
- 粒子多 dirty-rect/瓦片合成、间接绘制、粒子池压缩或参数调优。
- 激光材质、粒子外观、Hold/Fade 时长、公开设置 API 或持久化行为修改。
- 将临时诊断直接固化为长期 metrics schema；待用户回传日志后另行决定清理或扩展。

## Acceptance Criteria

- [ ] 单 contact 快路每帧 coverage 提交量只包含新增稳定 delta 与当前 live 尾部，不随整笔总点数线性增长。
- [ ] 新 shader 对 L1/L0 coverage 逐通道 MAX 后只解析一次材质；直线、急弯、自交、压力变化和 prediction 回缩无轨迹消失或辉光重复加深。
- [ ] 粒子开启时，粒子 dirty 内稳定 Laser 能正确重放，主体不再回到整笔几何重画。
- [ ] 第二个 Laser contact 出现后当前批次锁定完整重绘；多指 Down 顺序、最终 Bake 和取消语义与当前实现一致。
- [ ] 新资源只在绘制线程首次选择 Laser 时创建，创建失败只记录一次并回退；Pen/Highlighter/Eraser 帧不增加 draw/dispatch。
- [ ] Resize、Clear、Hold/Fade、新 Down、Present 失败恢复均无残影、丢失或过期增量状态。
- [ ] 每批诊断汇总包含模式、粒子状态、layer/点数、stable/live 提交量、完整重绘等价量、dirty 面积、活动帧、coverage 提交耗时和回退原因。
- [ ] 新增 CPU 状态测试覆盖稳定边界、L0/L1 重叠、prediction 回缩、多 contact 回退、Resize/clear reset 和 coverage MAX 等价性。
- [ ] ARM64 原生 MSBuild 对完整 `inkStrokeModelerTest.sln` 执行 `Debug|ARM64` 成功，VS、PS、UpdateCS、EmitCS 与测试工程均编译链接。
- [ ] 用户在白色、深色和混合背景上验证粒子开关、多 contact、Resize/Clear 与长笔画动态观感，并回传临时诊断输出。
