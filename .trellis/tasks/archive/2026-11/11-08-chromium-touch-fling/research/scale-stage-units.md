# Research: 当前白板平移的尺度单位、参考锚点与最小适配边界

- Query: 在用户已确认4b4edb91手感可用的基础上，如何完整适配屏幕物理尺度，同时保留当前电脑的参考体验；固定参考毫米、当前设备锚定和仅改动作/摩擦各自能保证什么。
- Scope: internal；只读生产代码、现有规范/交接，独立代数复算；没有编译、GUI、Git操作或生产代码修改。
- Date: 2026-11-08
- Status: 研究建议；用户要求方案，尚未批准本阶段具体锚点/参数/实施。

## Findings

### 1. 必须从当前向量摩擦出发，旧PPI96表不再描述产品现状

当前产品门面显式使用Friction曲线；PhysicsBased/Legacy和96 PPI仍留作参考组件。`CanvasPanFlingCurveKind`默认值仍为PhysicsBased不能证明产品沿用它，实际调用见`Draw3.CanvasNavigation.cpp:975–978`附近的Dispatch FlingStart。

当前自由曲线（未触硬限位、无boost）：

```text
s0 = hypot(vx,vy), tau=.30 s, c=1000 DIP/s²
T = tau*ln(1+s0/(c*tau))
D = s0*tau - c*tau*T
s(t) = max(0,(s0+c*tau)*exp(-t/tau)-c*tau)
```

证据：`Draw3.CanvasNavigation.cpp:759–773,809–819`；距离保持释放方向。当前Friction分支只验证viewport两轴>0，不采用PhysicsBased分支的viewport×3×boost端点限制（后者在786–796）。不得把旧“v²距离/2s时长/PPI分母”作为本阶段参数修改对象，也不能把旧表的2.34倍差异当作本版本结论。

独立标准库Python按当前公式复算，同500mm/s、无boost、未限位：

| rho=mm/DIP（示例，非用户实际设备） | 对应DIP/s | 当前总距mm | 当前时长s |
| ---: | ---: | ---: | ---: |
| 0.166666667 | 3000.000000 | 114.031571 | 0.719369 |
| 0.234375000 | 2133.333333 | 105.845827 | 0.627970 |
| 0.390625000 | 1280.000000 | 91.591489 | 0.498419 |

仍有设备差异，但曲线是近线性摩擦，不能把平方模型的风险倍数照搬。以上是理论完整终点，不是实测或任意离散中间帧位移。

### 2. Files found

所有`Draw3.*`短名均在`Inkeys/Inkeys/Drawing/Draw3/`。

| 文件与行号 | 作用 |
| --- | --- |
| `Draw3.CanvasNavigation.cppm:14–18,25–66` | 参考/产品时空常量集中区 |
| `Draw3.CanvasNavigation.cpp:345–412,443–505` | 逐指尾段、异常时间/方向检测和末段上限 |
| `Draw3.CanvasNavigation.cpp:525–582,609–629` | slop/几何Scroll、产品释放向量门限 |
| `Draw3.CanvasNavigation.cpp:637–701` | boost的速度门限、方向点积、续期和当前残余投影 |
| `Draw3.CanvasNavigation.cpp:759–819,850–887` | 摩擦曲线与动画时钟/终点补足 |
| `Draw3.CanvasNavigation.cpp:916–939,1073–1124` | hand/content速度、年龄gate、诊断与释放选择 |
| `Draw3.DrawingController.cpp:2619–2632,2750–2787` | 当前scale仅含DIP/px，首手势/无动画时锁存 |
| `Draw3.DrawingController.cpp:2898–2929,2953–2960` | 输入像素→DIP、输出DIP→像素、viewport与sourceKey边界 |
| `Draw3.Host.cpp:1618–1661` | 已发布Display快照→DIP/px与可信cm/px，检测输出映射 |
| `Draw3.SpeedEraser.h:99–118` | DisplayScale已有物理密度、输出/方向、generation/revision等事实 |
| `Draw3.SpeedEraser.cpp:248–312,336–365` | 真实直接/间接来源、完整映射/密度校验、场景权重与fallback |
| `Draw3.PalmEraser.cpp:30–45` | 已有借用尺度校验的例子；只接受明确毫米单位 |

