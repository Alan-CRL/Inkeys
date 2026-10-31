# 本轮追加验证：3 秒等待与属性命令身份

- 用户明确仅 Undo/Redo 布局、弹窗空闲与连点保护由 5s 改为 3s；共用 RecallChromeIdleSeconds=3.0，deadline/WM_TIMER 使用同一值。通用 hover 5s 和边缘光 grace 5000ms 保留。
- 原属性 TransitionContent 只发布目标，但未推进 icon/name 内容关键帧，Draw 继续读初始化的 Redo 值，缓存也未失效。现身份改变同步 preset，使用既有 SVG InitializationFromResource 和 Word SetStringImmediate 同帧提交，取消旧关键帧并使 SVG 缓存失效；同身份普通内容变化继续 TransitionContent，两路 AdvanceContentTransition 均独立调用并保持 changed/active 续帧。主栏 Redo 对应属性 Undo，反向对称，几何/透明度动画不改。
- 完整 InkeysRepo.sln Debug|ARM64：当前 VS ARM64 原生 MSBuild，进程环境规范化 PATH，10 分钟预算，沙箱外获批执行；退出 0，0 error，130 既有 warning，耗时 1分28秒。日志 Build/recall-3s-debug-arm64-20261012.log。
- 新生成 InkeysHeadlessTests.exe --no-window：获批沙箱外执行，5分钟预算，退出 0，stderr 空，PASS animation correctness，RecallAttribute/EraserAttribute failures=0。日志 Build/recall-3s-headless-20261012.log。
- 回归包含 2.99/3 秒边界、生产显示接线、真实即时内容事务和 SVG 缓存失效源级断言；复用既有 animation_tests 内容事务/关键帧回归。实际 D2D/HWND 视觉、点击验收未运行，源级断言需要能访问仓库源码。
- 独立 trellis-check 通过，唯一机械注释遗漏已在编译结束后修复为3秒，不改变编译出的行为。git diff --check 通过，BOM/CRLF 保持；不 commit/push，不启动 GUI。此前未提交修复保留。
