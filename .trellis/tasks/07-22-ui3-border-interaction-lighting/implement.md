# UI3 边框点光追光实施清单

## Implementation

- [x] 增加默认纯色的边框绘制模式，并标记当前指定控件为点光模式。
- [x] 在 `BarUIRendering` 中实现双径向渐变、柔光层、帧内缓存和纯色回退。
- [x] 增加受锁保护的交互光状态、独立时间曲线、最终静态帧和渲染唤醒。
- [x] 接入主按钮、主栏按钮、颜色、画笔、荧光笔按下以及主按钮拖动位置更新。
- [x] 扩大点光边框的脏区外延，保持现有填充、布局和动画不变。

## Validation

- [x] 搜索确认 `Solid` 默认路径与 PointLight 显式启用范围。
- [x] 检查交互入口、最多两个光源、450ms 衰减和动画配置隔离。
- [x] 检查 CRLF/编码、`git diff --check` 和改动范围。
- [x] 运行 Trellis 质量检查。
- [x] 使用 ARM64 Host MSBuild 后台构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`，超时不少于 5 分钟。
- [x] 不启动程序，不提交、不归档，输出人工验收项目。

## Validation Result

- 2026-07-22：ARM64 Host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64` 成功，退出码为 0。
- 源码保持 UTF-8 无 BOM 与 CRLF；`git diff --check` 通过。
- 未启动生成程序、未执行窗口或屏幕操作；视觉与静止 CPU 等待人工验收。

## Risk and Rollback

- 渐变创建或绘制失败时立即回退原纯色边框，避免控件边界消失。
- 若连续渲染未在 450ms 后结束，优先检查最终帧状态和交互时间戳，不改动现有 UI 动画时间轴。
- 若出现柔光残影，仅调整 PointLight 脏区外延，不扩大整个分层窗口的更新范围。
