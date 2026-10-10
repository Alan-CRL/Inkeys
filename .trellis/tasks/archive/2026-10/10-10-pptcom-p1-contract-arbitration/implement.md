# P1 执行与验证

1. [x] Trellis implement读取父设计/P0/spec，检查复用，直接实现纯值服务与headless。
2. [x] 合同校验、full/delta/epoch/gap与身份归并。
3. [x] 活动分级/回滞/恢复、绑定确认及route/target修订。
4. [x] 单一路由、执行guard、deadline/去重/迟到result。
5. [x] 登记工程；保持BOM/CRLF，关键中文注释。
6. [x] ARM64原生MSBuild完整InkeysRepo.sln Debug ARM64，至少5分钟，无窗口隐藏进程；同一PowerShell invocation先Remove-Item Env:PATH及MSBUILDDISABLENODEREUSE=1。不单独构建Inkeys.vcxproj，不改代码绕过环境错误。
7. [x] 本次InkeysHeadlessTests.exe --no-window，记录exit/关键输出；不启动Office/GUI/UAC。
8. [x] 独立Trellis check全范围P1-AC1..10，必要修复/测试后最终检查。
9. [x] 主会话更新真实合同spec/父进度，核对COM及产物基线与工程diff；P0工程文件允许已声明新增登记，COM不变。
10. [x] 用户授权必要阶段commit，保留SSH签名；失败停止提交，不绕过，不push。通过后归档/journal禁自动commit，不启动P2。

## 回滚与限制

P1产品无调用者，无配置或在线接管；撤销新增纯逻辑/测试登记即可回P0。fake绿色不能宣称真实COM多候选、Win7、双权限、Office释放或时延通过。
