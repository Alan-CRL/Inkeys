# 阶段 1 静态交接（2026-11-08）

> 以下原文保留为第一轮历史；其中A–E待决与未构建状态由文末“第二轮修订”取代。第一轮真实构建/测试结果由主 Agent 提供，本轮不冒称已复跑。

## 状态与验证边界

S1–S5 的无冲突实现、CPU 测试、控制器探针及文档已写入。下列 A–E 尚未裁决，不能称 AC1–AC8 全部通过；任务保持 in_progress。阶段 2 未实施。

- 本环境启动 PowerShell 即 CreateProcessAsUserW Access denied，且任务明确禁止执行 exe/git/构建；所有文件检索改用 Node fs 的递归文本匹配。
- 未执行 Python、MSBuild、应用、GUI、Headless 或 Git diff。results_chromium.txt 明确“未运行”；不能把脚本存在当成黄金值已运行。
- 已完成：六类导出方法声明/定义与新使用符号核对；全仓旧导出/常量引用扫描（包含 Host、HiddenWindowTest）；源码公式逐项静态复核；Node 独立 double/float 运算复算部分黄金值。
- 已完成：编码/EOL检查，接口 render-budget/tile 尾部及实现 tile 尾部逐字保持；背景/Freeze 测试块逐字保持；animation_tests.cpp 原有三个注册点保留，无需修改。

## 改动文件

| 文件 | 用途 |
|---|---|
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cppm` | 公开接口、集中常量、曲线开关、诊断与缩放预留 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CanvasNavigation.cpp` | 共享tracker、手势、booster、曲线、fling控制器与门面 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 客户区DIP尺寸接线、逐帧日志、旧生产探针契约同步 |
| `InkeysHeadlessTests/draw3_canvas_pan_tests.cpp` | Chromium CPU数值/手势回归，保留背景与Freeze块 |
| `.trellis/tasks/11-08-chromium-touch-fling/design.md` | 源码核对、输入适配和A–E待决 |
| `.trellis/tasks/11-08-chromium-touch-fling/implement.md` | 执行交接状态与未运行验收说明 |
| `.trellis/tasks/11-08-chromium-touch-fling/implement.jsonl` | 新增research实现上下文 |
| `.trellis/tasks/11-08-chromium-touch-fling/check.jsonl` | 新增research检查上下文 |
| `.trellis/tasks/11-08-chromium-touch-fling/research/chromium-fling-model.md` | 源码行为修正与PPI/feature来源 |
| `.trellis/tasks/11-08-chromium-touch-fling/research/sim/chromium_fling.py` | 纯标准库公式参考脚本 |
| `.trellis/tasks/11-08-chromium-touch-fling/research/sim/results_chromium.txt` | 黄金值未运行状态与主Agent命令 |
| `.trellis/tasks/11-08-chromium-touch-fling/research/stage1-static-review.md` | 静态验证、符号与未决交接 |
| `.trellis/spec/native/runtime-and-rendering.md` | 惯性契约/矩阵/误区同步并标历史证据 |
| `.trellis/spec/native-desktop/input-and-ink.md` | 输入模型引用与阶段边界 |
| `.trellis/spec/native-desktop/draw3-integration.md` | 集成模型引用与阶段边界 |
| `.trellis/spec/native-desktop/whiteboard.md` | 白板模型与hover契约引用 |
| `.trellis/spec/guides/cross-layer-thinking-guide.md` | 共享tracker/拓扑清窗跨层规则同步 |
| `.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/design.md` | §2–§3添加后续覆盖说明，保留历史 |

## 已核对的新类型、枚举与函数

命名空间沿用已有 Inkeys::Drawing::Draw3；没有新增外部命名空间依赖。

