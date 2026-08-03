# 阶段性代码验收技术设计

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

## Stage 2 Implementation Unit Split

拆分只移动既有定义，不新增 module import 名称，也不改变 `.cppm` 的公开类型所有权：

- `renderer.cpp` 保留资源创建、Resize/Release、operator layer 合成和 shader 资源加载；新增 `renderer_primitives.cpp` 承担 Pen/Highlighter/Cursor，新增 `renderer_laser.cpp` 承担 Laser coverage、rect resolve、粒子绘制和预热。
- `ink_prediction.cpp` 保留配置、Laser 生命周期、输入宽度策略和 interrupted reconnect；新增 `stroke_geometry.cpp` 承担宽度估算、Highlighter geometry、ActiveStroke、dirty rect 与 L0/L1 提交。
- `laser_particles.cpp` 保留配置校验、发射计划、纯 CPU 数学和 dirty tracker；新增 `laser_particle_system.cpp` 承担 D3D buffer/SRV/UAV/CS 生命周期与 dispatch。

多个 implementation unit 通过同一 `module draw3.xxx;` 归属既有 named module。确需跨 implementation unit 共用、但不能暴露给 import 方的常量或布局，放在 `.cppm` 的非导出 namespace；其他 helper 继续留在对应 `.cpp` 匿名 namespace。

## Stage 2 Redundancy Rules

- 删除无调用的 Highlighter 去重旧实现；`MergeHighlighterGeometry` 的参考等价逻辑移到测试侧，生产 module 不再导出测试专用 wrapper。
- Particle dirty tracker 统一只保留 `Snapshot`，删除测试专用的 `ActiveBounds` / `HasActive` 与无调用的 `HasAny`。
- Renderer 和 particle system 只保留批量 `Step`，删除无运行调用的单步 `Simulate` / `Emit` wrapper。
- 删除 shader 已不再消费的核心 white-mix CPU mirror、Renderer 私有缓存和常量填充；`LaserParticleConfig` 的现有公开字段仍保留，避免接口破坏。
- 不以减少行数为目标抽取只有一次调用且语义清晰的 helper，也不合并 Pen/Highlighter/Eraser/Laser 的不同渲染数学。

## Stage 2 Style And Comments

- 新 implementation unit 使用 UTF-8 BOM + CRLF、tab 缩进和 Allman 大括号，文件开头用一句中文说明职责。
- 修正触及区域中已存在的缩进、声明对齐和过时注释；注释解释 module 边界、GPU 绑定、状态恢复或行为不变量，不逐行翻译代码。
- `.vcxproj` 和 `.filters` 只增加实际编译文件，保持现有配置、模块接口标记、Shader 与资源项不变。

## Stage 2.5 Performance HUD Architecture

HUD 不进入 D3D backbuffer。当前 presenter 只接受一个矩形 dirty；若 HUD 与远处墨迹共同合成，二者包围矩形会显著扩大复制和 Present 面积，使性能测试反过来污染被测路径。因此由 `WindowController` 的既有窗口线程创建独立 owned layered popup，使用 `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，固定在主显示器工作区左上角并保持点击穿透。

绘制线程只在 1 秒统计窗闭合时发布新的不可变文字快照。窗口线程通过私有 `WM_APP` 消息消费最新文本，用内存 DIB + GDI 生成预乘 BGRA，再调用 `UpdateLayeredWindow` 更新整个小型 HUD。更新不等待窗口线程完成；重复发布只保留最新文本。HUD 窗口随主窗口显示/销毁，关闭测试模式立即隐藏，重新开启显示最后快照。

## Stage 2.5 Metrics Contract

`runtime_metrics` 增加独立的轻量 HUD tracker，不要求 `--metrics-output` 会话存在。tracker 使用固定容量数组保存最多一个统计窗内的 frame interval、work 和 present 样本；普通帧只执行常数次写入，不排序、不格式化、不查询系统状态。

统计窗达到 1 秒后才执行一次汇总：平均 FPS 使用有效间隔总时长，1% Low 取最慢 1% 帧时的平均帧时并求倒数，波动使用帧时标准差，同时输出平均/P99 frame、平均 work 和平均 present。进程 CPU 使用 `GetProcessTimes` 的相邻采样差除以墙钟和逻辑处理器数；工作集使用动态解析的进程内存计数 API。Renderer 初始化时尝试取得 `IDXGIAdapter3`，支持时每秒查询本进程 local/non-local video memory `CurrentUsage`，旧系统或 WARP 不支持时显示 `N/A`，不得失败启动。

只有帧开始和结束都处于物理 contact 绘制序列时才记录样本。活动绘制和 Laser Fade/粒子动画统一等待到目标帧截止时间，Pen 光标或其他 ControlWake 不能提前开始下一帧；队列只保留最新快照。Laser Hold、断触等待和完全空闲仍可被输入唤醒。物理 contact 结束时切断未完成统计序列，避免下一笔把空闲时间算成长帧。HUD 同时显示实际锁帧 FPS 与按平均 work 耗时推算的 `UNLIMITED FPS`，保留最后文本但不更新，也没有独立 timer。

## Stage 2.5 Compatibility And Rollback

- Layered HUD 创建、GDI bitmap 或显存查询失败时只隐藏对应 HUD/显示 `N/A`，墨迹窗口、输入和 presenter 继续运行。
- Win7 缺少 `IDXGIAdapter3` 时通过 QueryInterface 失败自然降级，不静态调用新系统函数。
- HUD 代码按窗口发布、纯统计和可选 GPU 查询三个边界实现；任一部分可单独关闭，不改变墨迹 D3D 资源和 shader。
- 当前会话不执行 GUI 验证；真实透明度、DPI、点击穿透和 owned-window z-order 留给人工验收。
