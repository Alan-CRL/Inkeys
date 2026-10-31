# 验证记录

实际工作树 D:/Project/Inkeys/Repo/Inkeys，分支 bugfix/ink，基线 dev718b0ebd。诊断上一项签名提交00377811，不合入本分支。

## 红灯
- 完整 InkeysRepo.sln Debug|ARM64，ARM64原生MSBuild，退出0；日志 Build/InkReconnectValidation/red-build.log。
- Inkeys.exe --draw3-control-fence-test 退出17；日志 red.stderr.log。17条FAIL均为新重连身份回归，覆盖旧key残留、ID复用及连续接管。既有EraserIdleWake/WorkspaceTransition PASS。

## 绿灯
- 相同完整Solution配置退出0；日志 green-build.log。
- 同一无窗口参数退出0；green.stderr.log 的 ReconnectIdentity、EraserIdleWake、WorkspaceTransition 全部PASS。
- 调用生产共用AdoptReconnectedContact；修复增加新Touch key同步，非Touch保持0。既有重连判据及几何计算未改。

## 边界
所有测试均为严格早退无窗口参数，使用Start-Process -WindowStyle Hidden -Wait取得真实退出码；TEMP/TMP定向仓库Build/InkReconnectValidation/temp。不启动应用主窗口或GUI，不把状态机回归当真实触屏观感验收。

独立 trellis-check 已通过，见 check-review.md。Release x64完整Solution构建退出0，release-x64-build.log；相同无窗口参数退出0，release.stderr.log。编译已见旧代码和第三方数值窄化/nodiscard警告，本次不做无关清理。

## 交付与状态
- 本地修复包：Build/InkReconnectValidation/Inkeys-bugfix-ink-x64-20261031.zip，沿用既有单Inkeys.exe布局，PE机器类型0x8664。
- ZIP SHA256：3D251E9F3763FCF79EE4C24DF82E34F4B55E597F3F0FDA7C14107BDEECA2019F。
- EXE SHA256：BE5A89C71E6984F31E906D6F178898B90949CF5DF81D2F88B2501447A19328D9；已核对ZIP内文件哈希一致。
- 完整机器可读结果：Build/InkReconnectValidation/validation.json。编译产生的两份shader二进制差异已还原。
- 用户只授权提交前一项诊断分支工作；本修复保持未提交/未推送，任务in_progress等待提交收尾，不调用会自动提交的archive/journal脚本。
- 全程使用原D:/Project/Inkeys/Repo/Inkeys worktree；之前临时空worktree未使用，应用archive接口因primary checkout限制拒绝移除，未绕过工具手工删除。

## 用户现场验收
2026-10-31 用户明确反馈“人工验证通过”，并授权提交、结束归档及合并前审阅。此前未提交状态说明为历史记录，由本验收与最新授权取代。
