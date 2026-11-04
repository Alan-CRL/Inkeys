# 实施与验证（已批准）

- [x] 最终规划确认后才task.py start，按上下文交trellis-implement，不在planning改产品。
- [x] 实现生产复用的窄指示器状态，单调500ms门槛与现有曲线/动画开关分离。
- [x] 以原InputWorker tick接缝发布/取消deadline；新pending和expiry/取消/注销/隐藏/换场验证，不新线程、不延长隐藏窗口寿命。
- [x] 缓存round stroke与弧几何，绘制track/arc与可打断opacity/scale；角标尺寸包络和dirty尾帧保持正确。
- [x] 更新旧dots观察与真实像素fixture：小于500ms完全无像素、持续显示/旋转/退场/反向、左右一致/最大数字不遮/partial=full、错误文字和原ready不变。
- [x] headless以注入steady时间覆盖499/500ms、长idle、dt0/clamp、重复true、快速结束/重启、全局off、session/visibility取消、原子deadline替换及旧wake无害；所有线程测试失败先放行再join。
- [x] 独立trellis-check并更新实际spec；UTF8/BOM/CRLF与最小diff，不改其它任务文件。
- [x] 动态定位VS ARM64原生MSBuild，按AGENTS同invocation规范化Path/PATH、禁node reuse；完整InkeysRepo.sln Debug|ARM64，预算至少5分钟（建议900秒），不单独构建Inkeys.vcxproj。
- [x] 既有page-control/PPT定向及实际bar-eraser-offscreen-test、必要InputWorker/Scheduler无窗口回归，核对真实exit和metadata；不运行GUI/ComputerUse/Office。
- [x] 不变managed源/DLL/TLB及EXE内嵌核对，PrepareOnly配置SHA不变。
- [x] 用户人工环观感确认；无提交授权不commit/push。

回退边界：只回退本任务视觉状态/接线，旧控制/稳定数字/ACK/错误与Retry不动；定时机制不得通过新增循环draw或隐藏窗口常驻来绕过问题。

验证见 [implementation-validation-20261101.md](research/implementation-validation-20261101.md)，人工交接见 [manual-handoff-20261101.md](research/manual-handoff-20261101.md)。

## 人工反馈调参验证

- [x] 半径1.5倍、track .6/arc1.2、500ms参数与统一几何/dirty来源。
- [x] 完整ARM64、499/500定向、实际非空字形遮挡/partial/尾帧测试和配置/资源核对。
- [x] 用户确认新尺寸/细线/半秒显示观感生效，明确要求收尾和提交。

见research/tuning-validation-20261101.md。

2026-11-01：用户确认最终参数已生效，批准结束本任务并提交；人工视觉验收通过。软件验证见research/tuning-validation-20261101.md，旧性能问题不属于本任务。
