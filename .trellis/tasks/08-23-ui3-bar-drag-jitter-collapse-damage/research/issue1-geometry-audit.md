# Research: 问题 1 竖向底栏几何与端点交接审计

- Query: 核查普通按住底栏的零弹簧基准是否贴真实 dockLine；实际被抓住的局部点是否跟随指针；捕获、脱离、恢复、重捕获与 HWND 位移交接是否连续。
- Scope: internal；只读生产源码和已有 Headless 测试，仅在本任务 research 中写报告。
- Date: 2026-09-09

## Findings

### 结论与证据等级

| 问题 | 结论 | 限制 |
| --- | --- | --- |
| (a) 普通按住 Docked 的基准底边 | 固定显示环境、根节点和描边下，名义外框与 dockLine 只差一次 HWND 整数取整，误差至多 0.5 物理像素；并非数学上的精确相等。Docked/恢复布局会强制 h.val=80。 | capture-bottom 非零时，底边另加 c*zoom，这是允许的捕获动画。尚未证明实际机器环境、成功元组和实际像素满足这些前提。 |
| (b) 实际局部抓取点 | **不具备不变量，已由生产调用链证实。** 主按钮的 Y 也使用主体仿射变形；输入保存的却是“未映射中心 + 普通偏移”代理。 | 这证明跟手公式不保持实际抓取点；不能据此独断截图底边间隙或瞬时闪动的唯一原因。 |
| (c) 端点交接 | F→D 对最后成功底端作显式重基准，D→F 保留 c、换基准并立即积分。两方向实现不对称；理想条件下保留 c 可以正确抵消，不能说 c 必然重复叠加。 | 形变量限幅、首帧积分、候选与成功状态差异及吸收后的成功快照坐标都需要日志分辨。 |
| 松手吸收后的成功快照 | **存在可静态证明的坐标不一致窗口。** 吸收减少 snapshot.directTranslation，却未平移其 mapping 端点；此时公式 O + visualBottom*z + T 不再表示原来的成功屏幕底边。 | 只有记录证明该窗口被下一次捕获/命中消费，才可归因到用户的实际闪动。 |

### 文件与代码锚点

以下所有路径均相对仓库根；后文短文件名指向本表。

| 文件 | 职责与锚点 |
| --- | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` | dockLine/中心/阈值、tracker、仿射映射和弹簧；280、304、328、618、748、1370、1404、1440、1463。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp` | Seek 的成功快照、抓取偏移、指针采样、整数 HWND 位移；5981、6094、6130、6163、6294、6391、6417、6446。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` | 显示/布局所有者、弹簧接线、真实绘制变换、成功发布及松手吸收；977、1204、7271、7484、7660、9693、11421、12603、12869。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm` | 成功快照定义/读取和直移重基准；305、463、731。 |
| `Inkeys/Inkeys/UI/Bar/Bar.WindowGeometry.h` | 直移与吸收后 translation 的生产纯函数；273、282。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp` | 主按钮实际 h.val、描边与超椭圆路径；2453、2553、2613。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp` | MainButton 初始 80×80、1 DIP 描边；MainBar 80 DIP 高、1 DIP 描边；251、280。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm` | `BarButtonFrameThicknessDip=1`、`BarMainBarHeightDip=80`；11、27。 |
| `Inkeys/Inkeys/UI/Bar/Bar.UI.cpp` | Center 继承由父中心减子半高得左上角；33、50。 |
| `Inkeys/Inkeys/UI/Bar/Bar.State.cpp` | PositionUpdate 只分类布局方向，不修改根节点 Y；13。 |
| `InkeysHeadlessTests/bar_bottom_dock_tests.cpp` | 现有种子、理想端点连续和代理抓手测试；155、191、840、1094。 |

### (a) dockLine、80 DIP 基准与真实外框

记 O 为 monitorOrigin.y，z 为最终 frame.zoom，y 为根节点 mainButton.y.val，T 为该帧 HWND 竖向 translation，s 为 mainButton.ft.val，H=80+s，a=y-H/2，b=y+H/2。记 e 为普通竖向形变量，c 为 capture-bottom 项。

生产 mapping 的两种端点为（假设未触发正高度保护）：

```text
Docked:    visualTop = a + e;  visualBottom = b + c
Floating:  visualTop = a;      visualBottom = b - e + c
屏幕端点 = O + z * visualEndpoint + T
```

`Bar.BottomDock.h:1370`/`:1404` 实现上述公式；`Bar.RenderLoop.cpp:7489` 确认 H 使用 80+s，并由 `:7660` 把 e、c 两项传入，preserveCaptureBottomOffset=true。e 始终保留 ±24 DIP 保护；c 的成功像素种子不再次被 24 DIP 截短。

`Bar.BottomDock.h:280` 的普通桌面 dockLine=L 是有效 workArea.bottom（严格小于 monitor.bottom）或 monitor.bottom。白板路径改为 monitor.bottom-round(5*dpiScale)，不跟任务栏；生产入口为 `Bar.RenderLoop.cpp:349`，输入环境来自 `Bar.Interaction.cpp:6108`。不能仅因看到工作区间隙就把 dockLine 定错；须记录实际环境和 WhiteboardActive。

`Bar.Interaction.cpp:6130` 固定 bodyHeight=80，s_seek 读取 ft.tar。tracker 捕获时 stableGrip=P-(floatingBottom-L)，结合 floatingBottom=P-grabOffset+(80+s_seek)*z/2，故 constrainedGrip-grabOffset 得到：

```text
D = L - (80 + s_seek)*z/2
T = lround(D - R_down)              // Interaction:6448
R_current = O + y*z
baseBottomScreen - L
  = (R_current - R_down) + (s - s_seek)*z/2
    + [lround(D - R_down) - (D - R_down)]
