# 工具状态设计报告与共享方案（已批准）

## 范围和状态

用户已确认：形状、硬笔、软笔颜色和粗细完全共享，修改任一处同步。新增需求：桌面/PPT 共用颜色，白板按浅/深背景分别记忆，浅色默认红色、深色默认白色；宽度跨场景全局共享，各工具属性组之间仍区分。用户进一步确认：荧光笔、激光笔各自的颜色和粗细跨全部场景共享，互不合并；浅红/深白默认仅用于 Brush。用户已批准进入实现。数位板主栏点击问题先不修，等待日志。

## 1. 当前状态组织

### 工具身份

`StateModeClass`（Inkeys/IdtState.h:50）保存顶层工具：选择、笔、橡皮、形状、穿透等；Pen.ModeSelect 保存软笔/硬笔/荧光笔，Shape.ModeSelect 保存形状种类。当前可切换实线、虚线、矩形；三角形和圆仅有枚举，不应因本任务扩大支持。

Laser 以 laserActive 独立记住；只有顶层为 Pen 时才激活，进入形状不会因为上次选过 Laser 就使用激光属性。lastAnnotationMode 保存最后的笔/形状/橡皮，快捷键从选择/穿透恢复时只恢复工具身份，具体子型和属性保留（IdtState.cpp:2004）。StateModeSelectTarget/Echo 是现有切换入口同步维护的模式字段，不是额外的颜色/粗细存储。

### 属性分组

| 当前属性槽 | 使用者 | 本轮目标 |
| --- | --- | --- |
| Pen.Brush1 | 软笔、硬笔 | 扩展为软笔、硬笔、实线、虚线、矩形共同使用 |
| Shape.StraightLine1 | 实线、虚线 | 不再具有独立的有效颜色/粗细状态 |
| Shape.Rectangle1 | 矩形 | 不再具有独立的有效颜色/粗细状态 |
| Pen.Highlighter1 | 荧光笔 | 保留独立颜色、粗细及现有半透明合成 |
| Pen.Laser | 激光 | 保留独立颜色、粗细 |
| 橡皮偏好 | 各橡皮入口 | 不纳入 Brush 样式共享 |

当前 getter/setter 按工具选择不同槽：宽度 IdtState.cpp:1585/1680/1782，颜色 :1740/1805。旧构造函数只在启动时给形状相同的初值，之后不会同步，这就是需求差异。

## 2. 粗细的真值与解析

宽度的权威值是 Selection（Inkeys/Inkeys/Business/PenWidthPolicy.hpp:13），而非显示数字或 float width 缓存：

- Preset：记录细/中/粗身份，根据设备类型、输出尺寸、DPI 的 Context 解析。
- ExplicitDip：记录手动调整得到的 double DIP 值；换显示条件时保持这个逻辑值。
- width 为解析缓存；Metrics 提供逻辑 DIP 与画布像素，输出时做一次 DPI 转换。Bar zoom 只影响布局。

共享应让形状/硬笔/软笔的 Selection、颜色及必要缓存都来自同一个 Brush。只同步 float width 或仅把几个预设常量改成一样，都会留下换屏后恢复旧值的问题。

共享的是名义粗细。软笔仍可随压力/速度模型变化，硬笔与形状仍保留各自画法，不要求最终轮廓逐点一样。

## 3. 一次属性操作怎样生效

现有链路：Bar 提交操作 → 状态锁内更新槽/宽度解析 → 发布 Draw3 ProductState → Host 设置当前工具与样式 → 新笔划在 Down 时捕获样式。

状态快照和 revision 用于隔离排队操作：用户切工具/工作区后，旧滑轨、FineDial、PPT 异步结果不得写进新状态。宽度条件 setter 会检查 expectedRevision。磁盘、窗口和桥接副作用按现有边界处理，不在本轮重构线程模型。

