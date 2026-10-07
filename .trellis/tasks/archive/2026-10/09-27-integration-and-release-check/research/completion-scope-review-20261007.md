# 首发工程任务修订范围收尾复审（2026-10-07）

Active task：`.trellis/tasks/09-27-integration-and-release-check`。按Root转达的最新用户决定：不再追求性能验证，可完成则结束并commit；Win7 ULW仍独立处理。本checker只读核对父/七个09-27子任务状态、现有closeout和Oct5/6验证/独立review，只新写此报告，不改源码、task状态、Win7文件或执行commit，不重构建/GUI/历史审计。

## 结论

**可以完成修订范围下的发布前工程任务。** 最新直接审查未发现本轮尚未处置且必须修复的confirmed P0/P1；原状态入口/FineDial逻辑补证、粗细增量、必要Release构建/资源/逻辑验证及普通GUI/Office验收已有证据。旧空复选框或历史待修不能自动重开任务。

这个完成是用户缩减范围后的工程收口：UI3/Draw3完整性能验收由用户取消，历史未达目标仍不是PASS；Win7最终修复/重建/目标机验收和正式元数据/CI/签名/上传不属于本次完成声明。不能改称所有原始验收通过或已具备发布许可。

## 七子任务状态处理建议

| 子任务 | 读取时状态 | 建议及必须保留的边界 |
| --- | --- | --- |
| baseline-and-acceptance | in_progress | completed：来源、环境/审计集合/协议/基线已记录，精确0811身份已取得；取消尚欠完整对照，不伪称H2/全部设备实测。 |
| code-and-state-unification | completed（Oct5） | 保持completed：迁移/所有权、真实状态/bridge交错和FineDial取消、两配置及最终Release/普通GUI有证据，不等同全UI/全故障场景认证。 |
| ui3-performance | in_progress | completed（用户终止剩余验收而收口）：原改动/撤回方案与结果保留；150ms工作量不等价、350ms端点失败，完整“不倒退”未PASS；旧More/SVG fixture exit90不删除、放宽或变绿。 |
| draw3-performance | in_progress | completed（停止继续优化/验收而收口）：保留缓存局部收益及Oct3匹配硬笔5累计CPU；不宣称最终F81F帧率/输入延迟达标或旧Draw2与Draw3内部指标等价。 |
| commit-and-security-audit | in_progress | completed：沿用611定义集合覆盖、findings处置及最新直接增量无新确认缺陷；保留用户接受的HTTP回退/来源认证高影响风险、依赖及特定环境未验证，不称全部风险消除。 |
| crash-restart-recovery | in_progress | completed：已确认保存/身份/输入/退出期限问题有后续修复及验证，工程收口；真实GPU fatal→RTS→durable全链、部分故障/低内存/provider交错与Win7未验证，不承诺未durable墨迹零丢失或跨进程PPT自动恢复。 |
| integration-and-release-check | in_progress | completed：Oct5完整ARM64 Release最终exit0、五必要检查/资源包、Oct6有限独立复审和已确认人工验收足以本地工程收口；最终Win7代码重建、正式三架构CI/标记/版本/签名上传另列。 |

各task完成说明须明确“原性能验收取消/相关外部门移出本轮”，而非把原PRD未验条目勾PASS。父task亦可据此completed；已completed的粗细子任务保持。Root负责状态、当前入口与提交，提交仅收尾记录，排除并行Win7源码/spec/诊断/日志和生成物。

## 已知风险与证据边界

安全closeout明确：HTTP来源未独立认证是用户保留的高影响风险，未修复；旧EXE兼容、跨进程恢复政策及15秒到期允许丢未durable pending不改变。原F-026/F-027等confirmed问题的后续containment/保存及独立红绿记录优先于旧“未修”历史描述；不能因缺全部自然故障动态验证改称已确认新缺陷，也不能声称全链已验。

HEAD仍`8d15d05119b85777d8b0aabac4c99930cf438045`。Oct5 Release SHA256 F81F790F7DABEA30A1F230F58E03B5DFB991710A57DFCA02E2CDB0F52D5092E9只代表当时冻结代码，不能覆盖后续独立Win7脏工作区。取消性能验收不需要新增测试或修改该程序。父旧人工矩阵/空勾选保留为历史并由最新closure覆盖，避免继续重复执行。

## Findings / Verification

- Findings (fixed)：无源码修补；本checker仅新增此报告。
- Findings (not fixed)：明确接受的政策风险、特定未验故障链、用户取消的性能验收和独立Win7/正式发布门，均保留状态而不计PASS。
- Lint：新报告严格UTF-8无BOM/CRLF、无尾随空白。未运行源码lint。
- TypeCheck / Tests：没有新源码，不重新构建/运行；沿用Oct5完整Release及必要检查、Oct6证据独立复审。未新增动态PASS。
