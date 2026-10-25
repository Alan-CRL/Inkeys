# 实施与验证
1. 检查现有主题/背景状态流和大表面及浮层颜色消费者。
2. 在主题适配层实现模式感知表面颜色；生产接入当前原生背景模式，明确保持浮层实色。
3. 新增模式/主题/回退/前景与边界不变及缓存失效回归，复用既有DPI与离屏测试。
4. 子代理审查，主会话按既有 PATH workaround 使用 ARM64 原生 MSBuild 构建 InkeysRepo.sln Debug ARM64（15分钟上限），执行 --no-window --setting-home-only。
5. 检查 diff 编码/换行、恢复构建改写的原有 DLL/TLB 字节，记录实际验证结果。不 commit/push。
