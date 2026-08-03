# 阶段 1 绘制性能优化技术设计

## Design Principles

- 以输出等价为首要约束。优化只减少容器、复制、扫描、资源上传、状态切换和脏区像素，不调整任何视觉参数或输入策略。
- 优先建立无窗口基线与参考实现断言，再分批替换热点路径。每一批都可独立回退，不删除现有正确 fallback。
- CPU/GPU 共享结构、shader shape、slot 和常量用途必须由 `static_assert` 或静态源码契约测试固定。

## Point Range And Geometry Pipeline

renderer 与 rect helper 的只读点输入统一改为 `std::span<const InkPoint>`。现有 vector 调用可隐式构造 span，子区间通过 `std::span::subspan` 传递，单点/dot fallback 使用栈上 `std::array<InkPoint, 2>`，从而保持点序、边界重复点和胶囊段生成顺序不变。

Pen、Eraser、Laser 的稳定前缀、实时尾部和 coverage 提交都只传范围，不建立临时 vector。所有索引在构造 span 前完成边界校验；空范围沿用原先 no-op 语义。

Highlighter 增加 in-place geometry rebuild：输出 vector 只 `clear()` 并复用 capacity，输入流式去除连续重复点并直接生成 primitive，不再先保存 deduplicated points。保留值返回 wrapper 供既有测试或非热点调用。完成帧中 committed 与 live primitive 在同一个 MAX/MIN operator target 上按原顺序分别绘制，不构造合并 vector；由于 operator 对相同 primitive 集合的组合规则不变，最终结果与合并后单次绘制等价。

## Multi-Contact Laser Fallback

第二个 Laser contact 仍把当前批次锁定为完整重绘，避免改变 contact 间 source-over 和 Down 顺序。优化的是回退的像素工作范围，而不是改变 coverage 所表示的完整几何。

每个 `LaserStrokeLayer` 在 fallback 中继续维护稳定边界、旧/新 live bounds 与 stable delta bounds：

1. `frameDirty` 只并入新增稳定区域以及旧/新 live 区域，批次结束或状态重建时再按实际需要扩大。
2. 最终 frame dirty 在基础合成前闭合。
3. 每个 layer 计算 `layerBounds ∩ frameDirty`；交集为空则跳过该 layer。
4. scratch 只清交集矩形；设置 scissor 后仍按原实现上传/绘制该 layer 的完整几何，使交集内 coverage 与全量光栅化完全一致。
5. 所有 layer 仍按 Down 顺序在同一个裁剪区域 source-over resolve，最后 Bake 语义不变。

scissor state 由 renderer 显式设置并恢复，不能泄漏到 Pen、Highlighter、Eraser、粒子或 presenter。Resize、Clear、取消、设备恢复和批次结束重置所有范围状态；任何范围不变量失败都锁定到原完整脏区 fallback。

## Laser Rect Pass And Style Constants

Laser 矩形 clear/resolve shape 使用全局常量中对这些 shape 未使用的 `globalColor` 保存 `(left, top, right, bottom)`，VS 依据 `SV_VertexID` 生成矩形 quad。CPU 不再 Map/Unmap `inkDataBuffer`，也不绑定/解绑 t0。其他 shape 对 `globalColor` 的含义保持不变；静态测试验证矩形 shape 不读取点 buffer。

Laser 样式常量缓存最近成功上传的配置 generation 与 opacity。coverage、dot、rect、particle 路径请求样式时，仅在键变化后 Map；Configure、device resource 重建和上传失败会使缓存失效。缓存只消除相同字节的重复上传，不改变常量值或绘制顺序。

## Particle Work Snapshot And Batching

CPU dirty tracker 每帧执行一次 prune，并产生不可变 snapshot：`hasActive`、`activeBounds`、是否需要清理旧 bounds。controller、simulation、draw 和 frame dirty 共同使用该 snapshot，不再分别扫描 512 项。

粒子步骤按以下资格执行：

- 有新 emission request：执行 update（若需要）并按原 request 顺序 emit。
- snapshot 表明旧 batch 仍可能存活：执行 update 和 draw。
- 没有存活 batch 且没有新 request：不 dispatch、不 `DrawInstanced`。
- 活动状态刚从有变为无：保留旧 bounds 的一次基础重建，但不提交粒子 draw。

renderer 暴露单个 batched particle step：绑定 UAV/公共常量一次，按既有顺序 dispatch update，然后逐请求上传 emit 常量并 dispatch，最后统一解绑。spawn cursor、seed、请求顺序和每请求粒子数量完全沿用现有逻辑。

## Particle Layout And Initialization

`LaserGpuParticle` 的 C++/HLSL 字段按实际读取顺序压缩到 80 字节，保留一个 padding uint 使两端 stride 和字段偏移明确。容量仍为 2048，StructuredBuffer 大小从 262144 字节降至 163840 字节。C++ 使用 `sizeof`/`offsetof` 静态断言，测试解析 HLSL 定义并核对字段顺序。

默认 GPU buffer 创建时不再提供 CPU 侧 2048 项零数组；资源和 compute pipeline 就绪后调用已有 reset dispatch 完成初始化。若 reset 失败，粒子资源标记不可用并沿用现有无粒子降级行为。

粒子 VS 删除仅用于 white-mix、但当前粒子 PS 完全不读取的 hash/输出计算。仅删除已由 shader 数据流证明无贡献的运行时状态；公开参数和阶段 2 才处理的命名/接口清理保持不动。

## Headless Measurement And Equivalence

测试工程先新增固定工作负载的无窗口性能入口，复用全局 allocation counter：

- Pen/Eraser/Laser 的稳定与 live range 提交规划。
- Highlighter 长笔画的逐帧几何 rebuild 和完成合成。
- 单/多 contact Laser 的 dirty/scissor plan。
- 粒子 tracker snapshot、空闲资格与多请求 batch 顺序。

基线记录分配次数、复制/处理元素数、命令资格计数和多轮耗时中位数。优化后的硬断言要求输出数据逐项相等，并要求目标热点的可避免分配/复制/命令归零或下降到设计常数；耗时只打印对比，不因机器噪声单独判失败。

## Compatibility And Rollback

- span/in-place 几何、Laser fallback 裁剪、rect 常量、样式缓存、粒子 snapshot/batch/layout 分成独立提交前检查批次。
- 每批保留旧测试作为参考，任一等价性断言失败就回滚该批，不用其他优化掩盖差异。
- Shader 或资源创建失败时走现有 CPU/renderer fallback；不新增强制硬件能力。
- 阶段 1 不更改公开设置、默认值、文件格式或持久化数据，因此不需要数据迁移。
