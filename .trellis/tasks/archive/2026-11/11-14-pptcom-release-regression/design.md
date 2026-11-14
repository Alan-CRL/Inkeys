# 回退到三行GC与诊断，保留后续架构方向

## 当前实施边界

- 业务行为恢复至本轮调查前HEAD基线，仅在FullCleanup末尾保留 Collect → WaitForPendingFinalizers → Collect。
- 保留PptComDiagnostics、引用取得/释放账本、事件时间戳队列、真实调用耗时及对应测试；诊断不增加Office属性读取或重复执行命令。
- 撤回2025/2004关闭延后、2031直接退休、关闭中的ROT让出、旧观测关闭fence、0x800706BA分类扩展和busy时追加Count探测。回到基线事件/错误处理及重试策略。
- 删除只验证候选修复的新增用例，保留诊断测试；规范不再宣称候选行为为当前合同。
- 公共COM ABI、dynamic分派、原有单次Release、native/UI、依赖与构建系统保持既有边界。

## 当前结论

旧服务同步等待Unadvise或CP Release，随后才能开始下一轮发现。实机分别捕获33.458秒与55.578秒清理等待；重启Inkeys可在旧POWERPNT仍在时恢复新联动。此前最终关闭候选只提前进入了清理，没有解决产品阻塞；历史设计/验证和原始日志证据保存在research下，不作为当前成功验收。

## 用户确定的后续方向（未实施）

采用多线程动态绑定，使旧绑定由明确所有者继续清理，新绑定独立发现/工作。后续设计须处理COM apartment/封送、逐笔所有权、旧事件与命令失效、共享页码写入隔离、失败租约及后台工作有界、退出收尾。

核心验收：用闸门阻塞旧Unadvise和旧Release时，新放映已进入Active；旧回调/命令/页码不污染新session，解除闸门后旧取得正确配平。仅最终配平不足以证明已解决进度问题。结束放映后的未响应需另有对应现场。

当前回退决策及验证见 research/rollback-to-gc-and-diagnostics.md；完整时间线见 research/fourth-user-log-and-rebinding-block.md。
