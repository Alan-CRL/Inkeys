# 2026-11-01 新实测正常退出与导航长尾的独立复核

## 范围与结论

只读复核 `Build/Validation/ppt-navigation-20261101-135913-372-bfe291f9` 的 manifest、stderr、stdout、native log、summary，并对照当前清理控制流及上次 `130949-399-0a27afde` 失败记录。没有修改产品或测试，没有运行 GUI、Office、构建或提交；只新增本研究文件。

本次 Inkeys 正常退出，未复现上次 15 秒兜底；退出与清理 span 全部闭合。但旧故障没有在带退出阶段的新版本中复现，因此尚不能定位旧卡点或宣称已修复。导航长尾仍有明确样本：Next epoch39 总计 1636.5365ms，其中 mailbox 等待 1238.9103ms，不能把本次正常退出视为翻页延迟验收通过。

## 采集身份与独立计算方法

- PID 75956，manifest status=`exited`、exit_code=0、capture_errors=[]。
- EXE SHA256=`DCBD18044222815605B448C40FEA6015C9DAA84DAF826033824F95E3DBC864E8`。
- DLL 与 embedded DLL SHA256 同为 `1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3`。
- QPC frequency=10,000,000；同 PID/QPC 时钟下以 `(end-begin)/10000` 计算 ms，不用日志文本写出顺序代替真实边界。
- 独立解析 stderr/stdout/native log 得 PptPerf 9563 条（managed 9030、native 533），与 summary 一致；额外 ShutdownTrace 86 条组成 43 个完整 span，没有缺 begin/end 或重复边界。
- managed cleanup/shutdown 共 28 个完整 span，包括启动时 rebind cleanup；正常退出 final 的 17 个 span 单独核对，避免把重绑当退出。

## 正常退出时间与阶段

接纳退出后首个 set-off marker 的 QPC 为 1106240444892（约北京时间 14:01:09.940593）；main-shutdown end 为 1106250181073（约 14:01:10.914211）。两者相差 **973.6181ms**。main-shutdown 自身为 **781.1611ms**，因为主线程约在 set-off 后 192.4570ms 进入清理。

manifest ended_utc 为北京时间 14:01:11.5487194，距离首个 set-off 约 **1608.126ms**；这不是精确进程死亡耗时。`Scripts/Start-PptNavigationDiagnostic.ps1:162-168` 在 WaitForExit 和 stdout/stderr CopyToAsync 排空之后才写 ended_utc，包含父采集者观察及流排空。main scope 后仍有局部对象/进程收尾，故也不能仅凭 main-shutdown end 宣称进程已退出；本次进程确实退出的独立证据是 manifest exit_code=0。

| 原调用阶段 | 耗时 ms | 证据含义 |
| --- | ---: | --- |
| set-off-arm | 141.6977 | Arm 已返回；原生 marker 记录 error=0，不额外推断 ArmResult 枚举 |
| set-off 全段 | 142.8850 | Hide/Wake/CrashHandler/Magnifier 原调用均返回 |
| ppt-join-info | 76.0144 | infoThread join 返回 |
| ppt-service-status-wait | 500.8554 | 原 500ms 轮询间隔产生的等待，本次并非永久阻塞 |
| main-join-state | 60.7240 | join 返回 |
| main-join-ppt | 487.6164 | join 返回；与 PPT 状态等待重叠，不能重复相加 |
| main-stop-draw3 | 101.4260 | 原 StopProduct 返回 |
| main-setting | 5.1461 | 设置退出返回 |
| main-window | 122.1533 | Window StopAndJoin 返回 |
| main-thread-status-wait | 0.2337 | 没有消耗原 20×500ms 等待上限 |
| main-com-cleanup | 0.2225 | FreeLibrary、activation context、CoUninitialize 阶段均闭合 |

完整原生序列见 stderr 第 9475-9598 行；native 日志第 1063-1065 行还记录了等待各线程、COM 反初始化完成及关闭程序。没有发现未闭合的退出阶段。

## managed 释放与 retired lease 的证据边界

最终退出记录在 stderr 第 9517-9548 行：

| 阶段 | span | 耗时 ms |
| --- | ---: | ---: |
| shutdown-managed-finally | 4572 | 10.4083 |
| shutdown-mailbox-stop | 4573 | 4.2748 |
| cleanup-final | 4574 | 3.9292 |
| cleanup-unbind | 4577 | 3.2374 |
| cleanup-event-deactivate | 4578 | 0.0101 |
| cleanup-event-disconnect | 4579 | 3.1889 |
| cleanup-release-window | 4581 | 0.0052 |
| cleanup-release-presentation | 4582 | 0.2673 |
| cleanup-release-application | 4583 | 0.1506 |
| shutdown-retired-events | 4587 | 0.0080 |

