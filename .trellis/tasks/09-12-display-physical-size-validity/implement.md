# 显示物理尺寸获取与有效性判定执行计划

1. 扩展 `Inkeys.Display` 公开类型与纯策略入口，拆分原始 EDID、活动 target、拓扑和业务物理尺寸。
2. 用 CCD 活动路径替换旧的间接 EDID 映射；通过 source GDI 名称关联逻辑屏，通过 target device path 和 SetupAPI 精确读取 EDID。
3. 实现 EDID 基础块校验、5 cm 最小边规则、逐屏扩展策略、复制全局禁用和旋转后的业务尺寸。
4. 扩展快照语义比较与刷新失败降级，保持现有订阅、缓存、fallback 和通知生命周期。
5. 迁移所有原始 EDID 尺寸业务消费者，并扩充设置诊断；不改 Draw3 橡皮算法。
6. 补齐纯策略、EDID、快照与现场枚举兼容测试，并把 `Setupapi.lib` 加入主程序及 headless test 工程。
7. 更新 native-desktop 显示快照规范，记录原始 EDID/业务尺寸分离和拓扑失效规则。
8. 运行 `git diff --check`、编码/EOL检查、`InkeysHeadlessTests.exe --no-window`，再用 ARM64 host MSBuild 构建完整 `InkeysRepo.sln Debug|ARM64`。

风险点：CCD 查询期间拓扑可能变化，必须在 `ERROR_INSUFFICIENT_BUFFER` 时重新分配并重试；SetupAPI 路径比较必须完整且不区分大小写；刷新失败必须先清除业务可用状态再发布，避免旧标尺继续生效。

回滚点：公开数据模型与纯策略、CCD/SetupAPI 采集、消费者迁移和诊断文本可分批回退；任何回退都不得恢复 DPI 伪造或不可靠 EDID 匹配。
