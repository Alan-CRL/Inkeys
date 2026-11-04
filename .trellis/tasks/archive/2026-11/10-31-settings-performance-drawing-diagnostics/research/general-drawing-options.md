# 绘制“通用”三项设置的现有能力与接线边界

日期：2026-10-31。仅源码调研和规划，无产品代码修改、无构建、无 GUI。本文件补充 `draw3-controls-and-canvas-info.md`；需求以用户最新“多指绘图 / 抬笔笔锋修正 / 墨迹预测”及同一笔锁存语义为准。

## 1. 已确认的产品含义与 UI 映射

“通用”分类位于绘制页靠上，顺序固定：

| 设置 | 默认 | 实际能力 |
| --- | --- | --- |
| 多指绘图 | 开 | 原产品可接受多根 Touch；新增总门禁，关闭后拒收额外 Touch Down，Pen/Mouse 不受影响 |
| 抬笔笔锋修正 | 开 | 当前完成笔划的自适应尾段拟合，修正明显抬笔异常折线；不是关闭实时 taper 或改变基础笔宽 |
| 墨迹预测 | 精准 | 精准=Kalman，快速=StrokeEnd，关闭=Disabled |

`Draw3.InkPrediction.cppm:30` 的内部枚举顺序为 `Disabled, StrokeEnd, Kalman`，与 UI “精准、快速、关闭”顺序相反。配置/控件映射必须显式对应，不能直接 `static_cast` 下拉索引。

建议的面向用户说明（供页面文案采用或压缩，不把技术名暴露在普通设置里）：

- 多指绘图：“允许多根手指同时绘图。关闭后，仅响应首根手指；已开始的笔迹不受影响。”
- 抬笔笔锋修正：“修正抬笔时笔迹末端可能出现的异常折线，使收笔更自然。修改后从下一笔生效。”
- 墨迹预测：“调整墨迹的跟随方式。精准模式根据书写趋势预测笔迹；快速模式将笔迹补至最近输入位置；关闭后保留基础平滑，不生成预测笔迹。修改后从下一笔生效。”

不承诺“精准一定更准确”“快速一定更省 CPU”或关闭预测就完全无延迟；本轮没有性能/硬件测量依据。

## 2. 抬笔笔锋修正：当前真正的调用链

### 2.1 当前已集成，自适应入口才是正确目标

`Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:15` 已包含 `<ink_stroke_modeler/terminal_tail_reconstructor.h>`。

当前生产链是：

1. `Draw3.DrawingController.cpp:2393` 的 `CommitRuntimeStoredStrokeCpu` → `:2456` 的 `FinalizeStoredStroke`。
2. `Draw3.StrokeGeometry.cpp:889` 的 `FinalizeStoredStroke` 对 `StoredInkType::Pen` 分支复制完整 `stroke.realPoints` 到 scratch（`:904`—`:906`）。
3. `:908` 无条件调用 `FitCompletedPenGeometry`。
4. `:755` 的 `FitCompletedPenGeometry` 限定物理尾窗，`:798` 调用 **`ReconstructAdaptiveTerminalTail`**，只接受通过有界校验的完整补丁。
5. 返回后 `:916` 继续 `ApplyLiveTipTaper`，`:918` 继续 `EnforceCapsuleTangency`，最后把完成几何写入 `InkStroke`，按原历史/重绘/保存链使用。

`.trellis/spec/native/terminal-tail-reconstruction.md:5` 的“阶段2尚未接产品”是历史阶段说明，不能当作当前整体状态。同一规范 `:70` 起明确写当前产品调用 `ReconstructAdaptiveTerminalTail`；`:97` 记录阶段四最终验收。旧 `ReconstructTerminalTail`、`ReconstructLocalTerminalTail` 为历史/对照入口，本任务无需改用它们，也无需改变 canonical 求解器算法。

### 2.2 开关应只控制完成态拟合

建议给每笔 `ActiveStroke` 保存 `terminalTailCorrectionEnabled`（候选名称），默认 true。`FinalizeStoredStroke` 只在本笔保存值为 true 时调用 `FitCompletedPenGeometry`；false 时跳过这次拟合，后面的 taper、公切线约束、真实 Up、合法性校验、Stored 生成和历史提交都照常执行。

- 不要关闭 `LockPenTerminalState`、`BeginEndpointAdmission` 或模型 `Update(kUp)`；它们分别负责真实 Up、端点约束与正常收尾，并非这个异常折线修正功能。
- 不要用 `liveTipTaperSeconds=0` 来实现该开关；那会改掉已有笔锋外观，却并未准确关闭尾段拟合。
- 不要根据设置页当前值在 Finalize 时决定；同一笔 Up 或断触等待期间即使用户改设置，也必须使用起笔保存值。
- 可以继续维护原固定 raw/模型尾窗，最小 diff 只跳过求解调用，不顺手优化缓存收集。
- 当前拟合适用 `StoredInkType::Pen`；Highlighter/Eraser 在 `StrokeGeometry.cpp:927` 直接保存原真实中心线，Shape 走 `FinalizeStoredShape`，Laser 不持久化到普通墨迹。开关不得扩大它们的修正范围。

