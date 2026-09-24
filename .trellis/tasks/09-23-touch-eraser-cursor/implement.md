# 实施与验证

1. 在 Draw3 主光标纯逻辑中加入 Touch 有效归属和系统箭头显隐判定，并补回归断言。
2. 在 `WindowController` 中沿 RTS/Pointer Touch Down 设置暂时接管；沿可信 Mouse/Pen 输入恢复；动态读取 Win8+ 鼠标消息来源，保留 Win7 回退。
3. 核对窗口线程到绘制线程的请求唤醒、Touch Pan 真实 Mouse 接管、Pen Hover/Up 与多指 Up 的路径；不改触点圆环或擦除几何。
4. 运行 `git diff --check`、ARM64 原生 MSBuild 构建 `InkeysRepo.sln` 的 `Debug|ARM64`，再运行相关 `InkeysHeadlessTests.exe --no-window` 与 `Inkeys.exe --draw3-hidden-test`，只做非 GUI 验证。
5. 根据验证结果更新任务验收与相关 Trellis 规范；检查最终 diff 的编码、CRLF 和范围。不提交、不推送。
6. 在实验选项新增独立 Cursor 控制台开关、配置默认值和三语文案；启动时按现有 Debug 控制台规则生效。
7. 从窗口/RTS 回调到绘制帧接入有界光标事件队列，输出输入过滤、归属、系统光标及最终视觉来源，不改现有光标行为。
8. 用完整 ARM64 Debug Solution、`InkeysHeadlessTests.exe --no-window`、i18n `check` 和 diff/编码检查验证；等待用户复现日志再继续根因修复。
9. 核对用户三次触摸日志的 Touch Up → 来源未知原位 MouseMove → 主悬停圆环链，补窗口侧末触点位置判定和边界测试；保留诊断输出供用户复测，任务继续进行中。
