# 五入口配置、连续尺寸与验证状态

## 当前连续会话
普通橡皮使用ConfiguredEraser和完整五入口表；显式Fixed/Speed工具仍强制类型。笔尖/笔尾响应分别保存，Win7使用稳定枚举安全回退。非Touch各入口复用独立MouseLifecycle会话与所有权票据，普通Up/Down不再Reset/夹小；Detached状态按真实时间回落，真实重连仍使用原冻结协议。SessionConfigCompatible保留真实context/映射/单位安全检查，剔除无害revision与识别补全。首点与待接触光标共用解析，细节和验证见validation-entry-sessions-20260915.md。

## 当前精细区（2026-09-15）
只替代最小到标准的立即响应。精细观察使用140ms真实路程窗，平台/解除阈值为各模型原细段上界的0.20/0.35倍；100ms进入、160ms解除和方向确认并行，双向tau为200/260ms。迟滞带不累积解除证据，原idle历史参与同一精细确认；目标稳定40ms后精确收敛，统一double最小log以保证休眠。清扫资格和面积主导路径不串联新确认，跨standard连续。详情及测量见validation-fine-band-20260915.md。

## 分层
EraserSizes仍是DIP属性单一来源。真实InputSource、ResponseModel、motionSource分离；只有屏幕笔保留标准以上增量的beta补偿，Touch不再要求整目标毫米一致。物理、DIP和经验动作单位不混算。

## 面积
保留RTS原始宽高与per-context换算，校验实际返回的单位、分辨率和范围后才按轴转DIP。首次稳定拖擦50ms建立本接触参考；原位移解锁不变。参考锁存，普通拖擦下限无需清扫资格，但不能由面积或帧时间制造新移动。

面积下限是 `max(speedTarget, contactFloor)` 后的连续动态结果，实际几何、光标和历史宽度区间共用。缺包2s、显式无效200ms宽限、180ms参考释放；重连平移时钟，终态零面积不进入新参考。面积开关单独锁存，不改变Mouse/Pen Hover显示标尺。

## 已定位的正确性问题
- 每包目标变化小于settle tolerance时立即吸附，会让高回报率Touch跳过阻尼。Touch仅在目标稳定时吸附，其他模型保持不变。
- 休眠后诊断快照的idleSeconds不再增长。隐藏测试等待needsAnimation=false和实际尺寸，不等待年龄跨越任意阈值。
- 原错误等待额外耗时15s，暴露模型的单次2000输出上限。测试等待已修正；复查又补齐真实Touch笔速长停顿恢复：使用相同模型参数在最后已接受点短时间重锚，继续累计原结果/几何。保持原输出上限、其他设备行为及保存格式，合成种子不经过速度/面积统计。

## 实验入口与验证
面积辅助迁入绘制设置的橡皮擦块，原Experimental.Inkeys3.Draw3.TouchContactAreaAssistance键不变，程序调测继续同步读写；实验页仅保留独立控制台输出。五入口与两个笔响应选择写入Drawing.Eraser。headless保留第八轮回归并验证会话/配置写读；隐藏测试验证入口、首点、普通跨段及原有面积/历史链。

## 2026-09-23：Touch 场景曲线修正（本轮设计）

### 解析边界与优先级

`ResponseModel` 继续表示真实输入关系（IndirectDip / ScreenPenHybrid / DirectTouch）；`ScaleSource` 表示动作单位来源（TrustedPhysical / ManualCalibration / ResolutionDpiHeuristic / DipOnly）；新增 Touch 场景强度仅在 DirectTouch 且物理标尺有效时选择清扫参数。同一 `Config` 在接触 Down 时锁存，资格、证据、退出和目标都读取它的有效阈值；显示/场景变化只影响后续安全批次。

现有 `paintDevice` 的 Laptop/LargeScreen 是用户可选择并持久化的场景先验，Host 原样传入，不由 EDID 覆盖。内部 `Automatic` 解析仅在调用者明确请求时使用，不增加正式设置页；无可靠尺寸时退到 Laptop 先验。来源和回退理由进入诊断。现有产品在首次无保存值时的启动推荐仍由原入口产生；已保存的 0/1 选择不能被自动识别改写。

可靠尺寸只取已映射且有效的物理/手动标尺，从当前方向的两轴 `mm/px × pixel` 得到长边毫米。旋转交换两轴后权重相同。集中经验节点为长边 320 mm（Surface 端）和 1200 mm（教室端），中间用 `smoothstep` 的 0..1 有界权重；它是表面尺寸先验，绝不声称测得字迹或手速。Laptop 把权重上限约束为 0.25，LargeScreen 把下限约束为 0.25，Automatic 使用原权重。这样约 28cm Surface 的 Laptop 选择严格保留旧端点，约 139cm 教室 LargeScreen 到达大屏端点，20–32 英寸及中型演示屏连续落在两端之间。显式 Laptop 在大屏仍保留较轻的 Laptop 倾向；显式 LargeScreen 在小屏仍保留较强的清扫倾向。

物理或手动标尺下，`fine=30 mm/s` 不变；enter/exit/large 三项共用权重 `w`：`90+260w / 60+190w / 250+1050w mm/s`。小屏 30/90/60/250，大屏 30/350/250/1300。阈值由一个解析结果集中发布并保持有限有序。`SweepActionSpeed` 继续仅作用于精细上界以上，0.85/1/1.15 的增益同时影响资格与目标；标准以上仍用 smoothstep 后的指数尺寸映射，不调全局 tau/保持/面积系数。B 只改变 0.5B/B/5B 尺寸，不改变场景速度。

