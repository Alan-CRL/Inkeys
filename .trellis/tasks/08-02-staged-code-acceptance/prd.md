# 阶段性代码验收与优化

## Goal

对当前绘制系统进行分阶段验收。在不改变可见效果、输入语义和公开行为的前提下，先完成 Pen、Highlighter、Eraser、Laser 四种绘制类型的极致性能优化；后续再分别处理冗余代码与代码规范，以及最近一个月提交的安全验收。

阶段 1 已完成并提交；当前执行阶段 2。阶段 3 继续保留在同一 Trellis 任务中，不得提前执行提交安全审计。

## Stages

### Stage 1: 绘制性能优化（当前范围）

- 优化 Pen、Highlighter、Eraser、Laser 的 CPU 计算、内存分配与复制、GPU 状态切换、资源上传和无效绘制。
- 优先优化 Laser，覆盖单指增量路径、多指完整重绘回退、粒子模拟与绘制、coverage 清理与 resolve。
- 保持四种绘制类型的最终像素、几何、压力、prediction、合成顺序、生命周期和异常回退行为不变。
- 优化必须兼容 Windows 11 ARM64、现有 D3D11 shader 编译链、完整解决方案构建和当前公开设置接口。

### Stage 2: 冗余代码与写法规范（当前范围）

- 删除确认无运行调用、仅为旧实现保留或能由现有权威接口完全替代的代码；测试仍需要的参考算法移入测试侧，不继续扩大生产接口。
- 将职责杂糅的大型实现文件按稳定模块边界拆成多个 implementation unit：Renderer 区分普通绘制、Laser 与资源生命周期；Ink Prediction 区分输入/重连策略与 stroke geometry；Laser Particles 区分纯 CPU 规划与 D3D 粒子系统。
- 同一 C++20 named module 可以拥有多个按职责命名的 `.cpp` implementation unit，但保持现有 `.cppm` 公开契约和导入名称不变。
- 统一触及代码的 tab 缩进、Allman 大括号、命名、guard 写法和中文关键注释；不对未触及文件做全量格式化。
- 保持所有绘制效果、输入语义、线程边界、公开设置字段、默认值、CPU/GPU 契约和失败回退行为不变。

### Stage 3: 最近一个月代码安全验收（延期）

- 检查最近一个月提交的代码，识别并修复潜在危险。
- 本轮不执行提交审计，不修改与阶段 1 无关的安全问题。

## Requirements

- Pen 的压力、笔尖预测、实时光标、L0/L1/L2 提交与中断恢复语义保持不变。
- Highlighter 的去重点、起止帽、MAX/MIN operator 合成、完成提交和脏区语义保持不变。
- Eraser 的命中范围、擦除顺序、完成提交和其他绘制类型的交互保持不变。
- Laser 的材质、辉光、压力、prediction、Hold/Fade、Down 顺序、多 contact source-over、自交、烘干和清理行为保持不变。
- Laser 粒子的发射顺序、数量、种子、生命周期、位置、速度、颜色、层级和保守脏区行为保持不变；设置默认值和公开接口不变。
- 优化前先建立可重复的无窗口基线，覆盖关键工作量、分配次数和等价性断言；优化后用同一工作负载复测。
- 不以不稳定的单次墙钟耗时作为正确性门槛；耗时只用于报告趋势，硬性门槛采用确定性的分配、复制、提交资格和输出等价性断言。
- 任何缓存、批处理或裁剪状态必须在 Configure、设备/尺寸资源重建、Clear、取消、Present 恢复和批次结束时正确失效或重建。
- 资源或优化状态异常时必须回退到已有正确路径，不得产生丢笔、残影、顺序变化或无效资源访问。
- 修改保持最小化，并遵循仓库现有编码、换行、中文关键注释和模块边界规范。

## Constraints

- 禁止启动主程序、创建或操控窗口、控制电脑或浏览器。
- 禁止运行会查找窗口、注入输入或调整窗口的 `--benchmark` 模式。
- 允许静态分析、shader 编译、ARM64 完整解决方案构建和无窗口控制台单元测试。
- C++、HLSL 和工程文件保持原有 UTF-8 BOM + CRLF；Trellis Markdown 保持 UTF-8 无 BOM + LF。
- 不触碰无关的未跟踪 `Vcpkg/`。