当前完成拟合只改副本、不改活动 `realPoints` 和 `committedIndex`。`FitCompletedPenGeometry` 的 `:763` 注释及 `RunPenTaperProductionProbe` 中 `:1264` 的等待接续回归均明确这个边界。

## 3. 三种预测方式的代码事实

### 3.1 Draw3 的现有参数选择

- `Draw3.InkPrediction.cppm:75`：当前 `kActivePredictionMode = Kalman`，是编译期常量。
- `Draw3.InkPrediction.cpp:246` 的 `ApplyPredictionMode` 当前无模式参数，内部 `switch(kActivePredictionMode)`：Disabled → `DisabledPredictorParams`，StrokeEnd → `StrokeEndPredictorParams`，默认 → 已调参 `KalmanPredictorParams`。
- `Draw3.DrawingController.cpp:9989`—`:9992` 在 Run 开始时生成一次普通模型参数与禁用预测的橡皮参数。新设置不能只在启动时替换这个常量，否则无法满足“后续新落笔生效”。

### 3.2 快速模式与精准模式的区别

- `Inkeys/additional/ink_stroke_modeler/params.h:190`—`:197`：StrokeEnd 把最近输入假定为结束位置，快速追上该位置，不实质外推到未来；没有独立可调参数，复用 position/sampling 参数。
- `internal/prediction/stroke_end_predictor.cc:28` 的 `Update` 保存最后位置；`:32` 的 `ConstructPrediction` 从当前位置模型状态调用 `ModelEndOfStroke(last_position)`，这正是用户所指“根据最新位置补一段”的既有模式。
- `params.h:199`—`:204`：Kalman 除了追上最近输入，还可预测最近输入之外的趋势；Draw3 使用 `CreateStrokeModelConfiguration` 的现有 Kalman 参数。
- `stroke_modeler.cc:92`—`:106` 按三种 variant 创建预测器；Disabled 对应空 predictor。`:160`—`:175` 的 `Predict` 在禁用时返回 FailedPrecondition，因此 Disable 时还必须跳过调用，不能只改模型参数而继续照常 Predict。

### 3.3 不能遗漏的四处生产 Predict 门禁

现有四处 `kActivePredictionMode` 判断都应改为该笔保存值：

| 位置 | 用途 |
| --- | --- |
| `Draw3.DrawingController.cpp:11817` | 断触候选创建时，冻结最新 prediction 给可能同帧到来的新 Down 判断/显示 |
| `:15917` | Shape 的活动端点预测；raw fallback 时仍维持原禁用逻辑 |
| `:15956` | Laser 活动轨迹预测 |
| `:16133` | 普通笔、荧光笔、HardPen 等活动轨迹；橡皮仍保持独立禁用，endpoint admission 状态不额外预测 |

已有工具范围应维持：墨迹预测设置应用原本使用普通模型参数的工具，不给橡皮打开预测，也不改变 Shape/Laser 的独立几何和完成规则。

## 4. 每笔锁存与接续的最小实现建议

### 4.1 希望值与每笔值分开

建议 Host 保存“通用绘制偏好”期望快照（总多指、尾修正、预测模式），Setting/启动同步只发布值并唤醒，Host owner 将期望设置交给 Controller。Controller 不遍历或重置活动模型。

新独立笔划的初始化点：

- `Draw3.DrawingController.cpp:11242` 调用 `ResetNominalStroke`，后者 `:2266` 调 `ActiveStroke::Reset`；
- 在 Reset 后把尾修正和预测模式赋给该笔 `ActiveStroke`，再为该笔生成 `modelParams`；
- `:11245`—`:11248` 为该笔 `modeler.Reset(modelParams)`，应由同一个保存模式选择预测 variant；橡皮仍选 disabled 参数。

`ActiveStroke`（`Draw3.InkPrediction.cppm:480`）比仅保存于 `RuntimeStroke` 更方便尾修正 finalizer 直接读取。若实现选择 RuntimeStroke 字段，也必须把值传入 Finalize，不能让 Finalize 读取全局期望值。

建议把 `ApplyPredictionMode` 改为接收显式 `InkPredictionMode`，原默认可保持 Kalman 以减少测试/辅助调用改动；生产每笔必须明确传自己的模式。预热池 reset（Controller.cpp:10052、:10563）仍可使用安全默认，真正新笔必须再次按该笔模式 reset。非法持久化值回退精准，不能让非法 variant 进入库。

`ActiveStroke::Reset`（StrokeGeometry.cpp:1839）需要清理上笔锁存状态或恢复默认；生产初始化必须随后写入这次实际值，避免池复用串值。

### 4.2 同一笔断触续接不得重读偏好

