# Research: 白板平移的设备标尺与 Chromium 单位链

- Query: 固定 96 PPI 为什么无法保证不同尺寸设备的惯性体验；笔速/手掌橡皮哪些能力可以复用；Chromium 的速度、DPI 与 PPI 是否同一单位。
- Scope: mixed；只读调查生产源码及 Chromium main，不修改代码、不构建、不启动 GUI。
- Date: 2026-11-08
- Task: `11-08-chromium-touch-fling`；本文件是续接研究和拟议方案，不表示新方案已获实施批准。

## Findings

### 1. 当前实际只使用 Windows 逻辑 DPI，尚未使用可信物理尺寸

| 文件/行 | 已确认行为 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h:99` | `DisplayScale` 已含 DIP/px、cm/px、物理可用位、monitor、输出像素尺寸、旋转、revision/generation。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1618` | `PumpDisplayScale` 从已发布显示快照生成完整标尺，不要求平移重复枚举 EDID。 |
| `Draw3.Host.cpp:1646` | DIP/px = 96 / effectiveDpi；这是系统缩放，不是物理密度。 |
| `Draw3.Host.cpp:1651` | 检查输出窗口确实落在目标显示器内，设置 logicalOutputKnown。 |
| `Draw3.Host.cpp:1654` | 可信物理尺寸可用时，cm/px = physicalSize / pixelSize。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2625` | `CanvasPanScaleFromDisplay` 只保留 DIP/px，丢弃物理密度、可信度、来源和显示身份。 |
| `Draw3.DrawingController.cpp:2752` | 无触点且动力学不活动时才锁存 scale；持续 fling/连甩沿用旧标尺。 |
| `Draw3.DrawingController.cpp:2928` | 输入物理像素乘 DIP/px，进入动力学。 |
| `Draw3.DrawingController.cpp:2880` | 客户区像素乘同一标尺，作为惯性距离上限。 |
| `Draw3.DrawingController.cpp:2894` | 动力学增量除 DIP/px，再应用到画布，保证直接拖动 1:1。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cppm:36` | 固定 `kCanvasPanPixelsPerInch=96`。 |
| `Draw3.CanvasNavigation.cpp:565` | 所有设备减速度相同；距离按速度平方计算。 |
| `Draw3.CanvasNavigation.cpp:575` | 时长仅依赖 DIP/s，`min(2, vmax/2300)`。 |

表中缩写文件均在 `Inkeys/Inkeys/Drawing/Draw3/`。当前系统 DPI 换算本身没发现“重复缩放”：主要缺口是缺少物理尺度适配。直接拖动的 1:1 输出应保留，不能为增加惯性距离而给拖动整体乘增益。

### 2. 量纲分析：DIP 不代表屏幕上的真实 1/96 英寸

定义每轴：

```text
s = physicalPixels / DIP = effectiveDpi / 96
m = mm / physicalPixel
rho = mm / DIP = m * s = m / dipPerPixel
p = physicalPixels / inch = 25.4 / m
q = DIP / inch = p / s = 25.4 / rho
v_mm = v_dip * rho
```

当前固定 96 的未限幅端点与时长：

```text
a_dip = 96 * 2.7559e-5 * 1e6 = 2645.664 DIP/s²
d_dip = sign(v_dip) * v_dip² / (2*a_dip)
d_mm = d_dip * rho
T = min(2, max(abs(v_dip_x), abs(v_dip_y)) / 2300)
```

因此同一物理速度下 `d_mm` 与 `1/rho` 成正比，时长也随 `1/rho` 改变。所谓“DIP 适配所以 PPI=96”只保证一套逻辑单位，不能保证物理手势一致。

独立代数复算示例：水平手速 500 mm/s、无 boost、未碰 3 倍视口上限；不是设备实测，也不是离散派发尾差后的总位移。

| 表面宽度 / 分辨率 / 系统缩放 | mm/DIP | 当前输入速度 DIP/s | 曲线端点 mm | 时长 s | 15 DIP slop 的物理长度 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 320 mm / 1920 px / 100% | 0.166667 | 3000.000 | 283.483 | 1.304 | 2.500 mm |
| 600 mm / 3840 px / 150% | 0.234375 | 2133.333 | 201.588 | 0.928 | 3.516 mm |
| 1500 mm / 3840 px / 100% | 0.390625 | 1280.000 | 120.953 | 0.557 | 5.859 mm |

同一速度在这里产生约 2.34 倍距离/时长差，足以说明设备尺寸是实质性变量。但这不证明用户卡顿/反向均由尺度造成；在单设备内固定标尺不会自行改变速度符号。

“只把 96 改为物理 PPI”也不充分：若速度仍是 DIP/s，物理一致的分母应该使用 `q=p/s`，不是 `p`。即使修正距离，现有时长和 30/150/350/17000 速度门限仍在系统 DIP/s，物理意义依然不同。

### 3. Chromium 复核：浏览器路径与物理一致性目标必须区分

