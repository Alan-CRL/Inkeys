# 实施计划（2026-10-19 已批准实施）

## 已完成规划
- [x] 用户授权新建任务、读取未来提案及当前源码。
- [x] 确认两样式双Delete相同；最新分离式全局退出先清空覆盖初始保留墨迹要求。
- [x] 两份专项研究、官方API核对；形成PRD/设计/验证计划和context。
- [x] 用户于2026-10-19明确批准整份规划并要求开始实现。

## 获批后实施顺序
1. 按Trellis读context→PRD/design/本文件，激活原任务；先纯生产matcher/ownership与独立有界transport，固定六组合，不恢复旧KeyboardInteraction。
2. 稳定页scope、条件enqueue和Controller真实准入、requestId回执；生产测试先证明Clear→Undo同页FIFO、过期页拒绝、Clear真实失败识别，再接复合动作。
3. IdtState窄条件往返/Selection内核；两样式X及具名ClearAndSelect事务，冻结取消收尾和新输入屏障；双Delete同Ctrl会话，测试CtrlUp候选/已形成意图区分。
4. 同线程RegisterHotKey+LL服务及失败状态；接Bar有限poll、安全退栈、生命周期和正常设置/模态优先。普通组合归属饱和不泄漏下层；PPT所有未绑定原键保留。
5. 六个Inkeys3 bool与设置toggle/说明、正式三语sync/check；逐组合禁用、热键注销/注册失败、无自定义/角标。
6. 新fresh独立全范围检查；必要修正后最小diff/编码/BOM/CRLF/源码登记审查，更新spec。
7. vswhere定位ARM64原生MSBuild，同PowerShell先Remove-Item Env:PATH并设MSBUILDDISABLENODEREUSE=1；完整InkeysRepo.sln Debug|ARM64至少5分钟。环境问题不改源码/依赖绕过。
8. Headless --no-window与适用初始化前真实生产探针；不得自动安装系统Hook、打开应用/设置/隐藏HWND或Computer Use。系统/Office/Win7/UIPI/AltGr实际测试另获GUI授权或用户人工。
9. 记录实际退出码/限频压力统计/未验证边界；人工验收后再获本任务commit授权，旧任务提交授权不继承。

## 验证矩阵
- Matcher：六组合、左右Ctrl/Shift/Alt/Win、ShiftZ不重入Z、extra modifiers、先终键后Ctrl、repeat/Up、注入交错、启动已有按键、码/时间回绕。
- Ownership：按下后切Selection/关开关/先松Ctrl/切前台，Up固定；UndoRedo重复按已定策略；Del/Q/X长按仅一次；归属组合队列满仍不让下层Undo/Delete；不可置信route整次Forward。
- Register/service：冲突/安装失败独立退化、重复Start/Stop、卸载/注销同线程、旧包跨重启、配置开关注销、WM_HOTKEY与LL恰好一次、专用X在设置/模态与AltGr边界描述一致。
- Routing：空画布与主栏收起仍批注；Selection/ClickThrough普通组合透传；Settings原生文本CtrlZ/Y/Delete优先；保留原生面板消费者、不恢复箭头/WASD/Esc泛拦截；PPT原键无COM双翻。
- 命令：两样式×四工具×空/非空×Selection/ClickThrough；X恢复宽色/laser且冻结不自动恢复；连续两个X串行退出再返回，前一失败退休依赖切换。分离式退出与双Del只在真实Applied/NoContent后Selection；blocked/failed/stale保持最新工具。
- 身份：强制队列延迟后A→B→A、Host重启、PPT同页号新session/binding/target、工作区转换，旧命令/回执不改新页/新意图；同页Clear→Undo→Redo不得因内容scene推进被拒。
- 两击：无时间窗、Del完整Up间隔、Ctrl未全松两侧切换、首Clear未完成快速第二击、原本空白、失败后新起点、第二击前CtrlUp取消候选、第二击后CtrlUp不撤销已捕获意图；另键/焦点/配置等失效；第二击不重复Clear、不覆盖恢复点；第一击后继续落笔不被门禁阻塞，新内容变更退休原两击候选。
- 事务：Clear成功与Selection提交间新contact不穿透；取消/新工具/停止仅释放自己的新输入租约，不解开PPT/load门；无锁等待/死锁/嵌套控件引用失效。
- Config/UI：六bool独立开关/strict bool/缺省/ReadMini/reset/copy/FIFO/NoUpload、旧配置不影响、三语和缩放；只显示六固定组合，Clear说明两步。
- 性能：停consumer、持业务锁、队列饱和下回调有限；聚合P50/P95/P99/max及drop/consume计数，无逐键格式化日志。不将无窗口桥测量说成真实系统零延迟。

## 规划阶段检查记录
只验证规划文档一致、context引用有效、git变更只在本任务；不执行实施项、不构建、不改产品/正式spec、不commit/push。

## 实施进度（2026-10-19）
- [x] 六组合生产Matcher/按压归属/独立SPSC与受管Hook/注册热键服务。
- [x] 条件页身份、具名回执、实际Controller只读Fence及独立复合租约。
- [x] Bar顶层安全分发、两样式往返、双Delete及真实状态/owner生产探针。
- [x] 六个Inkeys3独立bool、设置开关/真实状态、三语正式sync/check。
- [x] fresh全范围检查与修正、正式spec合同更新。
- [x] 完整Debug|ARM64源码编译；增量链接器LNK1000后仅命令行关闭增量，完整Solution exit0。
- [x] 最终产物无窗口回归与交付记录，详validation.md。
- [x] 用户确认设置关闭后快捷键正常、没有问题，授权提交commit并结束任务；不push，不启动GUI。
