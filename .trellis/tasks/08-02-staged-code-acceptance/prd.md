# 阶段性代码验收与优化

## Goal

对当前绘制系统进行分阶段验收。在不改变可见效果、输入语义和公开行为的前提下，先完成 Pen、Highlighter、Eraser、Laser 四种绘制类型的极致性能优化；后续再分别处理冗余代码与代码规范，以及以最近一个月提交为优先扫描深度的安全验收。一个月是推荐审计深度，不是修复边界；扫描中发现的更早实际缺陷也要修复。

阶段 1、阶段 2 与阶段 2.5 已完成并提交；阶段 3 的代码修复、静态检查、跨架构构建与无窗口验证已完成。总优化任务继续保持进行中，等待用户验收与后续提交。

## Stages

### Stage 1: 绘制性能优化（已完成）

- 优化 Pen、Highlighter、Eraser、Laser 的 CPU 计算、内存分配与复制、GPU 状态切换、资源上传和无效绘制。
- 优先优化 Laser，覆盖单指增量路径、多指完整重绘回退、粒子模拟与绘制、coverage 清理与 resolve。
- 保持四种绘制类型的最终像素、几何、压力、prediction、合成顺序、生命周期和异常回退行为不变。
- 优化必须兼容 Windows 11 ARM64、现有 D3D11 shader 编译链、完整解决方案构建和当前公开设置接口。

### Stage 2: 冗余代码与写法规范（已完成）

- 删除确认无运行调用、仅为旧实现保留或能由现有权威接口完全替代的代码；测试仍需要的参考算法移入测试侧，不继续扩大生产接口。
- 将职责杂糅的大型实现文件按稳定模块边界拆成多个 implementation unit：Renderer 区分普通绘制、Laser 与资源生命周期；Ink Prediction 区分输入/重连策略与 stroke geometry；Laser Particles 区分纯 CPU 规划与 D3D 粒子系统。
- 同一 C++20 named module 可以拥有多个按职责命名的 `.cpp` implementation unit，但保持现有 `.cppm` 公开契约和导入名称不变。
- 统一触及代码的 tab 缩进、Allman 大括号、命名、guard 写法和中文关键注释；不对未触及文件做全量格式化。
- 保持所有绘制效果、输入语义、线程边界、公开设置字段、默认值、CPU/GPU 契约和失败回退行为不变。

### Stage 2.5: 绘制性能测试 HUD（已完成）

- 增加公开测试模式开关，默认开启；关闭后不采样、不格式化、不更新 HUD。
- 启动后在主屏幕左上角显示尺寸适中的半透明性能 HUD，保持点击穿透且不取得输入焦点。
- HUD 使用最近 1 秒的实际墨迹绘制帧统计平均 FPS、1% Low FPS、平均/P99/波动帧时、绘制工作耗时和 Present 耗时。
- 每秒低频采样本进程 CPU 占用、工作集内存和可用时的本进程 GPU 显存占用；不引入系统级高频性能计数轮询。
- 只在存在物理绘制 contact 时推进统计并刷新文字；空闲、Laser Hold/Fade 和仅粒子动画期间保持最后结果，不主动驱动墨迹帧或 Present。
- HUD 使用独立 Win32 layered overlay，由现有窗口线程创建和更新，不加入墨迹 backbuffer、dirty rect、shader 或 presenter 合成路径。
- HUD 分为中文性能统计与接触设备区；每个活动 contact 独占一行，显示设备、实际分派工具、颜色、实时粗细、输入 X/Y、压力、滤波速度、接触面积、高度角和转动角。
- 接触行使用固定宽度数值列与等宽字体，字段标签不得随数值位数变化而移动；面板宽高按内容扩大。

### Stage 3: 代码安全验收（代码与无窗口验证完成）

- 检查最近一个月提交的代码，识别并修复潜在危险；发现的更早实际缺陷同样纳入修复。
- 覆盖不可信输入数量/数值、浮点到整数、等待截止时间、GPU Map 失败、异步 Resize copy、变长编码缓冲和动态 DLL 加载。
- 兼容基线严格为 Windows 7 SP1，仅安装 KB2670838；不得依赖 KB2533623 或其他 KB 提供的 DLL 安全搜索 API。
- Stage 2.5 已完成并提交后再执行提交审计；不把 HUD 实现与安全验收混入同一提交。

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
- HUD 指标使用固定容量或预留容量；绘制热路径不得因统计产生逐帧堆分配、文件 I/O、GPU readback 或同步等待。

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

