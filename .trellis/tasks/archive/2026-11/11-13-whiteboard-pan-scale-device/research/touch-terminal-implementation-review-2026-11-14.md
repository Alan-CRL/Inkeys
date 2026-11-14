# 独立检查：Touch末端平台与空间进展证据

日期：2026-11-14。最终结论：PASS。静态审查、最终绿构建、五专项及完整headless全部通过。本检查未修改本轮产品/测试源，不构建、不运行GUI、不提交/推送/归档；主会话统一构建与冻结，implement拥有四文件源码变更。此前八文件修复保留。

## 已确认的最小范围与实现

本轮相对baseline-source只改CanvasNavigation.cpp/.cppm、DrawingController.cpp和既有canvas测试。PenCursor.cpp/.cppm、WindowControl.cpp及contact测试逐字节不变；没有改笔尾压力/slop/开关、EDID、后端、PTP隔离、配置或摩擦参数。

Tracker仅在TouchPhysical/TouchFallback启用touchProgress_，每指增加固定progressSamples[64]/progressCount；raw数组/计数/LastSampleQpc仍记录全部真实观察。非Touch的MotionSamples直接返回原raw span。新增空间证据以相对上个实际运动点的坐标变化超过可计算float误差为准，没有用reversalDistance删合法小步。

raw真实反向与40ms无回调gap会同步清对应运动证据。重复raw不推进运动时间或裁运动窗口；新真实进展使用原QPC，50ms运动窗仍保留一个左锚。>=70ms无进展后新运动先退休旧证据，可用前一真实raw静止点作新腿锚；没有给Up补Move或按固定报告率造时间。

## 三时间域及安全闭合

- Contact.lastQpc、真实事件稳定排序、导航水位、raw延迟和终态时间合法性仍按raw QPC。释放年龄改为Up事件QPC−LastMotionQpc，Snapshot按frameQpc读取当前估计但不清排队事件证据。
- Touch40ms无raw回调与70ms无空间进展不同：key108实际进展B→C40.3477ms，中间raw连续6–10ms，不能用去重后gap触发raw40ms。源码保留两条独立判断。
- 非最后一指Up判断其它指旧方向是否仍新鲜，也已使用LastMotionQpc；不因另一指重复raw把>=70ms陈旧反向速度当新鲜。对应多指回归检查未误清剩余指运动证据。
- Touch age>30ms单独禁止本次旧residual boost，防止年龄衰减被接力补回；支持限幅和年龄限幅有不同flag，最终向量cap保持。Pen/PenTail/Indirect仍用原raw末点年龄及原boost规则。
- Clear/Stop/ClearPointer统一复位新数组；ClearAxis同步清raw与progress对应轴，不改时钟、不恢复旧轴速度。新运动数组追加/裁窗有固定容量检查，无Move heap、线程或timer。

未发现越界、时间域混用、旧方向/旧epoch复活或未闭合held问题。新增存储约24KB样本数据加计数/Tracker；原生pan专项已通过，不以未运行GUI宣称主窗口资源/手感验收。

## 呈现速度消费者与已落实分工

只读追踪ContentVelocity→CanvasPanInput.VelocityDip/Pixels：viewportRefreshPending建tile预取计划时使用速度；fallback blur只在frameDirty非空且viewportVisibleClear=false时使用。平台期间若仍在恢复或其它重绘，释放保留速度确可进入模糊，所以不能只保释放历史而忽略这个出口。

实现用HasStationaryObservation（仅Touch且lastRaw>lastMotion）令已明确重复观察后的呈现速度0，并跨无新raw空帧保持，释放证据不被改写。正常低报告率的最后观察若仍是progress，空帧不随意清零。headless分别验证无新raw、明确repeat、repeat后的空帧、随后Up仍可释放。

调度navigationActive来自held/curve/恢复标志，不由velocity大小决定；held原本按WaitForFrameDeadline推进。没有因保留候选新增busy-loop来源；本轮未扩围其它渲染包。无dirty且画面已清时，非零估计本来也不会独自产生Present。

## 非Touch兼容证据

已独立对比本轮baseline-source：profile/曲线策略、GestureDetector/Booster/Fling、event Dispatch、Stop/Phase/ContentVelocity边界的原实现区域文本完全相同。非Touch的MotionSamples返回原raw点，LastMotionQpc等价于原LastSampleQpc，HasStationaryObservation短路false，额外Touch age禁boost条件也false；旧拟合、求和顺序和支持/age/cap算术不变。

