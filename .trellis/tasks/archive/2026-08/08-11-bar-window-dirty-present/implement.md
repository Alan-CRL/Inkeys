# Bar 非全屏 HWND 与脏区呈现优化：执行计划

> 状态：Phase 1-3 已实现并通过 headless 与完整 ARM64 构建；任务保持进行中，等待 GUI、Win7 和 PresentMon 实机验证。本轮不包含可选 staging/DIB。

## Phase 0：建立基线和研究边界

- [x] 记录当前全屏 HWND、D2D target、GetDC 和 ULW 的尺寸与生命周期。
- [ ] 加入或启用仅用于研究的分阶段计时方案，确定 4K 下 `GetDC(COPY)` 的实际占比。
- [ ] 单独测量 `ReleaseDC(nullptr)`，并原型验证 HDC 只读时传空更新矩形在 Win7/Win11 上的正确性与耗时。
- [ ] 确认 Windows 7 SP1 + KB2670838 测试环境和可观察指标。
- [ ] 设计 PresentMon 对照场景，记录 Bar 覆盖下方 flip-model 窗口时的 presentation mode。

## Phase 1：稳定坐标与最大容量模型

- [x] 拆分显示器布局域、`capacitySize/capacityOrigin`、`reservedEnvelope/viewportRect` 和已提交窗口状态，停止复用单一 `barWindow.w/h` 表达所有语义。
- [x] 列出所有可能改变 Bar 外边界的顶层 Surface、越界视觉、提示层和动画状态。
- [x] 统一计算阴影、模糊、光照、描边和抗锯齿 padding。
- [x] 定义锚点相对容量纪元：启动、DPI、显示器、布局配置和容量突破；主按钮屏幕移动不得扩大资源容量。
- [x] 定义容量突破时的安全回退和重建事务。
- [x] 为容量与 layout/surface/client/screen 四空间转换增加纯逻辑测试；负显示器原点和多 DPI 仍保留实机验证。

## Phase 2：动画批次包络

- [x] 将停用的 `visibleContentBounds` 清单整理为顶层外框生产者，并登记 Popup、Preview、PointLight 和调试文字等越界视觉。
- [x] 使用 `startV/middleV/tar/curve` 与 Back 超调估计主栏容量；顶层动画 viewport 使用更保守的容量包络。
- [x] 为 Slider、颜色拖动等不可预知连续输入预留完整合法交互域。
- [x] 实现 envelope generation：展开前扩张一次、批次内保持、最终成功提交后收缩一次；重定向只在突破时扩张。
- [x] 让失败重试继保留旧包络和全脏状态，直到 ULW 与帧事务共同成功。
- [ ] 增加纯逻辑测试，覆盖开/关、快速反向、More/绘图属性交替、上下换边、捕获中断和动画关闭。

## Phase 3：动态非全屏 HWND 与输入

- [x] 在 D2D 绘制入口应用 `-capacityOrigin` 变换，保持布局对象坐标不受 HWND 原点变化影响。
- [x] 定义并复用 `layout <-> surface <-> client <-> screen` 转换接口。
- [x] 将鼠标、触摸、Raw Input 派生悬停、计时器重新命中和边缘光区域迁到统一映射；整栏拖动使用屏幕 delta。
- [x] 主按钮捕获期间使用固定尺寸直接移动 HWND；与 ULW 共用几何锁，全部退出路径都会重新请求 Bar 完整呈现，且不阻塞共享 UI3 调度器中的 PPT 客户端。
- [x] 在一次 ULW 中提交 `pptDst`、`psize`、`pptSrc` 和内容。
- [x] 窗口或源映射变化帧使用全窗口脏区；稳定帧映射局部脏区。
- [x] 明确除主按钮直接位移外，渲染线程是 Bar 动态几何的唯一提交者；初始化只设 `1x1`，动态模式下 WindowService 不再发独立 `SetBounds`。
- [ ] 验证扩大、缩小、移动、多 DPI 和跨显示器场景无裁剪、残影或命中区域错位。
- [x] 脏区调试开启时增加蓝色 HWND 边框；活动帧脏区保持红色，idle 前最后一帧脏区改为绿色，并移除帧率文字中的“休眠”。
- [x] 将红/绿/蓝框的旧/新边界纳入调试 damage，覆盖开关关闭、viewport 扩缩和失败重试后的清理。