- `CanvasPanVelocityTracker`：`AddMovement`、`Clear`、`Reset`、`ClearPointer`、`PointerUp`、`Estimate`、`SampleCount`、`LastSampleQpc`、`ClearAxis`
- `CanvasPanGestureDetector`：`Down`、`Move`、`Up`、`Reset`、`Scrolling`、`Tracker`
- `CanvasPanFlingBooster`：`ShouldBoostFling`、`GetVelocityForFlingStart`、`ObserveGestureEvent`、`ObserveProgressFling`、`Reset`、`StartingVelocity`、`CurrentVelocity`、`CutoffSeconds`
- `CanvasPanFlingCurve`：`Start`、`Advance`、`ClearAxis`、`Duration`、`Distance`、`P1`
- `CanvasPanFlingController`：`Start`、`Progress`、`Cancel`、`StopFling`、`ClearAxis`、`Active`、`Velocity`、`Offset`、`StartSeconds`
- `CanvasPanDynamics`：`Snapshot`、`Dispatch`、`Step`、`Stop`、`StopFling`、`ClearAxis`、`Phase`、`Active`、`ContentVelocity`、`HeldContactCount`、`VelocityTracker`、`SeedContactHistory`
- 独立函数：SolveCanvasPanCubicBezier。
- 类型/枚举：CanvasPanPointer、CanvasPanPointerDiagnostic、CanvasPanGestureType、CanvasPanGestureEvent、CanvasPanGestureBatch、CanvasPanFlingCurveKind；FrameInput/Output 新字段均核对声明与控制器消费者。
- 接线：WindowSize/WindowController::Size、CanvasPanInput::Step、LogCanvasPan 已检索现有定义。控制器从客户区像素尺寸经锁存 dipPerPixel 标尺传入 viewportSizeDip。
- 标准库：array/span/vector/optional/cstdint/cstddef、copy/move/stable_sort/find_if/count_if/clamp/min/max、isfinite/isinf/abs/hypot/sqrt/exp/log/floor/llround/copysign/numeric_limits；对应 include 已核对。新 min/max 调用均采用括号防 Windows 宏。
- 删除：Slipping、CanvasPanFriction/FrictionStep、StepCanvasPanFriction、CanvasPanGlideDistance、CanvasPanReleaseGate、FindTracker，以及旧摩擦/释放门/合并/样本有效性常量；相关控制器与 CPU 测试同步。

## 已修正的静态发现

- .1 阈值使用 float .1f，防 float(.1)提升 double 后错误越限；派发为二维 OR 门，小轴保留。
- 终态先冻结释放年龄，再执行80ms清窗并读取释放速度；Cancelled不贡献释放速度。补充age=.080/speed=0/Idle回归，避免清窗把年龄诊断抹成0。
- Provider 在零二维增量时不派发 Begin/Update；detector 的越 slop/last focus 状态仍更新，保留后续 UP 的 Begin→FlingStart fallback。
- Raw tracker 同 QPC movement 是源码允许的；门面重复 Move/帧拒绝，终态可与 Move 同 QPC，避免卡住 held。
- float QR 静止非零坐标存在源码舍入噪声；不加入归零阈值。测试用可精确表示的原点首指覆盖零速不变量。
- 控制器探针把重复释放帧推进到下一帧；连甩有效窗口场景的 Down 放到首消费帧末，使30Hz下首 Move 也在50ms内；生产时间/阈值未放宽。

## 待决 A–E 与当前实现

A. PRD/design 要求首帧包含 slop，GestureProvider 最终事件会扣 slop。当前保留旧完整内容位移并 TODO，不代表已裁决；未把争议数值写成已通过断言。
B. design 要求内容终帧补足，源码 controller 对 inactive 直接结束。独立 Curve 返回完整 distance；controller 当前不补发并 TODO，完整内容终点验收未完成。
C. 同帧所有 Move 合一与跨生命周期严格 QPC 全序冲突。当前在 Down/Up 边界 flush 连续 Move 段并 TODO，混合跨边界适配尚未验收。
D. 历史入口仅实现首指；有活动触点时多指历史加入返回 false，缺失其他 id 的同步历史格式待定义。
E. 静止加减指的 focus 恒定与质心定义冲突；当前按源码重算 focus。float QR 还可能给出微小舍入速度，不能承诺任意非零坐标的估速位级不变。

## 需要主 Agent 执行

1. python -I .trellis/tasks/11-08-chromium-touch-fling/research/sim/chromium_fling.py；检查退出码并回填 results_chromium.txt，对照现有断言，不凭结果放宽预期。
2. 按当前 VS 安装定位原生 ARM64 MSBuild；同次 PowerShell 规范 Path/PATH，再以 InkeysRepo.sln Debug|ARM64 验证，完整构建至少允许5分钟。
3. 运行 InkeysHeadlessTests --no-window（含 --canvas-pan-only）及无窗口 production/control-fence 入口；实际入口与 task plan/已有Host参数核对后执行。记录退出码与首个有意义错误。
4. 重点确认 C++20 module 导出/链接、float QR/曲线容差、base::Seconds(float duration) 微秒量化、生产探针的 release/pen/topology 定位。当前无已知未修复的必然编译错误；这不代表构建通过。
5. 执行真实 git diff --stat/逐文件 diff，核对编码、换行与修改范围；本轮仅有字节基线核对证据。

补充：SampleCount(key) 是连续含该id的环内存储数量；100ms筛选在 Estimate 内执行。门面 booster metadata 固定(0,0)，完整 source/modifiers 拒绝规则由独立 booster 测试覆盖；不增加设备/键盘路由。PPI与slop展开依据见 design 和 chromium-fling-model.md。

最终文件检查：18个任务文件无编码替换字符；源码按原BOM有无保持CRLF；Markdown/脚本CRLF；两份JSONL保持原LF、各13条均可解析且目标存在。旧导出/常量全仓源码零残留；animation注册原样保留。

