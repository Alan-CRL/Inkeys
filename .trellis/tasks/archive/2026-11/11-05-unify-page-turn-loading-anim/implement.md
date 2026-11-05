# 实施计划：统一翻页加载动画

设计见 [design.md](design.md)，需求/状态表见 [prd.md](prd.md)。实现时按顺序做，每个阶段末尾运行验证；构建/测试命令遵循 `.trellis/spec/native-desktop/build-and-compatibility.md`（`InkeysRepo.sln`，`Debug | 当前设备原生架构`，对应架构 MSBuild host，超时 ≥ 5 分钟）与 `InkeysHeadlessTests.exe --no-window`。

## 阶段 0：基线

- [ ] 先跑一遍当前 `InkeysHeadlessTests.exe --no-window`，记录与 `PageControl`/`Bar` 相关的现有失败（避免把已有失败算到本任务）。
- [ ] 阅读 `PageControl.cpp:3790-4320`、`:5040-5070` 与 `Bar.Interaction.cpp:2830-2850` 的现有加载相关自检，列出需要同步改的用例清单。

## 阶段 1：指示器模型（无视觉改动）

- [ ] `Bar.PageLoadingIndicator.h`：入参改为 `BarPageLoadState`；新增 `text/dim/ring` 三轨道，共用 deadline；去掉 `scale`；`angle` 改为 `phase`（秒）；`NeedsFrames`/`WakeDeadline` 随之更新。
- [ ] `Bar.Scene.cppm`：`BarPageSurfaceContent` 新增 `loadState`、`statusPrimary/statusSecondary`；`switching` 保留为命中锁。
- [ ] `Bar.Scene.cpp::UpdateContent/Advance`：用新输入驱动；`Loading→FinalFailed` 时 deadline 不重置、环退场、变浅保持。
- [ ] 用例：500ms 门控、短于 500ms 不出现任何视觉/文字、`Loading→FinalFailed`、`None` 立即复位、动画关闭、设备资源重建保持相位。

## 阶段 2：状态映射与文字

- [ ] `PageControl.cpp::BuildPageContent`：按 design 2.1 映射 `loadState`；产出 `statusPrimary/Secondary`；删除白板 `kind=Status`+`Loading` 路径；PPT 恢复期间上/下一页 `interactive=false`；`RetryableFailed` 页码按钮 `interactive[1]=true`，但在 `textActive` 前不可点。
- [ ] `Scene.UpdateContent`：根据 `indicator.textActive` 选择显示常态或状态文字（`TransitionToString`）；状态文字出现/消失不改变布局目标，页码槽宽度按最大值预留（核对 `UpdateHorizontalPageWidths`）。
- [ ] 保持 `debugNavigationDelay` 的行为：调试延迟视为 `Loading`（PPT 与白板同一路径）。
- [ ] 用例：状态表 6 行全覆盖（文字、可点性、环/变浅标志）。

## 阶段 3：变浅

- [ ] `Bar.Scene.cpp:729` 视觉 `enabled` 改为 design 2.3 的 `visualEnabled`；`content.switching` 只留命中锁。
- [ ] 补 `pageTotal.pct` 的 retarget（`id==3`），含 `visible=false` 时为 0。
- [ ] 其他常驻内容（拖动柄、`unsaved` 文字等）按 design “整窗口变浅”口径处理；待决项 1 的结论落实。
- [ ] 用例：像素断言——加载态 `/N` 区域像素与禁用态一致（参考 `PageControl.cpp:4503` 的 `GetThemeDisabledContentOpacity` 断言写法）；`RetryableFailed` 页码按钮不变浅、上/下一页变浅。

## 阶段 4：加载环

- [ ] **原型比较**：dash 描边 vs 采样折线，测每帧耗时/分配（`ui3-render-diagnostics.md` 的帧诊断），选定方案并在 `design.md` 记录结论。
- [ ] 实现沿按钮内缩圆角矩形路径的弧段（起点顶部中点、顺时针、圆角端帽、无轨道、亮色 `IconPrimary`）。
- [ ] 非线性相位函数 + 单元用例：一圈内弧长在 `[Lmin,Lmax]`，起止点连续、无突跳。
- [ ] 删除 `Bar.BusyRing.h`、`DrawBusyRing`、`switchingTimed`、`switchingSince`、`ringTrack`、旧常量；更新工程文件与 `.filters`。
- [ ] 损伤区域改为页码按钮包络；用例：四个表面、不同 DPI（1.0/1.25/1.5/2.0）下环不越界不被裁。
- [ ] 实机/截图目视调线宽（默认 1.5 DIP）、内缩、弧长范围、周期。

## 阶段 5：i18n

- [ ] 先改基准 `zh-CN.jsonc`，再对其余 19 种语言逐个校正 `Recovering`（进行态）、`Wait`（状态词）及必要缩写，参考 prd 的 i18n 审计。
- [ ] 按待决项 2 处理 `Loading` 键。
- [ ] 运行 `Scripts/i18n.ps1 sync` → `check`；审查生成差异（`IdtI18nKeys.g.h` 不手改）。
- [ ] 槽宽自检：20 语言 × 7 个词，最大页码宽度下不溢出。

## 阶段 6：收尾验证

- [ ] 完整 `InkeysRepo.sln` `Debug | 当前设备原生架构` 构建通过。
- [ ] `InkeysHeadlessTests.exe --no-window` 全部通过（对比阶段 0 基线）。
- [ ] 手动：PPT 翻页卡住（调试开关 `DelayWaitingOperations`）、白板翻页卡住、PPT 恢复中、失败可重试/终态失败各看一次；短于 500ms 无闪烁。
- [ ] 更新 `.trellis/spec/native-desktop/rendering-and-ui.md`（其中引用了 `PageLoadingIndicator`）与 `whiteboard.md`/`ppt-idle-fade.md` 相关合同（页码槽状态表、500ms 门控、`pageTotal` 变浅）。
- [ ] 向用户出汇报（见 prd “任务结束后需向用户汇报的事项”）。

## 回滚点

- 阶段 1 完成后可单独提交（行为不变）。
- 阶段 3、4、5 各自独立提交；阶段 4 的删除旧环在新环通过全部用例后再做。