Draw3 的 CaptureProductStrokeStyle（DrawingController.cpp:2297）在 Down 捕获颜色、粗细及显示上下文。后续样式变化用于下一笔，已画内容和正在进行的一笔保留原样；历史墨迹不会因为共享当前样式而重新着色或改宽。

## 4. 新需求：工具身份、颜色范围、全局粗细分开

此前每个工作区保存完整 StateMode（包括宽色），首次继承、之后各自恢复。该现状不再满足本轮要求。新的权威状态应分三种：

| 状态 | 归属 |
| --- | --- |
| 顶层工具、Pen/Shape 子型、lastAnnotationMode、Laser 选择记忆 | 继续按 Desktop/Presentation/Whiteboard 各自保存 |
| Brush 颜色 | 按 Desktop+PPT、WhiteboardLight、WhiteboardDark 三个范围保存 |
| Highlighter/Laser 颜色 | 每组各自全局一份，跨全部场景共享 |
| 粗细 Selection | 跨所有场景一份；各工具属性组独立 |
| DPI/设备/输出尺寸 Context | 当前显示环境，恢复工作区时不回滚 |

形状/硬笔/软笔始终合为 Brush 属性组。Highlighter/Laser 保持不同工具组，每组各自一份全局颜色和宽度；不采用白板浅深分色，保留原有默认色。

### 颜色行为

- Brush 的 Desktop 与 Presentation 同用 Primary 颜色槽，在任何一个改色，另一个使用新值；切换工作区仍恢复各自工具身份。
- 白板以当前页实际生效的背景类别选择 Light/Dark 槽。浅色类别首次默认红色（建议沿用现有画笔红色预设），深色类别首次默认白色；用户修改后记住该类别颜色，切换背景类别或翻页时恢复对应颜色，不每次重置默认。
- 现有白板六色：墨绿/深蓝/黑归深色，白/浅灰/米白归浅色；空白/点阵/网格纹理不改变类别。同类别页面共享类别颜色，不为每页再保存一份当前画笔颜色。
- 主题是按页保存的背景，当前画笔颜色是上述类别记忆；已有笔迹保留原色，不随背景类别更改。
- 使用当前真实主题而非预览/请求值选择颜色；失败或过期主题/翻页请求不改变有效颜色。主题/页内容与新笔样式需要有序生效，避免背景已经变深而首笔仍使用浅色槽。

### 全局粗细行为

- 在任意场景改某工具组粗细，其他场景该组立即采用同一 Selection。切换到旧工作区不得从旧 StateMode 恢复旧宽度。
- 全局共享的是 Preset 身份/ExplicitDip 真值，DPI/输出模式仍影响解析后的像素；不是强制每屏输出相同像素数。
- 工作区恢复、批量偏好恢复和 GetCommittedWorkspaceStateSnapshot 都必须投影最新共享属性，不能只在当前活动 stateMode 改值，留下旧快照反向覆盖。

### 状态所有权建议

在现有状态锁保护的业务层维护全局粗细和分范围颜色，StateMode 快照为按工具身份、场景与背景类别解析后的值型快照。继续提供一致快照给 UI/Draw3；保留普通值复制语义，不给 StateMode 增加跨实例引用或裸指针别名。工作区记忆保留工具身份，旧宽色字段不再拥有独立权威；具体字段调整在实施前按消费者清单确定。

白板主题权威在 DrawingController 的当前页 whiteboardThemes；主题命令接纳后经 publishWhiteboard → Host RuntimeSnapshot → IdtState PublishPageSnapshot 到 UI。主题命令接纳不等于背景 Present 成功。themeEdited 仅覆盖主动改主题，不能覆盖翻页、冷恢复及首次进入；不能只挂这个回调。实现需在实际主题/页面生效路径同步样式选择，并对在途首笔进行版本校验或必要门控，不能仅轮询 UI 主题；也不能在绘制线程反调持业务锁并等待 Host 的路径。详见 research/style-scope-and-whiteboard-theme.md。


