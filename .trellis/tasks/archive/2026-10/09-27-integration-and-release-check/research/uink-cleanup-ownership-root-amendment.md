# F069 Root 补充冻结：部分提交和异常保留

Active task: .trellis/tasks/09-27-integration-and-release-check

2026-10-01。独立设计review在原Save PartialCommit分支发现：recoveryPath选backup时temp guard仍armed，若backup已foreign/不可读，身份正确删除反而会丢本轮唯一本地selfvalidated temp。Root采纳以下最小合同，作为B320CBE6…3FDC设计的强制补充；不改原schema/Replace/rollback，不提前授权源码或写PASS。

- 所有 PartialCommitRequiresRecovery 分支无论recoveryPath选backup/temp/newRecovery，都disarm temp自动删除；pin保持到函数结束。保留已接受但未durable不承诺恢复能力，只保留仍可能有用的artifact。
- Move/Replace可能已触target之后，异常和无法证明原target仍为原strong revision时默认retain。只有原target强证未变或明确的安全abandon分支才能再授权清本轮temp。
- pre-commit partial write/flush/selfvalidation失败若created object与内容强证成立，仍可清自己的创建物；它与PartialCommit不是同一状态。
- 确定性回归必须覆盖foreign/不可读backup选择后temp保留、post-mutation异常保留，以及pre-commit own失败正常清理。任何失败都不能退回DeleteFileW(path)兜底。

原作者已停写；4线程上限阻止新设计turn，Root仅写本独立补充，不改原设计及源码。独立review必须合并两文件给结论，后续唯一writer上下文一起读取。
