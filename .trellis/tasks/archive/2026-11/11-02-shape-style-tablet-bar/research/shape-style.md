# Research: 形状颜色/粗细未跟随硬笔软笔

- Query: 20261028a 中形状为何不随硬笔/软笔的颜色和粗细变化；最小修复边界是什么。
- Scope: internal
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar

## Findings

### 结论与证据等级

**已确认（当前生产源码）**：硬笔和软笔共用 `Pen.Brush1`，直线/虚线另用 `Shape.StraightLine1`，矩形另用 `Shape.Rectangle1`。形状入口只切工具身份，没有从 Brush 复制宽色。UI 显示、业务 setter/getter 与 Draw3 bridge 一致读取这些独立槽，因此“未跟随”首先是业务状态合同与本轮期望的差异，不能只修主栏图标或渲染器。

**版本映射（主会话提供，非本研究自行 Git 核验）**：主会话定位发布提交 `dc8d0a13c99dcb8de8273f28d7029525cf7c7ab9`，提交说明 `Prepare to publish 20261028a`。主会话核对其与 HEAD 的 IdtState.cpp/.h 增量为掌擦偏好相关49行，未改变形状槽、宽色 getter/setter 或形状切换。该映射支持本链路也存在于该发布源码；不证明用户安装包的二进制来源及现场行为。

**现有合同**：`.trellis/spec/native-desktop/pen-width.md` 明确硬软共享 Brush，形状独立直线/矩形槽；不能凭旧“跟随画笔”注释新增跟随关系。归档 `10-04-pen-width-dip-defaults/research/preset-shape-scope.md` 也明确保留独立槽。用户本轮报告表达新的跟随期望，实施前应明确同步方向/时机，并更新相应合同和旧测试。

### Files found

- `Inkeys/IdtState.h`：共享笔状态与形状独立宽色槽定义，构造默认值。
- `Inkeys/IdtState.cpp`：工具切换、状态锁和 revision、宽色提交、快照查询、Draw3 bridge。
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`：主栏形状入口与属性面板开合。
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`：形状预设点击、异步保存包装及真实业务生产测试。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`：颜色和形状预设读数呈现。
- `Inkeys/Inkeys/Business/PenWidthPolicy.hpp`：Brush 等族的预设和 DIP 解析策略。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp`：桥接工具及样式提交。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp`：产品样式有锁快照。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`：Down 锁存样式、DIP 到画布转换及测试。
- `Inkeys/IdtMain.cpp`：初始化窗口前的生产测试入口。

### UI → 业务 → Draw3 调用链

1. `Bar.Button.cpp:433` 首次点击形状时调用 `ChangeStateModeToShape()`，随后关闭画笔/几何属性面板；再次点击只切换几何属性面板。没有宽色复制。
2. `IdtState.cpp:2128` 的 `ChangeStateModeToShape()` 在状态锁内设置顶层形状身份、lastAnnotationMode、背景对比和 revision，锁外 `SyncDraw3State()`；没有修改 Shape 或 Brush 宽色。`IdtState.cpp:2143` 子形状切换同样仅改 Shape.ModeSelect。
3. `IdtState.h:48` 遗留注释称直线和矩形样式跟随画笔，但实际构造在 `:70` 只以 Brush 默认宽度初始化两个形状宽度，并各自默认红色。结构 `:120` 为每个形状保存独立 `width / widthSelection / color`。初始化相同不等于持续共享。
4. `Bar.Interaction.cpp:1711` 颜色包装调用真实 `SetPenColor`；`:1723` 预设包装调用真实 `SetPenWidthPresetIfRevision`。几何面板 `:6294` 定义直线、矩形和0/1/2粗细按钮；`:6355` 有效抬手才调用该条件预设 setter，保留版本/拖出取消语义。
5. `IdtState.cpp:1585` 的 `SetPenWidthSelectionImpl` 将硬/软笔提交到 `Pen.Brush1.widthSelection`，在形状模式则只写当前直线/矩形槽。`:1453` `ResolveWidthSlotsLocked()` 虽全部用同代 Context 解析，Brush、StraightLine、Rectangle 各自有独立 Selection。
6. `IdtState.cpp:1680` `GetPenWidthSelection` 根据形状类型读取独立 Selection；`:1690` 形状仍使用 Family::Brush 预设策略。这是“同一档位算法”，不是“共享当前选择”。`:1782` 的 float 宽度 getter 也读取相应独立缓存。
7. `IdtState.cpp:1740` `SetPenColor` 对笔族与形状分别写槽；`:1805` `GetPenColor(snapshot)` 在形状模式读取 Shape.StraightLine1/Rectangle1.color。改变硬笔/软笔颜色不会写 Shape。
8. `Bar.RenderLoop.cpp:1834` 主栏笔色从相同 getter 得到；`:4556` 几何档位读数走 `GetBarThicknessPresetMetrics(SoftPen, ..., stateMode)`，复用 Brush 档位算法，没有另一套错误粗细表。
9. `IdtState.cpp:418` 将形状映射 SolidLine/DashedLine/OutlineRectangle；`:474` `PublishDraw3StateLocked()` 从 `GetPenWidthMetrics(stateMode).resolvedWidthDip` 和 `GetPenColor(stateMode)` 发布样式。
10. `Draw3.Host.cpp:1750` 映射形状工具，`:1766` 直接 `SetProductVisualStyle(state.colorRgba, state.widthDip, state.penWidthContext)`。`Draw3.WindowControl.cpp:772` 有锁保存样式，未另选 Brush。
11. `Draw3.DrawingController.cpp:2297` 仅在 Down 捕获 ProductVisualStyle；`:12044` 实际调用。`:256` 的 `CanvasDiameterForTool` 对形状与普通笔同样只做一次 DIP → canvas px。修复不能靠绕过业务 getter 或在绘制中途强刷活动形状。

