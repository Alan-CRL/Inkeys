# 2026-10-12 追加检查：3 秒保护与弹窗真实身份

## 范围

- 依据 prd.md/design.md/implement.md 顶部本轮追加要求；3 秒覆盖前一轮 5 秒合同。
- 在同一未提交工作区上复核本轮增量，不撤销前一轮修复。源码只读检查，不重复构建，不开 GUI，不 commit/push。
- 本轮聚焦 RecallChromeIdleSeconds、真实面板内容提交/推进、相关注释与既有 Recall 回归。

## Findings (fixed)

- 实现已补齐原缺失的属性按钮 icon/name AdvanceContentTransition。该按钮不属于主栏注册表，原先 TransitionContent 只设置目标，没有进入主栏的推进路径，Draw 消费 val 时仍可显示旧 Redo。
- 命令身份切换依同一 chrome.popupShowsRedo 同帧更新 preset、InitializationFromResource 与 SetStringImmediate。已追踪 Bar.UI.cpp：SVG 即时入口 CancelContentTransition -> ApplyContentDirect -> svg.Initialization -> ResetCache；Word 即时入口在 ApplyBarImmediateContentUpdate 事务内取消旧关键帧并同时初始化 val/tar/pending，恢复内容倍率/透明度。没有只改目标而遗漏真实显示提交。
- 同身份普通内容变化仍调用 TransitionContent，然后分别执行 icon/name AdvanceContentTransition(dt,speed)。两路调用没有通过短路表达式跳过其一；changed_ 和 active_ 均合并结果，在 keyframe/结束提交前保持续帧。

- 检查发现 Close 旧注释仍写“5 秒保护”；主代理已在构建结束后仅将注释改为 3 秒，独立复核确认。该修正不改变编译后的运行逻辑。

## Findings (not fixed)
- 未发现未修复的源码行为问题或需要扩大任务范围的设计/接口改动。
- 新源级断言要求仓库源码可访问；未执行真实 GUI/HWND 呈现与点击验证，不以源级检查代替视觉验收。

## 独立复核证据

- RecallChromeIdleSeconds=3.0 是唯一 Undo/Redo 等待来源；deadline、WM_TIMER 毫秒间隔和 RecallChromeIdleElapsed 共享它。
- 既有边界回归改为 static_assert(3.0)、2.99 秒仍保护、3.0 秒可调整；之前连点/灰色/关闭/工作区/可用性等待策略保持。
- 通用 BarButtonHoverFadeDurationSeconds 仍为 5.0，BarBorderCursorGraceDurationMs 仍为 5000，相关文件没有被本轮修改。
- popup action 继续读同一布局策略的相反命令；此前当前主栏锚点/可见性快照修复保留。
- 即时 SVG/Word 提交仅影响内容身份；面板 Motion、几何和颜色最终目标调用保持，禁用变灰仍使用原真实颜色动画。
- 既有源级回归新增相反身份 mapping/preset/SVG/Word 提交、两路 Advance+active、真实即时内容 helper 的 val/cache 提交链检查。它补生产入口接线，不能证明真实 D2D 呈现。
- 独立 Python 静态断言覆盖 3 秒共享计时源、即时/动画内容接线、原 5 秒 hover/grace，结果通过。
- git diff --ignore-submodules=all --check 通过。原 UTF-8 BOM 状态与 CRLF 约束沿用，不做无关格式化。

## Verification

- Lint/静态：通过 diff --check 与本轮生产接线静态断言；此 C++ Solution 无已配置的独立 lint 命令。
- TypeCheck/编译：通过。主代理本轮完整 InkeysRepo.sln Debug|ARM64 构建退出 0、0 errors、130 既有 warnings，耗时 1 分 28 秒；日志 Build/recall-3s-debug-arm64-20261012.log。
- Tests：通过。主代理本轮新生成 InkeysHeadlessTests.exe --no-window 退出 0、stderr 空，stdout 结尾 PASS animation correctness；RecallAttribute 与 EraserAttribute 均 failures=0。日志 Build/recall-3s-headless-20261012.log。GUI 未运行，源级断言要求仓库源码可访问。
