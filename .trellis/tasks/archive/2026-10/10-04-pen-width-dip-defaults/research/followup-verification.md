# 本轮追加调整验证

基线 `79e5fe0835b39c482cd7c87724c4a8ffa21e2ed8`，分支chore/publish。同一Trellis任务保持in_progress；本轮没有commit/push/归档/GUI/Computer Use。原41项工作区改动在journal记录前逐项hash未变。

## 实现

- Bar.I18nFormat和3语言ThicknessFormat仅显示安全四舍五入整数；精确DIP、Preset身份与连续Draw3不被展示值回写。全部候选/预设/浮窗读数去DIP和≈，sub1仍最小显示1。i18n snapshot由脚本生成。
- IdtConfiguration的Initialize入口以中文注释禁用Decode/flags/GetMemory，第一次只建立目标Context及共享Medium；旧Preset默认、新Startup和记忆宽色均不恢复。后续初始化只ApplyContext，不重置手动值。Capture仅安全保留原数据/future schema，不应用runtime。Draw3文档/uink历史恢复未修改。
- Touch真实候选发布被抽取为原交互与测试共用窄session方法；GetBarThicknessPreviewMetrics消费同代目标，UpdateBarThicknessPreviewWidth对活动candidate SetDirect。原core/outer.SetTar的缓动追赶已消除；非交互动画、morph/Laser阶段、锁定/取消/修订保护保留。AdvanceAnimationsAndDeriveLayout显式包括previewDragging脏区，即使数字未跨整数且IsSame仍刷新真实预览。
- 生产测试覆盖普通/大屏启动残留配置、颜色不恢复、重复初始化保留手动值，以及真实候选发布→统一metrics→立即读取preview.val、不写业务/bridge、公共panel关闭取消、锁定/有效与过期提交。headless覆盖半整数/小数/sub1/非法值/fallback纯整数。

## 精确命令及结果

从仓库根运行，通过run_verification.py保存完整argv/cwd/exit/耗时/stdout/stderr，900秒超时。完整构建使用vswhere发现的ARM64原生MSBuild；同一次pwsh先规范化PATH和禁用node reuse，使用已知linker内部故障的一次性LinkIncremental=false参数，未改工程设置/工具链。

| record | argv | exit | seconds |
| --- | --- | --- | --- |
| followup-bar-offscreen.result | `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test` | 0 | 29.932 |
| followup-baseline-policy.result | `Build/ARM64/Debug/InkeysHeadlessTests.exe --pen-width-only --no-window` | 0 | 0.086 |
| followup-baseline-width.result | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 0 | 0.161 |
| followup-build-rerun.result | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 17.848 |
| followup-build.result | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 1 | 174.277 |
| followup-config.result | `Build/ARM64/Debug/Inkeys.exe --config-boundary-test` | 0 | 1.632 |
| followup-draw3-width.result | `Build/ARM64/Debug/Inkeys.exe --draw3-pen-width-test` | 0 | 0.109 |
| followup-final-build.result | `pwsh -NoProfile -Command Remove-Item Env:PATH -ErrorAction SilentlyContinue; $env:MSBUILDDISABLENODEREUSE = '1'; & 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false /m:1 /nr:false /v:minimal; exit $LASTEXITCODE` | 0 | 20.765 |
| followup-headless.result | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | 0 | 95.178 |
| followup-i18n.result | `pwsh -NoProfile -File Scripts/i18n.ps1 check` | 0 | 1.348 |
| followup-state.result | `Build/ARM64/Debug/Inkeys.exe --state-mode-production-test` | 0 | 0.089 |
| followup-width-production.result | `Build/ARM64/Debug/Inkeys.exe --pen-width-production-test` | 0 | 0.490 |

第一轮编译首错C2248来自新probe直接访问protected CloseThicknessSlider；改造测试取消入口，最终通过现有public CollapseAuxiliaryPanels调用真实关闭方法，未扩大API/友元。中间wrapper版本虽编译通过，但检查发现其fixture memberAccess为空，在运行前修正；没有运行该错误版本production probe。最终完整Solution及全部本轮运行入口通过，所有原失败日志保留。

已有thirdparty/Setting等warning未扩大修补；PATH规范化后vcpkg的pwsh找不到日志为旧环境行为，现有targets回退系统PowerShell，build和实际探针均exit0。没有随机改源码/依赖绕过环境。

## 验收分级与收尾

- 已实现/自动通过：完整Debug|ARM64、全headless、真实config/startup/state/Bar candidate/Draw3 width seams、离屏Bar、i18n和差异/格式检查。独立trellis-check记录在followup-review.md。
- 源码确认：真实Touch消息的ProjectRelativePreviewWidth→ApplyCandidateWidth→session发布→Submit同帧metrics/val→Advance显式MarkChanged→实际draw读取.val。生产CPU probe没有驱动完整coordinator帧或真实触摸消息，不把接缝测试当作实机运行。
- 待本轮人工：直接触摸预览下方滑动的真实跟手手感，长按/锁定、mixedDPI与PPT实际显示；上一轮用户确认通过不是本轮触摸硬件证据。遵循无可见GUI限制未运行visible hidden tests。
- 单位契约不变，无渲染整数降级；仅展示去单位/≈。未修改橡皮、历史文档恢复或无关Win7现场。
- 回退仅本次增量；不要回退已提交79e5fe08或reset工作区。兼容配置证据保留，当前发布暂不支持启动恢复。