### 3. 所有空间参数的迁移清单

| 当前值/逻辑 | 当前单位与证据 | 尺度适配处理 |
| --- | --- | --- |
| 最小fling 30 | DIP/s，cppm:26；产品Up用向量模长，cpp:613–614 | 在选定惯性度量域统一定义/比较；不是按物理像素直接复用30 |
| boost新/旧速度350、滚动150 | DIP/s（平方存储），cppm:47–48；cpp:637–687 | 与release及residual在同一域；方向点积同样应先统一度量 |
| 产品最终速度上限12000 | DIP/s的向量模长，cppm:64；cpp:657、1081 | 同域限幅；物理域的数值等于12000×参考毫米/单位 |
| Coulomb减速1000 | DIP/s²，cppm:66；cpp:769、814 | 同域转换；tau不乘尺度 |
| 反向确认1、末段资格1 | DIP，cppm:62；cpp:383、395、464 | 如要求同物理动作资格一致，应定义在惯性参考空间；不可遗漏这两个位置用途 |
| 短dt异常允许距离 | cap×(dt+1/QPC频率)+两端float误差，cpp:358–364 | cap和坐标必须同域；数值误差界从原始float像素量化经线性变换传播，不能随意加固定毫米epsilon |
| 最新可靠方向与拟合方向点积 | 坐标速度向量，cpp:503–505 | 各向异性变换后重新求范数/方向，不能仅改最终distance |
| touch slop15、epsilon.05、scroll epsilon.1、provider 1e-3保护 | 几何UI DIP，cppm:27–30；cpp:545–568 | 独立产品政策：最小惯性阶段可保持；若保持则不能承诺整手势起步毫米一致 |
| focus/span、viewportSizeDip | 当前几何DIP；Controller:2905–2906及输出Snapshot | 不应让名为DIP字段偷偷装入毫米/参考单位；需要明确输入/输出边界和单位诊断 |
| tile prediction150ms、1.5视口对角线 | 时间/视口空间；既有渲染策略 | 仍消费真实输出px/s；不重新用reference速度当px/s，不改tile策略 |
| blur门限300、归一分母6000、最大12 | 显示DIP/s、DIP，cpp:102–108 | 显示/性能域，首阶段保持；不能随惯性毫米换算一起改变视觉模糊 |
| viewport安全边界±1048576、坐标有限性 | 既有世界/显示安全域，cpp:21–45 | 保持已有安全合同，不乘设备物理密度或重解释旧文档坐标 |
| KeyboardStep64 | DIP，cppm:15，非当前鼠标/Touch尺度需求 | 不扩大范围到键盘/滚轮等新平移入口 |

保持不随空间尺度变化：tau=.30秒、tail50ms/左锚点、至少3点20ms、age30–70ms、gap40ms、异常最小间隔2ms、boost1ms测速下限与50ms续期、容量64/2048/16、时间戳epoch和QPC、动画首消费帧起步/终点补足、来源隔离、笔/页面/模式Stop。参考组件的17000轴限、100ms LSQ、80ms Up、Physics/Legacy常量保持参考测试语义，不把它们批量替换成产品参数。

### 4. 共同单位框架与数学等价

设每轴`d_i=DIP/px`、`m_i=mm/px`，则`rho_i=m_i/d_i=mm/DIP`。

选定固定参考长度`r0`（mm/reference-unit）：

```text
x_ref_i = x_px_i*m_i/r0
v_ref_i = v_px_i*m_i/r0
K_i = rho_i/r0, x_ref=diag(Kx,Ky)*x_dip
fling d_px_i = d_ref_i*r0/m_i
```

在ref域使用当前30/150/350/12000、1、c=1000、tau=.30，即等价于物理域：

```text
v_min_mm = 30*r0
v_boost_mm = 150*r0,350*r0
v_max_mm = 12000*r0
reverse_mm = tail_distance_mm = r0
c_mm = 1000*r0
```

物理初速度保持原始可靠手速；不能额外乘一个“适应大屏”的倍率。拖动几何始终1:1；向量摩擦统一标量进度，正确线性逆映射仍保持实际释放方向。boost点积与投影、cap、tail质量护栏必须在同一空间里完成。

