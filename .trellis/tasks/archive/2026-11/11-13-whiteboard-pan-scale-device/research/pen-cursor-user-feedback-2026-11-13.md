# Research: 移动模式笔光标消失的确定路径

- Query：用户反馈笔默认光标在白板移动模式消失，普通绘制正常；为什么系统默认与应用光标都没有，是否由本轮尺度/惯性改动引入？
- Scope：internal；当前生产源码、既有规范与归档记录，只读研究。历史 Git 对照由主会话执行并提供，本研究代理没有执行 Git 操作。
- Date：2026-11-13
- 当前实现提交：主会话已提交 `c2f21bd0761fe50696c8839c6e9ad73bd644f469`，任务继续 in_progress。本轮未修改产品/测试/配置/规范，也未运行应用、测试或 GUI。
- 用户范围已明确：**仅移动模式消失，普通绘制正常**；不再把“绘制光标也消失”当作待调查事实。

## Findings

### 1. 确定结论

移动模式中，**应用自绘光标被 Selection 分支统一关闭，而系统光标策略仅向 Mouse 交还箭头，Pen 仍无条件隐藏系统光标**。两个旧策略组合后，没有给笔保留任何替代光标。

这是源码直接可证的产品反馈缺口，与物理毫米换算、短尾曲线、释放速度是否有效没有依赖关系。即使 Pen 的新 token 权威反馈已判 accepted、没有 suppression、样本有效，Selection 早退和 Pen 系统隐藏仍同时生效。

该解释也符合用户“移动模式没有、普通绘制正常”的范围：普通绘制不会走 Selection 早退，可以生成按设备选择的自绘光标；移动模式在读取这些设置之前就返回。

不能据此声称所有硬件上的原生系统笔点都已经做过现场观测；本轮证明的是应用实际执行的系统 HCURSOR 策略和自身 visual 路径，未做 GUI/硬件显示验证。

### 2. 文件地图

