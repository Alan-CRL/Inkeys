# 最终用户设备验收与版本核对

2026-11-14。结论：用户在本轮交付版本测试后反馈“基本没有问题”，明确授权收尾、commit及push；基础只读核验未发现阻止归档的重大日志异常。签名源码提交为`b861d464`。本记录不修改代码/配置、不构建、不运行GUI、不创建commit或push，归档及Git操作由主会话完成。

## 验收范围

用户验收限本会话当前设备和实际执行动作，沿用此前Surface Pro 11场景；不扩大为所有屏幕、无EDID回退模式或每种硬件均已真机验证。日志可见TouchPhysical、ScreenPenPhysical和IndirectDip活动，不能仅凭日志条数推断每项动作的用户意图或全部设备手感。合法tap、disabled及低置信Stop不据此重新调参。Touchpad继续quarantine，未接入新的硬件时间或输入producer。

## 源码与产物一致性

当前全部9个源码/测试文件逐个SHA256与[touch-release-validation-results-2026-11-14.json](touch-release-validation-results-2026-11-14.json)的`finalSource`一致，没有在最终验证后改变产品源码。交付目录两个产物也与该记录一致：

- `Build/ARM64/Debug/PanTouchReleaseValidation/Inkeys.exe`：`b266c29b3c8bc1395998c3f1f50ee3e69271998d87b8a17a72bfb0823a73e941`。
- 同目录`InkeysHeadlessTests.exe`：`63cb071c66dc01f8b8b8f201e1a2dfe61da58a15eeee287ed75cc04506b82b3e`。

自动验证仍沿用已审查版本边界：最终ARM64完整Solution exit0（14.328s），最终pan/control/feel/pen-width/diagnostic五专项全通过；完整无窗口套件在green4通过（444.156s）。此后三文件的两个Touch保护、对应回归及一项probe采样条件以最终完整构建和专项复验，复用无关模块全套结果；不声称最终二进制再次执行完整WARP/设置长套件。详见[独立检查](touch-release-implementation-review-2026-11-14.md)及[机器审查证据](touch-release-final-review-evidence-2026-11-14.json)。

## 最终console.log基础检查

文件：`Build/ARM64/Debug/PanTouchReleaseValidation/console.log`。

- 21,495,541字节、99,381行；读取前后大小/修改时间稳定，UTF-8可完整解码、无NUL、末行有换行。SHA256：`3304f5ee5202befe0148c009826ea5a58fa94ae1a3aa1ebfcfc30c4f80fc4e7b`。
- 可见149条release-quality：TouchPhysical108、ScreenPenPhysical14、IndirectDip27。release日志可见120条ready、2条unsupported-tail、27条disabled；其余reason=none不是独立释放统计。可见Touch决策103条measured、2条stop，缺失日志不强行配对。
- 11条显式stop记录中，8条为通用fling-complete-or-cancel、1条leave-move-mode；另2条pen-mailbox-contact发生于旧class4 coast且上一帧held=0，分别紧邻真实Pen Down key9/key15，不构成Touch仍held时被hover打断的证据。
- 可见CanvasPan中未发现input-history-overflow/gap/epoch-changed、admission-failed、invalid-time或NaN/Inf；未见FATAL/ASSERT/未处理异常关键字。所有可见error字段为0；已尝试的Present/Fallback未见失败，raster-failed均0。
- 日志异步sink累计报告2084条contention丢弃（full=0、truncated=0），因此这里只证明文件基础完整、可见记录未见重大新异常，**不宣称事件日志无丢失或完整覆盖所有手势**。不用缺日志作绝对无异常证明，也不因这项诊断丢弃推断输入事件丢失。

用户认可与当前可见证据支持本任务收尾。未新增设备校准、触控板实现或系统API能力承诺。
