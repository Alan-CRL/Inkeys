# Observe细分定位阶段验证（2026-11-01）

## 执行结果与范围

在上一版分段诊断上，仅新增PptCOM.cs原有Observe/getter/release调用的子计时，以及managed回归和分析器维护树；未改变调度、分支、2秒超时、COM公共ABI/schema或支持范围。当前完成的是准确定位准备，不声称已消除1.356s长等待。

## dynamic与手动释放审查

普通生产属性/集合/方法仍由MarshalLateBoundComAccessor内真正dynamic分派；没有额外COM读取、反射/PIA包装替代（HWND既有受限标量例外保持）。原取得、借用root、所有权转移及finally子先父后逐笔Release未改，没有GC/FinalRelease。16个开关启用/禁用场景逐项比较原操作顺序/次数、释放账本/顺序、raw页码、polling、生命周期/generation，正常、busy、missing、Count0、断连、换窗/结束、迟到generation等均通过。独立trellis-check复核也未发现代码缺陷；按审查意见补齐了精确子阶段/children/unknown规范。

## 自动化验证

- managed Release|AnyCPU：ARM64原生MSBuild构建exit0，全部coretests exit0，stderr空。原动态/所有权/会话/诊断回归与新增16场景通过；日志Build/Validation/observe-substage-managed-20261101/managed-*。
- 原生ARM64 MSBuild，完整InkeysRepo.sln Debug|ARM64、PreferredToolArchitecture=arm64，同invocation规范化PATH/禁用节点复用，预算900秒：exit0，5.90秒，最终增量0warning/0error。沿用已验证PDB允许环境，不更改工程/工具链；已有pwsh.exe PATH后处理提示未影响solution退出码，未为此修改无关配置。
- DLL显式交接、最终EXE内嵌只读校验：55EA18DFE829E2FFD3EA4E9503D2F288BE61193A231FC4BF1D08D0CF9542413A，完全一致。TLB仍为C652B5DD0603576033B2E0D15869BE103F23FFDCAA2A1984E53F3B6A14DBA606，与原文件字节一致。
- 分析器17项自测通过。独立旧v1兼容复核保留1577事件/749span/19请求/0issues及所有原聚合字段；无子阶段显示null/unavailable，不倒填0。
- 采集PrepareOnly通过，未启动GUI；main.json前后SHA一致。元数据Build/Validation/ppt-navigation-20261101-121713-748-5a555a6d/manifest.json，标记prepared-only，不是人工采样。
- 原文件BOM/CRLF和git diff --check通过。没有重复运行未受影响的native/全headless WARP矩阵；本轮只修改managed计时与分析脚本，已运行对应回归和完整solution。

## 安全调度设计结论

只读研究见owner-scheduling-options-20261101.md。当前并非逐命令完整现场扫描：pending提示时先Observe，慢维护才固定ROT→Observe→排水。新方案不能把cached stamp当现场证明；轮询/Count0/missingCount、换窗/HWND、重入代次及墨迹准入需要分别验证。ROT若让出，应取消本次部分扫描、finally释放本轮全部资源，再显式Deferred，不能返回partial best或持临时图递归执行命令。让出后不能继续发布旧观察局部值；最小控制proof不等于InkReady。

## 待用户人工采样

AI本轮没有操控桌面、启动Office或产品GUI，没有commit/push。沿用Scripts/Start-PptNavigationDiagnostic.cmd，新目录独立保存。请用户只做3↔4控件约5轮，优先4→3 Previous，正常退出Inkeys后回复完成。拿到真实getter/State/HWND/释放子阶段后，再实施有证据且保持准入安全的最小调度修复；原像素/真实Office退出验收不补勾，不归档任务。
