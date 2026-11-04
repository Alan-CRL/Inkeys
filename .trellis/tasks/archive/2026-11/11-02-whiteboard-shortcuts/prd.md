# 白板中无法使用快捷键

## 根因（已定位）

- `BarKeyboardShortcutOwner::Poll`（`Inkeys/Inkeys/UI/Bar/Bar.KeyboardShortcuts.h:305`）把 `WhiteboardTransactionActive()` 并入 `modal` 并发布到路由。
- `WhiteboardTransactionActive()`（`Inkeys/IdtState.cpp:1486`）= 白板被请求打开 **或** 白板阶段 ≠ Inactive。白板打开后阶段一直是 `Active`，所以整个白板使用期间 `modal` 恒为真，不只是进入/退出的过渡阶段。
- LL 匹配器（`KeyboardShortcuts.Transport.h` 的 `Matcher::Handle`）在 `route.modal` 时不认领普通组合，按键交还系统；owner 的 `Current()` 也以 `modal` 拒绝。全局 Ctrl+Alt+X 同样被拒绝。
- `ChangeStateModeForShortcutIfRevision`（`IdtState.cpp:2093`）在 `WhiteboardTransactionActive()` 时也返回 false，X 与第二次 Delete 的 Selection 都无法执行。
- 这是 2026-10-19 快捷键任务的刻意设计（“白板未发布能力不因快捷键开放”“模态/白板事务暂停业务”），spec `keyboard-shortcuts.md` 写为“普通白板快捷键暂停”。白板现已开放，需要恢复快捷键。

## 需求

- 白板稳定可见（阶段 Active、请求一致、无工作区切换、未最小化）时，Ctrl+Z、Ctrl+Y、Ctrl+Shift+Z、Ctrl+Delete（清空当前白板页）按普通批注规则生效。
- 白板进入/退出过渡、加载中、最小化期间仍暂停（消费/放行规则沿用现有 modal 语义）。
- Ctrl+Q 在白板下维持“可用性不足时消费但无动作”（Freeze 在白板不可用）。
- 白板菜单 Escape 路径不受影响。

## 已确认决策（2026-11-02）

- 白板中 Ctrl+Alt+X、第二次 Ctrl+Delete 与主栏“选择”按钮一致：批注⇄Selection 往返；分离式在白板内也不清空，直接进入 Selection。

## 追加：翻页控件进入/退出闪烁（2026-11-02）

- 现象：白板翻页控件与 PPT 四个翻页控件在进入/退出时文字闪烁、背景渐变来回。
- 根因：淡入/淡出途中按退场/交接中的页状态重建内容（页码→加载中→页码、PPT 结束页码 -1），按钮启用/禁用往返。
- 修复：淡出期间冻结内容；工作区交接期的 switching 不改变可见内容，仅锁输入。

## 验收

- [x] 白板中 Ctrl+Z/Y/Shift+Z/Delete 对当前白板页生效；切页后作用于新页。
- [x] 白板中 Ctrl+Alt+X 与第二次 Ctrl+Delete 行为与主栏选择按钮一致（分离式不清空）。
- [x] 白板进入/退出过渡与最小化期间不执行。
- [x] 菜单 Escape、PPT/桌面现有行为不回归。
- [x] 白板/PPT 翻页控件进入、退出过程中文字与背景不再闪烁（透明度单调、淡出内容冻结）。
- [x] spec `keyboard-shortcuts.md` 同步；完整解决方案构建与 headless 测试无新增失败。

## 追加：动画速率入口迁移（2026-11-02）

- 用户已实测确认白板快捷键与翻页控件闪烁修复通过，并要求完成归档提交。
- 将动画速率从常规外观页移到实验选项页；保留动画总开关、原配置/i18n 键、0.1–5.0 倍范围、0.1 取整、实时预览及松手/离页/隐藏保存。
- 修改边界：Setting.Pages.cpp/.h 移除旧行与事件；Setting.cpp 承载实验页控件和保存；setting_design_tests.cpp 同步移除退休事件引用；rendering-and-ui.md 同步入口合同。
- 验收：常规页无动画速率、实验页仅在动画开启时显示；关闭动画不覆盖保存速率；完整 Debug|ARM64 构建及无窗口验证。

- [x] 动画速率入口迁移完成，现有实际布局断言已更新；ARM64 构建、完整无窗口测试、状态生产测试、分页离屏回归和 i18n 检查通过。证据见 `validation.md`。
