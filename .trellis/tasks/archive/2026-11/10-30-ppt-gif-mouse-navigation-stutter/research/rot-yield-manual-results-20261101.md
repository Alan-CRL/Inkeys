# Research: ROT让出修复的人工采样独立复核

- Query: 核验修复版ROT Deferred/下一轮forced是否实际发生，是否在扫描清理后仍保留完整Observe再执行请求，以及与上一轮性能如何比较。
- Scope: internal；只读当前源码和两轮已保存日志，未运行产品、Office、GUI、构建或git操作。
- Date: 2026-11-01

## Findings

### 1. 采集身份正确，但本轮不能记为整体通过

本轮目录 `Build/Validation/ppt-navigation-20261101-130949-399-0a27afde`（下文简称`<run>`），PID **87084**。

- EXE SHA256：`7A7D7832EA3B695B7B8B715E7DA67DE086CF214643DFF457BB528B423301B17F`。
- 随附与实际内嵌PptCOM SHA256均为 `351A529267C75C3F8588F8887626ABF22F6FECE36892AFA65D4C0DC1416C876B`，符合修复版；native诊断标记存在。
- **15条**导航请求；原始记录经现有解析器函数独立读取为 **7191条唯一事件、3564个span、0issues、status=complete**。数字complete只表示这些诊断区间配对完整，不代表退出正常或全部验收通过。
- manifest的进程退出为 **exited-nonzero**，`exit_code=-515702762`，即 `0xE1430016`。该值对应 `ShutdownSupervisor.cpp:47` 的 `kFallbackForcedExitCode`。用户已明确从Inkeys菜单/设置正常退出，不能按手工结束进程处理。
- 本研究不诊断退出卡在哪个阶段，也不将异常退出直接归为COM泄漏；主会话正在单独检查。

### 2. Deferred实际触发，且未绕过完整Observe

最慢Previous为native span19 / epoch4，managed request4；以下取原始QPC重算，频率均10MHz，实际owner线程 **87720**，调用managed的native业务线程 **80448**，UI生产者线程 **50272**。

| 阶段 | span | QPC开始 | QPC结束 | 时长ms |
| --- | ---: | ---: | ---: | ---: |
| maintain-binding |888|1075824437151|1075827721509|328.436|
| ROT |889|1075824437206|1075827720977|328.377|
| rot-deferred标记 |893|1075827721390|1075827721470|0.008|
| 完整Observe |894|1075827721588|1075834140315|641.873|
| Observe末尾temporary释放组 |916|1075834139989|1075834140264|0.0275|
| mailbox-dequeue |917|1075834140529|—|瞬时|
| owner Previous |918|1075834140896|1075835686820|154.592|
| 实际Invoke |920|1075834750449|1075835683801|93.335|

原始依据：`<run>/stderr.log:1816–1825`、`:1868–1877`；native请求边界为`<run>/native/idt1793509790830.log:163`和`:175`。

可直接验证的顺序是：**ROT区间已结束 → rot-deferred → MaintainBinding已返回 → 完整Observe（包括末尾释放组）已结束 → 出队 → owner执行 → Invoke**。owner起点比Observe终点晚 **58.1微秒**，不是把命令搬到Observe前。

资源边界要结合源码解释：`PptCOM.cs:1149–1166` 的外层finally会在scanner返回前清候选列表/枚举器/ROT，`MaintainBinding:1183–1191` 在scanner返回且rot scope结束之后才写rot-deferred。因而本轮的完成边界与“解扫描资源后再Observe”一致。**日志没有逐receipt引用计数，不能单靠这组时间证明真实RCW引用归零；逐笔所有权仍依赖原finally、已通过账本测试和代码审查。**

该请求总时长 **926.7537ms**，其中：

- native队列 **0.1581ms**；
- managed精确邮箱驻留 **771.2065ms**；
- owner执行 **154.5924ms**，其中View取得 **60.9144ms**、实际Invoke **93.3352ms**；
- focus **0.2697ms**；
- 邮箱驻留被maintain-binding剩余 **129.304ms**加Observe **641.873ms**几乎完整覆盖（精确合计 **771.1772ms**，仅余 **0.0293ms**间隙）。

所以Deferred机制实际进入了新路径，但本样本剩余主要等待已是原完整观察，不能把约927ms称为“Previous方法自身耗时”。

### 3. 下一次到期出现forced，未观察到连续Deferred

`<run>/stderr.log:1956–1961`：同一owner线程的维护span955内，先写`rot-forced` span956，随后ROT span957有完整begin/end：

| 阶段 | QPC开始 | QPC结束 | 时长ms |
| --- | ---: | ---: | ---: |
| rot-forced标记 |1075840989598|1075840989631|0.0033|
| 随后ROT尝试 |1075840989660|1075841205729|21.6069|
| maintain-binding |1075840989531|1075841205940|21.6409|

按manifest QPC/UTC锚转换：Deferred标记约 **13:10:28.668（上海）**，forced标记约 **13:10:29.995**，相隔 **1.327秒**，其中包含上一轮完整观察/命令/其它维护与原500ms调度。

