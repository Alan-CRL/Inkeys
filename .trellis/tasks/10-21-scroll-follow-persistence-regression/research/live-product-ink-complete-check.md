# 完整原生墨迹短流程：独立验收（PID22200）

2026-10-23，Asia/Shanghai。任务保持 in_progress。本 checker 只读原始 launcher/result/stdout/stderr、8 张已存在的专用 PNG 及实际私有 index/UInk，运行独立离线像素和 MessagePack 检查；没有产品源码修改、构建、产品 CLI、GUI、新截图、commit/push 或归档。允许进入下一项验收，不能据此标记整个任务完成。

## 结论

**本机专用纹理、OS 鼠标经原 RTS 的完整短案例通过**：画 A → 实际非空辅助窗交接 → 真实滚轮使底图向前 32 像素再反向 16 像素 → 正常退出并恢复主窗 → 画 B → 原 Exit 保存及真实 codec 读回。它不是物理笔/触控、十屏长滚动、水平跟随、外部浏览器或完整编辑历史验收；旧 PID10832 等失败案例仍保持失败。

## 版本与原始运行

- Round `product-ink-oracle-fixed-1023-5561046`；root `Build/ScrollFollow/product-live-22200-5561515`。
- launcher JSON：PID22200、exit0、32.320 秒，stdout30574B/stderr22282B。原始 stdout 的所有 ProductLive/ProductLiveInk 行与私有 result.log 逐行一致。
- 启动前文件 SHA256 `03DEA84FEA607475304C5E58B45C7F1F97B9408F124E47723347ECE8E4197251`，LastWriteUtc `2026-10-23T00:42:28.6692318Z`；`hashScope=file-before-StartProcess` 不是进程内存摘要。对应独立预审 inl D83E9F65，见 live-ink-oracle-check.md。
- 明示 `no_test_ports=1 no_manual_follow_target=1 no_manual_ack=1`、`contact_seam_enabled=0`，真实 SendInput 后经 RTS 消费；日志 `synthetic_sent=0` 指 contact seam 没有注入，不表示物理硬件输入。

## 输入、窗口及跟随

1. 初始主窗 pair(1,0)。A/B 样式发布后都观察精确 ACK 0→1，再发送各自 Down；不是手写 ACK。A、B 的 down/terminal/recycled/contact 分别从0→1和1→2，完成 Drawing、canUndo/ready 成立。每笔 RTS 有18 callbacks、16 Packets callbacks、decodeFailures0、publishOk18，实际 raw/decoded Down 与文件首点一致。
2. A 后 `currentPageHasContent=1`，实际 Tracking pair(0,1)、selection/content1、ACK1。它是非空墨迹的辅助呈现，不借用空页 pair(0,0) 证明可见。
3. 仅两条 tagged wheel：delta-240 使 Source offset(256,512)→(256,544)，delta+120→(256,528)。没有外来滚轮。原始所有 SourceGT 记录均99/99且精确256×192 ROI一致。
4. 正常 Off 为 phase Complete、latest/applied75、实际 ACK1、pair(1,0)、viewport(0,16)、clamp0；最后可信位置与独立 Source 累计(0,16)一致。随后 B 实际 Down=(768,1346)。

## 独立像素复算

脚本 `Build/ScrollFollow/product-live-stage/check_ink_complete_22200.py`；输出该 root 的 `checker-complete-verification.json`。独立 uint32 模运算重建世界纹理；强 core、背景 RGB 容差3、走廊范围沿冻结合同。对新增浅边缘，使用三个通道覆盖率区间的交集，而非照抄产品最小二乘投影；必须存在同一个0–5%覆盖率，使固定笔色和已知灰底各通道残差≤1。

| 已保存图像 | 核心像素 | 未知像素 | 新浅边缘 |
| --- | ---: | ---: | ---: |
| A 主窗、辅助窗、前进32、反向16、Off 共5张 | 每张18648 | 每张0 | 每张0 |
| B 主窗 | 18750 | 0 | 13 |

