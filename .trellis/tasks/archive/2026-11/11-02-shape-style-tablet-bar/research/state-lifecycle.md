# Research: 共享画笔属性的工作区生命周期与持久化边界

- Query: 形状/硬笔/软笔颜色与粗细完全共享时，现有工作区快照、首次初始化、持久化及直接字段消费者有哪些约束。
- Scope: internal
- Date: 2026-11-02
- Task: .trellis/tasks/11-02-shape-style-tablet-bar
- 本轮用户决策（主会话提供）：形状/硬笔/软笔颜色和粗细均完全共享；数位板问题待日志暂不修复；当前只调查状态设计供审核，不实施。

## Findings

### 工作区的真实保存与恢复（事实）

1. `Inkeys/IdtState.cpp:36` 为当前 `stateMode`；`:45` 当前 owner 初始为 Desktop；`:46` 以 `array<optional<StateModeClass>,3>` 保存 Desktop/Presentation/Whiteboard 三份完整工具状态。它们是同进程内状态，不是 JSON 持久化记录。
2. `IdtState.cpp:1483` `RestoreWorkspaceStateModeLocked` 是迁移核心；`:1485` 同工作区直接返回，不重复捕获或覆盖；`:1486` 将离开工作区的完整状态存入对应槽。
3. `IdtState.cpp:1488` 首次进入尚未初始化工作区时，先从当前完整 `stateMode` 复制宽色和其他偏好；`:1492` 白板默认 Pen，另外两区默认 Selection；`:1498` lastAnnotationMode 默认 Pen，`:1499` 关闭 laserActive，`:1500` SoftPen，`:1501` StraightLine。首次进入不是把笔宽/颜色恢复为中档/红色。
4. `IdtState.cpp:1505` 保留当前显示 Context，`:1506` 恢复目标工作区旧工具状态；`:1516` 将当前 Context 放回并 `ResolveWidthSlotsLocked()`，因此旧工作区不能恢复旧 DPI/设备；Preset 按当前 Context 重解，ExplicitDip 维持逻辑值。`:1521` 更新背景对比，`:1524` 推进工具修订号并处理退出选择交接。
5. `IdtState.cpp:1547` 切换事务对意图、工具、场景顺序加锁，`:1566` 调上述恢复；`:1574` 同状态锁发布 Draw3；锁外 `SyncDraw3State()` 和主栏 Refresh。不能把共享值放到这个事务之外单独更新。
6. `IdtState.cpp:1267` `GetCommittedWorkspaceStateSnapshot` 优先给出 Committed 工作区状态；准备目标尚未提交时可读保存的旧工作区，而不是强制读取当前目标。`Bar.RenderLoop.cpp:1196` 生产主栏消费这份 committed 快照，`:1204` 从同帧 `Pen.Brush1.color` 取色。
7. `IdtState.cpp:2620` 起真实生产测试覆盖工作区工具隔离、首次白板 SoftPen、首次 PPT Selection、快速往返、后台 PPT 不抢白板；`:2670` 附近覆盖恢复工作区时保留当前显示 Context。

**推荐（不改变已有工作区产品语义）**：将“完全共享”实现为**每份 StateMode 内，形状/硬笔/软笔共用该工作区的 Brush 槽**。保留三工作区各自完整快照及首次继承规则，不引入一个跨工作区共享的静态 Brush 对象。否则白板改色会污染 Desktop/PPT，属于超出本轮工具共享需求的行为变化。

### 初次启动与初始化（事实）

- `IdtState.h:53` 构造默认 SoftPen；`:56` Brush 默认 `Selection{}` 经 Resolve 得到 Medium，`:58` 默认红色；形状旧独立槽在 `:70` 起初始化。该构造也用于局部快照变量、测试和默认状态，不宜通过引用成员/指针别名破坏普通值复制语义。
- `Inkeys/IdtMain.cpp:3114` 读取现有设置，`:3116` 起建立 paintDevice/真实显示 DPI/像素尺寸 Context，`:3128` 调 `InitializePenWidthPreferences`，之后配置写回。
- `Inkeys/IdtConfiguration.cpp:1137` 的初始化先 ApplyPenWidthContext，再在偏好锁下做一次性初始化；`:1144` 已初始化则返回，不覆盖手动值；`:1146` 起的配置启动选择读取已注释停用；`:1152` 通过 `RestorePenWidthSelections({}, {}, {})` 建立 Brush/Highlighter/Laser 默认 Medium；`:1153` 的 GetMemory 也停用。
- `IdtState.cpp:1719` `RestorePenWidthSelections` 在状态锁内校验选择、更新 Brush/Highlighter/Laser Selection 与可选 Brush/Highlighter 色，Resolve 缓存并增 revision，锁外发布 Draw3。
- `IdtConfiguration.cpp:1294` 起测试残留旧配置；`:1322` 起覆盖版本化启动/记忆被忽略；`:1348` 起覆盖后续初始化不覆盖用户值。

**推荐**：共享属性继续以普通值拥有在 StateMode 中；初始化只初始化唯一 Brush 真值，形状直接读该真值即可。保留当前启动恢复停用的范围，不借本轮改动启用 memory/default 配置、不新增初始化入口。

### SetMemory 与启动持久化（事实）

