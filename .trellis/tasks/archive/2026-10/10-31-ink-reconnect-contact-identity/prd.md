# 修复重连后接触身份残留导致整笔丢失

## 目标
消除断笔重连成功后 contact ID 再次复用时整笔被 gesture_suppressed 的问题，保留原有重连和多指语义。

## 现场证据
2026-10-31 用户提供 Inkeys (1).zip。v4 的 input-lifecycle.csv 中 317 次 Down 全部发布/出队且无生命周期或门禁掉样；4 次重连分别使用 contact ID 2/10/4/6，其下一次 ID 复用均被 gesture_suppressed。重连行 5/43/136/246 对应抑制行 15/53/146/256。四笔持续 161/134/565/127 ms，准入版本均4、gate_flags均0。最早 output.log 的三次重连和疑似整笔丢失也都对应 ID 7/2/5。

## 需求与验收
- R1：成功重连后 runtime 的输入身份和手势身份必须指向同一新 contact；真实终态能清理新身份。
- R2：通过生产共用接管逻辑的无窗口回归复现 A.Up→B 重连→B.Up→B ID复用，修复前失败、修复后正常绘制且无残留。
- R3：覆盖连续重连、正常独立笔画、多指中非目标接触不被清除；不改变重连判据、窗口、预测/几何或准入策略。
- R4：完整 InkeysRepo.sln Debug|ARM64 构建和相关无窗口回归通过；为原反馈x64设备构建Release修复测试包；保留编码和 CRLF，git diff 无无关修改。

## 范围与约束
基于 dev 的 bugfix/ink，原 Inkeys worktree 实施。预计产品改动集中 Draw3.DrawingController.cpp，复用既有 production probe/test 入口；必要的最小共用接缝只服务真实接管契约。更新相关输入规范。诊断分支 v4 已单独提交00377811，不合入修复分支。退出 completion 缺失问题、性能采集扩展、UI、GUI测试和远程发布不在本任务内。沿用用户已批准计划中的修复包交付，生成普通Release x64测试包，不将诊断分支功能合入。

## 授权
用户先审阅了上一轮给出的回归→最小接管修复→多指验证计划，随后明确要求创建 bugfix/ink 并开始修复、创建 Trellis 任务；最新要求先提交诊断修改再在同一 worktree 继续。按该已批准计划执行，不自动提交本修复或推送。
