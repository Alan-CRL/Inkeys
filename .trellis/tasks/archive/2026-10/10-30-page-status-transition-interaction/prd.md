# 统一 PPT 与白板分页状态动画和按钮交互

## Goal

为 PPT 恢复状态和白板冷页加载增加可打断的主栏式文字过渡，普通页码保持即时更新；统一 Up 后 hover 抑制，补齐白板三语言加载提示；白板翻页像 PPT 一样立即收尾当前墨迹并切入目标页。

## Requirements

- 普通数字页码到普通数字页码继续即时更新，不为正常翻页增加动画延迟。
- PPT recovery 状态与白板 `switching` 冷页加载状态使用主、次页码文字槽显示本地化状态；数字与状态之间、不同状态之间使用主栏现有 `TransitionToString` 中点过渡，主、次文字同步。
- 过渡必须支持中点前/后打断、相同目标不重复重启、即时数字取消旧中点回写，并服从全局动画开关和速度。
- 白板加载使用新的三语言 `Loading` 文案，复用现有 `Wait`；按现有 i18n sync/check 链生成，不手改生成头。
- 继续复用 `BarButtonListClass`、`AdvanceBarButtonVisual`、`DrawBarButtonVisual` 和命中/光影，不新建第二套按钮树或绘制实现。
- 抽取并应用主栏已有的 Up 后 hover 抑制语义：Up 清除 pressed/hover；同屏幕坐标的后续 Move 不立即恢复 hover，真实坐标变化后重算；不得改变 PPT 长按、拖动和页面业务门禁。
- 白板首个翻页请求立即关闭新输入并复用 PPT 的 contact 封口，使 still-down 墨迹在旧页完成后启动翻页；旧 Move/Up 不得写入目标页，目标页真实就绪前不得接受新墨迹。
- 保留单页面事务串行、冷页加载完整性、保存和失败暖页规则；本迭代不扩展多冷页 latest-wins 重定向。
- 增加能证明上述行为的现有测试体系回归，并完成构建、目标测试、完整无窗口测试和 Trellis 质量检查。
- 本任务不扩大为普通 PPT `Pending/BasicControl` 全部显示加载，不实现白板读取重试，不修改存储性能。

## Acceptance Criteria

- [x] PPT recovery 和白板 loading 均通过共享按钮主、次文字渲染显示，并以可打断动画进入/退出。
- [x] 普通 PPT/白板数字翻页保持即时更新，无内容过渡动画。
- [x] 白板 `switching` 在三语言下显示加载/稍候，i18n check 通过。
- [x] Up 后同坐标 Move 不恢复 hover，真实移动可恢复；pressed/pressScale 正常释放，PPT 既有业务交互不回退。
- [x] 中点前/后打断、禁用动画、即时更新取消旧过渡和最终页码正确性有自动化覆盖。
- [x] 白板 still-down 翻页无需等待物理 Up 即封口并启动页面事务；旧输入不污染目标页，暖页/冷页仅在真实就绪后恢复新输入，no-op 不锁死。
- [ ] 完整 ARM64 Solution 构建、相关离屏测试、完整 `InkeysHeadlessTests.exe --no-window` 和 `git diff --check` 通过；真实 GUI 未执行项明确列出。（构建与目标回归已完成；追加全量 no-window 两次达到 300 秒工具上限，尚未取得本轮最终 PASS。）
