# 实施计划

- [x] 1. 在分页内容模型中标记数字/状态，接入 PPT recovery 和白板 switching + Loading/Wait。
- [x] 2. 在真实 `BarPageSurface::UpdateContent` 中实现数字即时、状态可打断的主次文字过渡。
- [x] 3. 在共享按钮指针层实现 Up 后同屏幕坐标 hover 抑制，并接入 PageControl 生产事件路径。
- [x] 4. 增加三语言 Loading，运行 i18n sync/check 并审查生成差异。
- [x] 5. 在现有 PageControl/Bar 测试中补充状态动画、打断、数字即时、hover/press 释放与三语言覆盖。
- [x] 6. 运行目标离屏测试、完整 `InkeysRepo.sln Debug|ARM64`、完整 `InkeysHeadlessTests.exe --no-window` 和 `git diff --check`。
- [x] 7. 完成独立 Trellis 质量审查，修正中点测试时序并补充中点后状态间打断、初次状态 Configure 回归，重跑受影响验证。
- [x] 8. 更新相关 Spec 与任务验证记录，报告真实 GUI 未验证项；未经用户要求不提交 commit。

## 验证记录

- `pwsh -NoProfile -File ./Scripts/i18n.ps1 sync`：更新三语言、快照与生成头；独立质量审查再次运行 `check`，en-US/zh-TW 均为 534/534、100% PASS。
- `Build/ARM64/Debug/Inkeys.exe --bar-eraser-offscreen-test`：最终通过，`[PageControlUI3] failures=0`；覆盖真实 Scene 的 PPT/白板数字即时、状态进入退出、中点前后打断、同目标幂等、最终数字、禁用动画、三语言槽宽/上下槽像素、生产 Down/Move/Up、Leave/Cancel 与 pressScale 恢复。
- 完整 `InkeysRepo.sln` `Debug|ARM64`：实施和独立质量审查均使用 ARM64 host MSBuild 构建通过，Inkeys、PptCOM、PptCOM.Tests、InkeysHeadlessTests 均成功。
- 完整 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：实施阶段最终运行约 240 秒，以 `PASS animation correctness` 结束；质量审查仅调整 PageControl 测试并重跑其目标离屏测试，未重复完整 no-window。
- `git diff --check`：通过。
- 未运行真实 GUI、PPT/Office/WPS、CPU 满载、触摸视觉或真实 ULW/DWM 人工验收；离屏与 no-window 结果不替代这些项目。

## 追加：白板立即打断并翻页

- [x] 9. Host 对白板页面命令关闭输入准入并标记场景边界，复用现有 contact 封口和成功 Present 回执。
- [x] 10. 移除工作区协调器对首个白板页面事务的冗余 load-pending 等待，以 commandScenePending 保留单事务串行与目标页完整性门禁。
- [x] 11. 增加真实 Host/Controller 的 still-down 白板翻页、旧 Move/Up 隔离、暖页 ready、冷页加载 ready 和 no-op 回退测试。
- [x] 12. 重跑目标测试、完整 ARM64 构建和完整 no-window；独立 Trellis 质量审查补齐冷页成功 exactly-once 命令回执及计数断言，并更新 Spec。

## 追加验证记录

- `Inkeys.exe --draw3-hidden-test` 最终完整运行约 227 秒并结束。新增的暖页 still-down 无需 Up ready、旧 Move/Up 隔离、冷页严格加载/Present ready、ready 后 fresh Down、第一页 Previous no-op 解锁，以及暖页 Next/冷页 Previous 原始命令恰好处理一次断言，在 DComp 与 ULW 两种模式均未出现失败；套件仍返回 1，因为两种模式原有的初始 Desktop contact/Clear 用例失败，不能记为整套 PASS。
- 完整 `InkeysRepo.sln Debug|ARM64`：实施和独立质量审查均最终通过，使用 ARM64 host MSBuild；MSVC 增量链接多次发生 `LNK1000`，同目标重试后完成，未改产品工程配置。
- 完整 `InkeysHeadlessTests.exe --no-window`：追加回归连续两次运行均在 300 秒工具上限被终止；主会话随后以 900 秒上限重跑，约 356 秒完成，退出码 0，以 `PASS animation correctness` 结束。
- 用户已于 2026-10-30 确认真实人工验收通过，可以结束任务、归档并提交；隐藏套件上述既有 Desktop contact/Clear 失败仍作为独立遗留问题保留，未在本任务修改。

## 回滚点

- 动画或交互回归无法收敛时，仅回滚本任务分页内容类型、文字更新策略、指针抑制和 i18n 改动，不影响已提交的白板 Geometry 修复及并行工作。
