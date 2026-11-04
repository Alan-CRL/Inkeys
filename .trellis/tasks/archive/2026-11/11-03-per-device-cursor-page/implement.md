# Implement：本轮增量执行计划

## 前置与评审门

- [x] 找回 `11-03-per-device-cursor-page` 和原历史决策，恢复任务为 planning。
- [x] 读取适用规范，记录所有权、改动边界与验证计划。
- [x] 用户在本轮最终规划后明确批准实现，task.py start 已成功执行。

## 执行顺序

1. 对比实际显示 DPI、产品上下文、resolver 和 GPU 尺寸，建立自绘箭头 DPI 接线回归；只修证实的缺口。
2. 增加四态小圆点接触隐藏模型及 schema/校验/State 传播/打包，测试默认值、四态往返、非法值、Bridge 更新。
3. 重排产品 resolver 的可见性决策，参数化覆盖两种 Dot 与其它样式、ScreenPen/Tablet/Unknown/Conflict/注入鼠标、Mouse/Touch、hover/contact、倒转橡皮和专用工具。
4. 接入设置草稿、变化事件、ComboRow 与精细控制区，保持信息条在数位板识别下方；用现有无窗口设置测试验证顺序、选项与交互。
5. 补齐 20 语言文案，使用 Scripts/i18n.ps1 sync/check 同步生成键与快照。
6. 对照全部变更进行 trellis-check，更新光标 spec；检查原始编码/BOM/EOL，git diff --check 与无关变更。

## 验证

- 按根 AGENTS 动态定位 ARM64 原生 MSBuild，同一 PowerShell invocation 执行 PATH 规范化，完整 InkeysRepo.sln Debug|ARM64，允许至少 5 分钟。
- 复用现有 Headless 测试入口，选择覆盖光标/配置/Bridge/设置的最小充分范围；若没有合适定向开关，使用 --no-window。
- `Inkeys.exe --draw3-pen-width-test` 是已有无主窗口生产接线测试；仅在覆盖本次 DPI/工具接线时扩充并运行。禁止启动产品或设置 GUI。
- 真实屏幕笔/数位板、混合 DPI 与视觉大小列为用户验收，不宣称源码或无窗口测试可替代。
- 最终说明修改内容、构建/检查结果及验证限制。当前没有 commit/归档授权，不沿用上一任务的授权。

## 进度

2026-11-04：用户批准规划，本轮已进入实现及无窗口验证。原轮实现与验收记录保存在 Git 历史 `3d22f108`。

## 最终实现与自动验收

四态模式、设备显隐、设置布局、20语言、实测Arrow大小校准及有界取证已实现；完整solution、i18n、设置专项、生产DPI/真实像素probe及完整--no-window均通过。参见research/validation-continuation.md。产品源码冻结，保持任务in_progress等待用户实机验收；不commit或归档。

## 实机反馈 revision 2

用户直接要求在当前已批准任务中修复箭头粗边和底部下拉不可点击，继续执行，无新任务或重复规划确认。

- [x] Arrow共享细边规则、设置预览和实际GPU黑边像素回归。
- [x] 共享Combo上下避让、受限滚动、保持动画和输入。
- [x] 完整ARM64 solution，定向无窗口popup/光标设置、GPU像素与生产DPI回归，i18n与编码/diff检查。
- [x] 更新spec，trellis-check复核完整任务与本轮差异；未授权commit/归档，不启动GUI。

Revision 2自动验证完成：完整solution、受影响Popup/输入/配置、20语言光标页、通用设置、真实细边GPU及DPI生产专项均退出0。见research/validation-revision-2.md；等待用户实机回核，未提交。

## 共享控件覆盖补充

- [x] 审计当前产品设置全部下拉入口，教学安全等均走已修复共享控件，无需页面专用产品改动。
- [x] 补真实教学安全底部上开/鼠标末项单事件回归，重新编译测试并跑受影响专项，更新AC8和最终证据。

2026-11-04 用户确认生效并授权归档提交，功能提交已完成；转入任务归档与日志记录。
