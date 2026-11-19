# 阶段 E · DComp 合成时机对齐

## 目标与范围

在 Windows 8 及以上实际使用 DirectComposition 的主画布上，微调连续绘制的下一帧起点，使新笔迹更有机会赶上桌面合成。保留用户设置的画布帧率，包括屏幕 120 Hz、画布 240 fps 的用法。Win7Dwm、ULW 和阶段 D 的实机补测独立进行。

## 现状与方案

- 阶段 A 的 waitable 控制可排队帧数。阶段 E 只调整已有的、可被输入唤醒的帧尾预算，保留下一帧领取令牌的顺序和原有超时、熔断计数。首帧和空闲唤醒直接沿用现有调度。
- 在当前 DComp 设备上低频获取 GetFrameStatistics，按 timeFrequency 转换预计显示时刻，并从有理数合成率计算周期。复用 Draw3.CompositionClock.h 的时基校验和限频规则，避免引入第二次等待。预计显示时刻仅用于估算，实际显示收益由 A/B 验证。
- 接入 Draw3.DrawingController.cpp 现有帧尾等待。以正常起帧时间及预计工作耗时为基线，限幅修正；连续超刷新率绘制时保留子帧间隔。统计失败或无效时本轮修正为零。刷新率改变后重新估算，设备重建、后端降级时作废旧样本。
- 性能页提供一个合成时机对齐下拉框，依次为“始终”“关闭”“仅 UpdateLayeredWindow”“仅 DirectComposition”，默认“始终”。复用原有两个配置键，保留已保存的组合。i 提示修改后须重启。

## 验收

- 缺失的配置键按开启处理，已保存的显式关闭值保留。无窗口用例覆盖固定 60/120 Hz、60↔120 Hz、240 fps 对 120 Hz、统计无效和设备重建。检查修正有界、无效时回到原预算、高帧率下不会逐帧等待下个刷新周期。
- waitable 开关可独立组合。关闭相位对齐时能复现阶段 A 基线，单纯的合成时序失败不会触发后端回退或计入 waitable 超时。强制 ULW 沿用阶段 B 行为。
- 通过 InkeysRepo.sln 的 Debug | ARM64 构建及相关无窗口测试。实机在相同帧率、输入负载和 waitable 设置下对照开关，比较提交相对合成时刻的分布、帧间隔与等待超时；单凭 Present 成功无法确认显示延迟改善。

参考资料见 [Microsoft Learn 的帧统计结构说明](https://learn.microsoft.com/en-us/windows/win32/api/dcomptypes/ns-dcomptypes-dcomposition_frame_statistics)。

## 验证结果

ARM64 Debug 完整构建、无窗口测试及 20 种语言校验通过。本机开关对照中，DComp 的 36 次时序采样均有效，两组 waitable 均无超时。ULW 对齐时 419 次提交中 397 次赶上预计截止。用户反馈两个后端开启时手感良好，端到端上屏延迟尚未量化。
