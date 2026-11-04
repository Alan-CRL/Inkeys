# Independent review findings

## Findings (fixed)

- File: Inkeys/IdtState.cpp, AcceptWhiteboardStyle
- Issue: 接纳白板主题后有效 Brush 颜色改变，但未同步 BackgroundColorMode，也未主动请求主栏刷新。静置主栏可能继续显示旧颜色/预览，直至其他输入唤醒。
- Fix: 在原状态锁内按实际工具/颜色重算背景对比状态；在释放锁后调用既有 Bar::RefreshToolState。真实状态探针补深色白板默认白色时对比状态1、浅色红色时状态0断言。没有新增GUI调用或输入路径修改。

## Findings (not fixed)

- 未发现其余确定的任务内正确性/死锁问题。工作区宽色快照已投影全局真值，形状兼容字段仅镜像；Host token 包含serial防同页A→B→A旧确认，owner应用后仅清style原因；cold-load和workspace等门继续独立。
- 不同页或generation即使同颜色类别仍等待一次身份匹配ACK；主会话已同意澄清设计，不为减少短等待重构门控。相同token重复回执不推进样式状态。
- SetMemory仍只在Desktop写盘，启动恢复仍停用；Desktop快照会投影最新全局Selection与Primary颜色，因此非Desktop修改不会被旧Desktop副本回滚，白板Brush色不污染Primary。
- 真实窗口/设备和PPT行为未验证，数位板R2按用户要求暂缓，未改其过滤链路。

## Verification

- Static diff / whitespace: git diff --check PASS。
- Encoding: 所有变更源码和测试BOM保持，工作树换行CRLF保持。
- Product/type check and tests: 由主会话执行完整InkeysRepo.sln Debug|ARM64与无窗口探针，最终结果待主会话写入validation记录；本代理按协调要求不并发build或运行GUI。
- 新Host gate回归入口为 --draw3-control-fence-test；不能仅运行width/state探针便声称覆盖gate。

## Focused probe follow-up

完整Debug ARM64编译已由主会话通过；三个width/control-fence探针exit0。StateMode首个失败属于旧测试合同：安全退出时旧Desktop快照的荧光18前旧值与白板最新全局荧光值不同，sameState整份比较错误。已改为严格工具身份+Target/Echo投影断言，并另逐组检查Primary Brush颜色/Selection、白板最新全局荧光/Laser颜色与Selection，未放松安全交接/Bridge/recovery身份断言。核对其余sameState调用均在无样式修改的事务拒绝/往返场景，保留原强断言。最终重新编译及完整探针结果由主会话追加。

## 主会话最终验证回填

修复后完整Solution Debug|ARM64 exit0；--state-mode-production-test、--pen-width-production-test、--draw3-pen-width-test、--draw3-control-fence-test均exit0；InkeysHeadlessTests --no-window exit0/406.88s。详见validation.md和ignored TestResults/shape-style/结构化日志。
