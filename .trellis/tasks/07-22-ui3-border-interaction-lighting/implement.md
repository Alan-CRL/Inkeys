# UI3 边框点光追光实施清单

## Implementation

- [x] 增加默认纯色的边框绘制模式，并标记当前指定控件为点光模式。
- [x] 在 `BarUIRendering` 中实现双径向渐变、柔光层、帧内缓存和纯色回退。
- [x] 增加受锁保护的交互光状态、独立时间曲线、最终静态帧和渲染唤醒。
- [x] 接入主按钮、主栏按钮、颜色、画笔、荧光笔按下以及主按钮拖动位置更新。
- [x] 扩大点光边框的脏区外延，保持现有填充、布局和动画不变。
- [x] 将光源缓存和绘制扩展为 Primary、Interaction、Cursor 三种来源。
- [x] 将交互残光和主光脉冲改为受 UI3 动画开关与速度倍率控制的 1.8 秒时间线。
- [x] 注册 UI3 Raw Input 并接入受锁的全局鼠标坐标、序号唤醒和 300ms 启用淡入。
- [x] 动画关闭时清除两个动态光源和主光脉冲，保证重新启用不恢复旧交互状态。
- [x] 非穿透绘制模式下使用动画画笔色绘制主光，并保留与其他来源边框色的混光。

## Validation

- [x] 搜索确认 `Solid` 默认路径、PointLight 启用范围和三种光源上限。
- [x] 检查 1.8 秒时间线、60/40 强度、动画倍率、关闭动画清理和 Raw Input 坐标路径。
- [x] 检查画笔色条件、动画颜色来源及 `SOURCE_OVER` 混光顺序。
- [x] 检查 CRLF/编码、`git diff --check` 和改动范围。
- [x] 运行 Trellis 质量检查。
- [x] 使用 ARM64 Host MSBuild 后台构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，超时不少于 5 分钟。
- [x] 不启动程序，不提交、不归档，输出人工验收项目。

## Previous Validation Result

- 2026-07-22（三光源修订）：ARM64 Host MSBuild 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0。
- 三光源、1.8 秒时间线、60/40 强度、Raw Input、动画关闭清理和画笔色条件的静态检查通过。
- 本次 5 个改动文件均为 UTF-8 无 BOM、CRLF，`git diff --check` 通过；任务保持 `in_progress`。
- 2026-07-22：ARM64 Host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0。
- 源码保持 UTF-8 无 BOM 与 CRLF；`git diff --check` 通过。
- 未启动生成程序、未执行窗口或屏幕操作；视觉与静止 CPU 等待人工验收。

## Risk and Rollback

- 渐变创建或绘制失败时立即回退原纯色边框，避免控件边界消失。
- 若连续渲染未在 1.8 秒后结束，优先检查输入序号消费、动画开关转换和最终帧状态。
- 若 Raw Input 注册失败或窗口外移动无效，仅禁用第三光源并检查注册/客户区转换，不接入传统 `IdtFloating` 钩子。
- 若出现柔光残影，仅调整 PointLight 脏区外延，不扩大整个分层窗口的更新范围。
