# 2026-11-01 退出阶段版实测结果

## 样本与版本

- 目录：`Build/Validation/ppt-navigation-20261101-135913-372-bfe291f9`，PID75956。
- HEAD f1267e12；EXE SHA256 DCBD18044222815605B448C40FEA6015C9DAA84DAF826033824F95E3DBC864E8。
- DLL 与 EXE 内嵌 DLL 均为1061DDF51BD7CF646030AC74830DCD67CE74508448D879D70BBEE3EAA4D850E3，采集错误0。
- 用户人工操作；AI没有启动GUI或操控桌面。本轮没有产品代码改动、构建或提交。

## 正常退出：本轮成功，旧故障未定位

manifest status=exited、exit_code=0。43个native ShutdownTrace span均闭合，首次set-off begin至main-shutdown end为973.6181ms；main-shutdown自身781.1611ms。最长关闭阶段包括既有PPT服务状态等待500.8554ms（主线程join与其重叠，不能相加）、Window关闭122.1533ms、Draw3停止101.4260ms；Arm返回141.6977ms。

managed shutdown-managed-finally10.4083ms，cleanup-final3.9292ms，event-disconnect3.1889ms；window/presentation/application release作用域分别0.0052/0.2673/0.1506ms，均返回。未见event-retire和retired-event-retry/wait阶段；仅能据当前路径判断没有走退役重试，不能据此证明所有实际Office资源已释放或PowerPoint进程已结束。

本轮没有触发15秒0xE1430016兜底。上次130949-399-0a27afde经用户确认正常菜单/设置退出，却发生该兜底；旧版缺少退出阶段，因此本轮成功不能倒推其卡点，也不能归为已修复、helper创建失败或COM泄漏。暂不盲改清理逻辑/释放次数/超时；保留默认关闭阶段诊断以捕捉以后复发。PowerPoint退出及下一文稿打开没有本轮人工证据，单列NOT VERIFIED。

## 导航：长尾仍在，ROT让出不足

离线分析退出0：9563事件、4678span、47请求、0issues、0incomplete。30次Next最小14.853ms、中位252.816ms、最大1636.5365ms；17次Previous最小5.876ms、中位138.847ms、最大1013.0081ms。本轮未按统一冷暖/页面/间隔对照，不据这些分布声称稳定改善。

最慢Next epoch39 / native span240：

| 边界 | 毫秒 |
| --- | ---: |
| native总请求 | 1636.5365 |
| managed邮箱排队 | 1238.9103 |
| maintain-binding剩余（包括ROT与微量本地开销） | 585.2994 |
| 其中ROT子span重叠 | 585.2461 |
| 随后完整Observe重叠 | 653.5884 |
| owner执行 | 396.8595 |
| 其中Next当前页预检 | 250.1071 |
| View取得 | 62.4652 |
| Invoke | 83.9622 |

585.2994+653.5884=1238.8878ms，与邮箱排队差0.0225ms。此条进入rot-forced；当前一次让出后的公平预算使本轮完整扫描不可延期。另Previous epoch4即使成功deferred，仍等待758.9417ms完整Observe；因此只处理ROT也不足。

本轮7次rot-deferred、7次rot-forced，证明让出与完整尝试均走到。不能把让出触发次数当作导航性能验收通过；下一步需同时审查forced发现的公平边界与完整Observe前置成本，保持真实场次/窗口校验、动态COM、逐笔释放和原墨迹准入。

## 等待反馈：用户明确失败

用户原话：不可见，同时等待完成后页数才发生跳变。

三点提示实机仍不可见，AC4/AC7不可标通过。等待期间保持真实旧页码符合合同，但必须显示等待标记；不能用提前跳页伪造成功。继续追踪PublishNavigationWaiting→Ppt→PageControl状态和渲染提交，已有秒级样本足够，不要求用户重复同版测试。

## 本轮结论与下一步

- 本轮正常退出成功；旧正常退出超时仍属未定位偶发故障，保留捕捉能力。
- 导航长尾及等待反馈均未通过，任务保持in_progress，不归档、不commit/push。
- 独立复核原始日志；只读研究等待渲染路径与安全的Observe身份/详细刷新分离方案。实施前应把最小文件边界、现场proof、异常/兼容回退、旧raw/descriptor失效和确定性释放回归写明，不能直接用cached stamp跳过现场检查。
