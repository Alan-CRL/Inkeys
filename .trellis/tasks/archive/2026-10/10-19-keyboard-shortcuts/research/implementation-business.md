# Bar 与工具状态快捷键实施记录

2026-10-19，用户已批准实施。本 agent 只修改 IdtState.h/.cpp、Bar.Initialization.cpp、Bar.Interaction.cpp、Bar.Main.cppm 及私有 Bar.KeyboardShortcuts.h；没有修改鼠标 Hook/Raw/WM_TOUCH、配置/设置、PPT COM 或执行 GUI/commit。

## 已实现
- 新独立键盘服务启动前装入六个 Inkeys3 开关；渲染启动失败及退出时先 Stop producer，交互线程退出时退休事务和自己的 lease。
- Bar 私有 owner 为唯一业务消费者。嵌套 TryRead 只有限收包并走现有 WM_CANCELMODE 私有标记取消手势；实际 Dispatch/Drain 只在 Run 顶层，防止关闭仍被嵌套引用的控件。
- 普通快捷键只用于批注。文本优先使用实际前台 Setting HWND/其 child，而非设置窗口只要可见就全部屏蔽。全局 X 允许设置普通编辑，模态/白板事务暂停业务。
- 捕获稳定 canvas scope、本地 scene/route epoch、服务/配置/前台/模式身份；外部变化退休旧包/回执，自己的连续 X 允许具名序列衔接。
- 合成式 X 退出无 Clear；分离式 X 租约→Clear→真实 Applied/NoContent 回执→条件 Selection→仅释放自己的租约。失败退休后继 X，返回恢复 lastAnnotationMode（Pen/Shape/Eraser，保留 Laser、子型和宽色）。
- IdtState 快捷键专用条件内核不调用 PPT 真退出 SelectionIfRevision。定格关闭按捕获 revision，旧事务不能关闭较新的定格意图。
- Delete 单击发布 Clear 并保持工具，第一击不长期阻止绘制；第二击同 Ctrl/gesture 不重复 Clear。第一击具名成功后，Acquire(具名 contentRevision)→只读 Controller fence→空态回执→条件 Selection，消除已准入 contact 的空态竞争。CtrlUp 在两击捕获后不撤销，另一终键/额外修饰通过独立 interruptionGeneration 退休。
- Undo/Redo 同页 FIFO 使用稳定 scope，不使用 Clear 会改变的内容 generation。

## 验证
- `git diff --check` 通过（ignore-submodules，避免只读子模块元数据权限错误）。原有文件与新头已检查 CRLF；保留各原文件 BOM 状态。
- 新增真实 IdtState production 测试：两种样式 × Laser/Shape/Eraser，恢复完整属性、过期模式拒绝、不写 PPT 真退出标识、定格 captured revision 与较新意图。
- 既有 `--state-mode-production-test` 增加 Bar owner production probe。探针共享同一 Dispatch/Drain 模板和真实固定回执槽，仅替换异步 Host 完成/输入 gesture 与 UI 刷新，不安装服务、创建 HWND 或访问 GUI。
- Owner probe 覆盖合成式两个 X 无 Clear、分离式两个 X 排序且仅一次 Clear、等待真实清空结果、CtrlUp 后第二击经过 fence 且不再 Clear、Blocked/Stale/Failed 退休依赖切换、第一击后新笔迹拒绝旧 Selection、另一终键取消、第一击后松 Ctrl 取消候选、Clear→Undo、迟到完成不覆盖新工具/新页面。
- 本 agent 未执行完整编译或探针运行；由 root 汇总后按完整 Solution Debug|ARM64 执行，不能把以上新增用例描述为已运行通过。

## 待人工边界
真实系统 Hook/全局热键冲突与 UIPI、Win7、国际键盘/AltGr，以及实际鼠标/触摸长手势中的键盘取消仍需产品人工验收。无窗口探针不证明系统送达或光标延迟。

## Fresh 检查修复
- 在 Drain 每次认领前刷新同一 canvas context，并在 SameDelete 前退休内容已变化的候选；同一轮收包看到新笔迹和 Delete 时，将该 Delete 作为新第一击 Clear，不丢键。新增真实 owner 回归，预期 Clear 次数 2、工具保持批注、无租约。
- Selection 的 Host 交接短暂关闭 available 时，保留已捕获的后继 X，等待同一稳定作用域恢复后再消费。Intake 身份核验与执行可用性分开；无跨页重认领。新增 pause-on-release 回归，确认先 Selection/队列保留，再恢复最近工具且仅一次 Clear。