### 白板样式同步机制

采用独立的 WhiteboardStylePending 原因，合并到 Host 现有 RefreshPresentationInputGateLocked / SetAdmissionBlocked 准入，不借用 workspaceTransitionEpoch。

1. ObserveWhiteboard 在公布需要重新绑定有效 Brush 色的实际页/主题身份前，同步设 pending；同一实际token的重复通知幂等，不因普通页dirty或仅纹理变化重复等待；不同页/generation即使颜色类别相同，也走一次身份匹配ACK，防旧页确认越界开门。
2. 主会话接纳带 hostGeneration、whiteboardGeneration、page、实际 themeRGB/类别的结果，同状态锁选择对应颜色并发布 ProductState；同页改主题不一定推进白板generation，不能只比generation/page。
3. 样式提交带回相同token。Host PumpBridgeState 真正 SetProductVisualStyle 后，只有匹配token与 appliedProductRevision 的应用确认才能清本原因；旧主题/旧Host结果不得释放新pending。
4. 其他pending/加载/工作区阻挡继续合并有效，清样式原因不能直接全局放行。离开白板按完整身份退休本原因，不让旧ACK放行另一次进入。
5. Host白板回调只登记短状态和准入，不反调重型业务；业务从Runtime快照异步接纳，不持业务锁等待Host ACK。绘制owner的样式应用完成后做确认。现有背景/内容呈现ready条件仍保留，不将样式ACK等同背景Present成功。

现有复用点为 Host.cpp:873/904 的合并准入、:1766 的样式应用、:1822 的 owner 应用确认。具体字段命名由实施者沿现有结构确定，合同与先后顺序不得简化为轮询后无条件放行。
## 5. 持久化与启动边界

现有 SetMemory 仅接受 Desktop 同锁快照，当前生产启动停用 memory 恢复。新增全局宽度意味着该保存门不能原样充当“全局状态”的模型：需要由权威样式构造保存快照，避免白板颜色覆盖 Primary，同时不能让非桌面调整的全局宽度退回旧桌面值。

本轮尚未要求启用跨进程启动记忆。设计先保持当前生产启动策略，不顺带启用旧 memory/default 恢复；三范围颜色在会话内记忆，默认值只用于首次初始化。现有 memory 编码没有 Laser 颜色和白板浅深颜色，本轮不为会话共享添加这些schema；仅在组合现有已编码字段时使用权威值。若未来要求跨进程恢复，再明确 schema 与迁移，不能将现有文件写入能力当作启动恢复已启用。

## 6. 拟议最小修复和验收

- 形状颜色 setter/getter 路由到 Brush；任一形状改色，软/硬笔及其他形状同步。
- 形状粗细 setter/getter、Selection、解析及 UI 高亮统一读写 Brush；子形状切换只变形状身份。
- 检查三范围颜色默认/手动记忆、全局宽度与工作区工具恢复、memory 保存快照、主栏 committed 外观与 Draw3 首笔样式；调整旧形状/工作区属性隔离测试及 spec。
- 保留荧光/激光独立、橡皮偏好、Preset/ExplicitDip、DPI、状态版本校验及每笔样式锁存。
- 验收同一工作区的双向共享、实线/虚线/矩形互切、由荧光/激光/橡皮返回、跨工作区往返不回滚全局宽度、浅深主题切换与翻页/失败、手动分数宽度和换屏、旧排队操作被拒绝，以及旧笔迹不受影响。
- 实施后完整 InkeysRepo.sln Debug|ARM64，ARM64 原生 MSBuild，同 invocation PATH workaround；运行适用生产探针及 headless --no-window。最终构建/无窗口探针与headless均通过，结果见research/validation.md；未运行GUI。

本文件与 implement.md、implement.jsonl/check.jsonl 一起供用户最终审阅。产品范围已收敛；2026-11-02用户已批准最新方案，任务已进入in_progress。