### 已有测试与后续验证

- `IdtState.cpp:2903` 附近真实状态测试验证有效宽度只改目标槽，`:2915` 明确矩形宽度修改不改变直线槽。
- `Bar.Interaction.cpp:3698` `shape-presets-use-shared-brush-policy` 对实线/虚线/矩形循环真实 setter，断言与 Brush 使用同一预设数值，`:3731` 又明确 Brush 和另一形状保持原值（`bar-shape-highlight-preserves-independent-slots`）。共享方案会使这条旧合同需要适当调整，不能只让新用例通过。
- `Draw3.DrawingController.cpp:4699` 起生产测试覆盖连续名义宽度和形状单次 DPI 转换；`:4749` 起覆盖活动样式锁存、下一笔接收新样式。
- CLI 位于 `IdtMain.cpp:1594` `--state-mode-production-test`、`:1610` `--pen-width-production-test`、`:1633` `--draw3-pen-width-test`；本次只定位入口，没有运行旧二进制冒充验证新行为。
- 后续至少覆盖：硬/软笔改宽色后切实线/矩形；由荧光/激光/橡皮进入形状；形状面板改粗细再返回笔；实线和矩形来回切换；Desktop/PPT/Whiteboard 各自工具状态恢复；Preset 身份与 ExplicitDip 的换屏行为；按下后改变工具不污染活动形状。
- 实施后按根规则完整 `InkeysRepo.sln Debug|ARM64` + ARM64 原生 MSBuild + 同 invocation PATH workaround；适用早退生产探针和 headless `--no-window`。真实笔/数位板和可见窗口测试需用户单独授权。

### 最小修复方向（尚未选择或实施）

- 方案 A：完全共享 Brush 颜色和宽度。统一形状宽度 Selection/getter/setter、颜色 getter/setter 到 Brush，并审查初始化、解析、状态恢复及旧测试；形状改粗细也会反向改变硬/软笔，直线/矩形不再独立记忆。不可只改 float width 缓存，否则 Selection 在下一次 Resolve/换屏时覆盖。
- 方案 B：进入形状时复制 Brush 的 color + widthSelection，保持后续形状独立。复制应在同状态锁、同修订事务内完成，再 Resolve/发布；需明确复制当前子形状还是全部形状、形状内切换是否重置、擦除返回形状是否复制，以及从非 Brush 笔型进入时是否采用保留 Brush 值。
- 两方案都应留在业务状态层，延续同锁快照、revision 和 Down 样式锁存，不需要修改 shader、DIP 策略或新增配置。

### External references / Related specs

- 无外部 API 争议，无新增依赖；本轮仅本地源码及任务记录调查，未引用外部资料。
- `.trellis/spec/native-desktop/pen-width.md`：Selection 为用户真值、形状独立槽、同 Context、一次转换及活动样式锁存。
- `.trellis/spec/native-desktop/index.md`：生产源码/历史路径与无 GUI 验证边界。
- `.trellis/tasks/archive/2026-10/10-04-pen-width-dip-defaults/research/preset-shape-scope.md`、`preset-shape-research.md`：历史形状独立槽要求；历史行号不得直接用于当前源码，本文链路使用当前行号。

## Caveats / Not Found

- 未运行应用、GUI、编译、生产探针或真实设备测试；以上为静态证据，不宣称已现场复现或修复。
- 当前实现证据只能说明独立记忆行为，不能代替用户决定“跟随”是持续双向共享还是进入形状时单向复制。主会话需就此收敛一个产品问题。
- `IdtState.h:48` 的旧注释与实际实现冲突；不能只依此认定当前实现意外偏离此前需求，因为较新的 spec、任务范围和生产测试明确要求独立槽。
- 未自行运行任何 Git 操作；发布提交差异由主会话确认后提供。
## 用户澄清（2026-11-02，优先于上文方案枚举）

形状、硬笔、软笔的颜色应为同一个颜色，修改任意一处同步；单次进入复制方案已排除。上文独立槽与历史任务仅解释现状，不能代替本轮需求。颜色修复候选应统一业务层颜色读写，并检查状态恢复与旧测试；不需要再询问是否共享颜色。后续用户进一步明确粗细也完全共享；颜色和粗细两项均已收敛。形状/硬笔/软笔统一使用 Brush 样式，进入复制方案不适用。用户要求先审阅 design.md 的状态设计报告；本轮不实施。
