# 旧任务归档与接管

日期：2026-10-08。用户明确要求归档 `08-13-setting-fluent2-responsive-ui` 和 `08-13-unified-render-pipeline-setting`，由 `09-06-setting-winui3-design-rebuild` 接管。本次只整理任务记录，不实施产品代码、不执行 GUI。用户随后明确授权提交本次归档与接管变更；不 push。

## 来源与完成边界

- `.trellis/tasks/archive/2026-10/08-13-setting-fluent2-responsive-ui/`：保存 ImFluent 引入、全页首轮迁移、resident/presentation 生命周期、窗口 chrome、DPI、DWM 材质与历史验证记录。原 PRD 未勾选的验收项不因归档标为通过；代码和自动验证的完成情况以其 implement.md 及本接管任务后续验证记录为证据。
- `.trellis/tasks/archive/2026-10/08-13-unified-render-pipeline-setting/`：保存共享 WARP 图形资产、唯一串行渲染线程、唤醒/设备恢复、Setting FIFO 与公开接口合同；原 PRD 的全部自动化验收项已勾选。dev 已在 2026-08-20 归档该任务；本次为 fluent 分支的归档整理，不覆盖另一分支的归档历史。
- 当前接管任务已完成首批、全页迁移、实时浅深主题及顶部边框代码阶段；证据见 full-migration-validation.md、native-visual-review.md、window-dark-validation.md、top-border-validation.md。

## 接管的要求与待验收事项

1. 保持共享设备、唯一串行渲染线程、CSO、FIFO、窗口公开接口和线程所有权合同；不重做已完成的渲染管线。
2. 保持 Initialize 预热、Hide 释放 presentation 并停止连续绘制、resident context/backend/font/image 常驻、Show 恢复呈现、隐藏/可见设备 epoch 恢复及 Shutdown 最终释放的现行合同。
3. 延续 A6/A7/A10：长页底部、插件详情、支持/调试、宽/默认/最小窗口、导航覆盖、简繁英与多 DPI；默认窗口截图已审查的可见区域沿用原证据，未覆盖区域继续待验收。
4. 补实际主题切换、标题栏/边框、八方向拖动、最小化/最大化/还原、系统菜单、Snap/Win+方向键、滚动条与跨屏检查，沿用现有窗口验收清单。
5. 补重复 Hide/Show、打开延迟、资源增长及设备 epoch 恢复的实机证据；记录可见空闲帧时间与打开/缩放/切页表现。旧任务“同等页面帧时间持续回退不超过约 10%”为比较目标，缺少可比基线时明确记录测量边界，不虚报通过。
6. 核验既有 Mica/legacy Mica/Acrylic/Solid 能力回退、composition 变化及透明 clear 呈现；无法覆盖的 Windows 版本或 API 失败场景明确记录自动验证/静态审计与实机验证边界。

7. 本轮发布基线复核确认 LnkHelper 的 `SetShortcut()` 在发布与旧 Fluent 中都同步枚举/读写桌面快捷方式，当前回调仍位于设置渲染路径。该既有边界问题留作后续设计：冻结修复/创建选项及路径后交业务 FIFO，验证连续切换与退出顺序；本次内容同步不扩大为 ShortcutAssistant 接口/线程重构。见子任务 `10-08-setting-publish-baseline-sync/research/baseline-check.md`。

## 历史要求与当前范围

- 旧任务的固定 Light、无主题持久化要求已经由本任务实时浅深主题与 SettingDarkMode 配置替代；旧导航、窗口几何和库补丁要求以本任务后续批准设计及现行实现为准，不恢复旧方案。
- 归档不新增动画、触摸、GIF/视频、系统主题跟随、高对比或独立材质引擎需求。既有功能的验证与新增功能实施分开记录；任何未实现的历史扩展不因接管自动进入实施。
- 历史记录中的 GUI/Computer Use、暂存或提交授权不自动延续。本次仅授权任务归档与接管；后续 GUI 仍遵守当前用户与仓库规则。
- 接管任务继续保持 in_progress。旧任务 completed 表示独立跟踪结束，不表示上述所有实机验收已通过。