全部6张墨迹 ROI1440×197、alpha255。B的13个浅边缘均由独立覆盖率区间法解释（区间整体端点约1.34%–2.60%，不是各点都共用同一覆盖率）；产品记录最大残差.6982，不把该记录误称 checker 独立最小二乘数值。不存在任意 unknown 容许量。两个 Source256×192 PNG 的全部 RGB 与公式精确相等、alpha255；实际共8张 PNG，即5A+1B+2Source。

四次 A 核心比较为真实交接(0,0)、前进(0,-32)、反向后净(0,-16)、Off(0,-16)。checker 直接构造整数平移核心集合并逐位比较，每次双向37296个核心、最大/中位误差均0像素。主/辅 PNG 字节相同，反向/Off PNG 字节相同。这比原≤2像素门槛严格，但只证明已有采样图，不证明采样间全程无闪烁。已目视已有 B 专用图；没有新截图。

## 实际 Exit 文件及鼠标量化

- 文件 `AutoSave/desktop/2026-10-23/084502678_25cee08f.uink`，实际12389字节，SHA256 `c4cab47aba6435c3a9dc2a2d75980aeaeaf090d08a957c2ba98927d59e24434e`。capture日志5951是估算，不能写成文件大小。
- 唯一 index entry：trigger exit、dailySequence/sequenceInSession均1、session `3f1f628c-e00f-4e7f-9b70-3beb86b74543`。fileGuid `12cbadcf-4daf-4e9b-96d8-949d9289c38c` 与实际文件头一致。
- 独立标准库 MessagePack reader 消费全部字节，恰5 records：v10头、一Desktop workspace、一canvas、两 inkType1 Pen；227和225点。workspace `0b051b9f-b332-4645-a0b3-6d31c603b1d8` 与canvas一致，page `47824fa4-acf4-4f64-bfbb-70a1476aa257`，viewport(0,16,1)。A/B首点宽度均16。
- 真正 C++ `ReadLastCommittedDesktopAutoSaveFixture`、v10 Read Complete、Import Success、最终运行workspace/page身份比较均由冻结夹具在成功路径执行并记录 actual_exit PASS；独立 Python 字节观察用于交叉核对，不冒充第二次执行产品 codec，也不把未输出的运行GUID称作独立日志值。

| 笔划 | 整数鼠标 Down | 文件首点 | 首点 Y 误差 | 192DPI 计算界 |
| --- | --- | --- | ---: | ---: |
| A | (768,706) | (768,706.0157470703125) | .015747px | .075762px |
| B | (768,1346) + viewport(0,16) | (768,1361.96533203125) | .034668px | .075918px |

实际 Display DPI=(192,192)、主屏2880×1920、client1:1且稳定。checker另用已日志 raw A(10160,9340)/B(10160,17806) 经 float32(192/2540)乘法及B视口相加，精确复现两首点。此量化模型仍限定OS鼠标；产品使用实际RTS系数，不能由此宣称任意数位板系数或硬件精度。保存 viewport `.001`、身份、两Pen及 codec 门槛保持。

此 case 没有比较 A 的完整点数组在跟随前后相同，也没有 Undo/Redo/eraser/清空/跨进程冷载；这些不能从两个首点或一次 Read/Import 推出。既有隐藏保存回归属于独立证据。

## 捕获、诊断及清理

Capture accepted75、rejected0/API0；原逐帧序号1–75各一次，Tracker accepted74（frame1初始化成功、frame2尚未进入Tracking为NotInitialized、frame3–75接受），不能混称75个运动输出。最终 confidence1、inliers318、FB/residual P95约.0001处理像素仅是该专用纹理最终帧指标。

75条完整 backend Read 平均83.937ms、nearest-rank P95 114.632ms、最大128.201ms；包含Backend开销，不是纯PrintWindow计时。此 case 混有工具/像素等待，不能用32秒总耗时推算捕获FPS或称达到20Hz。