## Out Of Scope For Stage 1

- 修改任何绘制类型的视觉参数、默认设置、手感或公开 API。
- 为性能数据启动 GUI 或自动化真实窗口输入。
- 对不在热点路径中的代码进行风格重构、文件重排或公共抽象改造。
- 阶段 2 的全面清理与规范化，以及阶段 3 的最近一个月提交安全审计。

## Out Of Scope For Stage 2

- 修改绘制算法、视觉参数、性能策略、公开设置字段或默认值。
- 引入新的 PImpl、公共抽象层或跨模块 D3D 资源依赖。
- 清理历史参考工程、第三方 `additional/` / `lib/` 或未确认用途的工程残留项。
- 阶段 3 的最近一个月提交安全审计。

## Acceptance Criteria

- [x] 新增的无窗口性能/等价性测试能记录四类绘制的优化前基线，并明确证明测试过程不创建或查找 GUI 窗口。
- [x] Pen、Highlighter、Eraser、Laser 的点范围处理不再为只读子区间制造不必要的临时 `std::vector`，热点帧的可避免堆分配和整段复制被消除。
- [x] Highlighter 实时几何复用已有容量，不再经过重复的中间点容器；完成合成不再构造可避免的合并容器，输出与基线逐项等价。
- [x] 单指 Laser 继续保持既有增量 coverage 行为；多指回退只处理最终脏区内的像素，同时保持 Down 顺序、source-over、自交和 Bake 结果不变。
- [x] Laser 矩形清理/解析和样式常量不再执行可证明冗余的 buffer 上传或重复常量映射，CPU/HLSL 契约由静态测试锁定。
- [x] 粒子在没有存活粒子和新发射请求时不执行模拟或绘制；多 contact 发射共享一次 GPU 绑定周期，发射顺序和粒子结果不变。
- [x] 粒子脏区跟踪每帧只进行一次有效快照扫描，并保留最后一帧旧脏区清理语义。
- [x] 粒子结构化缓冲 stride 从 128 字节压缩到 80 字节，2048 粒子容量由 256 KiB 降到 160 KiB；C++/HLSL 字段偏移和行为由测试验证。
- [x] 删除粒子 shader 中对最终像素无贡献的运行时计算后，静态契约和确定性粒子测试通过。
- [x] ARM64 原生 MSBuild 对完整 `inkStrokeModelerTest.sln` 的 Debug/Release 构建成功，两个配置的无窗口控制台测试全部通过。
- [x] `git diff --check`、编码/换行检查和相关静态契约检查通过；没有修改阶段 2、阶段 3 或 `Vcpkg/` 范围。
- [x] 因禁止 GUI，本阶段只声明静态、构建和无窗口测试结论；真实窗口视觉、D3D Debug Layer 与人工输入验证明确保留为未执行项。

## Stage 2 Acceptance Criteria

- [x] 大型实现文件按上述职责拆分，新增 implementation unit 已同步主工程、测试工程及两个 `.filters` 文件，原 module import 名称不变。
- [x] 删除的生产函数均经全仓引用搜索证明无运行调用；仍有价值的参考等价逻辑只保留在测试侧。
- [x] Renderer、Ink Prediction 与 Laser Particles 的运行签名、设置字段、默认值和 CPU/GPU layout 不变；只删除已确认无运行调用的导出 wrapper 与测试 helper。
- [x] 触及代码符合项目 tab/Allman/PascalCase/camelCase/成员尾下划线规范，关键模块边界和失败原因有简短中文注释。
- [x] ARM64 Debug/Release 完整解决方案、未改动的四个 Shader 资源链、两个配置的无窗口控制台测试和 `--drawing-perf` 通过。
- [x] `git diff --check`、编码/换行、模块声明/工程引用和未跟踪 `Vcpkg/` 范围检查通过。
- [x] GUI、D3D Debug Layer、真实输入和 Resize/Present 人工验证明确标记未执行。