原ScreenPen真实轨迹数值、质量受限/纯cap旧residual接力、Pen/尾及Indirect相关既有黄金/生产probe继续通过。尤其原age40ms旧residual正例仍用于Pen；Touch因本轮明确年龄降级改为禁boost断言，没有为了旧断言删新规则。该证据是分支/黄金回归，不把主要TouchFallback的pan-feel输出冒充每种Pen设备逐字节等价测试。

## 原生与离线覆盖分层

已独立从固定28MBconsole快照逐条核对native四条完整轨迹：74=18、108=20、107=16、109=13，共67事件，QPC/三位像素/phase一致；source class/context正确、完整Down→Up且key内ordinal连续，无补造缺失事件。

四条轨迹按逐事件和延后100ms同批消费进入内核，检查raw/selected、motion age、理论尾距和coast。新74期望selected约238.315mm/s/42.626mm尾距、108约388.584/80.364；107约875.764/213.283、109约883.028/215.323是正常长尾对照。它们是选择策略的CPU数值验证，不是动作真值或新硬件舒适性证明。完整38短尾/120个体只属离线比较，未声称全部native重放。

合成覆盖Touch physical/fallback的25/30/50/70/100ms平台、40.3477ms catch-up、50/60/70窗口边界、真实raw40ms断报、反向、多指其它指motion age、轴清除及量化/低报告率。empty frame190后才消费真实Up57@frame200，仍按Up-motion age而非较晚frame处理，首帧不追补自由滑行。

## 已独立核验的运行记录

Build/Logs/pan-touch-terminal-2026-11-14：build-red完整ARM64 solution exit0/19.828s；headless-pan-red exit1/1.453s，确实抓到旧Touch平台/年龄缺口。

build-green-1完整同Solution Debug|ARM64隔离OutDir PanTouchTerminalValidation exit0/24.344s，11项原有warning/0errors。五专项exit0：headless-pan 1.610s、control-fence 1.297s、pan-feel 3.594s、pen-width .281s、diagnostic-output 1.922s。完整headless-all-green-1 exit0/332.266s，末行PASS animation correctness。已独立核对完整套件和五专项的sourceBefore/sourceAfter均冻结且与最终构建、当前八文件完全一致；实际EXE和日志SHA匹配，Headless SHA256=59b140582b3ab052c4019255e5659df7c612c21b65ec4a1808469b60f6c31c14与pan专项为同一产物。未仅凭末行或旧产物推断成功。

八份源/测试均UTF-8可解码、BOM与本轮基线相同、全部CRLF；DrawingController继续无BOM。相对基线只有本轮四文件变更，git diff --ignore-submodules=all --check通过。

## pan-feel差分分类

与上一轮PanOverspeed的2241行输出相比，仅7条jitter-rest的max_dv_dip_s不同；其余release、滑距、stop、实际delta、held误差、nonfinite/failures等字段逐字节一致，没有新增零释放。六条估计速度差减小，一条240fps非等比scale由85.726291增为98.716726；该场景max_delta与release仍为0。不能将这7条全称改善，属于Touch显式静止/运动证据改变的估计瞬态，不是跟手位移回归。

标准pan-feel没有本次实测末平台交付形状，因此其释放结果相同不否定四条真实轨迹数值变化，也不能据此跳过真机验收。

## 已批准的体验代价与待用户验证

沿30–70ms motion-age门，真实1000mm/s运动后停约40ms再抬仍可能约725mm/s、171mm尾滑；>=70ms才0。这是Touch漫游取舍，不是只保留微小滑动，必须向用户说明并专测明确停指。

约60ms平台后加C可能推进50ms运动窗口而只剩两个锚点，保守变0；没有C时原候选仍可age衰减。主会话已接受此新证据不足边界，不承诺所有<70ms平台都恢复，也不加pending FSM/延窗/伪锚凑非零。

当前坐标流仍不能完全区分真实短停与设备重复。手指连续甩动、轻放、明确停住、中心/边缘及快慢接力需用户用新产物复测；Pen/笔尾情况仍分开确认。自动验证不宣称所有Touch手感最优。
