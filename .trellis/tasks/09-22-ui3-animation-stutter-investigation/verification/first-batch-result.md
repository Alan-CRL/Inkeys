# 第一批实施结果

日期：2026-09-23。用户于上一轮明确批准 S1–S4，本轮已完成实现和无 GUI 验证。未提交 Git、未 push，原始偶发故障仍保留现场验收项。

## 已完成

- **S1**：移除 11 个颜色块的 22 处中间描边目标；保留统一缩放目标、批次时长同步及一次性显隐/换边重定向。真实动画模块覆盖展开、收起、反向、不均匀正 dt，比例与最终收敛断言通过。
- **S2**：三个 Scene 私有 helper 逐层返回本次有效光照 damage；所有订阅者仍接收最新快照，只通知真正受影响者。旧光擦除、同区域亮度变化、主光保守全脏、首次订阅及其它业务/恢复需求沿原路径保留。
- **S3**：Bar 与 PageControl 的只读 DC 使用空 RECT 释放；COPY、Win7 clip-stack要求和四阶段成功提交事务保持。
- **S4**：当前回调 TLS 样本、客户端分项耗时、Bar/PageControl 阶段计时、实际 API 结果、源/目标几何、动画/退避计数、遮罩创建与绘制次数已接通。异常/恢复日志默认最多每秒尝试一次，健康帧静默；日志满队列时保留聚合，使用已有文件/线程池的非阻塞诊断 logger。

诊断只观察现有行为；50 ms clamp、退避策略、target容量、exact缓存判定及光影质量未调整。

## 验证结果

| 检查 | 结果 |
| --- | --- |
| 初次完整 `InkeysRepo.sln Debug|ARM64`，ARM64 原生 MSBuild | exit 0，约97秒；发现一处新增C4127并随后修复 |
| 修复警告和补测试后的完整 Solution 增量构建 | exit 0，约23秒，0 error；3个 warning 均来自未修改的 hashlib++ |
| 新构建的 `InkeysHeadlessTests.exe --no-window` | exit 0，`PASS animation correctness`，无FAIL记录 |
| 真实动画/FramePacing模块及当前脏区算法针对性验证 | exit 0，`functional_regression_failures=0` |
| 当前光影生产函数体的计数探针 | exit 0；135/375分片、exact额外1次、跳过0次、诊断关闭不改变绘制、AA状态恢复 |
| 独立全范围审查 | 修复新增编译警告并补齐计时器/sink替换/Stop-restart回归；没有其余需修改的第一批产品缺陷 |
| 差异、编码、项目登记 | `git diff --check`通过；原UTF-8/BOM/CRLF保持，工程XML可解析 |

MSBuild均在同一 PowerShell 调用中先执行 PATH workaround、关闭 node reuse，使用已有 Solution 和 Debug|ARM64；没有升级SDK/依赖或增加构建系统。构建没有超时。构建附带复制的跟踪文件 `Inkeys/PptCOM.dll` 已恢复到初始 HEAD，正式产物保留在 `Build/ARM64/Debug/`。

原始日志：

- [完整构建](../../../../Build/TestResults/ui3-stutter-msbuild-20260923.log)
- [最终增量构建](../../../../Build/TestResults/ui3-stutter-msbuild-final-20260923.log)
- [最终无窗口测试](../../../../Build/TestResults/ui3-stutter-headless-20260923.log)
- [独立审查](review.md)
- [功能修正验证](functional.md)
- [光影计数探针](lighting-counter-results.txt)

## 日志如何判因

在软件现有日志中搜索 `[UI3Diag]`，不必打开调试边框或帧率UI。

- `callbackMs` / `activeGapMaxMs` / 批次耗时：区分 Bar 自己慢、其他客户端占用、下一回调延迟。
- `rawDtMs` / `advancedDtMs` / `advance` / `backoffSkip`：区分长帧限幅与退避领取但未推进的时间。
- `attempt` / `ulwAttempt` / `commit` / `deferred`：区分绘制/呈现尝试、成功与正常布局交接；Retry不直接等于错误。
- `draw/getDC/ULW/releaseDC/endDraw/presentLockWait`：定位实际耗时阶段。GetDC会flush，不能把其中耗时直接解释为复制。
- `rounded/geometry`、`exactHit`、`fallback_*`、`maskDraws`：区分遮罩新建与缓存命中后的多分片工作。
- `latest/slowest/lastFailure`：保存epoch、backend、target/capacity、viewport/source、zoom、退避状态、HRESULT和ULW BOOL/Error，避免最后一个正常帧覆盖异常上下文。

没有新的异常且健康输出未启用时，不产生周期状态日志。sink拒绝或抛异常不会改变渲染结果；退出不为诊断延迟，最后不足一秒且仍处限频窗口的尾部聚合不保证落盘。

## 未验证与后续

没有启动 Inkeys、设置或浏览器窗口，没有执行 Computer Use。headless不包含完整 Scene 实现，故真实 Scene hooks通知次数与完整视觉效果未运行验证；本轮以生产调用链审查、真实相关算法、完整产品构建验证该接线，未用镜像布尔测试冒充集成覆盖。

当前已证明确定缺陷被修正、诊断合同测试通过，不能据此断言故障用户的低概率严重卡顿已完全消失。任务保持 `in_progress`，第一批状态为已实现并验证；待用户反馈或故障日志后，再按原设计判断 D1–D3 是否需要单独实施。
