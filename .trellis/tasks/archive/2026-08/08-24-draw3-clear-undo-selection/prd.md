# Draw3 清空撤回与选择语义

## Goal

把 Clear 从“永久截断当前页”改为可撤回、可重做的历史操作，并让清空按钮按内容状态承担“清空”或“进入选择”的两步语义。用户清空后可以恢复之前的墨迹；绘制且有内容时双击清空可以稳定完成“清空并进入选择”。

## Background

- 当前 `DrawingController` 的 Clear 会删除当前 Canvas Stroke、替换整套 runtime history，并使之后 Undo/Redo 成为空操作。
- 当前主栏只在“选择 + 无内容”时精简；Clear 命令和 Draw3 内容状态通过异步 bridge/observer 往返。
- 现有双击 continuation 会把第二击继续交给第一次命中的主栏按钮；300ms 点击合并仅用于展开/收起，不覆盖 Clear。

## Requirements

- R1：有内容的当前页执行 Clear 时，保留 `InkCanvas` 中的 Stroke 和 runtime history，把清空追加为当前页的一条历史操作；不得创建新页或划分文件。
- R2：`Stroke A/B -> Clear -> Stroke C` 的撤回顺序必须是 `C -> Clear -> B -> A`，重做顺序对称；Undo Clear 恢复清空前内容，Redo Clear 再次清空。
- R3：空内容时 Clear 为 no-op，不追加连续空 Clear；Undo 后执行有效 Clear 或新增 Stroke 都建立新分支并丢弃旧 redo。
- R4：Clear 仍清理当前呈现中的 L0/L1/L2、Laser、粒子、光标和瞬态恢复状态，但保留当前 Canvas viewport、其他页面及历史 CPU 真值。
- R5：当前页内容状态表示 Clear 边界后的可见内容：尾部为有效 Clear 时为无内容，之后追加或重做 Stroke 时为有内容；Clear、Undo、Redo 只在布尔值变化时推进 content revision。
- R6：有内容时单击清空只发布 Clear，保持当前模式；无内容且处于非选择模式时单击清空进入选择模式；选择且有内容时单击清空后保持选择并随空内容回报进入精简主栏。
- R7：绘制且有内容时双击清空必须稳定执行“Clear 后进入选择”。第二击不能依赖内容回报是否已经到达；只有第一击 Clear 返回 `Accepted` 才允许第二击直接进入选择，发布失败时第二击重试 Clear。
- R8：Clear 不接入 toggle 点击合并；白板保持现有拖动/完整布局语义，但共享可撤回 Clear 历史能力。
- R9：产品与 standalone Draw3 的 history/GPU replay 核心保持算法同步；产品 `DrawingController` 接入完整命令事务。
- R10：同步更新 Draw3 runtime/history 与产品集成规范，删除 Clear 永久截断的旧合同。

## Acceptance Criteria

- [x] 有墨迹页 Clear 后画面为空、内容状态为 false；Undo 恢复原墨迹并变为 true；Redo 再次清空并变为 false。
- [x] `A/B -> Clear -> C` 可逐步 Undo/Redo，顺序和每一步画面都正确；Clear 后的新分支不会复活旧 redo。
- [x] 空页重复点击清空不增加历史深度或 content revision；其他页内容和当前页 viewport 不受影响。
- [x] 选择有内容单击清空后保持选择并收缩主栏；绘制空页单击清空进入选择；绘制有内容双击清空稳定清空并进入选择。
- [x] 第一击 Clear 为 `QueueFull` 或 `NotRunning` 时，第二击不会错误进入选择。
- [x] CPU history/UI 决策测试、隐藏 HWND Clear/Undo/Redo 测试通过。
- [x] ARM64 host 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`，通过 `InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-hidden-test` 和 `git diff --check`。
- [x] standalone Draw3 history tests 构建并通过；不启动可见窗口。

## Out of Scope

- `.uink` 文件保存、Clear 后落盘、释放内存及从文件恢复。
- Clear 是否在未来划分文件或页面。
- 可见窗口人工交互、其他未定义的 Draw3 杂项。
