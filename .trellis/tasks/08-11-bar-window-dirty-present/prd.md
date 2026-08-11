# Bar 非全屏 HWND 与脏区呈现优化

> 状态：规划中。当前任务只沉淀目标、技术路线和研究清单，不进入实现。

## Goal

将 UI3 Bar 从“覆盖主显示器的全屏分层窗口和全屏 D2D 目标”迁移为“只覆盖 Bar 当前可见范围的 HWND”，并建立与动态窗口大小相匹配的脏区绘制和呈现策略，减少 4K 场景中的 D2D 到 GDI 同步、ULW 提交以及 DWM 合成负担。

启动、DPI 或影响布局的配置变化时，计算一次 Bar 所有可能状态的最大包围盒并据此创建持久渲染资源；正常动画帧只改变当前 HWND 范围、绘制脏区和提交脏区，不因可见内容范围变化而重建 D2D 资源。

## Confirmed Facts

- 当前 HWND 在 `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:121` 至 `:126` 被设置为主显示器宽度和高度减一，属于接近全屏的窗口。
- 当前 D2D 目标在 `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp:52` 至 `:84` 创建，尺寸来自窗口，且使用 `D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_GDI_COMPATIBLE`。
- 当前呈现路径在 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:9525` 至 `:9536` 执行 `GetDC(D2D1_DC_INITIALIZE_MODE_COPY)`、`UpdateLayeredWindowIndirect` 和 `ReleaseDC`。`GetDC` 没有脏区参数，因此当前主要风险是整个 D2D 目标的同步成本。
- 当前 ULW 参数已包含 `pptDst`、`psize`、`pptSrc` 和 `prcDirty`，见 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:9657` 至 `:9669`，可以作为同一次更新位置、大小和内容的基础。
- 当前渲染循环已经采集 `visibleContentBounds`，见 `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:6490` 至 `:7047`；同时已有按元素跟踪和提交成功后才确认的脏区事务。

## Requirements

### R1. 最大容量包围盒

- 启动、DPI 变化、显示器变化或影响布局范围的配置变化时，计算 Bar 所有允许状态的最大包围盒。
- 最大包围盒必须覆盖主 Bar、更多面板、绘图属性面板、几何属性面板、提示层、阴影、模糊、光照、抗锯齿边缘以及动画可能产生的外扩范围。
- 最大包围盒是渲染资源容量，不等于每帧 HWND 的实际大小。
- 最大容量未被突破时，不得因动画或普通状态切换重建 D2D target、GDI 互操作对象或设备相关缓存。

### R2. 非全屏动态 HWND

- HWND 只覆盖当前 Bar 可见内容所需的范围及预留 padding，不再长期覆盖整个显示器。
- 使用同一次 `UpdateLayeredWindowIndirect` 调用更新 `pptDst`、`psize`、`pptSrc` 和内容。
- 当前窗口矩形可以在最大容量范围内变化，但应研究 padding、尺寸量化或滞回策略，避免动画造成逐像素高频缩放。
- 窗口大小、源起点或坐标映射改变时，默认将新的当前窗口范围视为全脏；后续只有在 Win7 SP1 实测证明安全后才进一步缩小该帧脏区。

### R3. 坐标和脏区

- 明确区分屏幕坐标、最大容量画布坐标和当前 HWND 本地坐标。
- 现有元素脏区继续在稳定的画布坐标中累计，提交前映射并裁剪为当前 HWND 本地坐标下的 `prcDirty`。
- 普通帧同时用该脏区限制 D2D 绘制和 ULW 提交。
- 首帧、资源重建、窗口范围变化、无法分类的内容变化或上次提交失败重试时，使用保守的全脏策略。
- 只有 D2D、GDI 互操作、ULW 和帧结束全部成功后才能提交脏区状态；失败时保留全脏重试语义。

### R4. 分阶段 D2D 到 GDI 策略

