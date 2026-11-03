# R2 根因定位：保持感应跨窗口与终态丢失

## 本轮依据

用户在另一块数位板（型号未提供，文件名沿用M6-input.txt）复现：笔保持in-range悬浮跨到另一窗口后点不动，离开感应范围再回来恢复。日志为 Build/x64/Release/M6-input.txt，8677行/3295110字节，x64 build Nov 2 2026 22:01:56，ui_schema v1。原件已在ignored TestResults/tablet-ui-diagnostics-x64/other-tablet-capture-20261102.txt保存，避免下一次CMD重定向覆盖证据；hash见本文件末。

本轮用户要求调查/定位，不实施输入修复。没有修改源代码/工程/二进制，未GUI、编译、提交或推送。当前已有诊断改动及此前Release构建产生的工作区变更保留，不覆盖其他工作。

## 结论一：WM_TOUCH接收窗口与当前Pointer目标分离，应用没有统一业务归属

这是现场确定现象，不再仅为透明区域覆盖猜测。同一pointer/contact周期可在多个实际窗口收到Pointer Down，但解码Touch Down始终到一个旧窗口：

| 本日志contact | Pointer Down实际目标（bar_queue重复已排除） | Touch Down实际接收 |
| --- | --- | --- |
| 1263 | 主栏14、左下6、右下46 | 主栏71 |
| 1272 | 主栏15、左下12、右下6 | 左下34 |
| 1273 | 主栏26、左下21、右下7 | 右下57 |

计数包含未对应到Pointer的Touch记录，不能据总数不相等推断新硬件接触。按同contact、最近tick差<50ms可匹配137次跨窗口目标不一致的Down，作为汇总辅助；关键个案有精确坐标/窗口rect/业务结果：

- L5664：pointer1272 WM_POINTERDOWN进入主栏，屏幕点(1079,1856)，Bar rect=(726,1340,2154,1920)，capture=0。
- L5666/L5669：同次同点WM_TOUCH及解码Down却交左下窗口，左下rect=(-10,1484,710,1929)，该点已在窗口矩形外，capture也为0。因此不能仅解释为主栏透明区域覆盖了分页。
- L5671-5676：左下background=0/hit=0，按background_miss拒绝并释放临时lease。没有把触点转交真正命中的主栏。
- L5678-5681：主栏同时收到兼容鼠标Pen Down，但按pointer_promoted_mouse两层过滤；Pointer旁观不执行业务。于是本次点击没有到达主栏按钮。
- 反方向同样发生：L701 Pointer Down进入右下(2775,1835)，L703-705 Touch却到Bar（其rect右边2154）；L708 Bar先SetCapture，L709实际background=0/no_main_button。

程序缺口：Bar/Page各自按“收到WM_TOUCH的HWND”解释触点，未跨窗口统一实际业务目标。Bar又在判断业务命中前占lease/capture，会放大路由失配的影响。更具体源码见research/pen-hover-window-routing.md。

当前证据尚不能把Touch锚定旧窗的系统内部原因归为Windows或某驱动缺陷；应用层已实证不能假定WM_TOUCH目标总等于当前Pointer命中目标。这不影响定位业务路由需要处理该兼容行为。

## 结论二：PageControl用不稳定的来源分类拆开Down/Up，确定丢终态并阻塞其他窗口

现有接口Message.cppm:33明确“Pen与Touch的低位分类在同一次接触中不保证稳定”；:42明确IsPenGeneratedMouseMessage仅供诊断，交互层不能据此拆一条Pointer接触。

PageControl.cpp:2863却用Pen分类建立兼容会话，而:2816/:2902提前过滤Touch分类的兼容消息。现场0xff51572d与0xff5157ad只差0x80，正好命中这个缺陷。

### 右下卡住与恢复

