# 完整原生十屏往返：独立验收（PID6740）

2026-10-23。**本机专用纹理、真实OS鼠标经RTS的二维＋十个主屏高度往返完整案例通过。** 任务保持in_progress；外部应用、物理笔/触控、旧系统及完整编辑矩阵仍另行验收。旧失败/取消run保持原始结果。

checker只读原始日志、11张已有专用PNG及实际index/UInk，运行离线复算；没有产品源码修改、构建、产品CLI、GUI、新截图、commit/push或结束任务。

## 版本与原始证据

- Round `product-ink-gt-batch-long-1023-current`；root `Build/ScrollFollow/product-live-6740-8519687`。
- launcher `Build/ScrollFollow/live-product-ink-gt-batch-long-1023-current-test.json`：PID6740、exit0、160.123秒、严格long参数、timeout500000ms；内部480000ms监督未触发。
- 启动前磁盘EXE SHA256 `D58E04E15D3195388E47B0F5D8D01E7653503D5BE6E423C92DE31019D284BC76`，LastWriteUtc2026-10-23T01:32:36.4789258Z。对应Tracker BF3409（双向LK5）及inl05D40CCE（test-only批量GT），见各自预审。这不是进程内存hash。
- stdout251989B/stderr366436B；所有ProductLive/ProductLiveInk/ProductLiveLong行与root/result.log逐行一致。

## 路径、窗口与接力

| 独立核验项目 | 实际结果 |
| --- | --- |
| 真实源输入 | 306条tagged wheel逐条delta重建坐标一致，无外来wheel |
| 十屏 | 151步去程合计19200px、151步原列表反向返回；每步16–128px，原最大步长/距离未缩小 |
| 实际峰值/返回 | Source累计(16,19216)→(16,16)；long路程38400px、耗时152.688秒，另有前序纵/横路程96px |
| 全量GT | 626条全部99/99、精确256×192 ROI1、batch_copy1；无删点，每次fullbuffer仅22118400B瞬时内存 |
| 帧范围 | session1/capture_session1，frame1–1192唯一连续，全accepted；恰1条Initialized＋1191条None，**无startup NotInitialized** |
| 初点退休 | frame80首次accepted/None且initial_survivors0（total -16,-1840、added45），此后1112帧继续可信/初点0、后续补点14910 |
| 可信度/预算 | None日志confidence最低.9972（四位小数）、最少inliers280、FB/residual P95最大.0728处理像素；日志retained+added全≤320 |
| 实际交接 | 非空Tracking pair(0,1)/ACK1；最终latest/applied1192、Complete/handoff/clamp0，Off pair(1,0)/ACK1 |

每步成功记录来自冻结InkWheel真实paint、两个不同latest且applied位置及至少3个新accepted capture的门槛，并在收敛后检查全量GT；没有手工目标/ACK。每步ACK仅日志观察，初始/最终ACK才是断言。没有逐步owner完整误差数值，不能补造全采样帧GT误差曲线；已记录峰值姿态附近4帧total误差最大约.000854px，返回3帧约.001334px，受四位小数舍入影响，精确最终值见文件。

原暂停条件未改：严重无效图像/布局/时间等立即暂停，普通不可信连续3帧暂停、保持最后可信位置且不自动恢复；捕获超时及场景/设备退休沿既有规则。本run未触发暂停。confidence为工程质量分，不是概率。

## 独立PNG

11图=8A＋1B＋2Source，alpha全255；两个Source全RGB与独立世界纹理公式精确一致，9墨迹ROI均unknown0。浅边缘用独立每通道覆盖率区间交集（非LS投影）核验，固定笔色/灰底、≤5%覆盖、各通道残差≤1和原走廊/core不变：A每图1边缘、B8边缘。

