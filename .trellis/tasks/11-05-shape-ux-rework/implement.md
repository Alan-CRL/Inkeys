# 图形交互与样式整改 — 执行计划

在 feature/shape 上进行（或者从它切出一个工作分支）。按依赖顺序串行实施，**每个子任务完成并通过检查后单独提交**。提交信息中不包含 AI 署名。

## 顺序

1. **edit-performance**：先改数据流（预览槽、轻量预览、本地跟手、浮层降开销、计量），其他子任务都依赖这个新路径。
   - 检查：`--draw3-shape-renderer-test`、`--ui3-shape-editor-test`、`--draw3-shape-core-test`，新增合并与吞吐用例；跑一次大页面基准，记录计数器。
   - 回滚点：预览槽和轻量预览的开关。
2. **edit-interaction**：共享核心中的控制点表、各图形约束、弧角累计、辅助显示模型、渲染和 v4 codec。
   - 检查：共享 `--shape-core-only`、`--uink-file-only`、`--uink-presentation-only`、`--desktop-autosave-only`，以及 `--draw3-shape-core-test`、`--draw3-shape-renderer-test`、`--draw3-whiteboard-storage-test`。
3. **edit-visual**：Thumb helper（主栏回归保持像素不变）、外框、旋转钮、浮条重做、颜色三层、透明度和主题、按键截获修复。
   - 检查：`--ui3-shape-editor-test`（扩展截图矩阵）、`--bar-eraser-offscreen-test`、`--pen-width-production-test`、`InkeysHeadlessTests --no-window`。
4. **toolbar-category**：分类、分段控件、移除圆角矩形、轮播修复、参数行中的辅助开关。
   - 检查：`--ui3-geometry-attribute-test`、`--shape-tool-state-test`、`--state-mode-production-test`、`Scripts/i18n.ps1 check`。
5. **construction-prompt**：外观对齐、环形进度、四个图标按钮、Forward、暂停自动收起。
   - 检查：`--ui3-shape-editor-test`、`--draw3-shape-core-test`、i18n 检查、`--state-mode-production-test`（自动收起）。
- **icons**：只改 SVG，与上面各步没有文件冲突，可以在任意一步之间插入；先出预览，经用户确认后再替换。
   - 检查：`--ui3-geometry-attribute-test`，完整构建。
6. **收尾**：更新 spec（design §7），然后从头完整构建一次并跑全部回归。

## 通用验证命令

```
MSBuild InkeysRepo.sln /m /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false   # 使用 vswhere 找到 arm64 MSBuild，去掉 PATH 中的重复项，并设置 MSBUILDDISABLENODEREUSE=1
MSBuild inkStrokeModelerTest.sln /t:inkStrokeModelerTestTests Debug|ARM64
Build/ARM64/Debug/Inkeys.exe --<test>    # 离屏/无窗口测试
Scripts/i18n.ps1 check
```

基线（2026-11-05，b4c8d960）：构建 0 error，上述 10 项回归全部通过（日志在 `Build/shape-analysis/`）。

## 风险

- `Bar.ShapeEditor.cpp` 会被三个子任务连续修改，必须严格串行。edit-visual 开始时会大幅重写这个文件，同时把它改为符合仓库常规的格式（不再一行写多条语句）。
- 轻量预览涉及层序：被编辑对象上方还有笔迹时需要额外的纹理。显存不足时退回现有路径，并记录日志。
- v4 格式一旦发布就不可撤回。合入前要确认降级部件和 reference 的校验是对称的。
- 实机项目（触屏/笔的手感、DWM 层级、PPT 放映）只能由用户验收，离屏测试不能代替。

## start 前检查

- [ ] 用户批准最终规划摘要。
- [ ] 确定执行顺序中第一个子任务后，`task.py start` 到对应子任务。

---

# 2026-11-13 实机验收续轮执行计划

