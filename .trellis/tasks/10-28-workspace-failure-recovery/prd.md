# 修复三态切换失败补偿与 DComp 生命周期

## Goal

在不改变正常 Desktop/Presentation/Whiteboard 切换和文档语义的前提下，修复此前审阅确认的四类失败：源场景补偿不完整、非原生等待永久 Pending、DComp 无法可靠降级 ULW，以及 DComp 退休等待可无限阻塞。

## Background

- 当前 Window 回滚只恢复原生状态，退出 Whiteboard 时已 inactive 的背景和分页不会恢复，形成物理白板与逻辑半退出并存。
- 只有 Prepare/Complete 原生失败进入四次预算；背景 ACK、Draw3 ready 和 PPT UI-ready 可以无限等待。
- DComp 绑定后的窗口可能无法清除 `WS_EX_NOREDIRECTIONBITMAP`，直接在原 HWND 初始化 ULW 会失败。
- `DirectCompositionPresenter::Reset()` 的 `WaitForCommitCompletion()` 没有期限，可阻止恢复和受保护退出。

## Requirements

- 仅在真实回滚完成后恢复 Whiteboard 源预览；恢复屏障未退休时不得显示源背景或开放输入。
- 背景 inactive、Draw3/PPT ready、背景 prepared/final active 的无进展等待必须形成可重试故障，而不是无限 Pending；实际进度必须重置等待预算。
- DComp 退休必须有界。超时只报告真实失败，不得启用 ULW/新 HWND 或伪造恢复成功。
- 能清除 DComp 样式时沿用原 ULW 回退；不能清除时复用 legacy-compatible 窗口链重建，不得让旧 DComp 与 ULW 共用 HWND。
- 重启后必须重新取得匹配的工作区、PPT target、Host generation、文档/页身份和新首帧，旧 completion 不得确认新恢复。
- 保留正常三态、owner 树、单任务栏锚点、输入 fence、默认绘制、存储格式、PptCOM ABI 和现有 UI。
- 不改写成超时伪造 Active/ready、提前清 Committed/Bar、无界重试或删除恢复责任。

## Acceptance Criteria

- [ ] 原生回滚未完成时不恢复源预览；Rejected 后 Whiteboard 背景/分页真实恢复，但 Pending/failed 和输入 fence 保持，显式重试仍可用。
- [ ] 每类非原生等待在无进展时进入失败通知，实际进度后正常提交；Cancelled/Expired 不计为 Native API 故障。
- [ ] DComp 提交成功、失败、阻塞和迟到完成均有确定结果；退休超时没有 ULW/新 HWND/第二退休 worker。
- [ ] 样式可清除时走原 ULW；不可清除时完整重建 legacy 窗口链并禁止再次选择 DComp，重建失败进入受保护故障。
- [ ] 恢复后的新 generation 只有真实新首帧、输出/内容版本和必要背景 ACK 才能开放输入；旧帧/旧 completion 不得确认恢复。
- [ ] 生产 CLI、完整 `InkeysRepo.sln Debug|ARM64` 和 `InkeysHeadlessTests.exe --no-window` 通过；静态确认 Win7 无 DComp 路径没有新增依赖。
- [ ] 正常三态、焦点切换和任务栏恢复经人工复核；分平台及真实 DComp 故障结果单列，不以 headless 冒充 Win7–Win11 全部实测。

## Notes

技术设计见 `design.md`，执行与验证顺序见 `implement.md`。本任务不修改或归档既有白板任务，未请求 push。
