# Redo 2×2 点击暂态扩宽修复

## 根因与最小修复

Interaction 点击后的默认 UpdateRendering 触发 StateUpdate→PresetHoming；完整模式先临时写 Recall.hide=false，再调用 ApplyRecallChromeVisuals 重新隐藏。RenderPipeline 的独立渲染线程扫描活动按钮集合，若采到中间状态，原 2×2 Redo 后会多一枚 2×1 Undo，增加 75 DIP 列。Undo 本来已可见，没有同样的暂态。

生产只在 Bar.Button.cpp 删除 PresetHoming 两次 Recall 中间显隐写入并添加中文注释。保留最终 Recall 投影、选择空页精简门禁、命令/属性身份、3秒保护及正常布局动画；不改窗口宽度或通用隐藏按钮网格。

## 验证

- 完整 InkeysRepo.sln Debug|ARM64 构建：VS ARM64 原生 MSBuild，同一 PowerShell invocation 规范化PATH；退出0，0error，3既有warning，29秒。日志 Build/recall-width-debug-arm64-20261012.log。
- 新 InkeysHeadlessTests.exe --no-window：退出0，stderr空，PASS animation correctness，RecallAttribute/EraserAttribute failures=0。日志 Build/recall-width-headless-20261012.log。
- 使用前轮获批沙箱外环境，构建10分钟预算，测试5分钟预算；Hidden，不启动产品GUI，不改工程/依赖。
- 回归覆盖Redo-only→点击→availability→popup Undo→dismiss→末步灰色→workspace→3秒Compact的可见集合/尺寸单列策略，以及实际PresetHoming投影前无临时显隐写入。源级断言独立对照：原HEAD69a1e3f7失败、当前源码通过。
- 独立trellis-check通过本轮窄范围修复；git diff --check通过，原UTF-8 BOM与CRLF保持。
- 没有真实HWND/GUI并发呈现验收；纯策略列占用与源级检查不等价于实时窗口宽度采样。合法settle逐字段发布为未改动/未实测边界，不能视为本轮已复现的额外缺陷。
- 本轮没有commit/push，原任务继续保留。

## 用户验收补充

用户已确认本次主栏宽度问题修复，并授权提交本轮 commit；明确要求任务继续保持 in_progress，不结束、不归档。此为用户反馈，不是代理 GUI 测试结果。