本轮总共只有 **1次rot-deferred、1次rot-forced**；本轮日志中的下一次到期确实进行了不再延期的ROT尝试，与源码`rotScanDeferredLastPass`预算相符。不能由此声称已在真实持续pending压力下证明公平性：forced这轮是否始终有pending未由标记本身证明；持续pending的边界目前仍以已执行的集成测试/代码为证。

源码锚：`PptCOM.cs:1171–1191` 的预算和forced/deferred路径，`:1210–1223` 的缓存资格，`:1444–1480` 的原有完整Observe→排水顺序，`SlideShowSession.cs:303` 的只读导航队列查询。

### 4. 残余延迟仍以Observe/原有COM查询累积为主

第二慢Next（native span43 / epoch8）总 **867.1026ms**：邮箱 **458.6710ms**，owner **407.4281ms**，focus **0.1052ms**。邮箱几乎被当次已执行中的Observe剩余 **458.666ms**覆盖。owner又包含预检查 **264.8759ms**、View取得 **61.9233ms**和Invoke **79.7672ms**。

另一个Previous（epoch9）总 **423.2410ms**，邮箱 **287.8182ms**，对应完整Observe剩余 **287.812ms**。annotation也覆盖了其它两条请求的约114ms和29ms邮箱等待。当前修复不消除这些已经开始的维护/观察调用。

本轮15个请求均有完整导航边界、managed结果为1；未发现本轮2秒wait timeout。此结论只覆盖导航记录，不能推导整程序退出或Office及时退出。

### 5. 与上一轮只做描述性比较，不宣布稳定性能改善

基线为`Build/Validation/ppt-navigation-20261101-122321-464-4c57406c/summary.json`，是细分诊断版、未实现ROT让出的采样。

| 指标 | 上轮 | 本轮 |
| --- | ---: | ---: |
| 导航请求数 |15|15|
| Next / Previous数 |8 / 7|7 / 8|
| 请求总时长中位ms |98.758|163.567|
| 请求总时长最大ms |1610.020|926.754|
| >=1000ms请求数 |1|0|
| Previous中位ms |168.752|226.654|
| Previous最大ms |1610.020|926.754|
| Next最大ms |706.103|867.103|
| 邮箱等待最大ms |1500.262|771.207|

本轮最大值降低、Deferred确实触发，但中位数并未降低、Next最大值反而较高。两轮操作顺序、间隔、动画状态、冷暖与Office负载不是严格配对控制；不能据最大值计算收益百分比、P95或承诺稳定改善。可以确认的是**新让出路径实际生效，且最大请求剩余主要由完整Observe解释**。

## Files Found / Code Patterns

- `<run>/manifest.json`：版本、QPC/UTC锚、实际PID、采集完整性与异常退出。
- `<run>/stderr.log:1816–1877`：Deferred后完整Observe、temporary释放与owner命令的原始顺序。
- `<run>/stderr.log:1956–1961`：下一到期forced/ROT尝试。
- `<run>/native/idt1793509790830.log:163,175`：native span19请求边界、不同线程生产/完成。
- `<run>/summary.json`：主会话已生成的聚合数据；本研究直接重读raw并重算核心区间，没有覆盖该文件。
- `PptCOM/PptCOM.cs:1149–1166`：扫描外层finally；`:1171–1191`：预算与返回；`:1444–1480`：现场观察后排水。
- `PptCOM/SlideShowSession.cs:303`：gate保护的导航类型查询。
- `Inkeys/Inkeys/Helper/ShutdownSupervisor.cpp:47`：异常退出码对应fallback forced exit。
- `Scripts/analyze-ppt-navigation.py:135`起：按source/pid/span和包含区间构造导航报告，未把不同层编号视为相同身份。

## Related Specs

- `.trellis/spec/ppt-interop/com-contract.md`：真正dynamic、owner、每笔取得与手动finally释放、不能依赖GC/FinalRelease。
- `.trellis/spec/ppt-interop/native-session-ui3.md`：PptPerf协议、时钟/身份关联、Deferred与公平预算、完整Observe及ready链。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：验证证据与实际设备验收应分开。
- `.trellis/workflow.md`：研究落盘，不以对话代替长期证据。

## External References

无外部查询。本报告仅依据当前源码及用户本机已采集文件，未引入新的Office/Windows版本结论。

## Caveats / Not Found

- 未启动产品/GUI/Office、未运行构建/测试、未操作git、未改源码或规范；仅写本报告。
- 这些毫秒数是代码边界/QPC区间，不是屏幕光学翻页时间，也不区分Office内部CPU、动画呈现、RPC等待的细节。
- 实际引用计数、PowerPoint及时退出与下一文稿仍不能由时间span证明；本轮异常退出更不能记为关闭验收通过。
- 退出异常是否为本次ROT改动引入，或原有关闭路径问题，未由本研究证明；不能仅凭时间邻近归因COM泄漏。
- 下一forced尝试并不提供真实持续pending负载证明；严谨公平性仍依赖集成测试和源级约束。
- 不能由本轮较低最大值宣称稳定收益，后续修复/观察拆分也不能跳过场次、窗口和墨迹准入校验。