- 原主窗→非空辅助：核心17558/17558，双向35116点，最大/中位误差0/0px。
- 前进32、反向16、水平32、水平反向16、十屏返回、Off：每次核心17558/17562、双向35120点，独立最大/中位误差**1/0px**，通过原≤2px及count±10%门。不能写成全部图零误差或逐位相同。
- 主/辅字节相同；horizontal-reverse/long-returned/Off字节相同。采样结果支持返回及交接稳定，不证明连续显示全程无闪烁或强制cache eviction。

## 实际B与Exit文件

两笔均真实SendInput→RTS消费，各18callbacks/16Packets、decodeFailures0/publishOk18，Down/terminal/recycled/contact依次0→1→2、Drawing完成。B在样式ACK0→1后Down=(816,1346)。`synthetic_sent=0`表示没有contact seam，不能称物理笔输入。

实际 `AutoSave/desktop/2026-10-23/093628735_feef6eae.uink`：12519B，SHA256 `6193a5c5be4094de782149354a874aaef097258c5ac32eb8350ff46d6a8fb2b4`。标准库MessagePack独立消费全部字节：v10、一Desktop workspace/一canvas、两Pen228/229点、首点宽16。唯一Exit index的dailySequence/sequenceInSession均1，fileGuid `ed91a7c0-0400-4ad6-8114-10658fb3df75`与实际文件头一致。

精确viewport **(15.998664855957031,15.999662399291992,1)**，距Source(16,16)的欧氏误差 **.001377165px**；这是视口数值误差，和屏幕核心最大1px分开。保存viewport对final的原`.001`门未放宽。

A首点(816,706.0157470703125)，B(831.9986572265625,1361.9649658203125)。checker用真实raw A(10795,9340)/B(10795,17806)、float32(192/2540)及B保存视口相加精确复现；B整数Down加视口的Y差约.034697px，在受限计算界约.075918px内。该oracle只适用于本OS鼠标、稳定192DPI/主屏1:1，不外推数位板。

原C++成功路径另执行真实Exit索引读取、最终workspace/page/file身份比较、Read Complete和Import Success；Python字节观察不冒充再次执行codec。A完整点数组前后不变、擦除/Undo/Redo、任意跨进程冷载不在此case中，既有隐藏保存回归为独立证据。

## 耗时、日志与清理

- 同session1192条完整backend Read：均59.648ms、nearest-rank P95 89.083ms、最大126.583ms；不是纯PrintWindow计时。
- 626次GT read：均57.913/P95 84.100/最大107.780ms，包含GetDC/DIB/copy/flush/释放；verify均2.186/最大8.065ms，含安全门/内存验证；不含PNG编码。
- 缺精确F0首末采样时窗，**不以160.123秒总case或1192帧推算实际Capture FPS**，不声称20Hz；测试GT提速不是产品截图后端优化。
- 未见DiagnosticOutput dropped摘要，所需1192条frame完整连续；不泛称全部诊断无丢失。RTS每contact stored8/dropped10是有界trace保留；contacts2/2、slotDrops0/contentionDrops0，与异步日志队列不同。
- state/mag returned1/failed0，product/service/mag_ready/capture_occupied/own_left_held/log_failed全0、result0；无AV、watchdog终止或自己的Down未归还。

## Findings / Verification

无产品源码修复，无阻断此限定long验收的问题。离线脚本初稿误估GT621条，按实际调用核为626后修正；原日志、门槛和图像未变。完整版本/路径/GT/接力/PNG/文件/ACK/清理检查pass；checker未执行构建或GUI。

可复算证据：root/checker-complete-verification.json、checker-long-path-verification.json；脚本 `Build/ScrollFollow/product-live-stage/check_ink_complete_current.py`、`check_long_path_6740.py`。原launcher JSON hash `99fdb182ddd3ced42649ddf32bb2a3734254419f5b2525c00a9ba9686dc7945b`；stdout `8a213627f774bf796babfca634f3bc024c0de42dad955d084d4e76342b7bd3f0`；stderr `27dee8fe97fd7fec9e6ae4a3fd81a866a655549e2226fc94bab86a686f858977`。
