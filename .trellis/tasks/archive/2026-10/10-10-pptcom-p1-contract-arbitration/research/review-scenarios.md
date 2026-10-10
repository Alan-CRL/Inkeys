# 主会话复核关注点

> 属于P1验收范围；独立check应依据最终代码与公开测试核实，不能只相信实现报告。以下问题不预断最终仍存在。

- 已确认真实前台B但B无可用页/full时，必须撤销后台A命令route；无新目标时输出Unknown/Pending。
- gap/full恢复保留candidate generation/state高水位；旧sequence、旧epoch和旧候选代不能复活。
- source capability/health在同epoch变化时，不让保留候选的旧页能力继续授权；失效应撤route。
- SamePage跨源映射需要稳定文稿与页身份，Ordinal/缺文稿不得继承target；EndScreen映射不能仅因总数相同忽略拓扑。
- 明确Inactive/Ended后再Active、删除/全量遗漏后再出现、PID/HWND复用、所有来源失去连续观察后恢复，验证是否分配新logical show并拒绝旧确认。
- initial/pending状态多源到达顺序不同，稳定键决胜；同show独立活动证据来自非权威route来源时，持续挑战仍可计数，但重复事实不累加票。
- B准备后优势消失、前台切C、source代变，旧B确认拒绝；pageSafe不是Draw3 ready。
- 准备超时/确认失败，不让Advance每tick不断生成新owner重绑请求而没有有界诊断。
- 非幂等命令已投递后失联或target变化，OutcomeUnknown，绝不改投；fake执行guard及有限ID高水位都要实际被测试。
- RegisterSource/Receive批次校验失败不造成部分目录/能力提交；超限退役历史不能悄悄淘汰后允许旧identity复活。
