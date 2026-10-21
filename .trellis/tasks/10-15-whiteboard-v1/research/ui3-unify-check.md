# UI3 白板统一修订：独立检查

日期：2026-10-15。检查角色：trellis-check。与实现 owner 协调，共享源文件在 owner 释放前仅只读检查；构建和测试由主会话统一执行，不运行 GUI。

## 已发现并由实现 owner 修复

- `Bar.Scene.cpp`：弹出按钮绘制应用父子姿态，而命中仍用原始坐标。命中已改为同一组合姿态、圆角和继承透明度。
- `Bar.Scene.cpp` / `PageControl.cpp`：4×1 行文字的测量被标准 metrics 覆盖，且调用方没有左对齐标记。现先应用 metrics，再按共享字号测量左对齐。
- `Bar.Scene.cpp`：PPT 启动配置保留的稳定隐藏按钮切换为 panel/divider 时，没有初始化 chrome，可能保持透明填充。现提取 ApplyPanelChromeLocked，在初始化和 TransitionLayout 均应用。
- `Whiteboard.Theme.h` / `Bar.Scene.cpp`：预览缓存持有旧 device context，隐去后设备恢复可能延后释放。共享回调释放入口已接入 ResetWidgetDeviceCachesLocked，覆盖显式释放、目标重建和设备代变化。
- `PageControl.cpp`：内容更新原本全量布局并延长输入锁。现分离 UpdateContent，纯主题/选中状态不改 popup timeline 或布局期限。
- `Bar.EraserAttributeMotion.cppm` / `PageControl.cppm`：静态 magic number 包络已替换为共享曲线极值与父子姿态组合推导的分配包络。

## 后续复核及安全修复

- `BarSurfaceScene::PresentationBoundsLocked` 现在只包围当前背景与可见的组合姿态控件；保守分配预算与实际 HWND viewport 分离。渲染、damage、主栏共享光照和 presentation→logical 的 X/Y 偏移使用同一包络。PageControl 在取得 viewport 前 Advance，该帧 Render 通过帧身份去重。已检查负屏幕原点与 DPI 的坐标方向。
- `PopupAnimationActive` 只保持弹出退场所需的坐标系；不再被普通按钮五秒 hover fade 保留大 HWND。
- `UpdateContent` 已补齐变更后的共享 invalidate/wake；只更新选中或主题不会重新启动布局或面板时间线。
- popup 时钟直接复用 `FrameAnimationClock`，idle 后第一帧不消耗休眠时间。没有在业务层增加几何计时器。
- 实现 owner 同时将 4×1 图标后文字间隔修正为共享 5 DIP。
- 检查者在 `RunOffscreenTests` 添加真实 Scene 回归：PPT 稳定实例→白板，父子共同展开，0.5/1/2 倍动画速度、0.75/1.5 倍 DPI、负屏幕原点，逐帧矩形严格对照生产 EraserSurfaceMotion；同一帧 Advance+Render 去重，实际 viewport 包含组合子面板，裁剪坐标转换，展开中内容修改不重启，禁用动画即刻收起并回到主体。
- 像素检查增加绿色 colorSelect SVG 的约 22 DIP 可见绿圆包围盒（30 DIP 图标框）、三个标准 2×2 样式标签和纹理中心对称；另读回从 PPT 转换后的面板与分割线，避免只验证 spec 标志。

- 最终复核发现纯白预览上的白色样式标签不可读。实现 owner 已用既有亮度阈值选择共享 Light/Dark TextPrimary，通过共享 contentColor 与颜色动画推进，保留相同标签槽/字重及原 SVG。补充真实纯白预览的深色标签像素回归。

- 首轮新增回归找到真实 chrome 退场清理问题：TransitionLayout 的 StopBarButtonHoverVisual 在 chrome 初始化后清零面板/分割线 pct。owner 已把静态 chrome 应用移动到指针/hover 清理之后；保留实际像素回归。
- 同轮严格姿态比较的 174 处失败为测试初始条件不一致：首次禁用帧的 SVG/Word 内容替换仍报告 changed/active，尚未进入 idle；参考时钟却已显式 Suspend。测试改为两帧禁用结算，并断言 Scene 已 idle 后再启动弹出，保留每帧精确姿态断言和首个失败诊断，不放宽容差。

## 未修复问题

目前没有待修复的已证实源码问题。新增自动回归须以主会话最终执行结果为准；真实 UI 动画手感和帧率没有采样，不能声称已实测其流畅度。

## 验证边界

- Lint：`git diff --check` 通过（PRD 工作区 LF 有 Git 的 CRLF 提示）。
- TypeCheck / Build：主会话完整 ARM64 solution 最终构建通过，`ui3-unify-build-checked.log` 为 0 error / 3 个既有 hashlib++ warning。
- Tests：`ui3-unify-offscreen-checked-stderr.log` 为 `[PageControlScene] failures=0`，新增姿态、viewport、像素、资源与主题对比检查均通过。主会话统一检查进程退出码及最终 Headless --no-window，详见父任务 validation.md。
- 离屏像素和确定性动画回归不等于实际 HWND、任务栏、PPT 或帧率验收。