本轮已获用户明确实施批准，直接延续父任务，不再新增确认门。所有实现都在当前 `feature/shape`，严格串行修改共享文件。

## A. 共享模型、构造与持久化

1. 增加平面约束与 Solid baseProjection，更新工厂、Validate、构造会话白名单和默认工具状态。
2. 实现三角形/梯形转换及持续顶点约束，接入 SetConstraint 属性候选与 Replace/journal。
3. 修点工具单击/首拖、单点可见、开放预览与完成闭合，保证 Back/Forward 一致。
4. 将边数上限同步到 14，更新 codec、状态、UI 常量和资源。
5. 实现棱柱/棱锥底面投影控制点与面朝向遮挡；完成 v5 编码、旧版核验/规范化和标准降级。
6. 实现弧/扇形虚线首步与跨端点连续编辑。
7. 每组同步更新现有 core/runtime/renderer/uink 测试，先跑共享核心小门禁再进入 UI。

## B. 主栏与选中浮条

1. 浮条实时跟随 frameRect 边中心，实现会话级换边粘性和双边不足稳定 fit；更新旧冻结行为测试。
2. 仅 Line/Arrow/Rectangle 隐藏外框；保留旋转连接线、frameRect 与立体角标。
3. 主栏/浮条增加常驻图标+文字、三角形模式和梯形等腰控件；更新控件索引、键盘、hint 和命中。
4. 两处删除混合模式入口，Fill 隐藏线型/粗细/辅助控件和辅助视觉；保留旧混合状态兼容展示。
5. Center 控制柄增加 0.5 视觉透明度；调整目录顺序、14 边数字层、SVG/RC 和二十语文案。

## C. 字体与完成提示

1. 形状属性文本使用最终 pose 的 fontTargetSize；通过真实生产离屏路径复现并修 More 首次展开。
2. 保留并发布 construction finished 完成沿，进度环在退出时从 1/2 到 1，与收缩并行；取消负例不走满。

## D. 分阶段验证

每阶段运行与其改动对应的现有入口，失败先修再扩大范围：

```
Build/ARM64/Debug/inkStrokeModelerTestTests.exe --shape-core-only
Build/ARM64/Debug/inkStrokeModelerTestTests.exe --uink-file-only
Build/ARM64/Debug/inkStrokeModelerTestTests.exe --uink-presentation-only
Build/ARM64/Debug/inkStrokeModelerTestTests.exe --desktop-autosave-only
Build/ARM64/Debug/Inkeys.exe --draw3-shape-core-test
Build/ARM64/Debug/Inkeys.exe --draw3-shape-renderer-test
Build/ARM64/Debug/Inkeys.exe --draw3-whiteboard-storage-test
Build/ARM64/Debug/Inkeys.exe --shape-tool-state-test
Build/ARM64/Debug/Inkeys.exe --state-mode-production-test
Build/ARM64/Debug/Inkeys.exe --ui3-geometry-attribute-test
Build/ARM64/Debug/Inkeys.exe --ui3-shape-editor-test
Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
Scripts/i18n.ps1 check
```

实际可执行文件目录以当次 Solution 输出为准，不盲目沿用示例路径。使用 vswhere 找到 ARM64 MSBuild，设置 `MSBUILDDISABLENODEREUSE=1`：

```
MSBuild InkeysRepo.sln /m /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false
MSBuild inkStrokeModelerTest.sln /t:inkStrokeModelerTestTests /p:Configuration=Debug /p:Platform=ARM64
```

## E. 最终全范围检查

1. 加载 `trellis-check`，按 native 与 native-desktop 的 Quality Check 审核全部续轮差异、跨层数据流和旧文件兼容。
2. 从头构建两个 Solution 的 Debug|ARM64，运行上述完整回归和基线中的 `--bar-eraser-offscreen-test`、`--pen-width-production-test`；记录既有环境失败，不能冒充通过。
3. 检查截图/日志/cso 等新生文件，仅保留任务需要的源码、资源与 artifacts。
4. 加载 `trellis-update-spec`，同步 shapes.md、rendering-and-ui.md、bar-theme-material.md 中被本轮改变的稳定合同。
5. 实机触控、笔、屏幕边缘、遮挡观感和 PPT 交互最终由用户验收；离屏通过不冒充实机通过。

