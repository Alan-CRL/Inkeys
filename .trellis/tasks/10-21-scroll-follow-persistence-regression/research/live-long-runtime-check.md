# 首次真实长程尝试：独立失败边界核验（PID16860）

2026-10-23，Asia/Shanghai。任务保持 in_progress。checker只读现有原始日志/7张专用PNG/实际私有文件并做离线复算；没有源码修改、构建、产品CLI、GUI、新截图、重试或降低门槛。

## 结论

**十屏长程未通过。** 同一场内的短纵向和水平正反向已取得有效像素证据；第一步128像素长程移动触发三次一致性拒绝并暂停，owner保持最后可信(16,16)。没有完成第一个long step，更没有达到十屏峰值、初始特征全离屏接力、返回A、B或正常成功路径文件读回。不能用前四个小步和规划151步替代实际长程验收。

## 关联与前序通过

- Round `product-ink-long-live-1023-6135484`；root `Build/ScrollFollow/product-live-16860-6135937`。
- launcher PID16860、exit1/50.590秒，显式timeout500000ms；启动前文件hash24D8FAE8，完整值见long-stage review，LastWriteUtc2026-10-23T00:49:35.5876427Z。它是磁盘启动前关联，不是进程内存hash。
- stdout25802B/stderr68700B；checker将stdout全部ProductLive/ProductLiveInk/ProductLiveLong行与私有result.log逐行比对相同。
- 前四条真实tagged wheel：纵向+32/-16，水平+32/-16，Source最终(272,528)，相对起点(16,16)。非空辅助呈现、真实初始ACK及原输入凭据保持。
- 独立脚本 `check_ink_long_failure_16860.py` 输出root/checker-long-failure-verification.json。已保存6张A图（primary/selection/forward/reverse/horizontal-forward/horizontal-reverse）均17243 core、unknown0；边缘区间法确认新增已知浅边缘，灰底/固定色/走廊要求保持。五次核心集合严格整数平移相同，双向各34486点、最大/中位误差0像素。
- 所有18条既有SourceGT记录均99/99+精确ROI1，没有删点。实际另有1张源before-product PNG；没有失败后新截图。此处共7图，不是完整案例的8或更多图。

## 失败的精确位置

规划输出十个主屏高度19200像素、每程151步只是计划。下一条真实WHEEL delta=-960把Source从(272,528)移动到(272,656)，原Source paint sequence6及纹理hash已记录。随后：

| Frame | accepted / reason | 初始存活点 | 累计可信位移 |
| --- | --- | ---: | --- |
| 198 | 1 / None | 286 | (-16,-16) |
| 199–201 | 0 / InconsistentTranslation | 拒绝结果默认0 | 保持(-16,-16) |

三次拒绝均inliers2、FB P95约.878处理像素，覆盖率0。原普通三帧拒绝规则锁存Paused；没有把未知位移提交到owner。最终facts：business Paused、Capture accepted201/rejected0/API0、latest/applied198、viewport(16,16)、clamp0、当前ACK1，随后停止和清理正常。

独立逐帧检查现存201条同session1/capture_session1、frame1–201唯一连续：Tracker accepted197（含初始化成功）；frame2为尚未Tracking的NotInitialized，199–201为本次三拒绝。不存在任何 `accepted=1 reason=None initial_survivors=0` 帧；拒绝结果的零统计绝不能算初点退休。Long step完成日志0条，无peak/returned日志。

## 根因证据边界

本次明确记录失败阶段为追踪平移一致性门，但**不能由此唯一认定是LK/纹理算法或Mag刷新/PrintWindow的根因**。Capture201 accepted/API0是API和质量有效，不是128像素后每帧内容与Source完整像素匹配证明；post-settle SourceGT尚未执行，Mag原始失败帧也没有落盘。source paint/hash证明专用源已绘制，不证明每张Mag读回完全正确。安全检测security0同样不能代替完整新帧真值。

这是设计/调查判断项，checker不修改算法、步幅、可信度门槛或替换截图路线来消除失败。后续需单独设计受控帧真值/离线回放调查；本报告没有执行新捕获或推断确定根因。

## 清理与失败保存

