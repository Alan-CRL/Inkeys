# 形状第十一轮：收尾清理

## Goal

收尾前清理只读调查发现的冗余与隐患，不新增用户可见功能。

## Requirements

1. UInk 老图形兼容：不改代码，只更正 `shapes.md` 中已退役的 inkeysShape v1–v6 条款（当前只解 v7；保留 20261109a 无元数据四形导入、`LegacyKind`、未知版本标准降级）。
2. 去除主栏手写触摸准入名单（`PresentedTouchRegions` / `ProbeTouchScreen`），触摸与鼠标同准入：系统按分层窗口像素投到主栏即 Accept，命中交给 worker 的鼠标逻辑；关闭信号/消息框时 Blocked。SelectionOverlay 暂不改（后续并入画布窗口）。
3. 小重构：
   - 所有建笔失败分支（取笔失败、modeler Reset/Update 失败、图形初始化/构造拒收）释放已登记的 Touch key，取笔后的失败统一回收；
   - `ConfirmRuntimeConstructionContact` 自身幂等；
   - 控制点 ID 改为 `EditHandleId` 命名常量（数值不变）；
   - `Bar.ShapeEditor.cpp` 时长常量、样式栏操作释放、手势场景校验、结束手势去重；
   - 长按重置 helper 与易误读单行 if 拆分；提示栏键盘路径走同一 `Submit`（可被测试截获）、取消索引命名；
   - `[Draw3.ShapeEdit] gesture_end` 摘要仅在诊断输出可用时格式化。

4. 合并 dev 前审阅修复（Claude 规划与验收，Codex gpt-6.1-sol High 审阅与实现）：
   - 手掌橡皮与图形：多步构造中、编辑模式下不可用（编辑模式下该触点只结束编辑），非编辑模式照常擦除；持有构造会话的 runtime 不得被提升为手掌；
   - ConstructionComplete 按钮路径写 `completedConstructionSession`；
   - 几何面板键盘单次读取 `focused_`；
   - 编辑浮条同场景按钮捕获允许旧呈现代次的 Up（防御性）；
   - 工程登记补齐 60 个 SVG 与 `shape_interaction/presets` 头文件；旧四形禁止辅助位；过时注释修正。

## Acceptance Criteria

- [ ] `InkeysRepo.sln` Debug|ARM64 构建通过。
- [ ] 第十轮全部离屏/无窗口回归 0 失败（tool-state、geometry、editor、input-worker、native-input、shape-core、control-fence）。
- [ ] 提示栏 fixture：启用按钮触摸各一次意图，禁用按钮无意图。
- [ ] 规格 `shapes.md`、`ui-pointer-session.md` 与代码一致。
- [x] 真实触摸点击主栏各面板已实机验收。
- [ ] 手掌橡皮与图形新规则待实机验收。
- [ ] 与 dev 无冲突（origin/dev = 合并基点，快进）。