S4独立复核收束：AdapterOrderingProbe不受enum稳定排序变化影响；WindowSize::width/height及WindowController::Size已在Draw3.WindowControl.cppm核对；18个pan-step参数与4个pointer参数匹配；释放消费帧定位与30Hz boost窗口检查完成。releaseAge静态缺陷已修并新增80ms回归，未发现进一步有据必失败项；仍未编译或执行。

## 第二轮修订（2026-11-08，当前交接）

### 主 Agent 提供的真实结果（修订前）

- Debug|ARM64：InkeysHeadlessTests 与 Inkeys 构建均 exit 0；新增 C4244 位于 CanvasNavigation.cpp 的 Physics distance 计算。
- `InkeysHeadlessTests --no-window --canvas-pan-only` 和完整 `--no-window` 均 PASS（0失败）。
- `--draw3-control-fence-test` 两个每页视角断言失败；`--draw3-canvas-pan-feel-test` constant-100 的稳定delta断言32处失败。上述结果属于修订前，不代表本轮代码已通过。

### A–E 落实

- A：按本地 `gesture_provider.cc` OnScroll / ComputeFirstScrollDelta / SubtractSlopRegion：首次Provider事件当前<3指逐指径向扣15后平均，来源Down缺失则跳过贡献但仍除当前触点数；保留|d|≤1e-3原样返回的源码保护；≥3指不扣，二维全零不派Begin/Update；.05仅detector越限。测试覆盖径向单/双指、平均中的零贡献、当前三指及曾三指当前一指、零派发后重试。
- B：保留inactive立即结束、不补发；新增正向3000 DIP/s独立controller累计量、结束帧零delta、总量≤distance与最后活跃帧尾差断言。Curve终点黄金值不变。
- C：保持严格QPC排序与拓扑边界flush；同帧Move→Down→Move及Move→Up→Move均精确断言10+10 DIP，验证共享movement数量/时间与最终视口。
- D：首指seed与活动多指拒绝保持，新增拒绝后的tracker/held无副作用断言；活动多指历史继续等待阶段2同步格式。
- E：非零绝对坐标静止增减指，contentDelta=0且Apply后的视口保持(123,-45)，focus重算；速度模长<0.01 DIP/s，注明LSQ float舍入，无生产归零阈值。

### 探针根因与精确修复

- 每页视角旧断言在Up/Frame后读取，把2点LSQ产生的fling位移和未扣slop的旧预期混合。现改在Up前读取并Stop后回收终态；期望vport=−d×(1−15/hypot(d))，页0为(-107.5192455849,71.6794970566)，页1(-28,-21)，容差仍1e-3。重新进入Move的首次60 DIP也同步精确预期为−45；Touch/Mouse首滚动探针新增径向公式校验。
- **constant-100结论：首次scroll/slop边界探针问题，不是释放边界或fling start_time回拨缺陷。** 失败断言位于CanvasPanFeelRun::Drag循环，Release在整个400ms Drag结束后才调用；Locked从Down即成立，不能等同于已过slop。100 DIP/s约150ms才越过15.05，旧首次累计位移在30Hz约16.667 DIP，下一整帧仅3.333 DIP；A后首帧剩1.667 DIP，也不是整帧恒速增量。60/120/240Hz的理论首次累计为16.667/15.833/15.417，下一帧为1.667/.833/.417（实际坐标/QPC舍入可移动边界一帧）。
- 修复仅将稳定delta比较限定为连续两个完整后续scroll间隔，均在释放前；2%+0.5px与所有速度阈值保持。失败处无Up，不能产生FlingStart，也就不会触发ProgressFling的起点回拨。

### 本轮静态验证与未完成项

- 导航仅+28/-7，控制器探针+29/-11，CPU测试+87/-17（相对本轮开始字节基线）；cppm原样。render-budget/tile尾部与背景/Freeze测试尾部逐字未变。
- C4244仅为copysign符号参数显式转double、结果显式转float；保留原0.5 double、运算次序和最终舍入，无数值行为变更。
- 用Node文件接口逐项文本检索确认所用类型/函数/字段与声明，核对本地Chromium Provider/Controller；独立检查Agent未发现必失败代码或断言。修改文件BOM有无及CRLF保持，无编码替换字符；逐行比较未见无关源码变化。
- Python脚本只改A/B裁决说明字段，所有数值公式保持；results文件仍明确未执行Python。runtime/input-and-ink/whiteboard/draw3-integration规范镜像已同步A–E，未改变其它合同。
- 本轮未编译、执行exe/Headless/Python/Git/GUI；命令运行器Access denied、Node子进程EPERM是Codex环境限制。真实git diff、本轮ARM64构建、两种Headless与两个生产探针仍需主Agent复跑，C4244消失须以新构建确认。
- A–E无剩余待决；固定96 PPI/native-touch-major适配及阶段2边界沿用既定范围，未新增行为。未commit/push/归档，任务保持in_progress。
