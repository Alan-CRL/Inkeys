# 当前 UI3 动画与结构审计

## 快照

- 审计日期：2026-08-09。
- 分支：`feature/animation`。
- 审计起点 HEAD：`a4201c992bfb722ff2ca50c783e243bd1899612c`。
- 创建任务前工作树为 clean；本文件只记录只读源码调查结果。
- 下列行号只锚定上述快照。实施阶段必须重新搜索符号，不得把行号当作稳定 API。

## 文件与函数规模

| 范围 | 当前行号 | 行数 | 当前职责 |
| --- | ---: | ---: | --- |
| `Inkeys/Inkeys/UI/Bar/Bar.Main.cpp` | 1-17521 | 17,521 | 辅助算法、窗口回调、D2D renderer、布局/动画、交互、线程启动与初始化 |
| `BarUISetClass::Rendering()` | 3593-12466 | 8,874 | 设备资源、目标/布局、动画推进、dirty rect、D2D 绘制、ULW 呈现、休眠与限帧 |
| `BarUISetClass::Interact()` | 12468-15935 | 3,468 | 鼠标/触摸/键盘、hover/press、拖动、Fine Dial 物理、颜色选择器与点击动作 |

`UI/Bar` 下 15 个 `Bar.*` 源文件合计约 22,091 行，`Bar.Main.cpp` 占约 79.3%。`Rendering()` 内部可进一步按现状分为：

- 设备与 render-thread 私有状态：`Bar.Main.cpp:3593-3905`。
- target/layout 计算：`Bar.Main.cpp:3906-7753`。
- 动画推进与派生布局：`Bar.Main.cpp:7755-9677`。
- 实际绘制和呈现：`Bar.Main.cpp:9690-12437`。
- `GetDC` / `UpdateLayeredWindowIndirect` / `EndDraw`：`Bar.Main.cpp:12391-12434`。
- idle wait 与 60 Hz pacing：`Bar.Main.cpp:12439-12462`。

用户给出的 `17.5k / 8.9k / 3.5k` 估算均准确。需要修正的是：`Bar.Main.cpp` 承担大量动画编排和通用推进，但动画基础模型并不都定义在该文件中。

## 已存在的动画基础设施

| 能力 | 当前证据 |
| --- | --- |
| curve 与区间映射 | `Bar.UI.cppm:27-104` 的 `BarUiCurveEnum`、`BarUiApplyCurve()`、`BarUiApplyCurveRange()`、`BarUiCurveSpecClass` |
| batch timeline | `Bar.UI.cppm:107-145` 的 `BarUiTimelineClass`，含 `Restart/Advance/IsActive/GetRemainingDuration/GetProgress/CanJoin` |
| keyframe timeline | `Bar.UI.cppm:148-216` 的 `BarUiKeyframeTimelineClass` |
| state/value/color/pct | `Bar.UI.cppm:220-504` 的 `BarUiStateClass`、`BarUiValueClass`、`BarUiColorClass`、`BarUiPctClass` |
| widget 组合 | `Bar.UI.cppm:648-652,700-897` 将上述属性嵌入 Shape/SVG/PNG/Word 等 widget |
| SVG/Word 内容关键帧 | 声明在 `Bar.UI.cppm:768-897`，实现分别在 `Bar.UI.cpp:230-309,611-686` |
| 50% batch join 契约 | `Bar.UI.cppm:136-139`；当前主要调用点在 `Bar.Main.cpp:4782-4790,5453,6991` |

`BarUiStateClass` 是单个 widget 的动画属性；产品/交互状态 `BarStateClass` 是另一个类型，位于 `Bar.State.cppm:14-115`。实施时必须避免混淆这两层状态。

`Bar.UI.cppm:900-935` 的旧“动效实现备忘”已经漂移。例如其中仍称 Linear/Variable 暂按线性处理，而当前 `Bar.Main.cpp:7803-7935` 已应用具体曲线。Phase 4 应删除或更新这类失效说明。

## 当前所有权与线程事实