本地参考位于 `C:/Users/alan-/AppData/Local/Temp/inkeys-codex/chromium-src/`。沙箱中的 `$env:LOCALAPPDATA` 指向另一目录，因此应使用实际绝对路径。平台调用链另经官方 main 源码在线核查。

1. [ScreenWin](https://raw.githubusercontent.com/chromium/chromium/main/ui/display/win/screen_win.cc) 415–450：触摸设备的 `GetPointerDeviceRects` 提供 HIMETRIC device rect 和像素 screen rect，用二者之比算物理 px/in；741–745 按 DIP 屏幕坐标选显示器并返回其 PPI。无有效触屏来源时默认 96。这里没有把 PPI 除以 device scale factor。
2. [FlingScheduler](https://raw.githubusercontent.com/chromium/chromium/main/content/browser/renderer_host/input/fling_scheduler.cc) 70–77：Windows 将 PPI 查询交给 ScreenWin。
3. [MotionEventAura](https://raw.githubusercontent.com/chromium/chromium/main/ui/events/gestures/motion_event_aura.cc) 27–32：手势采样沿用 TouchEvent 坐标。[WindowEventDispatcher](https://raw.githubusercontent.com/chromium/chromium/main/ui/aura/window_event_dispatcher.cc) 654–663 的合成触摸路径明确区分已保存的 DIP 坐标与输入入口的 raw pixels。
4. [WebInputEvent](https://raw.githubusercontent.com/chromium/chromium/main/ui/events/blink/web_input_event.cc) 308–316 和 [BlinkEventUtil](https://raw.githubusercontent.com/chromium/chromium/main/ui/events/blink/blink_event_util.cc) 448–455：事件转换复制 gesture velocity，不在此乘系统缩放。后者 620–622 虽存在缩放 fling velocity 的通用函数，不能据此断言它在 fling 前执行。
5. **关键顺序证据**：[InputRouterImpl](https://raw.githubusercontent.com/chromium/chromium/main/components/input/input_router_impl.cc) 149–166 在收到 gesture 后先 `PassToFlingController`；设备缩放 `ScaleEvent(...device_scale_factor_)` 位于 618–619 的发送 renderer 路径。[GestureEventQueue](https://raw.githubusercontent.com/chromium/chromium/main/components/input/gesture_event_queue.cc) 68–70 直接转交 FlingController。因此不能把 renderer 前的乘 DSF 误套到 browser-side fling 输入上。
6. [FlingController](https://raw.githubusercontent.com/chromium/chromium/main/components/input/fling_controller.cc) 369 起把 booster 速度、根视口尺寸和 scheduler PPI交给曲线。[PhysicsBasedFlingCurve](https://raw.githubusercontent.com/chromium/chromium/main/ui/events/gestures/physics_based_fling_curve.cc) 51–73、79–94、131–148：PPI参与端点，速度参与时长和控制点，二者没有统一的物理归一化步骤。

结论：已核查的常规 Aura/browser-side 路径保留了逻辑手势速度与物理 PPI 这套经验模型。给 Inkeys 传物理 PPI，可能更接近该 Chromium 路径；给 Inkeys 传 DIP/in，或把曲线整体改为物理参考单位，是明确的产品适配。两者不能都称为“逐字复刻 Chrome”。用户本轮目标是稳定、跨设备舒适的白板，建议优先明确物理适配契约，并保留 Chromium 参考配置作对照，而非继续以源码一致性代替体验验收。

### 4. 可复用橡皮已有能力，但不能搬整套响应参数

`Draw3.SpeedEraser.cpp:265–295` 已检查真实来源映射、monitor、输出位置/尺寸、旋转、手动标定、密度有效性。物理可信时 `motionPerPixelX/Y` 是 mm/px（298–301），否则保持 DIP 或明确标记的启发式单位（305–312）。

`Inkeys/Inkeys/Display/Display.cpp:1104–1142` 的 EDID 物理尺寸解析已拒绝未知/复制混合拓扑、非唯一目标、失败快照、尺寸缺失/过小，并处理旋转。这些可信度机制可以复用；不应重新以“EDID valid=true”替代完整判断。

`Draw3.PalmEraser.cpp:30–45` 是已有借用示范：通过 `ResolveConfig` 获取映射校验后的 mm/px，仅在确认为物理单位时采纳，其他路径用显式估算 DIP；不会把橡皮经验增益当毫米。

笔速橡皮的模式并非另一套真实尺寸：`Draw3.SpeedEraser.cpp:11–14,342–355` 用 320–1200 mm 长边做平滑场景先验，Laptop/LargeScreen把权重约束在0.25的一侧，改变擦除意图门限；`186–193,306–312` 的分辨率/DPI增益是 EDID 失效时的经验退路。它们是擦除策略，不是白板位移物理定律。

建议复用：完整 DisplayScale、真实 InputSource 映射、可信度/旋转/回退规则、批次锁存、诊断来源。

不建议直接复用：橡皮 `fineToStandardSpeed/sweepEnterSpeed/largeTargetSpeed`、面积辅助、橡皮灵敏度、随速度增长的尺寸逻辑，以及把 ResolutionDpiActionGain 当物理 PPI。也不应让橡皮开关或橡皮专有调试覆盖默默改变画布行为。

### 5. 拟议最小修复方案（后续实现阶段）

先完成输入保真和再次接触状态修复，再接入设备尺度，否则调参会掩盖错误采样。尺度部分独立成可对照的修改：

1. 引入小型纯 CPU `CanvasPanMotionScale`/等价参数对象。由现有 DisplayScale + 本次真实 InputSource解析，保留每轴 mm/px、DIP/px、source/estimated、显示 revision/monitor；只复用尺度校验，不把整套 Eraser Config当平移配置。必要时仅提取这一小段通用校验，橡皮数值保持逐字段不变。
2. 可信触屏的惯性域采用统一物理参考单位：令 `rho0=25.4/96 mm/reference-unit`，把 `v_reference = v_dip*rho/rho0`、视口同样换算，在此域计算端点、时长和惯性速度门限，输出再乘 `rho0/rho` 回 DIP。这样相同 mm/s 的轨迹在不同分辨率/DPI下可对照。可以保留现有曲线形状，避免同时换曲线族。此处96仅是参考单位定义，不再冒充设备PPI。
3. 对斜向/非等比密度按轴转换后再做范数、点积与主导轴判断，不用一个平均PPI替代两轴。booster是否保留及加速上限由本次状态审计决定；若保留，其门限必须在同一惯性度量域，不能把物理速度与旧DIP速度相加。
4. 直接拖动保留物理像素 1:1。触发 slop 单独定义：最小首版可保留15系统DIP作为UI启动策略，明确它不物理等价；如真机证据显示大屏起步过滞，再独立引入有界物理slop，不能和惯性一起暗调。多指质心始终使用同一批次标尺。
5. 无可信物理尺寸/映射的设备显式回退当前DIP模式，记录原因。鼠标等间接设备保持DIP模式。禁止通过估算英寸伪造“物理一致”。大屏/桌面模式可复用现有分类作为有界响应配置的先验，但首轮系数保持中性；在真实小屏/大屏轨迹比较后再确定不同摩擦/距离上限参数，不直接使用橡皮阈值。
6. 每次手势/相连fling锁存参数，避免EDID刷新、设置变化或第二指加入导致坐标尺度跳变。真正输出屏幕/拓扑/DPI变化沿现有控制栅栏停止并清除旧booster，下一手势采用新参数。当前 `CanvasPanScale` 无revision，实施时应验证已有Resize等停止钩子是否覆盖同尺寸DPI变更。
7. 诊断输出同时记录原始 px/s、DIP/s、mm/s（仅可信时）、有效尺度来源、未boost/boost后速度、目标距离/时长/视口上限和最终派发位移，避免只有DIP数值却据此评判跨设备手感。

在相同500mm/s下，统一物理参考单位的上述基准计算给出约178.57mm端点、0.822s时长；这只是现有曲线常量的等价参考，不是已验证的最佳参数。

### 6. 后续验证设计

- 跨设备同物理轨迹：上述三种尺寸加100/150/200/300%缩放，比较起滑速度、距离、持续时间；在未碰视口上限时应物理一致。保持同DIP轨迹不是这个测试的替代。
- 同一设备仅改变系统缩放：物理惯性应相同；直接拖动永远1:1。
- 横竖屏/异向密度：每轴转换闭环；斜向方向不因错误平均密度漂移。
- EDID缺失、复制屏、source映射错误、热插拔、未发布快照：稳定DIP回退，无NaN，无旧booster跨标尺累加。
- 同一手势中显示快照更新：不重新解释已有样本；真实输出变更停止而非续用跨单位历史。
- 将当前DIP、Chromium经验PPI、建议物理参考三种配置作为离线对照，实机A/B才确定最终产品默认；不增加用户设置页作为本次修复前提。

## Related Specs

- `.trellis/spec/native-desktop/index.md`
- `.trellis/spec/native/index.md`
- `.trellis/spec/native-desktop/palm-eraser.md`
- `.trellis/spec/native-desktop/eraser-attributes.md`
- `.trellis/spec/native-desktop/pen-width.md`
- `.trellis/spec/native/runtime-and-rendering.md`（实际实施前复核平移契约）

## Caveats / Not Found

- 本轮没有真机轨迹、EDID实际数据、Edge版本/feature配置证据，不把数学差异断言为全部用户体感根因。
- Chromium在线链接是main可变内容，核查日期如上；正式差分测试应固定commit或保存源码校验摘要，避免与先前本地副本无意混用。
- 目前证据足以否定“固定96能保证跨物理尺寸一致”，但不能证明何种场景参数最舒适；大屏可能需要不同人体动作策略，必须与物理单位转换分开验证。
- 本文件未运行编译/测试；数值表为独立公式复算，源码和工程文件均未修改。
