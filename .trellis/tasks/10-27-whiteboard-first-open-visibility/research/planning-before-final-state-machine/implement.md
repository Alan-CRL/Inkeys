# 实施计划：已确认的白板回归

## 1. 证据与范围

- [x] 取得此前单独授权的首开失败、进入后后台闪烁、前台恢复对照，保留原始文件。
- [x] 从 20261021a 和直接父版本追到 77f76038，分清事实与推断。
- [x] 用户最新禁止 GUI；后续不启动/操作窗口，不读运行中窗口/PDB，不沿旧方案扩大背景重构。

## 2. 实现及真实生产回归

- [x] Window.cpp / Window.ZOrderPolicy.h / window_tests.cpp：白板有限相对排序，固定身份，保留 Bar；每次写后采样；测试现场隐藏 Middle、后台绝对 TOP 拒绝、四项保序/提升、Bar 缺失、外部穿插与实际失败。
- [x] 同三文件：Z-only 快照及恢复在通用路径前分流；仅 owner/band/政策/内部序；PPT 前序可恢复；不得 CanvasPolicy/Hide/Show/FRAMECHANGED；保留后到安全覆盖、代际、stop、真实恢复责任。
- [x] IdtState.cpp：每个进入 epoch 只准备一次 UI，原生模式失败有限退避；真实生产 helper 的 CLI 回归，不更改背景/首帧/激活合同。
- [x] 更新必要规范，保留其当前未提交改动。

## 3. 验证

- [x] 原始文本格式/编码、最小 diff、git diff --check；用户两份及所有九份 CSO 保持原 bytes/timestamp。
- [x] 动态定位 VS ARM64 原生 MSBuild，在同一 PowerShell 规范 Path/PATH，构建现有 InkeysRepo.sln Debug|ARM64；允许至少5分钟。
- [x] Inkeys.exe --state-mode-production-test（在 GUI 初始化前退出）。
- [x] InkeysHeadlessTests.exe --no-window；已有固定视觉导出文件先备份、后恢复，不能污染其他任务。
- [x] trellis-check 独立复审实际生产接线、完整事务边界、失败/过期及恢复。
- [x] 记录本次验证结果和风险边界，没有 GUI/跨系统实机结果。

## 停止与提交

若实际因果不再支持最小修复，停止并说明；不削弱真实 native/代际/内部关系检查，不伪造 ready 或丢弃恢复责任。用户已人工确认两项修复并明确授权提交本轮改动；不push，新反馈的退出异常另行调查。

## 后续退出异常：先调查

- [x] 原生窗口研究：LeaveWhiteboardWindowMode/SetWhiteboardWindowMode(false)、Refresh、完整Restore、Shell标记的失败传播。
- [x] 状态与输入研究：Exiting各门禁、背景/工作区/工具恢复及点击后的ready传播。
- [x] 主栏研究：Desktop布局快照归位、白板/PPT可见性与当前模式的发布依赖。
- [x] 汇合证据，必要时用生产纯helper离屏探针验证特定机制；无须为只读调查重跑完整GUI或solution。
- [x] 给出最小修复方案及验证/未证实边界。

调查汇总见 research/exit-investigation-summary.md；本次实际首失败点及点击墨迹原因仍未唯一确认，新产品修复尚未实施。

## 桌面退出简化修复执行

- [ ] Window三个policy/mode调用与完整group恢复复用私有物理提交；普通全局刷新不变。不新增scope状态机/公有API。
- [ ] 增加真实生产物理提交/相对排序的离屏案例：mode=false+topmost=false、隐藏PPT、外部间隙/拒TOP、保序、后半失败后重复交接、真失败/代际/取消。
- [ ] 鼠标/RTS选择入口精查后实施有把握的最小修复及生产测试；不猜测新的焦点/框架问题。
- [ ] 独立trellis-check；必要spec同步，仅记录确认的简化合同。
- [ ] Build/WhiteboardExitFixValidation现有solution/CLI wrapper，完整Debug|ARM64、state-mode与--no-window，CSO及旧任务视觉白名单恢复/格式检查。
- [ ] 本轮未请求commit/push，保留已提交ab79和本轮修复边界。

鼠标输入补丁限定一个Draw3.DrawingController.cpp：实际Selection所有workspace丢弃新Down，复用现有真实control-fence CLI队列回归。验证新增Inkeys.exe --draw3-control-fence-test，不能启动GUI。
