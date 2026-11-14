# Touch末端平台修复：执行合同

2026-11-14，用户已回复“好的，开始下一步修改”。已完成实现、独立检查及自动验证，待新版本真机验收；不commit/push/归档，不启动GUI；自动验证后交用户真机测试。原Pen/PenTail/Indirect路径、笔尾压力/slop/开关、EDID、后端与曲线保持。

## 1. Scope / Trigger

真实Touch日志118次Ready里有38次短尾，末尾相同坐标使双段支持退化为noiseBudget/dt，raw约374可被压到23.6mm/s、1.11mm尾距。最小行为缺口位于Tracker的Touch估速证据/时效；不通过抬cap或加长摩擦补偿。预计只改CanvasNavigation.cpp/.cppm、canvas测试，以及必要DrawingController诊断/生产probe。

## 2. Signatures / 所有权

- 保留LastSampleQpc、raw点数/原始到达时间含义；Contact.lastQpc、水位/排序、终态合法性、输入到消费延迟仍使用raw QPC。
- Touch另维护固定容量的真实位置进展证据及lastMotionQpc/motion point count；实际接口为LastMotionQpc/MotionSampleCount/HasStationaryObservation，释放诊断并列raw/motion时间与点数。每个进展点保留其原到达QPC，不伪造硬件时间。
- 进展证据只属TouchPhysical/TouchFallback，不改变已通过本机Pen及其它profile路径。逐raw几何/反向仍有唯一真值，不能因释放过滤丢真实Move。

## 3. Contracts

1. raw每份样本保持；只有空间变化超过可计算float误差才推进Touch运动证据。重复坐标保留为停止观察，不刷新运动时间、不按其新QPC裁掉运动尾窗。采用固定≤64点/每接触的进展存储，无per-Move heap/新线程。
2. Touch LSQ/独立锚/支持使用进展证据及实际QPC；平台后catch-up用前次进展→新进展的真实时间。进展尾窗仍50ms加一个左锚，可靠释放需至少3独立锚/20ms和至少两正向运动区间；不强保所有平台都起滑。
3. 原40ms断报保护只看相邻raw回调，不能套到去重运动点流：真实108有40.3477ms无进展但中间raw仍6–10ms连续。真反向同步切断raw与运动旧证据，不借旧方向复活。
4. Up释放年龄使用Up事件QPC−最后真实进展QPC，维持原≤30ms全速、30–70ms衰减、≥70ms归零。Snapshot/frame时钟只做当前呈现估计，不抢先清排队Up证据。
5. 无进展已≥70ms后恢复真实运动时退役旧运动证据，可用真实前一raw静止点作新锚；一个新jump不得刷新age后借回旧高速。缺点/取消/overflow/源或单位变化可靠闭合。
6. Touch motionAge>30ms、速度已衰减时禁止本次旧residual boost，避免衰减被接力旁路；沿用既有阈值，非Touch接力不改。质量限幅/输出cap仍按原规则区分，最终向量cap保留。
7. 释放候选保留不等于呈现速度也保留。Touch已有更新的同位置raw观察时，Snapshot给渲染的速度为观测静止0；仅frame推进而没有新raw时，不任意置0以免打断正常低报告率。held实际位移始终1:1，旧残余状态不被Snapshot改写。

## 4. Validation / Error Matrix

| 输入 | 期望 |
| --- | --- |
| 完整真实74/108短尾、107/109长尾 | 短尾不再按零段noise/dt压到1mm；同时对照正常长尾，记录selected/age/尾距，不只非零 |
| Touch平台25/30/50/70/100ms | 以真实进展age衰减；70/100必须stale/无coast；不被重复raw刷新 |
| 连续raw、40.3477ms平台后catch-up | 不被误当raw40ms断报；若有足够运动证据可起滑 |
| 50/60/70ms平台+C | 真实50ms窗可能只余左锚+C而不足；这是明确保守边界，不能假造第三锚或扩大窗强保 |
| 长停≥70后单jump/重复 | 不继承长停前速度；新腿必须重建真实证据 |
| 真反向/停指/取消、重复逆QPC、慢量化/低报点 | 原安全语义保持，不跨方向/epoch，原始终态正确闭合 |
| 显式raw重复 vs仅无新样本frame | 前者呈现速度0但释放候选保留；后者不能每帧强制0 |
| 有实际旧residual、motionAge>30 | aged selected不被旧boost抬回；非Touch现有接力/数值保留 |

## 5. Good / Base / Bad 与体验取舍

Good：重复上报不冒充新运动；触摸平台后的真实进展按原QPC桥接，符合漫游释放，真正长停仍退出。Base：held几何、Pen/PenTail/Indirect及曲线不变。Bad：无条件丢掉重复点和停手证据、重排/摊造时间、将motion间隔当raw断报、只追求非零率、Snapshot清掉待消费终态。

明确代价：沿30–70ms门，真实1000mm/s运动后只停40ms仍可能约725mm/s/171mm尾滑；不能称只保留微小滑动。明确停住至少70ms才归零。该Touch漫游取舍须用户复测；不能凭现有坐标区分真实短停和设备重复。60ms平台+C不足3锚可保守0，不保证所有<70平台都恢复。

## 6. Tests Required / 顺序

先新增红例并实际运行：build-red完整Solution通过，headless-pan-red预期32条失败，证明旧运动age/短平台/长停路径不符合新目标。再实现并独立审查，完整ARM64 InkeysRepo.sln Debug|ARM64，使用独立OutDir PanTouchTerminalValidation，专项/全套保存源/产物/日志hash。现有绘制/eraser/headless仍需通过，不以离线脚本代替native。

旧pan-feel主要TouchFallback，不可单独证明Pen/Indirect不变；增加/保留明确非Touch黄金值/真实Pentrace及源分支证据。Touch差异按真实轨迹和时效规则分类，未做光学/硬件时钟测量不冒称已通过。

## 7. Wrong vs Correct

Wrong：每个重复坐标都当新速度锚，最新零段将完整Touch尾窗压为noise/dt；或完全忽略停住时间一直沿用旧快速度。

Correct：原始观察与空间进展分开保存，用实际motion QPC估速并按停止年龄退出；旧速度不跨真反向、断报、70ms长停或终态复活。
