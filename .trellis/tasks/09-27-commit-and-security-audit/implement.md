# 执行清单

前置：baseline-and-acceptance 冻结 H0、refs 与初始 commit 集合；只读审计可提前，代码修复须和主 agent 冻结文件 owner。出口：逐 SHA 覆盖/缺口精确，严重确认问题有回归、修复和独立复验。

- [ ] 冻结 H0 与 refs 完整性，生成逐 commit coverage 初始化行与准确分母。
- [ ] 按批逐项读 diff/分类；高风险追当前代码、调用链和失败路径，补 finding 与状态。
- [ ] 处理 merge、旧日期近期合入、import、revert、未合入分支、缺失 refs，复核总/已/待/缺口。
- [ ] 跑可用静态分析/警告/受限故障注入；依赖 advisory 核官方来源，工具不可用保留缺口。
- [ ] 确认问题建回归、最小修复、独立 review 和复验；交最终覆盖/安全结论。
- [ ] 审 Draw3/UI3 的 Win7 SP1+KB2670838 设备创建，硬件 FL11.0 有/无与 WARP；正式 DComp/ULW 选择、保留 FLIP_SEQUENTIAL、导入表；若确认兼容错误，先回归再最小修复，不新增 bitblt 回退。
