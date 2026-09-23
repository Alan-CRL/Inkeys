# 实施与验证

1. 在 Draw3 主光标纯逻辑中加入 Touch 有效归属和系统箭头显隐判定，并补回归断言。
2. 在 `WindowController` 中沿 RTS/Pointer Touch Down 设置暂时接管；沿可信 Mouse/Pen 输入恢复；动态读取 Win8+ 鼠标消息来源，保留 Win7 回退。
3. 核对窗口线程到绘制线程的请求唤醒、Touch Pan 真实 Mouse 接管、Pen Hover/Up 与多指 Up 的路径；不改触点圆环或擦除几何。
4. 运行 `git diff --check`、ARM64 原生 MSBuild 构建 `InkeysRepo.sln` 的 `Debug|ARM64`，再运行相关 `InkeysHeadlessTests.exe --no-window` 与 `Inkeys.exe --draw3-hidden-test`，只做非 GUI 验证。
5. 根据验证结果更新任务验收与相关 Trellis 规范；检查最终 diff 的编码、CRLF 和范围。不提交、不推送。