- 五个 widget map 声明在 `Bar.Main.cppm:485-489`；当前搜索到的结构写入发生在启动阶段的 `InitializeUI()`，见 `Bar.Main.cpp:16493-16513`。运行时依赖“启动前建图、之后只遍历”的拓扑约定。
- `Rendering` 与 `Interact` 由两个 detached thread 启动，见 `Bar.Main.cpp:16510-16513`。
- 三个 batch timeline 和 57 个具名 `BarUiValueClass` 是 `Rendering()` 栈上跨循环存活的对象，见 `Bar.Main.cpp:3705-3881`，从访问证据看属于 rendering thread。
- Fine Dial 将运动阶段/速度留在交互线程，只发布原子视觉快照，见 `Bar.State.cppm:48-65`；颜色选择器也标明输入线程写、渲染线程读，见 `Bar.State.cppm:77-91`。
- 交互线程仍会直接对共享动画对象调用 `SetTar/SetDirect`，见 `Bar.Main.cpp:12619-12657`；渲染线程同时在 `Bar.Main.cpp:7803-7935` 推进这些对象。
- `IdtAtomic` 是逐字段原子封装，见 `IdtMain.h:103-185`。`Value/Pct/Color/Keyframe` 的 `SetTar/Start` 是多字段顺序写入，见 `Bar.UI.cppm:157-165,282-309,371-381,445-465`。单字段原子不等于一次动画事务具有一致快照；这是 Phase 1/2/5 必须证明的 ownership 问题，当前不预先认定为 data race 或已确认缺陷。
- `UpdateRendering()` 的 mutex 只串行化 producer 与 `StateUpdate()`，并未覆盖 render consumer，见 `Bar.Main.cpp:15937-15952`。
- 关闭路径显式等待 Rendering 状态，但未见对应的 Interact join，见 `Bar.Main.cpp:16517-16530`。detached thread、全局对象和退出顺序需要在 Phase 5 审计。

## 当前通用推进与全量扫描

`Rendering()` 内仍定义通用推进 lambda：

- `FinishValue/FinishColor/FinishPct`：`Bar.Main.cpp:7759-7789`。
- `ApplyAnimationCurve`：`Bar.Main.cpp:7790-7797`。
- `ChangeState/ChangeValue/ChangeColor/ChangePct/ChangeString`：`Bar.Main.cpp:7798-7940`。
- batch duration 同步：`SyncValueDuration/SyncPctDuration`，`Bar.Main.cpp:4032-4060`。
- hover 生命周期：`UpdateHoverAnimation`，`Bar.Main.cpp:8151-8185`；交互侧 `StartHover/StopHover` 在 `Bar.Main.cpp:12619-12657`。

每个活跃动画 tick 目前会：

1. 检查 57 个 render-side 私有 `BarUiValueClass`，`Bar.Main.cpp:7947-8063`。
2. 全量扫描 `shapeMap/superellipseMap/svgMap/pngMap/wordMap`，`Bar.Main.cpp:8065-8149`。
3. 再扫描注册按钮与 More snapshot，`Bar.Main.cpp:8329-8425`。
4. SVG/Word 即使没有活动内容切换，也会经过一次快速 `AdvanceContentTransition()` 检查。

这些是明确的 measurement candidate，不是已经证明的瓶颈。Phase 2 必须同时记录对象数、活动属性数、wake reason、完整 target/layout 成本和扫描成本，再决定是否建立 active registry 或 dirty generation。

## 专用动画状态

- 19 个独立 hover stage：`Bar.Main.cppm:491-510`。
- primary/cursor/drawing lighting 的 start/target/elapsed/animating 状态：`Bar.Main.cppm:381-440`，推进入口为 `Bar.Main.cpp:1386` 的 `PrepareFrameLighting()`。
- render-side Fine Dial range phase：`Bar.Main.cpp:3741-3751`。
- interaction-side Fine Dial inertia/settling：`Bar.Main.cpp:12713-13048`。
- SVG/Word 自己的 keyframe timeline：`Bar.UI.cppm:821,892`。

这些状态不得为“抽象统一”而强迁。优先复用共同的 curve/timing/interpolation；只有在减少重复状态、降低运行成本或消除一致性风险时才迁入公共 runtime。

## 建议的代码边界

Phase 1 的保守默认是从现有 `Bar.UI` 中形成清晰的 animation partition，而不是创建第三套 framework：

- animation partition 归属 curve、timeline、keyframe、State/Value/Color/Pct 与全局 animation options。
- 把 `Finish*/Change*` 收敛为显式接收 `dt/speed/force` 的无 UI-map 推进 API。
- UI widget 只组合动画属性，不拥有全局推进策略。
- 消除 `Bar.UI.cppm:12-16` 声明 animation extern、却由下游 `Bar.Main.cppm:22-25` 定义的反向依赖。
- Phase 1 先保持扫描与线程模型，建立行为等价和可测试边界；active registry/dirty-domain 留到 Phase 2 测量后决定。

Phase 4 的默认拆分候选为：

- rendering/resource：`Bar.Main.cpp:1295-3482` 的 `BarUIRendering`。
- layout/transitions：`Bar.Main.cpp:3906-9677` 的 target、批次与推进。
- interaction/input：窗口回调、`Interact()`、Raw Input 与 `Seek()`。
- initialization：`Bar.Main.cpp:16489-17521`。
- coordinator：singleton、线程启动、唤醒和模块装配。

最终模块名应服从当前 C++20 module 风格和依赖图；不得为减少行数制造循环 import。