- `IdtConfiguration.cpp:192` 路径为 `globalPath/Inkeys/Memory/memory.json`。`:160` 读取有大小/深度等边界，文件不存在时按空对象成功；`:184` 原子保存前检查已有文件有效性，避免覆盖坏配置。
- `IdtConfiguration.cpp:1184` `SetMemory` 先拿偏好锁，`:1189` 用 `TryGetDesktopStateModeSnapshot`，非 Desktop 返回 true 但不写盘。`IdtState.cpp:1223` 在同一个 stateModeTransitionMutex 内校验 owner 与复制值，避免工作区判断和快照跨代。
- `IdtConfiguration.cpp:1195` 仅编码 Brush/Highlighter/Laser 三族 Selection；未知 schema 不覆盖 PenWidth；`:1212` 和 `:1213` 分别写 Brush/Highlighter 颜色。磁盘没有形状槽，也没有工作区字段。
- `IdtConfiguration.cpp:1156` `GetMemory` 是保留兼容实现，会解码旧/新配置、恢复 Brush/Highlighter/Laser，再按需原子写迁移证据；生产初始化处没有调用。全目录搜索到旧 `IdtDrawpad.cpp:2192` 的调用，但该文件为工程 `None` 项，不是生产启动路径。
- `IdtState.cpp:2795` 真实生产测试写 Desktop memory；`:2802` 验证 Whiteboard/Presentation 改色改宽后 SetMemory 不改变全局文件；`:2806` 验证回 Desktop 仍可写入。

**推荐**：形状 setter 以后提交到唯一 Brush 槽，则 Desktop 的形状改色/粗细自然被现有 SetMemory 编码，无需新增形状字段、迁移或改变保存 API。PPT/Whiteboard 仍只保留各自会话记忆，不能用本次“完全共享”移除 Desktop 保存门。保留未知 schema 和原子写入规则，亦不恢复目前停用的启动读盘。

### 直接字段访问的消费者与风险（当前源码事实）

| 位置 | 角色/风险 |
| --- | --- |
| `IdtState.cpp:1453` | ResolveWidthSlotsLocked 用泛型 slot 解析 Brush/Highlighter/Laser/两个形状；旧形状槽若保留成无效副本，会继续产生看似有效但不被 getter 使用的第二份真值。共享设计应明确删除旧槽或停止将其视为有效状态。 |
| `IdtState.cpp:1729` | 恢复偏好直接写 Brush Selection/色，是正常批量状态入口，必须继续写唯一共享真值，不能改为只更新某个活动工具临时值。 |
| `IdtState.cpp:1599`、`:1605`、`:1608` | 宽度 setter 当前分别写 Brush/矩形/直线 Selection；属于主体共享路由改动，主会话负责，不能仅改 getter。 |
| `IdtState.cpp:1751`、`:1761`、`:1763` | 同样存在三条颜色写入路由。 |
| `IdtConfiguration.cpp:1195`、`:1212` | 持久化直接读取 Brush，不经过当前工具 getter。以 Brush 为唯一共享真值可沿用；若另建 Shape 共享对象则会漏保存或需要额外改动。 |
| `Bar.RenderLoop.cpp:1204` | 帧快照直接读取 Brush 色供光效/视觉；`:1643`、`:1647`、`:7590` 为后续消费者。共享值应落在 Brush，使视觉和 getter 一致。无需把这些直接读取强行重构。 |
| `IdtState.h:56`、`:70` | 构造值与旧形状槽的初始化；值复制语义供工作区 optional 快照使用。 |
| `IdtState.cpp:2293`、`:2302` | 生产测试 sameState 比较器直接比较 Brush/形状宽色；删除独立槽后测试辅助比较需要同步。 |
| `Bar.Interaction.cpp:3639`、`:3709`、`:3730` | 生产测试直接构造形状 Selection、比较另一形状槽；共享决策下需要改为验证所有形状与 Brush 同真值。其余大量 Brush 字段命中也是局部测试快照，不能误认为生产直接写入。 |
| `IdtDrawpad.cpp` / `IdtFloating.cpp` | 搜索可见旧复制画笔属性及直接字段读写；`Inkeys/Inkeys.vcxproj:943`、`:1189` 明确登记为 None，不参与当前生产编译。不为本轮共享去修复历史实现或重新编译它们。 |

搜索范围：Inkeys 下 `.cpp/.cppm/.h/.hpp` 的 `Shape.StraightLine1/Rectangle1`、`Pen.Brush1` 及其 width/color/widthSelection；另核对 LegacyDrawState 及 vcxproj。此处是静态源访问归类，不是编译证明。未发现 IdtState 外生产代码直接使用两个形状的宽色/Selection，主要直读位于以上持久化和 Bar Brush 视觉消费者。

### Files found / Related specs

- `Inkeys/IdtState.cpp`、`IdtState.h`：工作区和工具值状态、锁、恢复、生产测试。
- `Inkeys/IdtConfiguration.cpp`、`IdtMain.cpp`：默认初始化、memory codec 调用和启动边界。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`、`Bar.Interaction.cpp`：快照直接消费者和旧测试断言。
- `Inkeys/Inkeys.vcxproj`：生产/历史源码归属。
- `.trellis/spec/native-desktop/whiteboard.md`：三工作区工具记忆、首次 SoftPen 和 committed 视图合同。
- `.trellis/spec/native-desktop/pen-width.md`：Context/Selection 身份、memory Desktop 门及启动停用合同。

### External references

本轮仅本地静态源码研究，无外部 API/版本资料或新增依赖。

## Caveats / Not Found

- 本轮只写该研究报告，未改产品代码/规范/PRD，未运行 Git、编译、测试或 GUI。
- 共享工具属性和跨工作区共享是不同范围；用户明确的是形状/硬笔/软笔完全共享。报告推荐保持既有工作区隔离，最终由主会话设计呈现给用户审核。
- 没有新增持久化需求的证据；不应把“目前会保存 memory.json”误写成“下次启动会恢复”，后者当前已明确停用。
- 本报告不重新设计主体工具枚举和 getter/setter，也不调查数位板输入，避免与主会话重复。
