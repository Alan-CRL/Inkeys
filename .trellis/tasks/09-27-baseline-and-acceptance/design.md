# 基线与验收技术方案

- H0 用 HEAD/tree、`git status --porcelain=v1 -uall`、staged/unstaged diff 与 SHA-256 指纹记录；submodule 用 superproject gitlink 和各子仓状态单列。HF 同算法重算。
- HC/H2 分别追 release/tag/build metadata、源 commit、二进制和效果配置；用证据级别“精确/候选/未知”，不把历史标签自动当用户指的版本。
- 生产 diagnostics 先核口径：callback、attempt、success、idle、Retry；性能数据按场景/版本/构建/设备/配置/轮次单独存原始文件，不能拿 Debug 对比 Release。
- 回归表连接用户动作、入口、owner、状态迁移、成功 Present 或持久化结果、自动与人工验证。GPU/ULW/真实笔受 GUI 限制时仅给静态/无窗结论。
- 审计集合由 refs+拓扑+双时间+集成边界合成，先冻结 SHA 集合，再逐项审查。
- Draw3 无窗基线显式复用生产 ContactInputCoordinator：单接触点 Down→20000 次 Move 发布和一致读取→Up/Recycle；独立保存每块样本，绝不把这段成本称作完整笔迹或成功 Present 延迟。正式产品 Host 的 RuntimeMetricsSession 当前未接线，后续 Work 3 单独处理。