## Phase 4：缩小目标后的性能决策

- [x] 保持现有 GDI-compatible target + `GetDC(COPY)` 路线，未增加 staging/DIB 和额外像素拷贝。
- [ ] 对比全屏基线和最大容量目标的 GetDC、ULW、整帧时间及内存占用。
- [ ] 记录下方 flip-model 窗口的 PresentMon 变化。
- [ ] 根据数据决定是否结束优化，或进入 Phase 5。

## Phase 5：可选持久 staging/DIB 路线

- [ ] 原型验证 `CopyFromBitmap -> Map -> 脏行 memcpy -> ULW` 的 Win7 可行性。
- [ ] 设计持久 staging 的容量分档或只增不减策略。
- [ ] 创建并复用持久 DIB Section/HDC，保证未变化像素跨帧保留。
- [ ] 处理 pitch、BGRA 预乘 alpha、脏区偏移和边界裁剪。
- [ ] 窗口范围变化或提交失败时执行全窗口刷新。
- [ ] 与 Phase 4 非全屏基线端到端比较；只有收益稳定才保留此路线。

## Phase 6：验证和收尾

- [x] 运行 Bar 包围盒、坐标转换、脏区事务和失败重试 headless 测试：`PASS animation correctness`。
- [x] 完成共享 UI3 调度兼容收尾：Bar 回调内不再做本地帧等待；共享 `DXGI_ERROR_DEVICE_REMOVED/RESET/DRIVER_INTERNAL_ERROR` 上报 `DeviceLost`，由唯一调度线程恢复 epoch。
- [ ] 在 Windows 7 SP1 + KB2670838 与当前 Windows 11 ARM64 上验证。
- [x] 使用 ARM64 MSBuild 编译完整 `InkeysRepo.sln` 的 `Debug | ARM64` 配置：0 errors，仅保留仓库现有告警。
- [ ] 执行长时间动画、DPI 切换、配置切换、设备重建和显示器变化压力测试。
- [ ] 使用自动计数或日志确认普通动画批次的 HWND resize 次数符合 0/1/2 次模型，而不是随帧数增长。
- [ ] 使用固定屏幕参考点验证 resize 前后内容像素位置与鼠标/触摸命中一致。
- [ ] 记录最终选择、性能数据、回滚点和仍待研究的问题。

## 重点文件

- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`：当前全屏窗口尺寸和初始位置。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp`：D2D target、GDI 互操作和设备资源生命周期。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm`：渲染资源接口和容量状态。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：可见包围盒、脏区事务、GetDC 和 ULW 提交。
- `Inkeys/Inkeys/UI/Bar/Bar.Animation.cppm`：动画端点、中间值、曲线与时间线包络证据。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：客户区输入、触摸转换、拖动和边缘光映射。
- `Inkeys/Inkeys/Window/Window.cpp`：Bar 初始边界与动态几何单一所有权边界。

## 回滚点

- 动态 HWND 与 staging 分成独立阶段；Phase 3 可在不启用 staging 的情况下单独落地。
- 保留现有全脏提交作为正确性回退。
- staging 原型不满足端到端收益时，回退到缩小后的 GDI-compatible target + GetDC 路线。
- 包络或输入映射出现问题时，可暂时固定 HWND 为最大容量尺寸，而无需恢复显示器全屏 target。

## 开始实现前的门槛

- [x] 用户审阅并明确批准最终规划摘要。
- [x] Deferred Research 中影响首阶段行为的决策已记录为确定方案或保守回退。
- [x] 用户已明确授权 `task.py start` 与产品代码实现。