## 2026-11-13 接续收尾结果

A–C 实现和 D/E 自动验证已完成；两个 ARM64 solution 构建与全部计划回归通过。共享 fit 的浮点贴边问题、控件 ID/测宽缓存遗漏、完成沿消费和倾斜梯形关闭约束均已补回归，详见 `research/continuation-verification-2026-11-13.md`。

用户已授权提交本轮改动；不结束或归档父任务、不 push，实机验收清单保留待用户确认。

# 2026-11-14 第二轮执行记录

用户已明确批准实施。不新建任务、不归档、不commit/push。
1. 共享构造与v6、三角形、斜二测和约束；同步旧格式兼容。
2. 构造中点编辑与预览连续性；产品动作与状态。
3. 编辑条/控件/面板/本地化及虚线分布。
4. 完整solution Debug ARM64构建及现有无窗口专项；记录实际退出码、未验证范围。
5. Trellis check与spec补充，BOM/换行及最小diff审计。


## 2026-11-14 第二轮交付

已实现批准的15项第二轮需求，两套ARM64 solution、核心/运行期/renderer/UInk/白板/自动保存/Headless和两项完整UI离屏回归通过。具体统计及环境问题见 research/round2-verification.md。验证后的程序已回填 Build/ARM64/Debug/Inkeys.exe，未commit/push、未GUI实测、任务继续保留in_progress等待实机验收。

# 2026-11-14 第三轮（已批准实施，基线 ec1d5138）

本段替代前两轮冲突要求。三个子任务已建立，父任务继续 in_progress，不自动提交、推送或归档。
- 普通跟随直接移动，只有换边/避让分支改变时保留连续残差动画。
- 修复完成后撤回延迟与重做浮窗阻塞几何入口，验证真实输入及历史发布链。
- 平面胶囊旋转+四向箭头移动，矩形保留、直线箭头无；立体仅移动。
- 图文紧凑居中、圆形文案圆心，辅助柄24DIP附近避让、四向箭头且按下无跳变。
- 等腰双斜杠只在构造/编辑显示，退出编辑消失；按钮右侧三个点标识并循环约束顶角。
- 平行四边形固定原单位模板比例，以首末点为长对角线旋转绘制。
- 平面四边形允许过零翻向，梯形仍禁交叉；绘制中顶点调整后可继续加点和撤回恢复。
- 立体统一固定教材斜二测：x水平，y向右上45度或左上135度半长，z竖直。圆底严格数学投影。
- 箱体外接矩形宽高分解对应底面短对角线；立方体模型等边，第二笔仅高度方向。提示底面/高度。
- 底面柄径向等比缩放+切向Z旋转，长方体保留首笔比例；圆柱锥仅半径。不保留俯仰/二维旋转。
- 棱柱顶面中心/棱锥顶点横向侧偏纵向高度，侧偏随Z旋转，去底面平移。正形开关持续约束。
- 棱柱/锥3–8，正多边形3–14，三个独立会话值启动均6；约束与边数依次布局。
- 私有构造统一v7，删除开发期v1–v6解析迁移与旧立体算法。保留已发布直线矩形及通用UInk标准几何，不保证旧开发期构造恢复。

验证：核心几何、真实生产输入/历史、UI逐帧/DPI/主题/长文案、v7及标准降级/冷历史/自动保存；原生ARM64两个solution和现有无窗口测试。实机触控与动画观感留待用户确认。

第三轮全部实现和自动验证已完成；详细结果见 research/round3-verification.md。实机手感/观感待用户验收，任务继续in_progress，未提交或推送。
