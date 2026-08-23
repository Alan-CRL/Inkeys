# UI3 主栏拖动闪动与缩窄残影修复

## Goal

修复 UI3 主栏在屏幕边缘拖动、底栏二维吸附和居中底栏缩窄过程中偶发的卡顿、单帧闪回、无动画换向、脏区残影以及悬停失效，使拖动、呈现和命中始终消费同一份成功几何。

## Background

- 本任务是既有底栏二维吸附与居中根节点修复的独立后续任务，不重开或改写 `08-14-ui3-bar-bottom-dock-elastic`、`08-20-ui3-bar-bottom-dock-feedback` 或已归档的 `08-23-ui3-centered-bar-collapse-animation-root-fix`。
- 用户截图显示居中底栏缩窄后调试红框沿旧位置形成连续残影，蓝色 HWND 边界仍保留过大的消息接收区域。
- 现有呈现合同要求拖动位移、两轴吸附、viewport/source/size、dirty 和命中快照只在完整成功事务后共同推进。
- 静态证据表明：松手状态可早于直移吸收被渲染；过期渲染帧会回用自己的旧位移覆盖已经直移的 HWND；viewport 映射变化未统一选择整窗 ULW；主体 X/Y 命中仍存在分别读取快照的路径。

## Requirements

### R1. 拖动与松手换向连续

- 主按钮拖至显示器边缘、展开主栏超出屏幕时，拖动循环不得阻塞渲染线程或形成忙等卡顿。
- 松手后的第一帧必须先把最后一次实际直移吸收到布局根，再执行 `PositionUpdate()` 和既有换向关键帧；禁止在位移尚未吸收时先呈现释放态。
- 普通底栏换向继续沿用既有“收窄到主按钮宽度后镜像展开”的动画，不改变判定规则、曲线、时长或阈值。

### R2. 底栏二维吸附无单帧闪回

- 渲染帧计算后若拖动 serial 已更新，ULW 不得把 HWND 从交互线程已经提交的屏幕位置移回旧位置。
- 过期帧只允许在当前实际 HWND 位移上呈现其自洽几何；捕获/脱离屏障仍由成功呈现 serial 控制，不能提前移动尚未提交的新形态。
- 缓慢改变 y 并进入/离开底栏、在同一手势叠加水平居中捕获时，主按钮与主栏的屏幕位置逐帧连续。

### R3. 缩窄、脏区和窗口范围一致

- `pptSrc`、`psize`、target capacity、device generation 或 viewport 映射相对成功快照改变时，统一执行整窗 ULW 替换，`prcDirty` 必须为空。
- 居中底栏主栏缩窄及其最终 idle 收窗必须清除旧内容、旧调试红框和旧 HWND 外围，不留下截图所示的竖向红框残影。
- 成功提交后，viewport、present mapping、窗口屏幕范围、dirty、调试覆盖和命中快照必须共同推进；失败帧继续全量重试。

### R4. 命中坐标使用单一成功快照

- 一次主体或刚性抓手命中必须从同一个 `BarBottomDockPresentedSnapshot` 同时逆映射 X/Y，不得分别读取两个可能跨 serial 的快照。
- 窗口缩窄或移动产生的同屏幕坐标消息继续受既有 hover 抑制；发生一次真实物理屏幕坐标移动后立即恢复悬停，无需先离开旧消息接收区域。
- 消息在线程入队时继续固化为 monitor-local layout 坐标，不恢复 client 坐标的异步解释。

## Constraints

- 不改变 20 DIP 竖向吸附阈值、40 DIP 居中阈值、24 DIP 视觉上限、弹簧参数、主栏换向规则或动画曲线。
- 不新增配置项、国际化键、窗口、渲染后端或额外 GDI/ULW 提交链。
- 只修改 UI3 Bar 和对应 Headless 测试；不修改 UI2/IdtFloating、Draw3、PPT 控件或第三方代码。
- 保持原文件编码与 CRLF，关键并发/呈现步骤写简短中文注释；不启动可见 GUI，不创建 commit。

## Acceptance Criteria

- [x] Headless 覆盖释放态在直移吸收前必须延迟、吸收后才允许布局换向。
- [x] Headless 覆盖当前帧、过期帧和发布中帧的屏幕位移解析，过期帧不把实际 HWND 拉回旧位置。
- [x] Headless 覆盖 viewport/source/size/capacity/device 任一映射变化均选择整窗替换，稳定 tuple 才允许局部 dirty。
- [x] Headless 覆盖主体与抓手 X/Y 使用单一成功快照的组合逆映射，并保留非恒等横纵映射精度。
- [x] 现有全部 `InkeysHeadlessTests.exe --no-window` 通过，`git diff --check` 通过。
- [x] 使用 ARM64 host `MSBuild.exe` 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`，超时不少于 5 分钟。
- [ ] 自动验证不启动可见 GUI；屏幕边缘拖动、慢速二维吸附、截图残影和真实 hover 结果由维护者手工复核后再确认视觉缺陷关闭。

## Out of Scope

- 不重构全局动画类型、Bar 整体输入架构或 RenderPipeline 调度器。
- 不调整调试覆盖层颜色、文字、阈值或用户可见样式。
- 不处理与本复现无关的触摸指示器产品交互改版。