- `DrawingController.cpp:10840` 后根据已结束候选的来源身份、工具、时间和运动寻找接续；成功路径 `:11070` 调 `AdoptReconnectedContact`，继续使用旧 runtime/modeler。
- `:11082` 的 `ClearPenTerminalState` 仅清除旧 Up/笔锋封顶，不应清除新增尾修正/预测保存值。
- 新物理 contact 成功接入旧笔时保留旧设置；不要为了当前设置不同而在续接途中重建模型，也不要把设置变化新增为接续身份不兼容条件。
- 未命中旧候选而开始独立新笔，才在 `ResetNominalStroke` 之后读取最新期望值。
- 断触候选的 prediction 必须按旧笔模式生成；关闭预测时保持空 prediction，沿现有 RealTail 回退进行运动判断，不伪造 Kalman 点。
- 多指总门禁针对后续新 Down，但不能追杀已接受 contact；它与“接入同一笔保留旧模型”的逻辑必须分别核查，不能把待接续 runtime 当作仍物理按下的手指。

### 4.3 抬笔不是额外的实时预测开关

`Inkeys/additional/ink_stroke_modeler/stroke_modeler.cc:233` 的 `ProcessUpEvent` 始终先 `UpdateAlongLinearPath` 再 `ModelEndOfStroke`（`:255`），完成后 `last_input_ = nullopt`（`:277`）。这项正常 Up 收尾独立于预测器设置；关闭墨迹预测后仍应保留。

Draw3 `completeModelUp`（`Controller.cpp:11444` 起）会锁住真实 Up、按原模型 Update(kUp)、接纳/兜底尾点并结束 runtime。`:11817` 的 Predict 发生在 **尚未真正 Update(kUp) 的断触候选** 创建阶段；不能把它误写成结束后的任意 Predict，也不能漏用旧笔模式。Up 完成后库不允许再 Predict，设置切换不得改变该约束。

## 5. 可复用验证与必须新增的覆盖

### 5.1 尾修正开关

- `RunPenTaperProductionProbe`（StrokeGeometry.cpp:1140）已有 fitted 与 `finalizeBaseline` 对照；`:1245`—`:1344` 覆盖真实完成几何发生变化、活跃输入不变、raw 缺失/仅 Up、无效尾窗、宽度拒绝、Highlighter 保持、真实 Up 固定和失败回退。
- 可在这组生产 probe 增加开关 false 与原未拟合基线的逐点对照，同时保证 taper 仍执行；default true 的原案例应保持。
- 增加起笔开 → 中途关 → Up 仍修正，以及起笔关 → 中途开 → Up 保持原尾段；下一独立笔才使用新值。等待接续后完成也用旧笔值。
- `Inkeys.exe --draw3-pen-taper-test` 在 `IdtMain.cpp:1473` 进入既有无窗口生产 probe。GUI 与真正笔迹外观另属未授权验证，不以该 probe 通过代替。

### 5.2 三档预测和锁存

- `RunPenTaperProductionProbe` 的 `StrokeGeometry.cpp:1680`—`:1697` 已通过真实 modeler 直接构造 Disabled/StrokeEnd/Kalman 与各帧率模型配置，适合复用输入及尾部不变量。
- 新增显式映射/配置缺项及非法值、每种模式的真实 Reset→Down→Move→Predict→Up：Disabled 不调用 Predict；StrokeEnd 补至最后位置；Kalman 使用已有参数。
- 将“起笔锁存”测试落到生产初始化 helper 或真实 runtime 路径，覆盖两笔同时存活、期望值中途改变、旧笔 Up、候选等待/成功续接、下一独立笔及池复用。
- 覆盖普通笔/荧光笔/HardPen、Shape、Laser 的四个分支与橡皮始终禁用。不能只测 ApplyPredictionMode 的 variant 就宣称生产切换已贯通。
- 沿用本任务原多指生产 gate probe，验证条目重命名“多指绘图”和通用分类顺序没有改变全局/激光子项有效值及终态回收合同。

## 6. 文件与风险边界

必要候选改动：`Other.Config.cppm`、Setting 页及 i18n、`IdtMain.cpp` 启动同步、Host 接线、`Draw3.DrawingController.cppm/.cpp`、`Draw3.InkPrediction.cppm/.cpp`、`Draw3.StrokeGeometry.cpp` 及对应已有无窗口 probe。无需改 canonical Modeler/terminal-tail 求解算法、Shader 或工程依赖。

主要风险：

1. 把“笔锋修正”误接成实时 taper/Up 输入开关，会改变笔宽与端点且不能准确控制现有功能。
2. 只改启动 params 或一处 constexpr，导致活动笔、Shape/Laser、Disabled 或断触 candidate 仍走旧预测模式。
3. 在真实 Up/Finalization 时读取当前全局设置，违背用户“同一笔含收尾不变”的要求。
4. 把历史阶段2规范当当前实现状态，错误绕开或重做已集成的 `ReconstructAdaptiveTerminalTail`。

本文件未更新 spec；实施时如触碰旧 spec 与现有行为不符的段落，应在同一任务的知识同步阶段按当前真实调用链精确补充，避免全篇重写。
