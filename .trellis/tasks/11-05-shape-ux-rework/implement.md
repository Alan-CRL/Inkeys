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
