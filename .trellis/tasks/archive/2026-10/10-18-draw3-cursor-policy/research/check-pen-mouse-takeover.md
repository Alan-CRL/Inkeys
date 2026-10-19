# Pen 离开后 Mouse 接管回归审查（2026-10-19）

基线 e9e9b1f5；本次在 owner 通知 source stable 后审查 4 个源码文件、两份当前 spec、AC15/16 和实施记录。结论：未发现阻断项；check 未修改产品或测试源码。

## 生产路径

- `MouseCursorMessageFilterInput` 末尾追加默认 Unknown 的 `pointerAuthority`，旧调用保留原行为。Window RTS 分支传 `cursorOwner_` 的 acquire 持久快照，不用 Pen 样本有效位或 Touch 临时视觉归属替代。
- 生产 filter 只把既有明确系统 Move 保护扩至 Pen owner：消息必须是 WM_MOUSEMOVE、来源查询成功、IMDT_UNAVAILABLE、IMO_SYSTEM，并且 Touch 抑制或 Pen 持有成立。按钮位、坐标和消息间隔没有绕行；Touch 原诊断优先保留，新增 `system-pen-move`。
- Window 在 filter 拒绝时先退出，未到清 Touch 抑制、写 owner=Mouse 或发布 Mouse mailbox。ClearPenCursorSample 仍保留 owner；Pen owner 配空样本在 SelectPrimarySample 中返回空，不恢复旧 Mouse。
- 独立复核 Controller 的 buildDrawingCursorVisuals：primaryRuntime 只选择实际工具，不写 owner；普通 Pen 无补 Mouse 样本逻辑。System 在 Mouse 策略下不自绘、Drawing 绘 PenIcon，因此样式差异与错误接管路径相容；未用系统 CURSOR_SUPPRESSED 状态遮盖自绘。
- IMDT_MOUSE/TOUCHPAD、明确 Down、API 失败/缺失、应用注入等均沿既有过滤；未增加 Win8 静态导入、计时器或平行状态。Touch/Pan、兼容 Up 以及 Win32Mouse 早返回的独立 producer 未修改；真实 Mouse 已接管后系统刷新仍按旧行为。

## 回归覆盖与静态核验

- 新序列直接调用生产 FilterMouseCursorMessage、owner resolver、mailbox、visual resolver 和系统显隐策略，参数化 System/Drawing。旧 Mouse 存在时经历 Pen Hover/Contact/Clear、重复 Clear、约一秒/长间隔/重复/异坐标/按键态系统 Move；检查 Pen owner、Mouse 序号/位置及主视觉均不被错误接管。
- 清 Pen 后普通笔与荧光笔预览均隐藏，系统隐藏判断保持。真实 Mouse/TouchPad、Down、失败/缺失 API、其它未知来源与应用注入可以即时恢复；Drawing 明确断言恢复 PenIcon，System 沿原系统策略。再覆盖 Mouse 接管后的系统刷新、新 Pen Hover/Contact/再次离开。
- Window 未链接到此 headless 组合测试，接线采用实际调用点静态核验；未新增 HWND harness。固定 tick 是测试数据，不是产品计时器或现场复现记录。
- `git diff --check` exit 0。三个产品文件保留 UTF-8 BOM/CRLF，测试保留 UTF-8 无 BOM/CRLF，均无裸 LF；源码增量限于上述 4 文件，shader、guarded、配置和 UI 无本轮修改。两份 spec 与实际字段/行为相符。

## 验证边界

本 check 不构建、不运行产品/测试 EXE、GUI、GPU 或窗口 harness，不执行 Git 写入。当前完整 Solution Debug|ARM64 和 --no-window 结果由 root 单独记录；本报告不沿用此前验证为本轮通过。未采集 Win11 现场消息，尚不能证明约一秒的具体消息来源或固定计时器，也未做 Win7 实机验收。