| 文件 | 职责 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | Selection 早退，按实际工具/来源生成应用主光标 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | WM_SETCURSOR、Pointer/RTS owner、系统 SetCursor 以及新身份反馈 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp` | 系统隐藏纯策略及应用 visual resolver |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h` | 默认屏幕笔/数位板样式和落笔隐藏默认值 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.MouseInput.h` | Selection 与白板移动模式的关系 |
| `.trellis/tasks/archive/2026-11/11-08-chromium-touch-fling/mouse-latency-analysis.md` | 既有 Selection/Pan 不绘应用光标的历史说明 |
| 同任务 `research/mouse-latency-render.md` | 旧源码 Selection 早退的研究记录 |

### 3. 自绘光标：在解析默认样式前就被关闭

1. [MouseInput.h:29](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.MouseInput.h:29) 明确 `CanvasPan() = Selection() && whiteboard`。因此白板移动使用 Selection 状态，而不是一个仍绘制笔预览的独立模式。
2. [DrawingController.cpp:16671](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:16671) 的 `buildDrawingCursorVisuals` 先清 `currentCursorVisuals`、`laserTipVisuals`，随后 `if (window_.SelectionMode())` 直接返回（:16681），诊断为 `frame selection=1 visuals=0 laserTips=0`。
3. [DrawingController.cpp:16713](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:16713) 之后才解包 cursorSettings，[DrawingController.cpp:16763](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:16763) 才调用 `ResolvePrimaryDrawingCursorVisual`。移动模式不会到达这些分支。

因此设置里的默认小圆点、画笔图标、甚至给笔选择的“鼠标”样式都不会在移动模式出现。`CursorStyle::Arrow` 对笔也属于 Draw3 自绘，不表示 Window 要恢复真实系统箭头，见 [CursorSettings.h:8](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h:8)。

### 4. 系统光标：Selection 只交还 Mouse，Pen 仍 SetCursor(nullptr)

1. [WindowControl.cpp:2424](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2424) 在 `WM_SETCURSOR` 且 `HTCLIENT` 时调用 `ApplyWindowCursor`，并返回 TRUE；私有刷新消息也调用它。
2. [WindowControl.cpp:2080](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2080) 起先确认当前命中窗口确实是画布/子窗，避免覆盖其它应用的光标。
3. [WindowControl.cpp:2112](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2112) 取 `selectionMode`，将 `mouseUsesSystemCursor` 设为 `selectionMode || mouseUsesSystemCursor_`。旁边注释直接写明“选择态只交还鼠标；白板仍显示主画布时，Pen/Touch继续按原策略隐藏”。
4. [WindowControl.cpp:2119](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2119) 调用 `ShouldHideSystemDrawingCursor`。Selection 只屏蔽 Eraser/Laser 专用隐藏参数，**没有给 Pen owner 增加恢复例外**。
5. [PenCursor.cpp:475](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:475) 的 `case DrawingCursorPointerAuthority::Pen: return true` 不看 profile、EDID、Pen 样式或是否 accepted。
6. [WindowControl.cpp:2139](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2139) 执行 `SetCursor(hide ? nullptr : defaultCursor_)`，所以 Pen 结果是 nullptr；默认句柄在 [WindowControl.cpp:645](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:645) 加载为 `IDC_ARROW`。

Pen owner 不依赖屏幕笔还是数位板的视觉分类。RTS/Pointer 发布有效 Pen 样本时使用 Pen authority：[WindowControl.cpp:1780](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:1780)；Pointer 归属解析在 [PenCursor.cpp:496](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:496)。在一般无活跃 Touch takeover 的笔输入下，两种笔设备都会命中同一系统隐藏分支。

这不是全局 ShowCursor 计数失衡的证据：这里显式使用当前画布 owner 的 SetCursor，不调用 ShowCursor 计数。

### 5. 按实际输入通道区分

| 实际来源/模式 | 系统光标 | 应用光标 | 移动模式结果 |
| --- | --- | --- | --- |
| Windows Ink 屏幕笔，owner=Pen | 隐藏 | Selection 早退 | 无光标 |
| Windows Ink 外置板，owner=Pen | 同样隐藏 | Selection 早退 | 同样无光标，与其惯性使用 IndirectDip 无关 |
| 正常 Mouse owner | Selection 令 mouseUsesSystemCursor=true | Selection 早退 | 正常系统箭头（活跃 Touch 尚未真实 Mouse takeover 时仍可先隐藏） |
| 非 Windows Ink 板以普通 Mouse 到达，识别注入板开关关闭 | 按 Mouse | Selection 早退 | 系统箭头 |
| 注入 Mouse 已 opt-in 标为 ExternalPen | `mouseSampleIsTablet` 强制 mouseUsesSystemCursor=false | Selection 早退 | 也可能无光标 |
| Touch owner | 隐藏 | Selection 早退 | 无光标，原触摸策略 |

注入板例外直接见 [PenCursor.cpp:465](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:465) 与 [WindowControl.cpp:2125](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:2125)。它不是“数位板一定会有系统箭头”的保证，实际取决于设备是 Pen 还是 Mouse 通道及已保存识别偏好。

### 6. 普通绘制为什么正常，默认落笔隐藏为什么不是本案原因

普通绘制不走 Selection 早退，才会进入 resolver：[PenCursor.cpp:313](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:313)。SourceKind IntegratedPen 且无 Conflict 选 ScreenPen，其它 Pen 选 Tablet；随后按实际样式/工具/接触状态生成自绘 visual。

[CursorSettings.h:34](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.CursorSettings.h:34) 的默认值是屏幕笔 Dot、数位板 HighlightDot，`dotHideOnContact=ScreenPen`（:38）。[PenCursor.cpp:362](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:362) 仅在实际软/硬笔的 Dot/HighlightDot 且 contact 命中时隐藏；hover 不因此隐藏、其它样式与专用工具另有规则。

用户已明确普通绘制正常、仅移动模式丢光标，所以不应建议“关闭落笔隐藏”来解释本案：Selection 早退在 resolver 和这些设置之前，改该开关也不会让移动模式出现替代光标。

新 token suppression 会影响普通绘制接触样本是否有效，但本案在 suppression 为 false 时也必然无移动模式 visual。没有证据把本次移动光标现象归因于 token 失败或短尾参数。

### 7. 与本轮改动的关系和历史证据

主会话提供的只读 Git 对照（本研究代理未执行 Git）：

- `git diff 1b819d2d HEAD -- Draw3.PenCursor.cpp` 为空；系统 hide helper 本轮没有修改。
- `WindowControl.cpp:2112–2139` 的 Selection 只交还鼠标/SetCursor 逻辑，blame 归属 `e9e9b1f5f`（2026-10-19）及 `fc9a8b865`（2026-08-16），不是本次 `c2f21bd0` 新增。

仓库内已有更早持久记录也指出 Selection/Pan 不画应用主光标：

- [mouse-latency-analysis.md:6](D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/archive/2026-11/11-08-chromium-touch-fling/mouse-latency-analysis.md:6)：Selection/Pan 显示系统箭头、不绘应用主光标；该报告讨论 Mouse 路径，不能扩写成 Pen 也被交还系统箭头。
- [mouse-latency-render.md:48](D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/archive/2026-11/11-08-chromium-touch-fling/research/mouse-latency-render.md:48)：Selection/移动时不绘应用主光标，并给出当时源码定位。

结论边界：**策略早已存在，本轮延续了它；既有不代表用户理应接受无光标。** 当前移动模式支持笔拖动，但其反馈仍沿用“Selection 关闭自绘 + Pen 隐藏系统”的组合，因此存在需要后续处理的体验缺口。先确定这一原因，不在本轮擅自选择系统箭头或自绘笔光标，也不改代码。

## Related Specs

- `.trellis/spec/native-desktop/draw3-cursors.md`：设备样式、默认值、Arrow 对笔属于自绘、Selection 清应用主光标。
- `.trellis/spec/native-desktop/canvas-pan.md`：移动模式已有光标策略与 Pen/Touch 隐藏、真实 Down/hover 和惯性边界。
- `.trellis/spec/native/runtime-and-rendering.md` 的自由画布导航节：移动/Selection 路由与窗口反馈。
- `.trellis/spec/native-desktop/input-and-ink.md`：RTS/Pointer 来源、owner 与 latest 样本。

## External References

本问题由本地实际调用链与条件可回答，未使用外部资料。没有新增 Windows API、库版本或跨系统行为保证。

## Caveats / Not Found

- 没有运行 GUI、日志探针或测试，不把 static SetCursor 决策当硬件视觉显示实测。
- 没有读取用户当前持久光标设置，也没有把默认 Dot 误认成用户显式选择；本案早退发生在样式读取前，不影响主结论。
- 历史 Git 证据由主会话提供；代理仅读取已有源码/档案，不执行 Git 操作。自绘早退的历史还由旧任务文档支持。
- hide helper/Selection规则未改，不等于证明所有实际显示变化绝非本轮引入；本轮确有source/token发布路径调整，具体硬件到达顺序需日志才能归因。确定的是当前移动模式存在独立于这些时序的“系统笔隐藏+应用不画替代”组合。
- 本文只解释用户已界定的移动模式光标现象。屏幕笔惯性是否被释放后的其它路径取消，由独立惯性研究处理，不能用这个光标缺口直接推断。