```

固定环境下 `ApplyDisplayTransition` 冻结拖动显示过渡（`:1147`），普通手势内根 Y 不另行重定向；`PositionUpdate` 只分类方向。在 R_current=R_down、s=s_seek 时只剩 [-0.5,+0.5] 的物理像素取整误差。例：z=1.5、s=1、L=1000、R_down=700，D=939.25，T=239，baseBottomScreen=999.75。

大小混用不能解释普通 held Docked：`Bar.RenderLoop.cpp:12981` 定义 layoutLocked=(Docked||recoveryActive)，`:1204` 每帧 SetDirect(w,h)=80，停止浮动点击脉冲。mainBar 初始 h=80/y=0，Center 继承后与主按钮同中心，默认描边也为 1。未在这些布局路径发现主按钮/主栏描边宽度的常规动画写者。非拖动稳定态 `:1159` 每帧把中心直接重算到 L，仍以 ft.tar 为准。

名义真实主按钮外框底边应另外计算：

```text
actualNominalBottomScreen
  = O + z * M.MapY(y + (h.val+s)/2) + T
  = O + z * [visualBottom + scaleY*(h.val-80)/2] + T
```

普通 held Docked 有 h.val=80，所以 c=0 时名义可见外框确实落在基准底边，e 不改变底端。但**浮动点击脉冲**可以有 h.val!=80：`:802` 的 1.05 倍对应 84 DIP，`:1219` 设置该关键帧；真实绘制 `Bar.Rendering.cpp:2567` 消费 h.val，而 mapping 始终用 80。例：浮动 h=84、s=1、z=1.5、M 恒等时，snapshot.visualBottom 比主按钮名义描边外侧高 3 px；此时将 snapshot 底端称为“真实旧像素”并不精确。不要把这一边界情形推广成普通按住底栏的高度问题。

另外，`Bar.Rendering.cpp:2497` 以平滑 cubic Bezier 连接超椭圆采样点；曲线极值不严格等于 [0,h]。按该公式作浮点近似算术，80×80、n=10、z=1 的路径下极值约 80.03455，n=3 约 80.11423。这不是 D2D 实测，量级也不足以解释多像素缺口。若名义几何全部正确而像素仍异常，再记录 GetWidenedBounds 或对应成功帧像素；不可用阴影、dirty 或 HWND 外框替代可见描边。

### (b) 实际抓取点与代理的差别

生产绘制链已核实：

- `Bar.RenderLoop.cpp:9716` 的 gripTransform 仅在 X 刚性，Y 仍为 bodyScaleY/bodyTranslationY；`:11423` 和 `:11429` 将它用于 MainButton 与 Logo。
- `:12691` 的 snapshot.mainCenterScreenY 发布的是 O+y*z+T，未经过 M.MapY。
- `:12626` 发布的 rigidTranslationDip 在 Docked 为 e，在 Floating 为 0（`Bar.BottomDock.h:1398`、`:1429`）。它是附属层平移语义。
- `Bar.Interaction.cpp:6164` 却把 grabOffset 设成 P_down-(snapshot.mainCenterScreenY+rigidTranslationDip*z)，之后 `:6458` 发布 G=P-grabOffset。
- `Bar.RenderLoop.cpp:7325` 计算 e=clamp((G-(O+y*z+T))/z,±24)。这让“根中心+e”代理追上 G，没有求解实际 M.MapY(被抓点)。

对稳定、未形变按下后保持相同 h 的手势，令 δ 为被抓点相对主按钮根中心的局部 DIP，r=1/2+δ/H。生产仿射结果是：

```text
actualGrabScreen = O + T + z*[y + δ + e*(1-r) + c*r]
pointerScreen   = O + T + z*[y + δ + e]        // 无限幅/取整差异时
actualGrabScreen - pointerScreen = z*r*(c-e)
```

例：H=81、h=80，稳定状态按在中心（δ=0，r=0.5），随后按住下拉到 e=20、c=0、z=1，指针移动 20 px，真正被抓住的主按钮中心只移动 10 px，误差 -10 px。z=1.5 时误差 -15 px。按在上四分之一（δ=-20）时误差约 -5.0617 px。只有外框最上端 r=0，或瞬时 c=e 等特殊情况满足该不变量。静止等 c 从 e 回到 0 期间，实际局部抓取点仍会移动，虽代理保持不动。

若要记录“这次真正按住的点”，应在 Down 留下以下诊断数据，而不改变现有抓取行为：

```text
// 同一最后成功 tuple 的 O0,T0,z0,M0，以及那张成功帧的实际 y0,h0
u0 = M0.UnmapY((P_down.y - O0 - T0)/z0)
q0 = (u0 - (y0 - h0/2))/h0            // 在实际按钮高度内的归一化位置