| 附件行号 | 事件 |
| --- | --- |
| 6174 | 右下WM_LBUTTONDOWN extra=0xff51572d，被识别为Pen |
| 6176-6180 | 成功取得全局非鼠标lease gen1、capture ticket26，Down进入worker |
| 6183-6188 | Pointer Up已到；鼠标Up extra=0xff5157ad被识别为Touch并提前filter，无Pen Session Up/释放 |
| 6193-6283 | 其他窗口5次BeginTouch被other_channel_owns_lease拒绝，owner始终右下gen1 |
| 6287 | 右下自己后续Pen Down也被nonmouse_session_active拒绝 |
| 6301/6305 | Pointer1272 Leave，之后新Pointer1273 Enter；单独Leave未清旧Session |
| 6324-6335 | 新Touch Down终于到右下，按现有“新Touch替换旧非Touch”分支WM_CANCELMODE/Reset，释放旧gen1并取得gen2，恢复 |

### 左下再次卡住，证实可交换角色

L7104-7108左下接受Pen Down，lease gen48/capture34；L7115-7117 Touch分类Up被过滤。L7123-7563其他窗口21次lease申请被拒。L7577 PointerLeave仍可见capture持有；L7595终于收到Pen分类Up，7598入队，7599释放capture，7601释放lease。两段合计26次明确跨窗口拒绝。

这不是“系统没发Up”，日志直接记录Up已经到达后被应用过滤；也不是泛泛怀疑lease泄漏。整个日志189 acquire/189 release总数最终平衡，不能否认中间数秒持有未释放造成的失败。

## 为什么远离数位板后恢复

本次日志中保持感应时pointer范围ID保持，Touch旧接收目标持续。离开范围再回来出现新ID，Touch重新落到当前窗口；新Touch可以通过现有替换分支取消旧Pen状态并释放lease。部分恢复也由后来一次可接受的Pen Up完成。

不能写作“程序收到OutOfRange就自动正确释放”：PageControl没有显式WM_POINTERLEAVE/POINTERCAPTURECHANGED的Pen会话退休，第一段旧lease是在后续真实Touch Down触发取消后才释放。用户观察与记录相符，但具体恢复原因由上述实际事件确定。

## 已排除或降级

- HiMsg未生成WM_TOUCH、未改写extraInfo、未内部转投Pointer。四Page role bindMessages=false直接走自有WndProc；Bar经过的HiMsg只是callback/队列/DefSubclassProc。
- 五个UI窗口复用同一RegisterTouchWindow注册策略，未见程序在注册中主动把整段笔范围锁到某窗；Shell边缘手势设置不负责来源分类。
- 触觉GetFromPointerId present=0仅省略震动，不撤销先前UI操作；不是点击失败根因。
- Draw3 admission是画布新笔门，不是这些UI的全局lease；不能混为一谈。
- 错误捕获票据/队列溢出/渲染几何算法故障未被本次失败链证实，不作随手修复。

## 修复边界建议（尚未批准实施）

1. 保留用户期望的统一触点处理方向，但在实际业务Down准入前统一主栏与分页的触点目标/归属，整次接触有唯一owner；不能仅用接收HWND认定实际命中，也不能让未命中窗先占lease/capture。
2. 消除Page独立按Pen/Touch低位分类拆生命周期的处理。兼容副本去重要与已确定接触归属一致，终态必须完整退休；无法确认身份时只能正确取消，不伪造成功Up/点击。
3. 保留旧代次/跨设备隔离、长按/拖动、Win7兼容与既有capture票据语义。不靠固定超时、GetAsyncKeyState猜Up，也不单独删除过滤使两个流重复执行业务。
4. 后续用实际生产状态机回归本日志形态：同range跨三个窗、Touch旧目标/真实命中新目标、Down72d/Up7ad、PointerUp/Leave/Cancel、连续轻点和PPT触摸长按；无窗口通过后再由用户进行一次同路径设备验收。

当前详细日志已足以定位上述两层应用问题，无需先继续增加泛化日志或要求用户重复提供同粒度采集。是否以共享WM_TOUCH路由还是其他兼容方式落实，需要在修复阶段按minimal diff形成具体设计；本轮不自行改策略。
## 捕获校验

SHA256: B21B8A7977F3B7B517359C7AEE3980A05E51636137ED791B21EE6EEF65BCF9DB