- 第一阶段保留 GDI-compatible D2D target 和 `GetDC(COPY)`，但将目标从 4K 全屏缩小为 Bar 最大容量；先测量缩小后的同步成本。
- 如果缩小目标后 `GetDC(COPY)` 仍是明确瓶颈，再研究持久 CPU staging 和持久 DIB Section/HDC：D2D 只复制脏区到 staging，CPU 只复制脏行到 DIB，ULW 使用同一脏区。
- staging、DIB 和 HDC 必须复用，不得每帧创建；staging 容量可采用分档或只增不减策略。
- 不将“普通 D2D API 可以把一个脏区直接复制到 DIB/HDC”作为前提；现有接口没有这样的直接路径，单次复制方案只能作为独立研究项。
- 不在缺少性能数据时预先引入两次脏区复制的复杂实现。

### R5. 兼容性和性能验证

- 保持 Windows 7 SP1 + KB2670838 的 D2D 1.1 兼容边界。
- 验证 ULW 在同次更新窗口位置、大小、源起点和内容时的行为，特别覆盖扩张、收缩、移动和提交失败重试。
- 分别测量 D2D 绘制、`GetDC(COPY)`、可选 staging/Map/memcpy、ULW 和整帧耗时。
- 记录不同 DPI、分辨率、Bar 状态和动画负载下的脏区面积、窗口面积、CPU/GPU 占用和帧时间。
- 通过 PresentMon 或等价工具验证缩小顶层 layered HWND 后，对下方 flip-model 窗口的合成模式是否有实际改善；不把改善 Independent Flip/MPO 视为保证结果。

## Acceptance Criteria

- [ ] 启动或 DPI/布局配置变化后可以得到覆盖所有 Bar 合法状态及效果外扩的最大容量矩形。
- [ ] 正常动画和状态切换不会重建 D2D target、GDI 互操作对象或设备相关缓存。
- [ ] HWND 不再是显示器全屏尺寸，并能在一次 ULW 中同步更新位置、大小和内容。
- [ ] 当前窗口范围不变时，D2D 绘制和 ULW 均使用映射正确且裁剪后的脏区。
- [ ] 当前窗口范围或源映射变化时使用安全的全窗口脏区，扩大区域不会出现透明残影或未初始化像素。
- [ ] 提交失败后不会错误丢弃脏区，后续能够全脏恢复。
- [ ] Windows 7 SP1 + KB2670838 兼容性验证通过。
- [ ] 已取得缩小 HWND 前后的阶段化性能数据，并据此决定是否实施持久 staging/DIB 路线。
- [ ] 如实施 staging，资源不会每帧创建，复制量与脏区面积近似相关，且最终画面与现有路径一致。
- [ ] 已记录 layered HWND 覆盖范围变化对下方 flip-model 呈现模式的实测结果。

## Out of Scope

- 本规划阶段不修改 Bar 业务代码，不开始实现，不运行 `task.py start`。
- 不将主 D2D 画布整体替换为 IWICBitmap 或通用 CPU 光栅画布。
- 不按每帧精确可见内容包围盒重建渲染资源。
- 不承诺仅靠缩小 HWND 一定进入 Independent Flip 或 MPO。
- 不在没有基准数据时重写现有 D2D 1.1 效果和缓存架构。

## Deferred Research

- 枚举“所有可能状态”时需要纳入的组件、效果 padding 和动画最大外扩值。
- 当前 HWND 使用逐帧精确包围盒、分档尺寸还是扩大立即生效而缩小延迟的滞回策略。
- Win7 SP1 上 `prcDirty` 与同次 `psize`/`pptSrc` 变化组合的实际行为；默认方案不依赖局部更新正确性，尺寸变化帧直接全脏。
- 最大容量 target 缩小后 `GetDC(COPY)` 是否仍值得替换，以及 staging 的合适容量分档。
- PresentMon 在目标系统上可用的指标和 flip-model 对照测试方法。
