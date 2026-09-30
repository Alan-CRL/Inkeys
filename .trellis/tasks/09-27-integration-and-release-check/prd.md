# 集成验收与发布检查

## Goal

完整构建、自动化、人工矩阵、产物检查和最终独立复审

## 依赖与门禁

入口：依赖 baseline、状态统一、UI3、Draw3、commit/security、crash/restart/recovery 六子任务的代码与阶段验证交付；未验证项不自动视为通过。出口：最后修补后的完整构建/适用测试、HF 指纹、独立总 review、发布阻塞与人工矩阵齐全；本任务仍不归档/提交。

## Requirements

- 构建完整 `InkeysRepo.sln` Debug|ARM64，增加 Release 与可得 Win32/x64/ARM64；工程/ABI/shader 变动时适当 clean Rebuild，查 PptCOM DLL/TLB、shader、资源、运行时依赖和许可证。
- 运行真实生产逻辑的 InkeysHeadlessTests --no-window、Draw3、PptCOM 等适用测试；确认测试目标/参数/用例；构建与运行证据分开。
- 覆盖主栏/属性/模式/PPT 末页与结束页/保存与错误/重启/显示与 DPI/fallback/定格和设置 owner/关闭 gate；Win7 和其他组合只按实证声明。
- 最终独立 reviewer 审 H0→HF 全 diff、跨子任务交互、audit coverage 与晚期修补后的复验；清除临时调试代码与误入产物。
- 兼容矩阵单列 Win7 SP1 且仅 KB2670838：硬件 FL11.0 可用/不可用、Draw3/UI3 的 HARDWARE/WARP、DComp 不可用→ULW、resize/device-lost/透明/输入。两个 DWM 透明模式均禁用。
- ULW 保持 FLIP_SEQUENTIAL；记录用户提供的 Win7 实测与微软通用文档的冲突及本轮实际复验范围，不新增 bitblt 回退。

## Acceptance Criteria

- [ ] 每条验证记录命令、配置、退出码、关键输出、产物及代码/环境归因；未运行和人工项不记 PASS。
- [ ] HF 有 HEAD 加改动指纹；最终报告准确列修复、性能、覆盖、恢复、兼容缺口和发布门禁。
- [ ] 未提交、未推送、未归档、未发版；任务记录如实保留进行中/待人工状态。
- [ ] 无真 Win7 环境时仅确认代码和构建兼容，运行结果列需要人工；不通过安装额外 KB 改变目标。
