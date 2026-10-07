# 发布基线与验收方案

## Goal

冻结 H0/HC/H2、性能与功能基线、commit 审查集合和发布门槛

## 依赖与门禁

入口：父任务三份规划及独立 review 通过后启动；不依赖其他子任务。出口：H0/HC/H2、环境、回归表、性能与审计口径冻结，基线失败如实记录，才允许 Work 1 写代码。

## Requirements

- 冻结 H0 的 HEAD、tree、分支、staged/unstaged/untracked 指纹、submodule 与活动任务；记录执行截止时间。定位 HC 和 H2 的 tag/release/源码/二进制/配置，列候选与不确定性。
- 记录 OS/补丁、CPU/GPU/驱动、ARM64、DPI/分辨率/刷新率、多屏、电源、MSBuild、renderer/presenter、主题/效果；分开可获得的事实与待测。
- 建动作→模块→状态→可观察结果的回归表，覆盖主栏、属性、设置、模式、Draw3、PPT、显示变化、保存/退出/重启。
- 先确认 UI3 diagnostics 的回调、attempt、success、idle、Retry 口径；UI3/Draw3 分别准备生产代码 headless 与真机场景、warm-up、至少三轮、分位数与资源趋势方案。
- 冻结审计终点 H0、refs 与起点后的 commit 候选全集，不以一次 `git log --since` 冒充完整覆盖。

## Acceptance Criteria

- [ ] H0/HC/H2 的来源和可比性明确；不能定位的版本保留“未验证”，不虚构实测。
- [ ] 测量协议在优化结果出现前写定，含样本量、冷/首/稳/长期、median/P95/P99、长帧、延迟、CPU/GPU、内存/显存/句柄、cache。
- [ ] 回归表与审计候选集合有可追溯文件；基线构建/测试的退出码和首个有效错误已记录。
