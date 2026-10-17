# P2 实际代码调查与设计决策

## 已有接入点
- P1共享核心位于 `inkStrokeModelerTest/draw3/shape_core.*`，产品与共享测试引用相同实现；通用 renderer 与 `inkeysShape` v1 已存在。新增模板展开成已有低级几何，避免第二套几何算法。
- `IdtState` 负责业务状态与版本检查，`Draw3.Bridge` / Host / WindowControl 发布完整值快照；现有 Draw3 contact、InkStroke 与历史容器继续负责预览和最终文档顺序。
- `ResolveShapeLiveEndpoint` 已有 modeler 预测 / 模型 / raw 端点回退链；P2复用选点，再调用同一预设构造器。raw Up最终化与活动预测分离。
- 原 geometry panel 在 Bar.RenderLoop/Interaction 中硬编码。新 GeometryAttribute 子树沿 EraserAttribute 的 BarButtonClass 生命周期，保留公共布局、呈现、命中、悬停和按压动画。
- `RecallChromeIdle` 的 WM_TIMER 到期唤醒模式可供1.8秒轮播复用，不需要持续帧刷新。

## 跨层合同与特殊处理
- 稳定对象ID在Down生成；ToolSpec/ShapeStyle/SystemScale在Down冻结。现有contact不受后续工具变化影响；shape family 内新Down获取新工具，普通笔/橡皮批次锁定不变。
- 普通矩形既有4 DIP圆角属于兼容语义。Bridge保留DIP，在Down转换一次，Construction保存局部单位，不能在factory内再次缩放。
- 箭头是复合逻辑对象。头部实线通过primitive flag16覆盖杆的线型，同材质在一组覆盖中求并，防止半透明连接处加深。
- 仅填充隐藏线型/粗细，但不丢弃记忆值。圆两模式共享闭合图形样式；正多边形轮播只修改图标，不修改业务边数。
- 复用精调/调色子视图时不能只把其他按钮透明度设为0：旧presented-hit机制允许透明控件命中。必须对布局、绘制、hover和命中共同使用显式availability gate；退出后恢复普通画笔控件。
- Shape借用宽度控件一律采用Brush量程/语义，避免记忆的Highlighter或Laser模式污染宽度预设与预览。预设通过已有异步保存入口，UI线程不直接落盘。
- 借用视图保存当前workspace/page/presentation身份、白板generation和tool；切页或工具变化使旧操作失效。关闭返回面板前再次检查上下文。
- 新元数据使用v2并严格验证Construction与派生几何一致，P1无构造对象继续v1。UInk仍为10，复合标准部件可以在通用读取器中显示。

## 本轮边界
- 11类均为单笔拖动；逐点折线和任意多边形移至P3提示窗口完成后。
- 不创建SelectionOverlay、多阶段提示或浮动编辑栏，不新增启动配置，不commit/push。
- 验证仅CPU、命令行及无HWND离屏资源；真机系统兼容和实体输入手感单独记录待人工验收。