// 后续候选/成功帧 k 中同一个局部点
uk = yk + (q0 - 0.5)*hk
actualGrabYk = Ok + zk*Mk.MapY(uk) + Tk
errorYk = actualGrabYk - pointerY_of_the_same_input_sample
```

M.MapY/UnmapY 的生产语义见 `Bar.BottomDock.h:758`。h0 必须来自那张成功帧，不能临时读取渲染线程已经改变的 h.val；初次按下正遇到浮动点击脉冲时尤其如此。当前 successful snapshot 没有实际 h 字段（`Bar.Main.cppm:305`）。X 轴可保留同样的局部 qx 记录，但主按钮应用的是刚性 X，不应把主体 MapX 套给主按钮。

### (c) 捕获、脱离、恢复、重捕获

**捕获与重捕获：** `Bar.RenderLoop.cpp:7363` 先从最后成功快照计算 O_old+M_old.visualBottom*z_old+T_old，`:7369` 调用 `SeedBarBottomDockCaptureBottom`。helper 只认 successful Floating→candidate Docked，直接赋值：

```text
c_new = (successfulBottomScreen - nextBaseBottomScreen)/z_new
```

首个捕获候选不积分 c（`:7378`）。只要成功快照底端本身有效，即使旧 floating mapping 含 -e_old+c_old、z/O/T 已变化、c_new 超过24，也有 nextVisualBottomScreen=successfulBottomScreen。新 c 覆盖旧 c，不是累加，故“重新捕获必然重复叠加 c”被排除。高度不匹配和后述吸收元组是该前提的明确例外。

**脱离：** `Bar.Interaction.cpp:6417` 从固定 D 切到 raw G（并按 monitor 上下界 clamp），以新的 desiredCenter 计算整数 T。渲染 `:7346` 用输入 tracker 的 e 种子而非最后成功映射反推 e，`:7353` 立即积分；c 不在 Docked→Floating 重播入，`:7378` 继续推进现有 c。

在相同 z、O、base 几何、未触发正高度保护时，以上生产接线给出：

```text
Δtop    = (T_new-T_old) - z*e_old
Δbottom = (T_new-T_old) - z*e_new + z*(c_new-c_old)
```

如果 T_new-T_old=z*e、e_new=e_old=e、c_new=c_old，则两个端点精确连续，**即使 c 非零**。这正是已有 `bar_bottom_dock_tests.cpp:1094` 理想公式测试成立的条件。实际指针有新位移时，顶端应按该位移变化；不能把全部 Δtop 视为闪动。

但无条件连续的保证不成立：

1. **普通 e 限幅与 HWND 实际移动量不等。** tracker 在 `Bar.BottomDock.h:691` 把 e 限制为±24，但只要未到 monitor 边界，Floating 根仍追随完整 G。取前一成功 Docked e_old=20、c_old=-8、z=1；新采样 raw offset=30，T_new-T_old=30，e_new=24，暂取 dt=0、c_new=-8。新 top 相对旧 top +10（正常指针移动），新 bottom 却 +6 px：旧 L-8，新 L-2。这是生产限幅/接线的反例；24 DIP 是已有保护合同，不据此提议改参数。
2. **首个脱离帧已消费积分。** 即使 offset=21 不触发24限幅，`:7353` 也先积分 e，c 同帧继续积分。从 e=21/v=0、c=-8/v=0、dt=1/60 按生产 120 Hz 子步算术得 e'=19.69519125、c'=-7.50293。浮动新 bottom=L+21-e'+c'=L-6.19812125，与旧 L-8 相差约1.80188 px。它可能是正常恢复动画的第一步；需配合真实 dt 和成功帧间隔判断，不能仅称瞬跳。
3. **候选状态与成功像素可能不等。** e/c 是 render state，每个候选可能被推进；F→D 的 c 反推覆盖这类差异，D→F 没有对两个端点做同样的成功像素反推。此项需记录候选前状态、最后成功映射和失败/丢帧历史，由主会话的时间审计判断。
4. **monitor clamp / 显示变化** 会改变上述 T 的相消条件；日志须用实际 T，不能以 tracker e*z 代替 HWND 位移。

正高度保护也需注明：Docked visualTop=min(b+c-epsilon,a+e)，Floating visualBottom=max(a+epsilon,b-e+c)。极端 c 可使映射接近零高；这时上述简化等式不成立，应以实际 M 端点/scaleY 判断。

### 成功快照在“直移”和“吸收”时的不同语义

普通 SetWindowPos 后，`Bar.Interaction.cpp:6506` 调 `RebaseBottomDockPresentedWindow(desiredTranslation,moveDelta)`：位图局部端点不变、T 增加 moveDelta，O+M.visualBottom*z+T 跟随真实窗口移动，代数正确。

松手吸收则不同：`Bar.RenderLoop.cpp:12892` 把 Tabsorbed/z 加到根 y；`:12915` 经 `Bar.WindowGeometry.h:282` 得到 T_after=T_presented-T_absorbed；`:12917` 调相同 Rebase，但 screenDelta 默认0。`Bar.Main.cppm:731` 到`:763` 只更新 translation 和中心屏幕字段，**没有把 mapping.baseTop/baseBottom/visualTop/visualBottom 移入新布局坐标**。

```text
吸收前重建底端 = O + z*V_old + T_presented
吸收后重建底端 = O + z*V_old + T_presented - T_absorbed
真实屏幕底端未移动，重建值却减少 T_absorbed。
```

例：O=0,z=1,V_old=900,T_presented=100,T_absorbed=100，真实成功底端始终1000；吸收后的 snapshot 公式给900。同时 snapshot.mainCenterScreenY 未移动，反推根中心却已包含吸收量，因此快照内部的中心与 mapping 基准也不再一致。这一错误区间到下一张完整成功快照发布才结束（`Bar.RenderLoop.cpp:12614`、`:12680`）。

该结论是**源代码级坐标不一致**；尚不能推断用户必然在该区间重捕获。最小鉴别证据是在 Absorb 前后记旧/新 T、absorbedT、mapping 屏幕顶底、snapshot serial，再看后继 Seed 是否消费了这张重基准快照。

### 最少运行追踪字段与事件

复用主会话的手势编号、输入序号、单调时间、真实递增 frame/成功帧编号和 mode/barrier serial。几何侧无需新状态机或逐像素大日志。

| 事件 | 最少几何字段 | 能分辨什么 |
| --- | --- | --- |
| 成功帧基础记录 / 环境改变 | O、monitor/workArea、dockLine、WhiteboardActive、dpi/configZoom/z；根 y、main h.val/h.tar、main s.val/s.tar、mainBar inhY/h/s；M 的 baseTop/baseBottom/visualTop/visualBottom/scaleY；frameT 与 committedT | dock目标、实际高度、描边、取整/根漂移、提交位移差异。稳定帧可只在变化时记录。 |
| Down | 上述成功 tuple 的 id、pointerDown、生产 grabOffset、q0（或足以离线反推的 u0/y0/h0）、R_down | 区分真实局部抓取点与代理，避免用后来的动画高度解释旧按下。 |
| 采样 / 模式切换 | pointer、G、tracker stableGrip、raw floatingBottom、input e、desiredCenterY、desiredT、上下界 clamp 是否触发、捕获/脱离原因 | e 限幅、窗口整数位移和真实指针是否一致。 |
| 竖向 seed/integrate/derive | successful tuple id、旧成功屏幕顶底、nextBaseBottomScreen、e/c 各自 pre/post position+velocity、seed/recoverySeed 标志、实际积分 dt、最终 M 端点 | 重复播入、候选推进、首帧积分与非零 c 的真实去向。 |
| 直移 / Absorb | 操作前后 presentedT、desiredT、absorbedT/screenDelta，操作前后成功 tuple id 与重建屏幕顶底 | 识别真实窗口平移与只换布局原点；抓出上述吸收窗口。 |
| 成功提交 | 同源 pointer/input id、actualGrabY、proxyGripY、actualNominalBottom、dockLine、实际 committedT | 用成功画面计算误差，不能把从未显示的候选当作用户看到的帧。 |

建议离线派生：baselineError=(O+z*b+T)-L；edgeError=actualNominalBottom-L；grabError=actualGrabY-pointerY；以及根据 Δpointer、e/c 的正常积分剥离后的端点残差。先记录数据，再决定哪类差异与用户看到的闪动相关。

### 现有测试的证据范围

`bar_bottom_dock_tests.cpp:840` 同时断言 rigidGripYDip=20 和 MapY(40)=30，本身已经展示“代理抓手”和实际中心不等；测试通过不能证明实际按下点跟手。`:1094` 的 detach 测试人为令 windowShift=e*z 且两映射 e/c 相同，没有 Seek 的限幅、取整、monitor clamp、首帧积分或 snapshot 吸收。`:191` 验证有效旧 recovery 端点可用于再捕获，但没有运行生产 RebaseBottomDockPresentedWindow。以上都不是错误测试，只是覆盖范围不能支持整条 GUI 结论。

### Related specs

- `.trellis/workflow.md`：本轮是当前任务研究；研究内容只落当前 research。
- `.trellis/spec/native-desktop/index.md`：渲染/输入所有者和 headless 约束。
- `.trellis/spec/native-desktop/cpp-conventions.md`：保留编码/CRLF、最小范围、同一显示快照。
- `.trellis/spec/native-desktop/rendering-and-ui.md:764`：二维吸附事务；`:832` 竖向24 DIP保护，`:833` successful Floating→Docked底端种子，`:829` 松手吸收，`:838` 同源成功映射。
- `research/issue1-runtime-investigation-plan.md`：真实可见描边与0.5物理像素合同；临时日志保持行为和参数不变；实机验收不可由合成测试代替。

### External references / versions

未使用外部文档或联网查询；依据本次读取的本地生产源码和既有测试。父会话给定调查基线为 f7c9b5d5、342990fe 之后的当前工作区；本 researcher 未执行任何 git 操作，未独立验证 commit 身份。数值示例是按已核查 helper 和生产接线进行的算术推导，不是产品执行或新增回归的结果。

## Caveats / Not Found

- 未构建、未运行产品或测试、未创建/控制 GUI、未提交代码；没有像素、屏幕裁剪原点或视频帧的实测值。
- (a) 的几何等式必须与真实成功 tuple/最终提交 T 对齐；若日志忽略此前候选变化或吸收，仍会给出错误“实际底边”。
- 未把实际抓取点不变量失败、浮动 pulse 高度差、24 DIP 限幅、Bezier 小外溢或吸收快照问题中的任一项，认定为用户所有现象的唯一根因。
- 水平问题2/3已由用户验收；这里只为解释共享 tuple 引用水平旧代码，不提出水平改动。
- 后续仅建议增加上述诊断证据；不在本报告提出修复补丁或架构替换。
