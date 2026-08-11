# Bar 非全屏 HWND 与脏区呈现优化：执行计划

> 状态：全部处于规划阶段。未运行 `task.py start`，以下项目均未开始。

## Phase 0：建立基线和研究边界

- [ ] 记录当前全屏 HWND、D2D target、GetDC 和 ULW 的尺寸与生命周期。
- [ ] 加入或启用仅用于研究的分阶段计时方案，确定 4K 下 `GetDC(COPY)` 的实际占比。
- [ ] 确认 Windows 7 SP1 + KB2670838 测试环境和可观察指标。
- [ ] 设计 PresentMon 对照场景，记录 Bar 覆盖下方 flip-model 窗口时的 presentation mode。

## Phase 1：最大容量模型

- [ ] 列出所有可能改变 Bar 外边界的组件、面板、提示层和动画状态。
- [ ] 统一计算阴影、模糊、光照、描边和抗锯齿 padding。
- [ ] 定义容量纪元：启动、DPI、显示器、布局配置和容量突破。
- [ ] 定义容量突破时的安全回退和重建事务。
- [ ] 为最大包围盒计算增加纯逻辑测试，覆盖最复杂组合状态。

## Phase 2：动态非全屏 HWND

- [ ] 将资源容量与 `barWindow.w/h` 的当前窗口尺寸语义拆分。
- [ ] 定义屏幕、容量画布和 HWND 本地坐标的转换接口。
- [ ] 选择并实现 padding、量化或滞回策略，避免动画逐像素 resize。
- [ ] 在一次 ULW 中提交 `pptDst`、`psize`、`pptSrc` 和内容。
- [ ] 窗口或源映射变化帧使用全窗口脏区；稳定帧映射局部脏区。
- [ ] 验证扩大、缩小、移动、多 DPI 和跨显示器场景无裁剪、残影或命中区域错位。

## Phase 3：缩小目标后的性能决策

- [ ] 使用现有 GDI-compatible target + `GetDC(COPY)` 路线完成基准。
- [ ] 对比全屏基线和最大容量目标的 GetDC、ULW、整帧时间及内存占用。
- [ ] 记录下方 flip-model 窗口的 PresentMon 变化。
- [ ] 根据数据决定是否结束优化，或进入 Phase 4。

## Phase 4：可选持久 staging/DIB 路线

- [ ] 原型验证 `CopyFromBitmap -> Map -> 脏行 memcpy -> ULW` 的 Win7 可行性。
- [ ] 设计持久 staging 的容量分档或只增不减策略。
- [ ] 创建并复用持久 DIB Section/HDC，保证未变化像素跨帧保留。
- [ ] 处理 pitch、BGRA 预乘 alpha、脏区偏移和边界裁剪。
- [ ] 窗口范围变化或提交失败时执行全窗口刷新。
- [ ] 与 Phase 3 端到端比较；只有收益稳定才保留此路线。

## Phase 5：验证和收尾

- [ ] 运行 Bar 包围盒、坐标转换、脏区事务和失败重试测试。
- [ ] 在 Windows 7 SP1 + KB2670838 与当前 Windows 11 ARM64 上验证。
- [ ] 使用 ARM64 MSBuild 编译完整 `InkeysRepo.sln` 的 `Debug | ARM64` 配置。
- [ ] 执行长时间动画、DPI 切换、配置切换、设备重建和显示器变化压力测试。
- [ ] 记录最终选择、性能数据、回滚点和仍待研究的问题。

## 重点文件

- `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp`：当前全屏窗口尺寸和初始位置。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp`：D2D target、GDI 互操作和设备资源生命周期。
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm`：渲染资源接口和容量状态。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：可见包围盒、脏区事务、GetDC 和 ULW 提交。
- Bar 输入/命中测试相关文件：后续研究动态窗口原点变化后再确定。

## 回滚点

- 动态 HWND 与 staging 分成独立阶段；Phase 2 可在不启用 staging 的情况下单独落地。
- 保留现有全脏提交作为正确性回退。
- staging 原型不满足端到端收益时，回退到缩小后的 GDI-compatible target + GetDC 路线。

## 开始实现前的门槛

- [ ] 用户审阅并明确批准最终规划摘要。
- [ ] Deferred Research 中影响行为的决策已记录为确定方案或保守回退。
- [ ] 任务仍处于 planning，直到另一次明确指令授权 `task.py start`。
