# 实施步骤

1. 主会话创建任务，保存已批准PRD/design/implement/research，填充两个jsonl并validate/start。
2. 两个trellis-implement代理按design归属实施；不得互相覆盖，统一上述API/i18n命名，保持编码/CRLF。
3. 核心代理实现模式/桥接/输入/Bar与无窗口状态、布局测试；设置代理实现配置/说明块/下拉框/i18n/config tests。
4. 主会话整合，trellis-check全范围审查，直接修复实际问题。更新与任务改变的合同相关的spec。
5. i18n sync/check（sync不覆写已完成翻译），静态diff/编码检查。
6. 动态vswhere查ARM64 MSBuild，同一个PowerShell先Remove-Item Env:PATH -ErrorAction SilentlyContinue，设置MSBUILDDISABLENODEREUSE=1，然后MSBuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m。至少允许5分钟，用持续进程合理等待。
7. 执行Build/ARM64/Debug/Inkeys.exe --state-mode-production-test（早期无窗口）与InkeysHeadlessTests.exe --no-window，检查退出码。
8. 保存validation.md，列出人工未验项；不commit/push、不触发journal自动提交。仅确有证据后标记阶段状态，允许整体人工待验仍in_progress。

## 执行结果
- 步骤1至7已完成，两个 trellis-implement 和 trellis-check 产出已整合，相关 spec 已同步。
- 步骤8已保存 validation.md。自动验证全部通过，人工 GUI 项待验，任务保持 in_progress；实施阶段按用户要求未创建 commit/push。
- 后续用户明确授权为本次改动提交 commit，同时要求不要结束任务。沿用现有签名配置，不 push、不 finish/archive，不触发 journal 自动提交。
