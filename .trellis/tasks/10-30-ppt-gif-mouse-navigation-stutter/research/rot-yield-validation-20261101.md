# ROT安全让出修复验证（2026-11-01）

## 已实施范围

仅在当前已绑定、缓存Active且descriptor为StableSlideIds/PageIndexFallback、无恢复重试，并有排队Next/Previous时，可在原ROT调用返回后的检查点Deferred。本轮相对前版仅改PptCOM.cs、SlideShowSession.cs及ManagedCompatibilityTests.cs；此前未提交的诊断/脚本保持。完整Observe→有界排水、raw/descriptor/Present/ack、2秒超时和500ms维护节奏未改。

所有Deferred先清best别名与priorities，逐项/外层finally完成后才返回；无partial结果转移、无跨轮RCW/枚举器缓存。最多连续Deferred一次，下一到期完整尝试，不是无限优先导航。已取消但尚在队列的导航也可能消耗这一次机会，但没有执行授权。两个现有属性仅路由到原accessor，其生产switch仍是真正dynamic；无反射/新PIA包装/GC/FinalRelease。

## 测试与审查

- ARM64原生MSBuild Release|AnyCPU构建和全部managed coretests exit0，stderr空。日志Build/Validation/rot-yield-managed-20261101/*-verified.*，最终末尾PASS PptCOM descriptor ownership and session/owner contracts。
- 实际scanner测试覆盖每类检查点、postNext moniker、partial与multiplewinner、同identity多次取得、HRESULT失败但out非空、取得对象后异常、throwingConsole writer、扫描结束后才到达pending、资格/恢复/Unknown/未绑定/PageIndexFallback/HWND0/非导航请求和公平预算/cleanup重置。
- 真实client线程在GetObject期间请求导航，验证scanner先解全部临时资源、再完整Observe、最后单次Invoke，Invoke时root receipt仍为1；Count0/换窗后旧请求零Invoke。
- 系统测试边界可注入，但生产默认AcquireRot保留原HRESULT/out语义、ReleaseMarshalObject不变；真实CreateBindCtx RCW有非空接收与成功Marshal.Release计数断言，正常/Deferred/失败路径配平。默认ServerHarness全部NoTestRot隔离，不扫描用户真实Office；显式fixture独占其fake ROT。
- 独立trellis-check最终无未修findings：合法finally、取消winner、别名转移、failed out清理、dynamic和原完整Observe次序确认。设计审计记录rot-yield-design-audit-20261101.md。
- 全部原始文件BOM/CRLF与git diff --check通过。相对阶段前快照可在Build/Validation/rot-yield-20261101/before审计，未重写之前改动。

## 产物验证

- 完整InkeysRepo.sln Debug|ARM64，PreferredToolArchitecture=arm64，same invocation规范化PATH并禁用节点复用，900秒预算；在已验证PDB允许环境构建exit0，7.159秒，最终增量0warning/0error。原有pwsh.exe PATH后处理提示未影响solution退出码，没有改无关配置。
- DLL显式交接并通过最终EXE资源只读核对：351A529267C75C3F8588F8887626ABF22F6FECE36892AFA65D4C0DC1416C876B。TLB仍为C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606，字节一致。
- 入口PrepareOnly通过，未启动GUI、main.json前后SHA一致；元数据Build/Validation/ppt-navigation-20261101-130510-663-1927736f/manifest.json。此目录只是准备验证，不是人工性能样本。

## 尚待验证与交接

AI没有操控GUI/Office、没有commit/push。本轮是第一项实际调度修复，预期减少未完成ROT查询叠加，不声称已消除所有长尾。已开始的COM不可抢占，完整Observe耗时仍保留；最大一次发现延期是公平策略的取舍，不承诺固定毫秒上界。Office及时退出与下一文稿真实验收仍不由fake ledger替代。

用户沿用Start-PptNavigationDiagnostic.cmd，控件3↔4约8–10轮重点Previous，正常退出后分析rot-deferred/rot-forced与队列耗时，与原55EA18DF...细分版样本分开。原任务不归档、原像素/兼容验收不补勾。