在各向同性rho下，如果想让CPU内核仍使用DIP而仅传有效参数，则必须同时令`c_dip=1000*r0/rho`、全部速度门限`v_dip=V*r0/rho`、空间资格`x_dip=X*r0/rho`；这与完整ref域变换等价。**只改一个c不等价于完成全部适配。** 两轴rho不同，不能用一个平均c完美替代向量度量变换。

### 5. 三种方案比较

#### A. 固定r0=25.4/96=0.264583333mm

数学简单、跨设备同profile可物理一致，但96只是参考单位选择，没有用户当前设备标尺的含义。500mm/s得到总距102.666311mm、时长0.596330s；c=264.583333mm/s²、最小fling7.9375mm/s、最大3175mm/s。

若当前认可机器恰为rho=1/6，原版是114.031571mm/0.719369s；固定r0会令距离约缩短10%、时长约缩短17%。因此不能同时声称“采用固定96参考”和“当前电脑手感数值原样不变”。`.25mm`同样是任意参考，500mm/s给104.170157mm/.611065s，也不会天然保基线。

#### B. 当前已确认设备标尺锚定（建议作为待评审方案）

在可信参考输出上记录**固定版本的**rho_ref，取`r0=rho_ref`，并记录参考mode/profile、显示身份/方向、实际物理来源与有效DPI。以rho_ref=1/6为说明：c=166.666667mm/s²，500mm/s在任何同profile设备上都是114.031571mm/.719369s；参考机自身所有标量阈值和摩擦与现版相同。

锚点必须固定在产品参数/明确校准配置中，不能每台设备首次启动或每次切屏重新以自己为锚；后者令每台都回到旧DIP策略，失去跨设备统一的意义。用户机器的实际rho尚未取得，表中1/6不是该机器测量值。

“以当前电脑为锚”不等于每种profile都保持旧手感：参考电脑的原mode/profile应先确认，不能假定它当前一定为Laptop/Desktop。若当前确为桌面，则保留桌面基准，大屏profile若有意采用tau=.36/c系数.8，其距离与时长必然发生有意变化。若当前已处于LargeScreen或中间权重，场景曲线须在实际w_ref处归一化（例如tau(w)=.30*f(w)/f(w_ref)、c(w)=1000*r0*g(w)/g(w_ref)），否则启用profile本身会改掉已确认手感。这是场景响应选择，需要与事实标尺分开命名/评审；不能把场景调参说成纯单位修复。

#### C. 只归一动作意图，或只改摩擦

- 仅以mm/s选择profile/判断快慢，曲线仍按旧DIP的1000和.30：500mm/s仍产生第1节的114/106/92mm，不保证自由滑行物理一致。它可用于有界场景补偿，但需诚实把目标称为“响应适配”。
- 只令c_phys固定、c_dip=c_phys/rho，同时保持旧DIP门限：未碰门限和cap的轨迹可以得到一致曲线；然而最小起滑、boost资格、最高速度、反向确认与tail上限仍物理不一致。普通500mm/s表容易掩盖这些边界。
- 如果补齐C中所有空间门限且各向同性，它实际已经等价于B/A选定参考长度后的完整物理适配；不能再省略单位核查。

### 6. 各向异性与不能同时满足的目标

若参考机`rhoX != rhoY`，原DIP欧氏norm与物理欧氏norm不同。单一r0和各向同性物理摩擦无法对所有方向精确复制原基线。例如令r0=rhoX，水平可精确一致，竖直仍有rhoY/rhoX的差异。

可选项（均需明确产品接受标准）：

1. 使用标量代表值（例如几何平均）建立各向同性物理目标，接受参考机由已测各向异性导致的小偏差，给出实际容差；异常密度/映射则回退并诊断。
2. 保存rho_refX/Y参考张量，用`q=diag(1/rho_refX,1/rho_refY)*x_mm`建立精确参考机基线；代价是参考机各向异性会传播到所有设备，同样毫米速度在不同方向的停时仍不等，旋转基准也需定义。这不等同于纯物理各向同性。
3. 在事实源先拒绝可信度不足的异常物理密度，但不能为了保手感擅自把真实非方形像素说成坏数据；已有物理验证规则应先核对，新增判定不能自称已有。

