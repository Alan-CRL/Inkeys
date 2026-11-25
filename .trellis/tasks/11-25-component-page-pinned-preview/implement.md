# C6 执行计划

## 步骤

1. Tab 与“全部”（design §3）
   - 改 `Setting.cpp` 组件页 `entries.group`、`tabs`、“全部”单网格、ClassIsland 提示框下标。
   - 如需新 Tab 文案键：zh-CN 新增 → `pwsh Scripts/i18n.ps1 sync` → 补译 → `i18n.ps1 check`。
2. 删除边缘自动滚动（design §5）
   - `Setting.ComponentEditor.cpp` 删除边缘滚动块与 `scrollPosition`；预览高度上限改为外层视口一半。
3. 固定区（design §2）
   - 组件页在 `ComponentEditor::Draw` 后开 `##component-library` 内层滚动 child，其余内容移入。
   - 验证外层 `ScrollMax.y == 0`；必要时加组件页限定的外层兜底。
   - 验证页面入场/退场动效、进入页面滚动回顶、More 多行时视口变矮。
4. 长按起拖（design §4）
   - `DragSourceOverlay` / `ComponentCard` 增加 `holdForTouch`；触摸/笔路径：`AllowContentDrag` + 时长/阈值判定 + 按住期间唤醒。
   - 两处调用点传 `true`。
5. 测试
   - `setting_design_tests.cpp`：新增 `DragSourceOverlay` 长按用例（触摸静止达时长起拖、未达时长松开不起拖、越阈值转为滚动捕获且不起拖、鼠标越 4 DIP 仍立即起拖）；卡片多语言排版用例按新签名调整。
   - 如有用例依赖“全部”小节或边缘滚动，按新合同更新。
6. spec：更新 `settings-component-editor.md` §2 签名、§3.2 删除边缘滚动条款、§3.3 结构（固定区/内层滚动）、Tab 列表、“全部”单网格、长按起拖；§5 测试列表与实机项。

## 验证命令

- 构建：`InkeysRepo.sln` `Debug | ARM64`，ARM64 原生 MSBuild，超时 ≥5 分钟。
- headless：`setting_design_tests`（导航与组件页相关用例），`drag_session_tests`。
- 实栏门禁（确认无波及）：golden 35 项、`--ui3-trace-dump` 对 base 逐帧一致、`--ui3-host-offscreen-test`。
- i18n：`pwsh Scripts/i18n.ps1 check`（仅当新增键）。

## 审阅关口

- 步骤 3 完成后先自测外层不滚动与页面动效，再做步骤 4。
- 全部完成后 `git diff` 核对：白名单文件、BOM + CRLF、无无关格式变化。
- 实机验收（用户）：触摸长按手感（450 ms / 阈值）、卡片上直接滑动滚动、固定区在 More 多行时的挤压、Win7 页面淡出。

## 回滚点

- 每步独立可还原；步骤 3 若页面动效无法覆盖内层 child，回退到步骤 2 状态并回到规划重新评估固定区方案。