## Out Of Scope For Stage 2.5

- 提供完整设置界面、持久化配置、远程遥测、日志上传或性能报告文件。
- 使用 PDH/WMI/ETW 等系统级持续采样器统计整机 CPU/GPU 利用率。
- 把 HUD 混入墨迹 backbuffer，或为 HUD 扩大墨迹 Present dirty rect。
- 在无真实绘制 contact 时持续刷新数字、唤醒绘制线程或制造额外 Present。
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

## Stage 2.5 Acceptance Criteria

- [x] `StrokeModelConfiguration` 和 `DrawingController` 提供测试模式开关，默认开启；setter/getter、关闭后的采样门控和绘制序列断点由静态/无窗口测试覆盖。
- [x] HUD 位于主屏幕左上角，具有半透明背景、可读文字、点击穿透和非激活窗口样式；窗口类、消息和资源生命周期由静态测试锁定。
- [x] 1 秒统计窗输出平均 FPS、1% Low、平均/P99/标准差帧时、work/present、CPU、工作集和可用时的 GPU 显存；确定性帧序列计算由无窗口测试覆盖。
- [x] 只有物理 contact 绘制帧会记录和触发 HUD 更新；Hold/Fade、粒子动画和完全空闲不推进统计，也不产生 HUD 自身的持续定时器或 Present。
- [x] 统计 tracker 使用固定容量样本，绘制帧不执行文件 I/O、GPU readback 或跨线程同步等待；HUD 文本在物理绘制帧发布最新值，平均 FPS 与无等待性能 FPS 仅在 1 秒窗口闭合时更新。
- [x] HUD 全部字段中文化并按性能/接触分类；每个活动 contact 一行显示最新输入和实际绘制状态，数字使用固定宽度列，面板扩展至宽版动态高度。
- [x] RTS 请求并解码 Touch 接触宽高，按 tablet context 的轴缩放转换为像素并发布到绘制线程；缺失、非法或非 Touch 输入保持未知值，回退链不影响原有笔属性。
- [x] ARM64 Debug/Release 完整解决方案构建、两个配置的无窗口控制台测试与 `--drawing-perf` 通过，编码/换行和 `git diff --check` 通过。
- [x] 因当前对话禁止 GUI，HUD 位置、透明度、字号、点击穿透、多 DPI 视觉效果和真实触摸面积明确记录为未执行的人工验证项。

## Stage 3 Acceptance Criteria

- [x] 以 `6a032a8..817ebde` 的 86 个提交为优先审计范围，并继续修复扫描中确认的更早 UTF-8 与 ULW resize 缺陷。
- [x] contact/RTS 的容量、指针、属性数量、scale、packet 布局、坐标和终态闭合均有显式边界与异常回退。
- [x] QPC deadline、Laser Hold、几何/脏区和光标中的浮点转整数均拒绝或饱和处理 NaN、Inf 与超范围值。
- [x] GPU buffer/constant buffer `Map` 失败后不再消费旧数据、推进 ring head 或继续绘制。
- [x] UTF-8 变长编码缓冲容量与 API 写入长度一致；ULW dirty copy 同时裁 producer/staging/destination 三方尺寸。
- [x] 本轮由工程源码直接编译的系统 DLL 加载只经 System32 绝对路径；不依赖 `LOAD_LIBRARY_SEARCH_SYSTEM32` 或其他需要额外 KB 的安全搜索机制，缺失可选模块时保持既有回退。
- [x] 预编译模型库带入的 Win8 `GetSystemTimePreciseAsFileTime` 静态导入已由兼容单元替换为运行时探测；纯 Win7 回退到 `GetSystemTimeAsFileTime`，ARM64/x64/x86 最终导入表均不再包含 Win8 API。
- [x] 新增无窗口单元/静态契约覆盖上述边界；最终 Debug/Release 构建、全量控制台测试、`--drawing-perf`、编码换行和 `git diff --check` 结果记录在实施文档。
- [ ] 按当前对话限制不执行 GUI、真实输入、D3D Debug Layer、Resize/Present 人工验证或 `--benchmark`；这些项目不得声称已验证。