invalidate、工具缓存、根字段与页码 reset 也均闭合。随后 native ppt-managed-service、ppt-service-snapshot-reset、ppt-state-worker 都结束；表明本次 managed owner 清理返回，未卡在 Unadvise、root SafeRelease 或服务快照重置。

本次整个采集没有 `cleanup-event-retire`、`shutdown-retired-event-retry` 或 `shutdown-retired-event-wait`。按当前 UnbindEvents 和最终最多三轮退休重试的控制流，观察到 disconnect 后不进入 retire，最终退休外层 scope 完成且零 retry，支持**本次该 owner 退出时没有待重试的 CP lease**。普通 `observe-retired-window-release` 是观察窗口替换的临时对象释放，不是事件 CP retirement，不应混读。

这些 marker 包的是原调用边界，不是每笔真实 COM 引用计数账本；尤其 SafeRelease 内部保留了原异常吞吐处理，scope end 只证明函数返回，不能单独证明 ReleaseComObject 的实际计数归零。也没有 PowerPoint PID 死亡/重新打开另一份文稿的观察证据。因此不能由本次闭合进一步宣称“PowerPoint 已自动退出”“全进程没有任何 COM 泄漏”或“下一文稿必然能打开”。

## 导航长尾仍然存在

summary 的 complete/0 issues 表示诊断配对完整，不表示延迟问题解决。独立按原始 native-request begin/end 得 **47 请求：Next 30、Previous 17**；最大值与 summary 一致：native span240，Next，epoch39，request39。

- native-request QPC 1105708600089 → 1105724965454：**1636.5365ms**。
- mailbox-enqueue QPC 1105708603820 → mailbox-dequeue 1105720992923：**1238.9103ms**。
- owner 执行：**396.8595ms**；其中 command-precheck 250.1071ms、view-get 62.4652ms、invoke 83.9622ms。不能将整个 1.637 秒当作 View.Next 调用时间，也没有测量实际画面完成。

邮箱等待分解为同一 owner 上的两个连续区间：

| 占用阶段 | 完整阶段 ms | 与邮箱等待重叠 ms |
| --- | ---: | ---: |
| maintain-binding span2411 | 713.1839 | 585.2994 |
| 其中 rot span2413 | 713.1190 | 585.2461 |
| Observe span2417 | 653.5884 | 653.5884 |

父项 maintain-binding 的剩余 **585.2994 + Observe 653.5884 = 1238.8878ms**，与 mailbox 的差仅 **0.0225ms**。严格 rot 子项为 585.2461ms，父项多出的 0.0533ms 是其它维护开销，不能把父子再次相加。该轮有 rot-forced，表明公平策略要求完整扫描的场景仍可占用 owner；本轮退出诊断并未消除这条长尾。

原始证据：native log 第 682-698 行；stderr 第 4902-4980 行。已有足够证据确认长尾主要来自 owner 在命令开始前完成 ROT/Observe，并非 focus（本样本 0.0475ms）或 native 队列（0.1625ms）。至于进一步调度方案，必须另按真实 COM acquisition/finally 与场次正确性合同设计，不能凭本次正常退出宣布无需修复或擅自削弱现场校验。

## 与上次 E1430016 的比较

上次 PID87084 的 manifest 为 exited-nonzero，exit_code=-515702762，即 0xE1430016；用户已确认正常菜单/设置退出。该码证明进程内 15 秒截止兜底结束了仍存活的 Inkeys，不能单凭它推断外部 helper 创建失败。

旧样本 stderr 没有 ShutdownTrace、shutdown-managed-finally、cleanup-final、disconnect 或 root release 边界；没有线程栈或 dump。新样本所有相应阶段很快闭合，只能确认这次没复现，并展示下次若卡住时可定位的阶段，不能把旧故障倒推给上次没有标记的某个 join、Unadvise、RCW 释放或 ROT。两次运行的时序/Office/设置状态也不是受控一致的反事实。

本轮修改仅增加诊断并保持原退出顺序与截止，不能把一次成功归为已修复旧故障。当前准确状态是：**本次正常退出通过；旧偶发退出超时的根因仍未知；翻页 1.6 秒级长尾仍有明确证据。**
