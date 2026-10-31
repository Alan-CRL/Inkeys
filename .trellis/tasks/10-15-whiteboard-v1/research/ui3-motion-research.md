# Research: 白板菜单统一 UI3 属性展开与按钮能力

- Query: 找到主栏绘制/更多/橡皮属性可复用的展开、继承、分割线和按钮能力；调查当前白板独立几何造成不一致或卡顿的证据。
- Scope: internal
- Date: 2026-10-15

## Findings

### Files found

- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeMotion.cppm`: 已生产使用的父子面板 Back 几何、Sine alpha、timeline join、姿态组合。
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp`: 实际父子 motion 配置、推进和组装实例。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`: 绘制属性 batch/inheritance 与 More 面板布局、分割线生产实现。
- `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp[m]`: 分页共用的 Scene 按钮呈现/交互；当前 springLayout 为白板加入的逐控件几何分支。
- `Inkeys/Inkeys/UI/Bar/Bar.Metrics.cppm`: 共享 gap/尺寸/标准文字槽和按钮反馈常量。
- `Inkeys/Inkeys/UI/PageControl/PageControl.cpp[m]`: 当前菜单布局、独立窗口 bounds 时钟、workspace/内容配置入口。
- `Inkeys/Inkeys/UI/Whiteboard/Whiteboard.Theme.h`: 当前自绘预览、纹理相位与手画白勾。
- `Inkeys/src/UI/color/colorSelect.svg`: 用户指定的现有绿色圆形、白色勾与白边 SVG。

### Reusable production patterns

1. `Bar.EraserAttributeMotion.cppm:37` 的 `EraserSurfaceMotion` 已具备：`Retarget(open,duration,parentTimeline)` 只在 open 改变时启动；父时间线前半程可 join，使用剩余时长与 phase；展开 EaseOutBack、收起 EaseInBack，alpha 为 EaseOutSine/EaseInSine；`Advance(context)` 统一消费 dt、speedRate、animationEnabled，零 dt 不推进；`Geometry/Opacity/Visible/Active/Timeline` 可被 Scene 统一消费。
2. 同文件 `:18` `ComposeEraserPose`、`:20` `ResolveEraserSurfacePose` 将完整 panel 和 trigger rect 映射为从按钮中心紧凑态长出的统一 scale+translation，而非让每个子按钮独立变形。`:32` `FitEraserSurfacePose` 用于工作区边界。
3. 生产调用在 `Bar.EraserAttribute.cpp:231-239`：先 retarget 父与子（子传父 `Timeline`），再在本帧推进二者。`:270-277`：根姿态由 root panel+trigger 计算；子姿态由 child panel+child trigger 计算，再 `ComposeEraserPose(parent,child)`；子 alpha 是父 alpha*子 alpha；可见性也继承。
4. 主绘制属性 `Bar.RenderLoop.cpp:3259-3294` 只在可见性/换边 batch 改变时重启，父 batch 前50% join；曲线与橡皮相同。真实继承位于 `:6316-6326`：MainBar center 继承 MainButton，Draw button CenterFromTopLeft 继承 MainBar，panel center 继承 Draw button，内容 TopLeft 继承 panel。
5. More 分割线 `Bar.Initialization.cpp:368-373` 为 SurfaceFrame 纯色 Shape，半径0.5；`Bar.RenderLoop.cpp:5388-5404` 宽=panel宽-两侧padding、高=1*scale、alpha=0.30*panel opacity。不是边框的0.18。
6. `Bar.Metrics.cppm:6-32` 是尺寸/反馈单一来源：gap5、one32.5、two70、radius4、图标28、2x2标签13/y20/height25、pressScale0.95、pressed alpha0.10、hover alpha0.18。`:166-175` StandardTwoTwo 使用 primaryText 槽，secondary字号0。标签应复用该布局，不手算字体字重。
7. `Bar.Scene.cpp:289-410` 的 Start/Stop/Update hover、RetargetBarButtonInteractionVisual 是完整共享反馈。`:454-509` DrawBarButtonVisual 统一 scale 变换后绘制 Shape/Icon/Word。主题预览内容仍应进入这条按钮路径，不能用自绘替代其标签、悬停或按压逻辑。
8. `Bar.Initialization.cpp:420-424` 画笔色板 colorSelect 固定15x15。因此白板要求是30x30，直接保留 SVG 原色。当前 `Whiteboard.Theme.h:46-51` 是手画白勾，不符要求。
9. 现有 `BarButtonSizeEnum` 仅twoTwo/twoOne/oneTwo/oneOne，无fourOne。用户明确要求宽140、高32.5；不能按四格推成145。可在共享 Metrics 增加 FourOne visual metric，复用 TwoOne 的18 DIP图标和12 DIP标签规范，再扩展文字槽到140宽；无需污染主栏配置保存的按钮尺寸枚举。

### Existing defects and evidence limits

- `PageControl.cpp:760-805` 用 springLayout 让菜单、九个预览、两个背景分别 x/y/w/h 变形，隐藏态人为压成零宽或零高。`Bar.Scene.cpp:1527-1537` 所有方向均用 EaseOutBack，收起也没有 EaseInBack。它只是曲线接近，不是父子属性继承。
- 同时 `PageControl.cpp:800-801` 把分页主体与所有子项按 `WhiteboardMenuHeight` 预先整体偏移；`PageControl.cppm:675-679` 固定272；`:1021-1022` 根据菜单开关跳到305/580宽目标窗口，造成窗口另走一条变大轨迹。当前菜单宽305而不是140，中心也没有跟More的265 DIP中心对齐。
- `PageControl.cpp:879-903` bounds 动画按固定墙钟0.4s、自写 cubic；`Bar.Scene.cpp:1063-1076` 内容动画用 dt（夹到0.05）、全局 speedRate/animationEnabled。这两套时钟在 speed!=1 或动画禁用时必然不同步，影响窗口包络与内容节奏。应让弹出包络来自同帧共享 Scene 几何，保留已有PPT/白板 workspace位置转换的必要边界，避免把业务层 bounds timer 当菜单弹出动画。
- `PageControl.cpp:995-1011` 任意 WB 内容revision都走全量 TransitionLayout，并刷新layout deadline；`1059-1067` 该deadline锁输入。主题颜色变更/状态回执不应作为弹出重新布局事件。至少需区分拓扑/锚点/模式变更与仅主题/内容变化。
- `Whiteboard.Theme.h:12-17` 每个preview每帧创建一个Brush和一个RoundedGeometry；九个preview会创建九组，另两个panel回调 `PageControl.cpp:785-789` 每帧各创建Brush。这是潜在开销，尚未profile，不能宣称已经证明某fps问题。使用现有renderer/SVG cache和共享panel Shape可去掉额外业务层对象生成。
- `PageControl.cpp:1178` 生产只有一次scene.Render，没找到生产scene.Advance调用，不存在可证实的“双推进”现象。`Bar.Animation.cppm:798-807` 对同target早退，所以也不能说SetTar每帧重启。`RetargetBounds` :913 与ConfigureSurface :1061重复Advance同墙钟是幂等的绝对采样，不是累加dt。
- `PresentScene` 在Render内推进内容，但presentation rect在 `PageControl.cpp:1951` 之前已读取；当前按固定窗口目标时不保证基于实际弹出包络。若统一Scene envelope，应先统一Advance(frameTime)，再读取layout/envelope，然后Render同frameTime；需保持同帧推进幂等，或Render不再二次推进。

### Smallest correct integration path

- 在共享 Scene 内描述两级弹出拓扑（panel->trigger widget、child widget->panel），业务层只提供完整目标rect、开关、内容。复用现有 EraserSurfaceMotion/pose 与共享 BarAnimation 参数，不在PageControl新增曲线/时钟。
- 父菜单相对More中心x=265 DIP摆放，操作140x32.5、padding5、gap5；完整菜单宽150。Theme子面板在菜单右侧5 DIP，展开源为Theme行本身。关闭时保持父子姿态关系和低于零Back边界的安全绘制。
- 子控件几何、绘制、hit、damage及HWND envelope从同一个当前pose推导。透明5 DIP间隙不能作为交互面板；panel实色区需阻止画布落笔。
- 背景颜色和样式组用More divider样式，所有布局间隔取BarButtonGapDip。More现存anchorGap12与separatorGap10是它自己的布局常量（`Bar.Main.cpp:190-196`），不应复制到用户明确要求5的白板。
- 标准2x2按钮显示样式名称在primary槽；纹理预览在button drawContent层，由按钮统一压缩反馈。格点以预览中心cx/cy为phase，24 DIP间距，左右/上下对称；目前left+12/top+12在70宽中并不中心对称。
- 颜色选择用30 DIP原SVG，避免手画或重着色为单色勾；该资源可以使用缓存的BarUiSVGClass，设备资源清理沿已有Scene释放合同。

### Observable regression checks

- 同一frameTime调用Advance+Render不得推进两次；零dt反向target应保持当前可见pose。
- .5x/1x/2x速度和动画禁用下，panel、子内容、envelope同帧一致；禁用立即落定。
- 反复提交相同目标、只改变主题色/选中项时，不重启popup Timeline、不延长输入锁、不退回紧凑态。
- 先菜单后主题、父仍在前50%时子加入、关闭父时子退场、主题关闭中反向展开，都从当前pose连续。
- 半程pose应按共享Eraser生产motion值比较，而非只验证最终rect。
- 更改颜色后按钮仍可继续点击，idle时无布局deadline/active wake残留。
- 不把以上确定性行为测试或离屏截图报告为真实GUI fps验收。

### Related specs / references

- `.trellis/spec/native-desktop/rendering-and-ui.md`
- `.trellis/spec/native-desktop/whiteboard.md`
- `.trellis/tasks/10-15-whiteboard-v1/prd.md` UI3统一修订（最新实施授权）
- No external references were needed; findings are repository implementation evidence.

## Caveats / Not Found

- 行号基于本研究开始时工作区，其他worker并行修改后可能漂移。
- 研究不改产品代码，不运行构建或GUI；卡顿相关只确认代码路径不一致及潜在成本，没有采集真实帧时间。
- 当前Erasure与Recall各有相似motion类，用户禁止额外自写部分；可直接使用已存在实现或小范围抽为共享helper，但不建议为本需求重构整个主栏状态机。