state/mag均returned1/failed0；product/service/mag_ready/capture_occupied/own_left_held/log_failed全0，result1。没有AV、watchdog超时或自己Down未归还。

失败清理仍沿原Exit提交一条A，不能称本case两笔成功保存验收。独立标准库MessagePack观察实际 `AutoSave/desktop/2026-10-23/085455457_e495fa53.uink`：6433B、v10、唯一Exit索引fileGuid匹配、一条Pen227点，A首点(816,706.0157470703125)、viewport(16,16,1)。它保存最后可信视口，而不是把新Source y144强行写入。SHA256 `4b9a2b90f69a6ad4c22fb46631f18ecc90d5a9e371c3486288891cf1bf4a91b2`；输出root/checker-failure-file-observation.json。此Python字节观察不是本次成功路径ReadInkExit/codec重跑。

诊断队列dropped1/contention1/full0；所需201条frame连续，但不能称全部诊断零丢失。

## Findings (fixed)

无产品源码修复。独立离线脚本初版的stdout过滤遗漏ProductLiveLong前缀，已在忽略脚本修正；原始日志/图像未变，最终完整复算exit0。

## Findings (not fixed)

- 首个128px步追踪拒绝的确定根因未证，属于后续调查/算法判断范围，未自行修改。
- 十屏、初点退休后可信接力、峰值/返程、返回墨迹、B及完整长程Exit成功链未通过，不能标记完成。
- 小步二维采样、正常暂停保留可信位置与资源归还可以单独记录，不能扩张为长程/硬件/浏览器通过。

## Verification

- 原始版本/退出/facts：pass（核实实际失败）。
- 已存6张墨迹PNG、五次二维核心位移：独立pass。
- session/frame/退休证据和未完成边界：pass（无可用退休证明）。
- 正常清理及失败Exit文件字节观察：pass。
- 长程验收：fail，exit1。

## 五层首次实际长程：安全取消而非追踪失败（PID30628）

2026-10-23。round `product-ink-lk5-long-1023-current`，root `product-live-30628-7972109`，启动前8DAF6D3B产物、timeout500000ms，PID30628/exit1/143.324秒。root因夹具预计耗时超监督预算，对核实归属的专用Source发送WM_CLOSE安全取消；原日志相符 `source_cancel ... message=16`，没有强杀或把该run称为十屏通过。

实际16/151个down步骤完成，距离1952像素；加前序纵向净16后Source总(16,1968)。第16步累计elapsed93672ms，约5.855秒/步。最新/applied661、viewport(15.9999,1968)、clamp0，依然Tracking、confidence1；没有本run三拒绝Pause。正常cleanup全归还。这与上文L3首128步InconsistentTranslation是不同结果，不能混为又一次追踪失败。

已核有限实屏接力正证：同session1/capture_session1，frame609 accepted1/None，total(-16,-1840)、initial_survivors0、added45；随后610及最终661仍可信/initial0，最终inliers318/confidence1。它证明该次初点退休后有限继续跟随；未达到19200峰值，也没有返回A、B或成功路径Exit读回，整体长程保持未完成。

### 下一步仅限测试 GT 的批量读回方案预审

当前ScreenSourceGt每次99个screen GetPixel，且每步前后各一次；每步慢约5.8秒与同步读屏瓶颈相符，但未逐项独立计时，不能唯一归因或说产品每步需要5.8秒。批准方向是仅优化测试oracle：一次主屏DIB BitBlt(SRCCOPY)+GdiFlush，然后内存检查**原全部99坐标与原256×192 ROI每像素**。它把不同时刻的99读点合为一次屏幕快照，不减少点或放宽门槛。

必须保留CheckDesktop/Source paint一致性、失败三点RGB及窗口元数据、所有GT和墨迹阈值、源步长128、距离19200、480秒监督。DC/resize/BitBlt/flush任一步失败即失败。完整screen buffer仅瞬时内存，不在正常/失败任一路径落盘；仅所有验证通过后可复制/保存原专用crop。需要新short及同128/19200真实case验证此oracle仍读真实画面，不能仅以提速称完成验收。不得因此调整产品Mag、Tracker、采样或呈现。

checker对该方案无原则阻断，后续须审精确diff；此段尚不是新批量实现或GUI通过证据。