无可靠物理尺寸时不计算假长边、不把 mm/s 端点直接套到 DIP/s：LargeScreen 且逻辑输出可信时沿用 100/120/80/400 reference DIP/s 的经验路径，Laptop 沿用 100/240/160/700 DIP/s；已有 ForceUnavailable 规则保留。Automatic 无尺寸退到 Laptop DIP 路径。来源/单位和有效阈值明确记录，复制拓扑与无效映射单列测试。

### 诊断与验证

帧级诊断包含请求场景、解析场景/强度/来源、动作标尺与 mm/px 或 DIP/px、有效 fine/enter/exit/large、B/清扫增益、短窗报告速度及清扫速度、长窗 fineSpeed、资格/证据、目标/实际 DIP。`cursorPx` 归属当前诊断 contact，另保留对应实际几何半径；沿用既有限频输出，不加逐包同步 I/O。

先以新增测试证明旧大屏可信物理路径错误，再实现集中解析；测试将公式值、控制器合成回放、隐藏窗口产品接入及真人实机四种证据分开。合成覆盖小平板、较大笔电、20–32 英寸触摸屏、中型演示屏、教室大屏及更大表面，分辨率/DPI/方向/采样率、普通与快速轨迹、回退和面积开关。大屏候选仅是首版标定；设备手感待实测，若中间段不足再作局部、可解释的下一轮调整。

## 2026-09-23：控制台诊断扩展设计

现有 `Experimental.Inkeys3.ConsoleOutput.TouchArea` 和 `DevelopmentOptions::touchAreaTrace` 继续作为兼容开关；仅扩大诊断覆盖并改设置文案，不迁移用户配置。Host 的 `PumpDisplayScale` 已取得同一 Display 快照，在启用时或显示配置刷新时输出一次 `[EraserDisplay]`：EDID 状态/原始尺寸、业务可用物理尺寸及失效原因、拓扑、活动分辨率、有效 DPI 和显示代际。活动分辨率不是 EDID 原始时序，不把缺失 EDID 伪装为厘米。

DrawingController 的可关闭帧级诊断附带当前选中 contact 的 ID/代际、设备类型、输入画布坐标和实际可见光标坐标；光标不可见时明确标记，不借另一指或鼠标的光标。固定橡皮尺寸取已解析接触；非橡皮工具的橡皮尺寸标为无效。Host 在原 250ms 门后追加同一快照的 `[EraserInput]` 行，含帧时间、身份、工具、面积、速度、目标/实际 DIP、光标/几何像素直径，并保留现有 `[TouchArea]`/`[TouchCurve]` 供旧调测。没有输入的启用边缘只报告显示和无接触状态；不在 RTS packet 热路径写控制台。

用隐藏窗口注入 Touch、Pen、Mouse 与固定橡皮，检查身份、坐标归属、可见性和尺寸；三语言文案经 i18n sync/check，完整 ARM64 solution/headless/专项隐藏测试验证。已有 Window Service owner 断言失败单列，不借诊断改动修复。

## 2026-09-25：暖状态短快划响应设计

复用一套 Controller 与既有清扫证据积分/许可目标/对数 Follow，不改 `ReferenceTargetDiameterDip`、场景权重、倍率、面积或小尺寸精细通道。先只替换 Touch 的 evidence start/full/decay=0.080/0.180/0.300s、growth tau=0.220/0.180s、log rate=4/6每秒；ScreenPenHybrid 对应 0.080/0.200/0.350s、tau=0.200/0.160s，log rate 暂保留4/6。Mouse 等间接模型参数和行为不变。若临界速度受 `strength*decay` 限制导致合法目标长期不可达，应以具体速度和证据上界说明，再局部调整，不能全局放宽。

回放将真实输入与独立帧时钟分开调度。先以 0.6×有效 enter 连续2秒使控制器达到 B 且清扫证据归零，再用1.5×有效 large 给50–1000ms快段，随后恢复普通移动、原地包或无Move。每个 trace 在有界内存保存时间、短窗速度、清扫速度、参考目标、控制器目标/实际 DIP、证据、资格和光标/几何宽度；结束后才汇总峰值、阈值到达时刻、后续增长及衰减，并输出代表数据为 CSV。对帧/输入频率不能整除请求快段时，记录实际结束时刻，不把取整请求时长冒充精确脉冲。

测试分别验证暖状态、新 Down、精细、已有大尺寸和跨抬起会话；Touch 场景大小与标尺回退、ScreenPen 物理/回退及 Mouse 冻结参考；中间速度可达、强快划抗短脉冲、面积独立下限和产品光标/新增几何。仅授权替代此前 ScreenPen 标准以上增长时间的冻结约束，其他已接受合同不动。隐藏产品测试原有 Window Service owner 失败独立记录。

候选已由生产 Controller 的 ARM64 无PDB命令行探针验证，暂不需要新增动态状态。`FollowTarget` 的面积下限若主导当前许可目标，Touch 仍沿用旧120/100ms和6/8每秒跟随，避免新清扫阻力拖慢普通面积辅助；无面积时使用本轮新参数。`SweepEvidenceCapDiameterDip` 只读推导既有证据对应的尺寸上限，供可关闭诊断与有界回放使用，不授予新增长许可。250ms产品控制台快照不能捕获100ms动作全程；全程指标由测试内存采样结束后汇总。
