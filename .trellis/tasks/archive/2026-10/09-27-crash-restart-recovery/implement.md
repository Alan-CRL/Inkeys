# 执行清单

前置：只读链路可在 Work 0 开始；共享代码写入待主 agent 冻结状态/窗口/Draw3 owner。出口：正常、主动、崩溃、自动、恢复分别有证据状态；GUI 缺口列人工步骤，交集成。

- [ ] 读启动、异常、退出、重启、单实例、PPT、UInk 当前代码与历史任务，画调用/失败链。
- [ ] 分别建立正常退出、主动重启、未处理异常/线程/初始化、loop、最后已提交恢复的验证项。
- [ ] 对最后已提交 UInk/索引做文件完整/身份隔离验证；单独追新进程可见恢复入口，未开放能力标不适用、实际受支持但无 GUI 则需人工。
- [ ] 对 confirmed 风险先建隔离回归，再最小修复；不留下正式默认可触发崩溃入口。
- [ ] F-029：生产 Desktop slot 源选择/快照无 HWND 红→绿，覆盖活动 Desktop、PPT/Whiteboard parked、空页/关闭开关和页身份；现有 Desktop service 文件/索引回归，独立 diff review。
- [ ] F-026/F-027：先验证 fatal 上报与一次性/显示门禁，再在 controller 存活时处理已完成数据和活动 contact 的 CPU 屏障；若不能安全保存，保留发布阻塞。跨 HWND generation 真机恢复不以重新 Start 空文档代替。
- [ ] F-031：先用生产 PPT builder 的无 HWND 双文稿 retained map 反例红测，所有 active/parked submit 调用点显式传同槽 map，绿测后复核隔离 UInk 导出/严格导入与真实 Office 多文稿/结束页人工门禁。
- [ ] F-038：生产活动 load 安装 helper 无 HWND materialize→install→builder 红绿，补齐 retained map 同槽迁入、拒绝安装保留当前状态；独立 diff review 与真实 Office load/save 人工门禁。
- [ ] F-039：生产 materialize T1 `{101,102}`→T2 `{101}` 红绿，缺席旧 active 102 保留完整 retained 身份；冲突/EndScreen/新 mutation 及 F-038/F-031 回归，真 Office 拓扑交错人工门禁。
- [ ] F-042：按用户报告追 Draw3 输入/清屏/切模式/呈现/退出停顿的同一或不同根因，先冻结低开销诊断和无 HWND 可达故障测试，再做最小修；真实 GUI 案例留独立人工步骤。
- [ ] F-043：从历史定位原 15 秒保护和移除原因，设计统一请求状态/线程或受控子进程 owner，先无 GUI 真隔离进程红绿测试，再接所有正常退出/主动重启入口；独立 reviewer 核重复拉起、持久化屏障与 Win7 API。
- [ ] 静态/headless 测试并独立复审；真实进程 GUI 运行、idle/绘图/退出边界在未获授权时保持需要人工。
- [ ] 记录自动重启与数据恢复不同结论、能力边界和可执行人工步骤。