因此以下目标不能无条件同时成立：每一台设备都完全保留旧DIP手感；所有设备同物理轨迹完全相同；所有方向物理各向同性；桌面/大屏有意采用不同响应。建议优先级为“当前参考机基线+同profile跨DPI稳定”，再明确各向异性容差与profile差异。

若保留15系统DIP slop，还须限定验收“进入滚动后的惯性物理一致”；因slop_mm=15*rho，它在各屏起步所需毫米本来不同。可在后续单独把slop改为参考毫米和同域径向扣除，但那是可观察手感变更，不能暗中随惯性换算实施。

### 7. 最小可实施边界与测试重点

建议主方案先确定锚点政策，取得当前参考输出的可信标尺和现有mode，再分两步：

- **单位闭环**：新增小型pan专用motion metric/parameter对象；从已有DisplayScale和真实InputSource解析每轴物理映射、可信度、source、revision；geometry与motion单位显式区分，motion→DIP/px回路可测。不要让VelocityDip、contentVelocity或诊断名仍称DIP却装reference值。旧kernel默认参数保持现有DIP测试；适配时完整处理第3节的产品运动参数。
- **场景响应**：Desktop基准与LargeScreen参数在已锁定物理域上选择，参考profile不改变。按真实物理长边和显式模式可复用橡皮320–1200mm的平滑权重思想，但参数需要平移专用定义；大屏候选tau .30→.36、c系数1→.8只是主会话提出的起始评审值，尚无实机依据，不由本研究批准。

可以借用PalmEraser已有`ResolveConfig`事实校验思路，但整个Eraser Config包含development强制响应、penBeta、橡皮灵敏度等策略。平移不能静默受橡皮专用override影响。必要时仅提取事实尺度验证函数，验证橡皮结果逐字段不变；或明确净化策略输入后复用校验，不复制多套EDID枚举。

锁存整个接触批次及相连fling的metric/profile；新触点来源不兼容、monitor/DPI/方向/映射改变时走停止栅栏并清历史/残余，下一手势取新参数。不用新尺度重算已存在尾段。相同显示快照重复发布不应每帧停止。

必须测试：

- 相同物理轨迹、同profile、100/150/200/300% DPI与分辨率变化，自由停时/距离、release gate/boost/cap/反向/tail资格均一致（限位与初始slop差异需单列）。
- 当前锚定参考机在相同轨迹上与4b基线比较；标量锚各向误差容差应由真实数据说明，不能假定为0。
- anisotropic每轴换算、斜向方向保持、旋转、未知/冲突来源、EDID缺失、复制屏/输出跨屏fallback和mid-gesture变化。
- mouse保持输出DIP；笔仍只有停止语义。源切换不跨metric继承boost；viewport/tile/blur仍得到正确显示速度。
- 旧960Hz/10MHz量化边界与真实压缩拒绝在新空间重复验证，不能仅用理想double毫米轨迹替代float像素入口。

## External References

本主题无需新增外部事实；公式来自当前本地生产实现，旧Chromium链接仅为历史参考。本轮没有访问远程源码或承诺Edge实际参数；所列数值通过标准库Python独立代数复算，不是产品运行。

## Related Specs

- `.trellis/spec/native-desktop/canvas-pan.md`：当前产品权威，尺度阶段仍标Deferred。
- `.trellis/spec/native-desktop/input-and-ink.md`、`draw3-integration.md`：真实输入、准入与显示发布边界。
- `.trellis/spec/native-desktop/palm-eraser.md`、`eraser-attributes.md`、`pen-width.md`：可复用事实与不可混用策略。
- `.trellis/spec/native/runtime-and-rendering.md`：世界坐标、视觉/安全边界、tile/Freeze合同。

## Caveats / Not Found

- 尚未取得用户当前电脑可信的rhoX/rhoY、有效DPI、输出身份和所选mode；不能凭旧示例、单屏假设或当前分辨率指定锚点。
- 没有实测任何profile参数，未运行C++/MSBuild/GUI；物理一致性只是可验证目标，不等于大屏更舒服。
- 本文件只记录研究和待决设计，未改prd/design/implement/spec/代码，未执行任何Git操作。
- 旧display-scale-audit里Physics/PPI96“当前”字样属于7b阶段历史；本研究优先用于4b之后方案，不覆盖历史证据。