原日志 DiagnosticOutput dropped1/contention1/full0；RTS每接触stored8/dropped10是有限trace保留，和诊断队列丢1条不同。没有“全部日志零丢失”主张。

正常 cleanup：state/mag 均returned1/failed0，product_running/service_running/mag_ready/capture_occupied/own_left_held/log_failed全0，result0。没有AV、watchdog超时或自己的Down未归还。

## Findings (fixed)

没有产品源码修复。新增忽略目录独立检查脚本和本报告；脚本执行exit0，未运行产品或改变原始证据。

## Findings (not fixed)

无阻断此限定短案例验收的问题。十屏/水平/初始特征全离屏接力、外部应用、用户主栏/快捷键实际交互、硬件笔/触控、旧系统、实际DPI/设备失效仍需后续对应案例，不据本次整体标完成。

## Verification

- 原始版本/PID/退出/日志关联：pass。
- 8 PNG 独立公式、浅边缘区间、严格核心平移：pass。
- 实际索引/UInk全字节观察与RTS/viewport首点交叉核对：pass。
- 真实C++成功路径 Read/Import/identity/cleanup receipt：pass。
- checker构建/产品CLI/GUI：未执行，所有权保持串行。

原始 launcher JSON SHA256 `ba1cfc1e85b81c72859be8b58e32625c9f947c13010e4fd35f6f5ebdbd2562df`；stdout `0ac0a9ad5958a495f4e144ae6d35fe7935f597cf2267b1a6006b0632f7e6e98d`；stderr `27e7926b2985835122dd3704592535bcbfafcf3c7b5430f23fd749e081d48277`。

## 当前 long 构建上的旧 short 回归（PID26356）

2026-10-23，原 short 单独重跑，launcher `live-product-ink-long-build-short-1023-6054687-test`：启动前文件 SHA256 为24D8FAE8（完整值见 long-stage review）、PID26356、exit0/37.169秒，timeout100000ms。root `product-live-26356-6055187`。

checker复用参数化独立脚本 `check_ink_complete_current.py`，不是运行产品CLI：8 PNG 全部通过世界纹理/覆盖率区间法；A每张18722 core、四次双向37444点且严格位移集合完全相同/max0；B18678 core/unknown0/6个浅边缘。原非空pair(0,1)及正常OffACK1/pair(1,0)成立。实际文件两Pen227/228点、viewport(0,16,1)、唯一Exit索引与文件GUID、实际raw首点/量化界均通过；原C++Read/Import成功及cleanup全归还。

输出为该root `checker-complete-verification.json`。逐帧99条，Tracker接受97（启动闸门拒绝与Capture质量不同），没有把它称为99个运动接受。该回归证明新shared helper及verbose事实字段没有破坏原short；long仍须单独实际验收。

## 五层参数新产物原 short 回归（PID32848）

原生可见调用由root执行，checker只做事后离线核验。round `product-ink-lk5-short-1023-current`，root `product-live-32848-7914359`，启动前主EXE8DAF6D3B，PID32848/exit0/37.844秒，timeout100000ms。

独立参数化PNG/MessagePack脚本exit0，输出该root `checker-complete-verification.json`：8PNG公式和固定颜色覆盖率区间通过，A四次严格核心集合平移相同；B18765 core、unknown0、10个已知浅边缘。两次真实RTS消费、非空辅助ACK、正常Off主窗ACK成立。实际唯一Exit两Pen229/228点，viewport(0,16,1)，首点与实际raw和192DPI受限计算界、索引/fileGuid匹配，原C++Read/Import PASS；cleanup全归还。

逐帧106条、Tracker接受104，启动闸门另计；完整Backend Read均值80.176ms、nearest-rank P95 101.942ms、最大128.950ms，不推算FPS。新L5参数离线检查见live-lk5-calibration-check.md；此处只升级原short的实际版本证据，仍等待同128px/19200px long结果。
