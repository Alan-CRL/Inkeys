# 发布基线同步验证结论

- 来源：chore/publish@e2c212d4d9cd68367c08363c450d392e9d63bcef；目标基础：fluent@834307a4。隔离分支 codex/fluent-setting-sidebar，保留 MERGE_HEAD，不创建 merge commit。
- 17 个实际发布视图、12 种更新状态完成来源/实施映射与独立复核，文字、控件、隐藏、配置、即时同步、保存和 FIFO 按记录逐页对应。Preset/Performance 无可达入口；SettingDarkMode、Fluent 窗口/字体/布局/常驻合同保留。
- 完整 ARM64 native MSBuild InkeysRepo.sln Debug|ARM64 退出0（486.7秒）；最后分组原文修正后的完整Solution增量退出0（13.0秒）。同一PowerShell invocation保留原Path、Remove Env:PATH后仅恢复单一Path，/m:1 /nr:false，允许900秒。
- 最终重编产物 InkeysHeadlessTests.exe --no-window 退出0，包含实际生产PPT同步helper连续拖动/重置/加入/取消等回归；i18n check退出0，三语言366/366。本轮准确源码/工程/资源路径diff检查通过、编码和CRLF保持。
- 原工作区IdtMain.h/PptCOM.dll/PptCOM.Tests全部记录SHA256已再次核对一致；fluent与chore/publish指向原提交，原Vcpkg和HiMsg源码/索引clean。
- 发布新增HLSL的配套inkPixelShader.cso与PptCOM.dll由既有构建生成，hash和理由记录于runtime-merge-validation.md；没有修改shader源/编译参数。
- 全量merge空白诊断中29个原发布文件与固定source blob相同，不做无关格式清理；记录于inherited-whitespace.json。本轮自有源码检查通过。
- 既有LnkHelper同步I/O/COM初始化边界与发布硬编码中文保留，已独立复核并转父任务后续。未执行GUI/Computer Use、HWND fixture、Office或真实磁盘交互；无窗口测试不等于完整Pages/Shell实际点击验收。

本前置任务的来源映射、编译及无窗口复核通过，可开始setting-sidebar-style-hover。真实鼠标、截图、DPI/窗口/常驻会话等实机验收仍由父任务承载；详细证据见content-source-audit.md、implementation-setting-content.md、baseline-check.md、runtime-merge-validation.md及原始日志。

交付日志仅移除本轮新生成构建stdout行尾的无意义空白，保持原换行与全部诊断正文/退出记录；原全量空白诊断以`research/merge-diff-check-preflight.txt.gz`原字节保留。

固定来源的原始快照保留在 Git 忽略的 Inkeys/Cache/SettingBaseline 下；研究路径和 audit_sources.py 已同步，提交差异仅保存来源、映射与哈希证据。

后续用户授权：将同一已验证tree转入 D:/Project/Inkeys/Repo/Inkeys 的 fluent 工作区，不保留临时分支。此前“原工作区不变/隔离保留”描述的是上一轮交付状态；当前转入及原目录重编证据统一见父任务 research/fluent-integration.md。旧PptCOM.dll及原用户测试产物先备份，IdtMain.h用户内容保持。仍未授权commit/push/GUI。
