# Whiteboard 初版合同

本合同来源于 2026-10-15 用户批准的 Whiteboard 初版计划，覆盖白板、共享分页、Draw3 页级存储及主栏场景生命周期；实现与验证记录见 `.trellis/tasks/10-15-whiteboard-v1/`。此前 08-17 白板任务中的无保存、统一墨绿背景、退出强制折叠和左右同宽约束由本合同替代。

## UI 与线程所有权

- PageControl 是四个分页 HWND 的唯一 owner；白板继续复用 PptBottomLeft/Right。白板的标准列表设计目标为左栏 305×80 DIP、右栏 230×80 DIP；PPT/白板的可见项及专用 Drag 槽通过同源列表布局计算背景宽度和按钮目标，主体侧边沿控件尺度留 5 DIP，底部按真实 DPI 保证可见描边外沿距屏幕底边 5 屏幕 DIP，透明光影包络不参与底距。分页、更多菜单与主题面板共用主栏三态缩放基准（关闭/DPI、48DIP基础量、任务栏真实量）×`UI.Bar.Zoom2`，不继承PPT局部倍率或保存位置；画布背景纹理仍按真实DPI绘制。
- 菜单及主题面板属于左下同一 HWND，但分别绘制独立矩形：菜单在分页栏上方回弹展开，主题在菜单右侧回弹展开，节奏复用主栏属性动画。菜单行使用左图标和左对齐文字。包络、命中、damage 与呈现基于同帧动画几何；左/底锚点不移动，不新建 popup HWND。分页列表与固定属性直接调用主栏公共按钮函数，BarPageSurface 仅负责数据和目标适配。
- More、Escape、面板外点击、翻页、最小化、workspace 转换收起菜单并取消 capture。白板返回放映时保留 More 实例与列表成员，使用共享 UI3 隐藏布局收拢到 Next 下层并渐隐，不得以空目标矩形或移除成员代替隐藏；按钮退场与列表背景宽度在同一布局批次连续变化，保留按钮和整条栏保持正常透明度；退场时不可命中，中途重入时宽度、位置与按钮透明度均从当前呈现值反向；首次隐藏配置从首帧保持透明。透明边界穿透，UI 操作不落笔。背景和预览共用同一组纹理视觉常量（24 DIP 间距、1 DIP 点半径/线宽、18% 黑/白、浅深判据），但不再是同一个绘制调用：预览仍用 `DrawThemePattern`（以预览矩形中心为相位），画布背景默认由 Freeze 用带视口相位的 `DrawThemePatternPhase` 绘制，见文末“白板点阵/网格背景的归属与视口跟随”。
- 按钮在白板中显示关闭白板，有有效放映时显示返回放映；英文返回标签使用短文案 `Resume`，保持标准按钮字号与文字槽。白板窗口组最小化后，主栏按钮显示白板图标和“返回白板”（英文 `Resume`），点击恢复同一白板，不切换 workspace 或重新进入。最小化/恢复边沿须立即刷新按钮内容，浮动和折叠主栏同样适用。菜单关闭使用同一退出事务。后台 PPT 结束不得结束白板。
- 主题按页保存，同进程新页继承当前页；新进程首张白板页读取最后一次成功修改的颜色/纹理偏好，缺省或非法配置回退墨绿 #123B32/空白。冷加载与切换已有页不回写偏好，重复或身份失效的主题请求不触发持久化；绘制 owner 只投递既有配置 FIFO，正常退出先停止生产者再排空写入。色板按三列两排：#123B32/#172A46/#000000，#FFFFFF/#E5E7EB/#F5F1E6；空白/点阵/网格，24 DIP 间距、1 DIP 点半径/线宽（视觉常量，按真实 DPI 缩放；画布纹理的绘制归属与随画布视口的相位见文末新节）。2026-11-02批准的共享样式合同替代“主题不改变画笔”：主题按实际RGB浅/深类别选择Brush的独立颜色记忆（浅首次红色、深首次白色），同类别/仅纹理变化不重置用户颜色；荧光/激光颜色各自全局共享、不随背景切色。主题不改变已有笔迹、不进入墨迹undo，修改使页dirty。
- UI 只发布带身份请求并消费快照，Draw3 绘制线程独占页内容/历史；I/O worker 不访问 UI、HWND 或可变 runtime。
- 白板首个 Next/Previous 页面命令在 Host 立即设置 `commandScenePending` 并关闭新 Down，沿用 PPT admission revision 的 contact 封口：旧页已接受笔按最后位置完成，旧 Move/Up 隔离到真实终态，不得跨页续写。协调器只保留一个页面事务，首请求不得再等待 `whiteboardLoadPending`，但在真实 ready 前不得发布第二事务；不扩展多冷页 latest-wins。
- 暖页重放和冷页严格加载后，只有真实 Present 回执才清除 pending 并重开输入。第一页 Previous、追加/加载失败或无效页命令留在原页时，必须补原页真实 ready，不能永久关闸；持续加载、呈现失败或身份不明不得伪造 ready。旧页保存、失败暖页和冷页完整性门禁不变。
- 首次尚未建立白板工具记忆时默认 SoftPen 绘制；Desktop/Presentation 的新工具记忆仍默认 Selection。已有工作区工具身份记忆及用户手动切换的拖动状态保持不变。样式按2026-11-02共享合同单独投影：三组宽度各自全局、Brush三范围颜色、荧光/激光全局颜色，不随旧工作区状态恢复旧值。

## 存储、历史与内存

- 每进程一个逻辑多页 UInk 白板，独立 AutoSave/whiteboard 命名空间；忽略桌面保存开关。同进程关闭重开保留，新进程新建，旧文件保留供未来历史浏览，不自动恢复。
- 保存只在离页/离开白板/正常退出安全点触发。主题、新空页、Undo/Redo/Clear 都是 mutation；无变化不写盘，无定时保存。退出扫描 active 和 parked dirty 白板。
- 流式扫描目录、按页 decode、流式复制未修改页面并原子提交新版本；读取、自校验、hash 及保存均不得全量物化多页墨迹。文件版本与页偏移必须绑定；先 durable UInk 后原子索引。索引发布失败不替换 current/backup，本次未提交 UInk 仅凭强 revision 清理，临时索引仅凭创建句柄清理；清理失败仍保留并报告失败，不能驱逐 warm 页。
- 版本化 extra 保存页主题及卸载前可用的完整 CPU 撤销/重做信息，标准 content 保持可见画面语义；冷恢复栅格 token 必须沿可见链和 Redo 弹出链重建，并与真实 Controller 的重做前置校验一致，不能用 item 序号猜测当前状态。GPU 纹理/缓存不得序列化。不扩大既有跨 Clear 规则。
- 非当前页仅在保存成功且 identity/generation/revision 仍匹配时卸载笔迹、历史及CPU/GPU缓存。正常稳态只保留当前页、轻量目录和有限在途数据；同页待保存快照合并。
- 保存失败可继续翻页，失败页 warm 并显示部分页面未保存；后续安全点重试。stale completion 不得释放新修改。冷加载锁定绘制/破坏命令，成功成对切主题/内容/页码；失败保留原页而非创建空白覆盖。

## 场景与底栏

- 首次 Desktop→PPT/Whiteboard 记录桌面主栏快照。只调整已有底栏贴屏底 5 DIP，保持是否居中；浮动、折叠不强制展开。PPT↔Whiteboard 不重拍基线，不重复折叠/复位；有效放映且启用底栏时，共用底部分页按活动会话事实跨越分步可见性发布，不经过 Hidden 或整栏渐隐/重现。真正结束、配置关闭及白板最小化仍按原显隐合同。最终回 Desktop 恢复基线。同屏、PPT 底栏默认偏移为零且局部倍率为 1 时，分页主体和箭头的底部 Y 在进入白板时保持不变；Drag 槽收起和 More 增宽仍沿原内容形变。
- 白板主屏，PPT 放映屏；同屏连续，跨屏适配显示快照/DPI，原显示器消失按既有回退。白板用户拖动吸附也显示标准底栏/居中底栏提示，沿同一手势资格和动画；程序进入、最小化恢复本身不建立提示资格。
- 最小化是窗口组操作，不是 workspace 退出；两侧分页隐藏，主栏保持显示并回到任务栏居中底栏，恢复不重放入口；PPT 场景禁止最小化白板，菜单最小化行的图标与文字复用 UI3 主题禁用透明度（Dark `0.30`、Light `0.45`）禁用视觉，并关闭悬停/按压/点击；禁用依据是有效放映会话事实，不能由被白板遮挡的分页可见性推导，后台放映结束时须唤醒并恢复可用状态。任务栏锚点使用 exe 图标和国际化 Inkeys 白板标题。任务栏关闭或 Alt+F4 必须转为白板退出请求，沿既有保存/离场事务完成；不能直接销毁共享 Freeze 锚点及其 owned 窗口链。保持 NOTOPMOST、单一任务栏锚点及退出穿透/Z序合同。Shell 全屏通知覆盖 Freeze 与 Drawpad 两个可激活面；激活仅异步重申，最小化、退出和销毁前撤销，恢复重新发布。
- 进入白板必须等待真实 Draw3 首帧、输出/内容版本、背景提交及必要窗口事务；先发布 Active，再异步尝试一次前台激活。Freeze 的背景提交由 Window owner 读回目标 RECT、真实 visible 且非 iconic，并发布固定回执/快照；UI 和协调器不得自行调用 HWND 查询来裁决 ready；最小化期间被抑制的 SetBounds/Show 返回未提交并保留重试，不得以“未执行但成功”推进 `committedBackgroundActive`。恢复边沿重新唤醒背景提交，不需要再次点击入口。焦点分配失败仍完成后台进入，不按周期抢焦点；同 epoch 的 Entering→Active 不取消已接纳请求，退出、新 epoch、最小化、隐藏或停机必须拒绝。设置的实际 Present 不持 ImGui 输入锁，避免与窗口焦点消息互等。
- 同一次进入 epoch 的原生窗口事务失败重试只执行一次主栏收起和 capture 取消；失败后的 250ms 退避先于 Entering 发布，新意图和取消不受旧期限阻挡。后台排序保留 Bar 位置、仅下调 PPT 并核验内部 owner/band/代际/顺序，焦点许可不参与准入；纯层级失败恢复不得 Hide/Show 或重建画布策略。
- 画布缩放、旋转仍禁用；页面管理、历史 UI、手动导出、跨进程恢复不在初版内。2026-11-06 起白板"移动（Selection）"模式支持画布平移与每页视角，见文末"移动模式的画布平移与每页视角"和 [runtime-and-rendering.md](../native/runtime-and-rendering.md#scenario-free-canvas-navigation-and-predictive-recovery)；此前"平移缩放仍禁用"中的平移部分由其替代。

## 验证要求

- 生产分页适配器和主栏公共按钮的离屏像素/命中/包络测试，覆盖六色三纹理、动画反向/DPI/设备重建/透明区域；不能仅以公式测试代替。
- 真实存储与Controller测试覆盖多页 roundtrip、Undo/Redo/Clear 分支、主题空页、foreign session、写/索引/读失败、快速重入与stale completion。
- 100 非空页保存遍历后非当前页内容/历史/CPU/GPU释放；统计流式buffer峰值，不用失败warm页假冒正常回收通过。
- 完整 InkeysRepo.sln Debug|ARM64 与相关无窗口测试。GUI验收必须另获明确授权；编译/headless不能代替真实PPT/任务栏/视觉验收。

## 截图反馈回归约束

- 非活动白板入口图标为标准 2×2；分页文字在模式转换后仍必须呈现。
- 色板和纹理预览均为 70×70 DIP 圆角方形，选中只显示中央勾，复用主栏按钮悬停/按压反馈，不添加外围选中高亮。
- 居中白板主栏换向使用屏幕边缘，不把分页控件当作可用屏幕边界。

- StandardTwoTwo 的标签必须写入标准主文字槽（BarButtonClass::name）。页码的第二行使用明确的字号和槽位，不能把分页按钮标签写入字号为零的次级槽；回归需检查实际公共按钮的文字像素。

## UI3 统一菜单与主题合同（覆盖截图轮旧尺寸）
- 三项菜单操作使用共享按钮交互，140×32.5 DIP（4×1），左图标/文字，无子菜单箭头。菜单中心锚定 More 按钮中心；面板、行、预览间距均为5 DIP共享常量。
- 主题面板的背景颜色、背景样式标题和分割线沿用主栏 More 的视觉能力；预览是70×70 DIP标准2×2按钮，不能只套一层自绘hover/press。空白/点阵/网格文字在按钮内使用主栏相同标签槽和字重。
- 选中叠加已有 color/colorSelect SVG，30 DIP（主栏画笔色板15 DIP的两倍），不得绘制替代勾或增加外圈选中效果。点阵和网格相位相对于预览内容中心对称，保持24 DIP标尺。
- 两个固定面板的触发分别为 More 和 Theme；面板及内容由共享属性面板函数计算当前 pose，分页业务层不另行 easing 或递归节点编排。PageControl 屏幕 bounds 过渡不得给菜单再叠加第二套时钟。
- 纯文案/主题/编辑状态更新不应重新启动面板展开、重置输入锁或让静态目标持续绘制。动画遵循全局速度和禁用设置。

### 共享计算、输入与渲染防回归
- Main/MainMore/PPT/白板分页分别拥有稳定的 BarButtonListClass；主栏每批消费稳定 Snapshot，分页仅在拓扑/尺寸目标变化时重算并缓存列表布局与隐藏优先顺序，正常热帧不逐帧复制列表；主栏、分组网格和分页单行使用同一 ResolveBarButtonListLayout。固定属性/More/主题继续保留明确字段或数组，不建通用节点图或动态注册表。
- 标准按钮共用 ConfigureBarButtonVisualTargets、AdvanceBarButtonVisual、PrepareBarButtonVisualInheritance、DrawBarButtonVisual 和公共命中。Draw/Geometry 的固定 shape/SVG/Word 通过非拥有引用视图使用相同组件函数。特殊页码/主题只替换内容，不能重写按压、悬停、字体槽或另一份动画推进。
- 固定面板共用 BarAttributePanelMotion、ResolveBarAttributePanelFrame；不保留 popupClock/parentPanel/BuildWidgets。每批消费同一 frameTime，同目标同时间重复 Advance/Render 不再次推进。内容更新不重开运动或延长输入锁；同布局的页码和主题只更新对应数据及脏区，保留现有几何动画，不能以零时长配置把转换提前跳到终态。
- 每个目标独立持有 renderer/context/bitmap/cache，显式消费字体、倍率、光照和设备代次；不得构造完整 BarUISetClass 充当分页 renderer owner。分组透明度采用共享有界 PushPanelOpacity，不能对子按钮逐个创建覆盖整幅目标的透明层。
- BeginFrameDraw 必须在 Clear 和实际 draw 前设置脏区裁剪；FinishFrameDrawCommands 在 GetDC 前清空 clip/layer，GetDC/ReleaseDC/EndFrameDraw 与主栏同一事务。高水位只保留容量，实际提交、命中、dirty 和光源映射使用同帧当前包络。
- 预览底色、边框、圆角和反馈复用 renderer 的 Shape/按钮能力，纹理仅绘制内容；不再维护 ThemeDrawingResources 及自有圆角裁剪缓存。一个属性每帧只有一个最终目标，禁止标准图标尺寸或 opacity 先写默认值再覆盖造成永久重定向。
- 成功完成绘制/GDI/提交/必要窗口交接后才发布命中与几何、消费 dirty；失败保留旧已呈现快照和待绘制状态。PPT 成对拖动/布局 revision/pagePresented 门禁保持。
- 一个主栏交互 worker 公平处理全部目标；手势等待采用非阻塞续接，WndProc 仅标准化/入队和即时原生捕获。Move 可合并但不能跨 Down/Up/Cancel；fixture 来源保留精确队列出处。输入发布反馈、render 独占视觉对象；长按计时不在 render 执行业务。对话框和保存采用异步结果或原服务队列。退出显式唤醒邮箱；生产 Hook 的100ms租约收包不请求渲染，离屏 worker 静置只等待邮箱或手势期限。
- 无窗口 --ui3-multitarget-benchmark 运行实际主栏协调器及分页提交，保留 D2D/GetDC/ReleaseDC/EndDraw，仅替换最终 HWND sink；120 帧预热、600 活动帧、三轮、1/1.5/2 倍率，保存 raw callback/batch、时钟单位和丢样本计数。热态批次 P95≤16.67ms/P99≤33.4ms 为目标，另查成功主栏提交间隔及收起后静置。结果必须引用任务 validation，不能把此规范或公式测试当作性能通过。
- 默认不启动 GUI。无窗口像素/吞吐不证明真实 ULW/DWM、任务栏、跨屏或 Office 动画；这些仍须明确列人工未验证项。

### 分页菜单叠放与缩略预览
- 同一目标内从后向前绘制：主题面板、更多菜单、分页栏；重叠命中遵守相同可见层级，面板背景遮挡不能点击到其后的按钮。位置和共享展开动画不变。
- 主题样式纹理覆盖完整70×70按钮，文字绘制在纹理上方，不能按标签槽缩短纹理区域。点阵为中心对称3×3九点，网格为三横三竖九交点；与画布共用纹理视觉常量（预览本身仍是 `DrawThemePattern` 的 3×3 中心对称，不随视口；画布走带相位路径），画布24DIP密度保持不变。
- 色板显示、选中和点击请求使用同一顺序映射，第三项黑色为#000000。

## 三工作区统一交接（2026-10-27）

本轮已批准合同；实现和执行证据见任务 whiteboard-first-open-visibility 的最终报告，不能由本规范推定实机通过。

- Desktop/Presentation/Whiteboard是三个稳定业务态。最新Desired、已完成Committed和仍有实际责任的Pending区分；最小化/焦点不是第四工作区。统一协调快照是业务权威，主栏稳定工作区、工具和Desktop基线按Committed投影，背景/分页可按Pending准备但不能据旧ACK开放业务。
- 接纳切换先真实关闭Host新Down，工具/场景准备和ControlWake、Frame/UI ACK、终态排空仍继续。实际目标工具/选择、匹配输出、必要背景与原生Prepare/Complete结果就绪后统一提交；foreground、Shell COM成功和全局TOP/连续性不进入完成门。
- 业务epoch复用现有奇偶模型，白板显示/最小化语义变化也退休旧ACK；普通PPT页/工具/输出变化不推进工作区epoch。业务版本不替代session/target、slot、tool、device/output/content或HWND代际。
- 白板背景复用Freeze并保持完整主屏rcMonitor，尺寸与DPI读同一Display快照；Desktop/PPT双画布底部1物理像素及旧定格/放大镜规则独立不变。默认归属下该 Freeze 背景除纯色外还绘制点阵/网格并随画布视口重画相位，相位帧是仅场景脏区的更新、不是内容失效，也不改变 Prepare/Complete 语义（见文末新节）。BackgroundPrepared仅证明当前请求、内容失效版本、设备与 HWND 代次的真实像素及窗口回执，实际Active仍需 owner 发布的真实visible/bounds/非iconic；inactive必须真实Hide后确认，且优先于minimized暂停。
- 白板最小化仍保留页/历史/工具/基线；系统确认只确认当前视图，不另发意图让在途请求自失效。最小化后可直接关闭。后台PPT变化只更新最新Primary事实，不能抢走白板。
- 普通失败停止无限重复显示/隐藏，保留owner真实责任；只有Rejected证明owner恢复已退休后才恢复Whiteboard源背景/分页，Pending/failed和输入fence继续保留，不能用源预览伪装切换成功。背景ACK、Draw3/PPT ready及背景prepared/final active的无进展等待进入同一可重试失败预算，实际输出/内容/背景进展才重置等待。
- DComp退休采用共享有界等待，超时/忙碌禁止第二worker、ULW和新HWND；能清除样式才走原ULW，否则运行期进入受保护故障退出；完整legacy窗口链重建仅限启动期UI客户端初始化前。旧generation的帧/completion不得确认新恢复，受保护/fatal只可在完整token及真实安全清理后退场，不等待已停消费者普通帧、不清fatal或伪造ready。
- Settings/安全UI与画布恢复的阻挡按实际写集区分；新场景Show/关穿透不得越过旧恢复，旧补偿不能作用于新HWND代际。保存/加载和失败warm数据规则不变。

背景渲染遵守 [UI3 原生窗口候选与真实回执](rendering-and-ui.md#ui3-原生窗口候选与真实回执2026-10-28)：成功像素等待 native ACK 时不重复整屏绘制；Invalidate 同时退休旧内容候选，同请求版本不能绕过主题/DPI/设备失效。完整 mask、frameSerial、workspaceRevision、HWND/generation 和实际矩形全部匹配后才 Prepare/ConsumeDamage；inactive 的 Hide 也必须取得精确真实回执。

背景完成/inactive ACK 通过 NotifyProductWorkspaceProgress 唤醒原协调等待；Freeze ULW 与政策几何/回滚共用 try-only 呈现租约及 pixel revision。Prepare/Complete 为显式异步回执；新通知不作为 ready，也不加速原失败预算。参见 rendering-and-ui 的直移与工作区异步协调合同。

## 场景：冷页读取失败的原页补呈现（2026-10-31）

### 1. Scope / Trigger

修改白板冷页失败、退出/重入或补 ready 命令时应用。防止失败完成后追加的通用 SetPage 在已切换工作区中切页或创建空页。

### 2. Signatures

- 内部 `CanvasCommandType::RePresentWhiteboardPage` 复用 `workspace/whiteboardGeneration/pageIndex/requireSceneMatch/sceneClosedAdmission`，只授权原白板页补呈现，不授权切页。
- `CompleteFailedWhiteboardPageLoad` 结算原 Next/Previous 后排队；`ApplyWhiteboardPageRecoveryCommand` 在消费时复核身份并调用既有 `acknowledgeUnchangedScene`。

### 3. Contracts

- 失败先清空本次 loading，保留原页；原翻页命令只结算一次，补呈现不得重复结算。
- 消费要求最终退出屏障尚未 `ExitPrepared`、仍为 Whiteboard、非零相同 generation、相同原页且无新 loading，并要求场景/准入标志齐备；退出重入即使回到同页，也因代次不同拒绝旧命令。
- 通过后只走既有 stageWorkspaceReady + forceFullPresent，等待真实 Present；不得经过 SetPage、创建页面或确认其他工作区输入。
- 不直接复用成功冷加载的 whiteboardLoading 校验：失败已 reset，该校验会错误阻断正常原页恢复。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 同场同页失败 | 原请求恰好一次终态，排队原页真实补呈现 |
| Desktop/Presentation 或退出后重入 | 丢弃旧补呈现，不切页/增页，不确认新场景 ready |
| 原页已变或新 loading | 拒绝旧恢复，不解除新页面输入门 |
| 最终退出保存已 ACK / ExitPrepared | 拒绝任何补呈现，不重开 ready，不重复结算原翻页 |
| FIFO 中退出先于补呈现 | 根据消费时场景拒绝，不依赖生成时检查 |

### 5. Good / Base / Bad Cases

- Good：冷页失败仍留原页，原页真正呈现后恢复输入。
- Base：成功冷页继续由原 SetPage + loading/generation 合同切入目标页。
- Bad：将无场景约束的 SetPage 追加到退出命令之后，误用白板页码修改桌面。

### 6. Tests Required

`--draw3-whiteboard-storage-test` 使用实际 WindowController FIFO 和生产生成/消费 helper，无 HWND，覆盖同场补呈现、恰好一次终态、Desktop/PPT、退出重入、换页、新 loading、退出排在恢复前，以及实际 ProcessPresentationExitBarrier 最终 ACK 后拒绝恢复。完整 Debug|ARM64 构建及无窗口测试仍须通过；该测试不代替真实画布 Present 的 GUI 验收。

### 7. Wrong vs Correct

- Wrong：失败后 `SetPage(currentPage)`，仅凭入队时仍在白板就授权后续执行。
- Correct：发送 `RePresentWhiteboardPage` 并携带原代次/原页；消费时核对后只补真实呈现。

## 场景：跨场景共享样式与白板首笔同步（2026-11-02）

### 1. Scope / Trigger

用户批准任务 shape-style-tablet-bar；涉及工具样式、工作区恢复、白板主题/页回执或Host输入准入时适用。此合同覆盖旧的工作区宽色隔离，不改变工具身份和页级墨迹存储边界。

### 2. Signatures

- `IsLightWhiteboardBackground(uint32_t rgb)` 位于 `Draw3.WhiteboardModel.h`，299R+587G+114B >128000为浅色，等值为深色；纹理不参与。
- `Bridge::WhiteboardStyleToken { hostGeneration, generation, page, backgroundRgb, serial }`；ProductState与HostRuntimeSnapshot携带同源token。
- `SetPenColorIfRevision(color,revision,save)` 与既有条件宽度setter共同约束长手势；同锁校验版本和写入范围。

### 3. Contracts

- Brush（软/硬/实线/虚线/矩形）颜色存Primary（桌面+PPT）、浅白板、深白板三份，浅首次现有红/深首次白，用户改色后按类别恢复。荧光/激光颜色各自全局，三组宽度各自全局，组间不合并。
- 工作区恢复仅恢复工具身份并投影最新共享样式；旧StateMode宽色字段只是快照/兼容投影，不能反写权威值。Preset/ExplicitDip与当前显示Context规则不变。
- 主题和翻页分类以Controller实际当前页为准，覆盖首次进入/冷恢复；预览、请求Accepted、主题持久化回调均不能代替实际接纳。模型允许任意合法RGB，不能只按六色索引分类。
- 新主题需要重新绑定样式时，Host先登记独立样式pending并关闭新Down，再发布主题/页身份。业务据同源token选择有效色；owner真正应用ProductState后，匹配token与应用版本才可清样式原因。不得复用workspaceTransitionEpoch，也不得绕过其余加载/呈现/恢复阻挡直接开门。
- 同页主题变化可能不推进generation，必须包含背景身份和单调serial（A→B→A也不能接纳第一份A的ACK）；旧Host/页/主题确认不释放新pending。同类重复通知保持幂等，不重置用户颜色。
- Host回调不持锁执行重型业务/窗口同步；业务不持状态锁等待Host应用。活动笔划在Down锁存，旧笔迹不改色。
- memory仅编码已有字段的权威样式投影，白板Brush色不污染Primary；不新增白板/Laser颜色schema，不启用停用的启动恢复。

### 4. Validation & Error Matrix

| 条件 | 预期 |
| --- | --- |
| 浅→深→浅或跨类翻页 | 选择相应记忆，首笔与新类一致 |
| 同类页、纹理变化、重复主题 | 不重置颜色，不覆盖全局宽度 |
| 无效/stale主题、cold load失败 | 保持实际旧页/主题颜色 |
| 已生效主题保存失败 | 保留当前颜色，仍报告未保存 |
| 旧token ACK、旧revision选色/粗细提交 | 不开新门，不写入新范围 |
| 工作区往返、显示Context变化 | 工具按工作区恢复，宽色按共享合同投影，DIP规则不变 |

### 5. Good / Base / Bad Cases

Good：白板深色手选黄，回浅色仍红，再回任意深色页恢复黄；PPT保留Primary色，全局宽度不回退。Base：首次深色白板Brush白色，荧光/激光保持各自全局默认色。Bad：themeEdited里只改UI颜色、先开门后发样式、把旧工作区整份宽色作为权威恢复。

### 6. Tests Required

生产State/Bar/Host测试覆盖三范围默认和改色记忆、三组全局Selection/颜色隔离、工作区工具恢复与保存快照、旧交互revision、同页改主题token、过期ACK、连续主题/翻页、加载失败/stale、pending未应用拒新Down且应用后仅清本原因；Headless直接调用公共RGB分类覆盖六色和阈值。完整ARM64 Debug Solution及适用初始化前探针必须记录退出码；GUI/实机结果不可由静态或headless推出。

### 7. Wrong vs Correct

Wrong：收到主题点击请求就选色；只比较generation认为同页主题没变化；收到任意新ProductState便直接开放输入。
Correct：实际当前页主题生成完整token；有效样式应用确认匹配本token，再清独立样式pending，其余准入原因继续生效。

## 移动模式的画布平移与每页视角（2026-11-06）

任务依据 `.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/`（输入/视口/背景）及 `.trellis/tasks/11-08-chromium-touch-fling/`（替代动力学）；完整可执行合同（状态机、估速器、常量意图、终止原因、验证矩阵与未验证项）见 [runtime-and-rendering.md](../native/runtime-and-rendering.md#scenario-free-canvas-navigation-and-predictive-recovery)，本节只记录白板侧边界。

- 白板的"选择"按钮显示为"移动"，不新增 UI。平移只存在于"白板工作区 + 移动（Selection）模式"（`Bridge::MoveModePansCanvas`）；桌面/PPT 的 Selection 仍是穿透的电脑操作，绘制工具下不平移，绘制模式没有双指/手势触发。白板 Selection 的输出仍是 `PrimaryDrawpad`，Drawpad 不穿透。
- 输入：触摸（任意指数，按质心）与鼠标左键立即成为平移触点；笔、鼠标右键不是拖动设备，继续丢弃到终态；触摸与鼠标同一时刻只允许一类设备持有。Win32 鼠标兜底路径（RTS 不可用）同样在白板移动模式放行左键，触摸被系统提升为单点鼠标，因此只有单点平移，属已知降级。用户的多指书写开关不影响移动模式的多指平移。
- 当前产品导航采用有序回调历史、逐指可靠尾段、向量摩擦与受限残余投影；Down立即停、笔停止与页面隔离保持。完整合同见[平移规范](canvas-pan.md)。
- 手势、估速、惯性与视口全部由 Draw3 绘制线程持有；UI/Whiteboard 层不参与平移输入、手势与动力学，Freeze 不接收输入、不驱动视口。默认归属下 Freeze 仅被动读取绘制线程发布的“当前显示页视口”只读样本，用来重画点阵/网格相位（Draw3 归属后备下 Freeze 不接收视口），合同见文末“白板点阵/网格背景的归属与视口跟随”。平移不改变页 `revision`、不使页变脏、不触发保存，也不进入墨迹 Undo；UInk 格式不变。
- 离开移动模式、翻页与其它画布命令、工作区或 PPT 目标切换、白板冷页加载、白板最小化、窗口 Resize、笔落下都立即终止手势与惯性，不遗留速度；仍按着的手指只跟踪到终态，不会因此落笔或重新平移。
- 每页视角：各页保持自己的视角，新页从 `(0,0)` 起，不继承上一页；白板冷页被驱逐后重载同样恢复离页时的视角。视角只在当前进程会话内记忆（侧表 `WhiteboardViewportMemory`），不依赖也不修改已保存的 UInk 视口；"新进程新建白板"的既有规则不变。
- 本节不包含缩放、旋转、边界阻力/回弹、键盘/滚轮/触控板平移、抓手光标和历史浏览；真机手感标定、RTS 鼠标拖出窗口行为与 Win32 兜底实机均未验证，见上述合同的"未验证项"。

## 场景：白板点阵/网格背景的归属与视口跟随（2026-11-06）

### 1. Scope / Trigger

修改白板点阵/网格的绘制位置、`kWhiteboardPatternOwner`、Freeze `RenderBackground` 的重画规划、`PublishCanvasViewport`/`WhiteboardViewportSample` 视口通道、主题纹理失效规则、预览纹理，或 Draw3 后备 under pass 时应用。依据 `.trellis/tasks/11-06-whiteboard-canvas-pan-inertia/`（PRD R6/AC5/D3 修订、`research/freeze-pattern-follow-design.md`、`research/background-pattern-ownership.md`）。

D3 修订：用户原需求没有要求用 HLSL。纹理默认由 Freeze 窗口（D2D/WARP）绘制并随画布视口重画位置，不占用画布 GPU、UI 层不打扰画布；先前实现的 Draw3 shader under pass 保留为编译期开关后备，真机体验不佳时再切换。后备合同见 [cpu-gpu-contracts.md](../shaders/cpu-gpu-contracts.md#scenario-whiteboard-pattern-under-pass-draw3-后备归属)。

### 2. Signatures

- `Draw3.WhiteboardModel.h`：`WhiteboardPatternOwner{Freeze, Draw3}`、`kWhiteboardPatternOwner`（默认 `Freeze`）、`kWhiteboardPatternOwnedByFreeze/Draw3`、`WhiteboardPatternDrawnByDraw3(whiteboardWorkspace, owner)`（Draw3 侧唯一门控）、`WhiteboardPatternDrawnByFreeze(owner)`；`WhiteboardViewportSample{active, moving, page, generation, x, y, serial, qpc}`、`WhiteboardViewportSampleChanged(previous, next)`（发布去重，不比较 `serial/qpc`）、`IsLightWhiteboardBackground`。
- `Draw3.WhiteboardPattern.h`：`kWhiteboardPatternStepDip=24`、`kWhiteboardPatternAlpha=0.18`、点半径/线宽 1 DIP、有效 DPI 缩放 `[0.5, 8]`、`WhiteboardPatternPhase(halfExtentPx, viewportPx, stepPx)`。
- `Whiteboard.PatternFollow.h`（`Inkeys::UI::WhiteboardFollow`，无 D2D/窗口依赖，生产与 Headless 共用）：`ViewportChannel::{Write, Read}`（`ReadResult{Empty, Ok, Contended}`）、`ResolvePhase(widthPx, heightPx, dpiScale, viewportX, viewportY) -> Phase{xPx, yPx, xDip, yDip}`、`PlanFollow(sample, knownGeneration, State, widthPx, heightPx, dpiScale, patternEnabled) -> Plan{usable, draw, phase}`、`SampleUsable`、`CircularDistance`、`kMovingEpsilonPx=0.25`、`ShouldWakeFreeze(patternKind, moduleActive, sampleActive)`、`ThemeChangeRetiresPixels(oldRgb, oldPattern, newRgb, newPattern, freezeOwnsPattern)`、`Profile`。
- `Whiteboard.Theme.h`（`WhiteboardVisual`）：`DrawThemePattern`（主栏预览；以 bounds 中心为相位）、`DrawThemePatternPhase`（画布；DIP 相位）、`DrawThemeSolid`、`DrawTheme`（视口 `(0,0)` 的旧整幅背景，仅作像素对照基准，生产不调用）。
- 发布链：`DrawingController` 的 `publishWhiteboardViewport`/`syncWhiteboardPattern` → `DrawingControllerRuntimeObserver::whiteboardViewportChanged` → `HostRuntimeCallbacks::whiteboardViewportChanged`（`HostRuntimeCallbacks` 的位置式聚合初始化，新字段只能追加在末尾）→ `Whiteboard::PublishCanvasViewport(const WhiteboardViewportSample&) noexcept`；页快照仍走 `Whiteboard::PublishPageSnapshot(current, total, changing, rgb, pattern, unsaved, generation)`。
- Freeze 内部：`RenderBackground`（外层，至多两遍）→ `RenderBackgroundOnce`、`EvaluatePatternFollow`、`followState/followPhase/followSample`、`patternKindMirror`、`knownPageGeneration`、`followRequestPending`。

### 3. Contracts

#### 3.1 归属与开关

- 默认归属 Freeze：点阵/网格由 共享 UI3 D2D/WARP 管线上的 Freeze 独立场景在 UI3 渲染线程绘制；视口变化时重画点/线位置（序号直算），不是位图滚动或平铺。
- `kWhiteboardPatternOwner` 是单一编译期开关，两个取值都必须能编译，弃用侧只是惰性、不得删除。
  - `Freeze`（默认）：`WhiteboardPatternDrawnByDraw3` 恒为 false，`syncWhiteboardPattern` 令 Draw3 侧纹理参数恒未启用，因此渲染器 under pass、`appliedWhiteboardPatternKey` 与由其触发的 `forceFullPresent` 全部惰性；控制器发布视口，`PublishCanvasViewport` 写通道并唤醒 Freeze。
  - `Draw3`：Freeze 只画 `DrawThemeSolid` 纯色，仅 RGB 变化退休像素（`ThemeChangeRetiresPixels(…, false)`）；`publishWhiteboardViewport` 与 `PublishCanvasViewport` 均为空操作，Freeze 不接收视口；纹理由 Draw3 under pass 绘制。
- 两侧绝不同时绘制：under 混合会把已绘纹理再叠一次（alpha 约 `0.18→0.33`）。切换开关不改变 UInk、主题持久化与主栏预览。

#### 3.2 视觉与相位

- 视觉常量单一来源：24 DIP 间距、点半径/线宽 1 DIP、颜色由 `IsLightWhiteboardBackground`（`299R+587G+114B > 128000` 为浅色 → 黑，否则白）、α=`0.18`、预乘；按真实 DPI 缩放，`step = 24 × dpiScale` 像素。Freeze 在 DIP 空间绘制（场景变换 `Scale(dpiScale)`）。修改任一常量必须同步 `DrawThemePattern`、`DrawThemePatternPhase`、`Draw3.WhiteboardPattern.h` 与（后备）shader 路径。
- 相位：`phase = ((W/2 − viewport.x) mod step, (H/2 − viewport.y) mod step)`；W/H 是 Freeze 目标像素尺寸（以 Freeze 自己的 `W/2` 为中心，保证视口 `(0,0)` 与改动前一致）；DIP 相位 = 像素相位 / `dpiScale`。屏幕位置 = `W/2 + k·step − viewport`（`screen = canvas − viewport`）。必须用 `double` 做 `fmod`，负值补 `step`，并吸收 `−ε + step` 舍入到 `step` 的情形；视口是 float 后备缓冲像素，在 `±1048576` 附近的亚像素分辨率受限。格点按序号 `phase + k·step` 直接计算，禁止浮点累加。
- 视口 `(0,0)` 时 `DrawThemePatternPhase` 的格点位置与旧 `DrawThemePattern` 相同；唯一差别是多画一圈落在边缘外小于半宽加 AA 的点/线（旧实现缺右/下边缘上的半个点）。
- 仅白板且纹理非空白时绘制；桌面/PPT/空白零图案像素、零唤醒。`dpiScale` 非有限或不在 `[0.5, 8]` 时不画并沿用上一帧相位。
- 主栏主题预览不变：`Bar.Scene.cpp` 仍调用 `DrawThemePattern`（70×70 DIP 的 3×3 中心对称），不参与视口。

#### 3.3 视口通道、每页视角与过期规则

- 写端是 Draw3 绘制线程内的 `publishWhiteboardViewport`，发布“当前显示页”的视口，三处：`updateCanvasNavigation` 之后（平移帧最早点）、命令处理后与 `syncWhiteboardPattern` 同位（翻页、冷页视角 `Restore`、`TranslateViewport`、工作区切换）、合成前再同步一次。按 `(active, moving, page, generation, x, y)` 去重；`active` = 当前工作区为白板且当前页有 canvas，`moving = canvasPan.Active()`，`generation = whiteboardGeneration`（进入白板/翻页递增）。离开白板与 `Run()` 退出（`EndDrawingActivity`）各发布一次 `active=false` 的空样本。冷页加载期间 `currentPageIndex_` 未切换，发布的仍是正显示的旧页。
- 通道是 seqlock：单写者（绘制线程）、单读者（UI3）；`ViewportChannel::Write` 只做原子存储加栅栏，无分配、无系统调用。`PublishCanvasViewport` 另做序号递增、按需诊断计时与合并唤醒，不能把通道写入的成本边界套到整个发布入口；读端自旋至多 256 次，超限返回 `Contended`，回调返回 Retry 稍后重读，不能丢落定帧。`serial` 由接收通道单调填写。回调直接到 UI3 只跳一次，不经 Host 快照、条件变量或 state 线程。
- 通道总是写入（含空白主题，保证开启纹理后立刻有最新样本）；唤醒受门控：`ShouldWakeFreeze(patternKind != 0, 白板 active, sample.active)` 为真且 `followRequestPending` 为 false 才 `RenderPipeline::Request(WhiteboardFreeze)`。Freeze 回调**开头**先清 `followRequestPending` 再读通道，使新样本不丢，且 120 Hz 写端不逐帧 `SetEvent`。
- 通道只带当前页视口，不存每页副本；每页视角仍由 `WhiteboardViewportMemory`/`InkCanvas::Viewport()` 持有（见上一节），各页保持自己的视角，新页 `(0,0)`，冷页被驱逐后重载同样恢复。翻页在同一控制器迭代内发布新页视口，Freeze 下一次更新单次跳到新相位，不是滑动。
- 过期规则（最新者胜出，不做页匹配）：`SampleUsable` = `active` 且 `x/y` 有限且 `generation >= knownPageGeneration`。`knownPageGeneration` 由 `PublishPageSnapshot` 覆盖写（不取 max，控制器重建后代次从头计数也不被旧最大值卡死）。更老代次的样本忽略；同代次不检查 `page`，不验证主题身份；更新代次的样本在页快照到达之前就立即生效（相位先于底色，更贴近墨迹，不等 state 线程的 ≥50 ms）。样本不可用时沿用上一帧所画相位，从未画过则用视口 `(0,0)` 相位。
- 主题与翻页后的相位始终取重画时读到的最新样本，不缓存旧相位；主题的底色、纹理类型仍走原 `PublishPageSnapshot` 路径。

#### 3.4 Freeze 更新路径不变量

1. **相位变化只是场景脏区**：只调用 `backgroundScene.Invalidate()`，绝不调用 `backgroundState.Invalidate()`/`CancelBackgroundCandidate()`、不退休 `Prepared()`、不推进 `ContentRevision()`。视口相位不属于“主题、DPI、设备、请求”退休像素的原因，否则连续平移每帧取消在途回执而永远追不上。
2. **在途提交期间不画**：`backgroundSubmission` 存在时 `RenderBackgroundOnce` 只轮询回执并返回，不读通道、不画、不新增 scene damage；否则回执确认时的 `ConsumeDamage` 会吞掉新脏区。在途期间到达的样本天然被最新者合并，没有队列。成功确认并移除在途提交后，才读取最新样本判断是否续帧。
3. **每次确认都 Prepare**：回执与身份/几何/`presentationRevision` 全匹配后用新帧 `backgroundState.Prepare(pending.frame)`，再 `CommitPresented`/`ConsumeDamage`；`presentationRevision` 随每次 ULW 的 `PixelsChanged` 递增，旧 prepared 在途期间暂时失配只会让 workspace 握手多等一轮。确认失败仍走既有 `Invalidate`/Retry，不由相位引发。
4. **流水线，至多一个链式额外帧**：确认后若 `PlanFollow` 判定通道又前进，先释放全部租约与渲染锁，同一次回调内由外层 `RenderBackground` 再运行一遍 `RenderBackgroundOnce` 画下一帧；第二遍只会提交新帧并返回 Retry，不会再确认，因此每个回调至多两遍。不做流水线则每个像素帧需要两个 UI3 批次，速率腰斩。
5. **ε 与落定帧**：运动期（`moving=true`）相位的环形位移小于 `kMovingEpsilonPx=0.25` 物理像素不重画（23.99→0.01 只算 0.02）；`moving=false` 的落定帧不受 ε，相位与已画不同就必须精确重画一次，之后进入 Idle 且无新像素。没有“结束事件”：最后一次 `moving=false` 的发布（即使 `x/y` 未变也要发）使通道 `serial` 与 `drawnSerial` 不同。
6. **窗口/ULW 合同不变**：候选仍是 `applyBounds=false`、`verifyBounds=active`、`requirePresentationMatch=active`、`visibility=Preserve`（revision≠0）；仍先 `TryAcquirePresentationLease(Freeze)`（try-only，取不到→Retry，不等待）；`SubmitFreezeSurface` 要求 `WhiteboardFreezeSurfaceOwned()`；成功后 `PixelsChanged(Freeze)` 取新 revision；平移期不 Show/Hide/promote/SetBounds；最小化/inactive 分支先于相位逻辑；`BackgroundPrepared/BackgroundMatchesActive` 仍由 owner 的真实 visible/bounds/非 iconic 快照与 Complete 回执裁决，相位帧不是 workspace 就绪证据。

#### 3.5 主题与纹理失效

- `ThemeChangeRetiresPixels = oldRgb != newRgb || (freezeOwnsPattern && oldPattern != newPattern)`：Freeze 归属下底色或纹理类型任一变化都是内容失效（`configuredBackgroundScale=0` 强制重配、`Invalidate`、取消候选、`Request`）；Draw3 归属下仅底色变化，纹理类型只更新分页预览快照。
- `drawContent` 在 `ConfigureBackgroundScene` 按值捕获 rgb/pattern，相位经文件内 `followPhase` 读取、闭包不捕获，所以相位变化不触发重配置；重配置整屏重画并作废 `followState`，下一帧按最新视口重新记录。

#### 3.6 诊断（仅显式）

- 仅 Debug 构建（`Inkeys.vcxproj` 为 `Whiteboard.cpp` 在 Debug 定义 `INKEYS_WHITEBOARD_PATTERN_PROFILE`）且环境变量 `INKEYS_WHITEBOARD_PATTERN_PROFILE=1` 时采样，沿用 `INKEYS_SETTING_ENTRY_PROFILE` 惯用法；Release 不编译，关闭时零开销（不读 QPC，通道 `qpc` 不填）。`Profile` 固定 512 样本，无逐帧分配/格式化/写盘，一次平移片段（`moving` 结束的落定帧）汇总输出两行 `[WhiteboardPatternPerf]`。
- M1 更新率：片段时长、更新次数、updates/s、相邻更新间隔 p50/p95/max、回调侧耗时 p50/p95/max（Draw+GetDC+ULW）；只统计相位触发的更新，排除主题/DPI 整屏重画。
- M2 滞后：相对 Draw3 视口的帧数（ULW 完成时通道最新 `serial` − 本次所画 `serial`）、`ack-lag-frames`（回执确认时再取一次）、ms（now − 样本 qpc）、px（视口距离）、落定时延（最后一次 `moving=false` 发布到对应帧完成）。不含 DWM 扫描输出，只是软件侧下界。

### 4. Validation & Error Matrix

| 条件 | 必须行为 | 证据 |
| --- | --- | --- |
| Freeze 归属，视口 `(0,0)` | 像素与改动前一致（100% DPI `maxDelta=0`，150% `<=1`，忽略 3 px 边） | `--page-control-hidden-test`：`viewport zero reproduces the pre-follow Freeze pattern`；`--draw3-whiteboard-pattern-test` 的 Freeze 段 |
| 视口平移且相位位移 `>=0.25 px` | 点/线按相位公式位移；每个回调至多提交一个新像素帧（确认旧帧后可续跑一次） | `TestWhiteboardFreezePatternFollow`（回放：每回调一次重画、取最新样本） |
| 在途候选期间到达任意多样本 | 不重画、不取消候选、`ContentRevision` 不变，确认后只画最新者一次 | `pan samples during a pending candidate neither redraw nor cancel it nor invalidate content` |
| 回执确认且通道已前进 | 同一回调内链式恰好再画一帧，是新 `frameSerial/nativeRevision`，无内容失效 | `receipt acknowledgement is followed by the next pattern frame in the same callback`、`chained frame is a fresh submission without content invalidation` |
| 落定帧（`moving=false`，亚像素差） | 精确相位重画一次，之后全部 Idle、`Prepared` 为最新帧、无新像素 | `protocol is Idle with the latest frame prepared and no further pixels`、`settled frame sits at the exact final phase` |
| 样本 `generation < known` 或 `active=false` | 忽略，不重画；更大 `generation` 立即重画 | `stale-generation and inactive samples never repaint the pattern`、`a newer generation sample repaints immediately` |
| 仅纹理类型变化（RGB 不变） | Freeze 归属退休旧帧并重画；Draw3 归属不失效 | `pattern-kind change retires the previous frame under Freeze ownership`；Draw3 分支 `pattern-kind change does not retire the prepared frame` |
| 仅 RGB 变化 | 两种归属都退休 | `ThemeChangeRetiresPixels` 纯函数用例 |
| 空白主题/桌面/PPT | 零图案像素；写端不唤醒 Freeze；Draw3 under pass 为空操作 | `ShouldWakeFreeze` 用例；`desktop/presentation gating leaves the canvas truly transparent` |
| `dpiScale` 非有限或不在 `[0.5, 8]` | 不画，沿用上一帧相位 | `PlanFollow` 用例 |
| 通道读到 `Contended` | 回调返回 Retry，不丢落定帧 | 代码路径；纯逻辑 `seqlock` 压力用例只证明无撕裂 |
| 切到 `kWhiteboardPatternOwner=Draw3` | Freeze 纯色；不发布视口；under pass 工作；上述后备测试仍通过 | `RunWhiteboardPatternValidation`、`RunHiddenBackgroundTests` 的 `else` 分支（编译期选择） |

### 5. Good / Base / Bad Cases

- Good：连续平移时 Freeze 回调确认旧帧后至多续跑一次并提交一个新像素帧，在途期间只合并样本；停手后落定帧精确落在最终相位并回到 Idle；翻页后相位单次跳到新页视角，冷页重载同样如此。
- Base：空白主题、桌面、PPT 不产生图案像素也不唤醒 Freeze；视口 `(0,0)` 的白板与改动前像素一致；预览与画布共用视觉常量。
- Bad：每个视口样本都 `Invalidate()` 并取消候选；在途期间新增 scene damage；经 Host 快照/state 线程逐帧转发视口；Freeze 与 Draw3 同时画纹理；用 float `fmod` 或浮点累加格点；把相位帧当作 workspace 就绪证据。

### 6. Tests Required

- `InkeysHeadlessTests.exe --no-window --canvas-pan-only`（含完整 `--no-window`）的 `TestWhiteboardFreezePatternFollow`：归属开关互斥与门控、通道往返与 NaN/带符号零位级保持、两百万次写/百万次读的 seqlock 无撕裂无回退、相位数学（多 DPI 缩放、`±1048576`、周期性、与 Draw3 后备相位逐位相同）、`PlanFollow`（最新者胜出、ε 合并、落定精确、过期代次、非有限视口、空白不画、非法 DPI）、回放（每回调一次重画、惰性尾巴、落定后静止）、`ShouldWakeFreeze`、`ThemeChangeRetiresPixels`、`WhiteboardViewportSampleChanged` 去重、`Profile` 固定容量；`TestWhiteboardPatternGeometry` 覆盖后备的 CPU 几何。
- `Inkeys.exe --page-control-hidden-test`（`RunHiddenBackgroundTests`，屏外真实 WARP + 真实 Freeze HWND/owner/ULW，`Step()` 绕过调度器直驱 `RenderBackground`）：相位像素与旧参考逐像素对比（视口 `(0,0)`、小数视口按锚点平移参考 `<=6 LSB`）、在途合并、链式帧、落定帧、代次过期、纹理类型失效。S8 的 `Whiteboard.cpp:RunHiddenBackgroundTests` 内 `RunFollowStream` 在 100/150/200% 与点阵/网格下，以 60 Hz 发布加速至 3000、恒速 3000/12000 DIP/s 的样本流；断言落定像素（含边缘）与最终视口参考逐字节相同、不取消相位候选、不改变内容版本、协议回到 Idle 且不继续重画；同入口还覆盖陈旧代次忽略、翻页、主题与 Blank。输出 `[WhiteboardNative] M1` 的新像素帧率/回调率及 `M2 max-lag-samples`，只代表离屏软件链路。
- `Inkeys.exe --draw3-whiteboard-pattern-test`（`RunWhiteboardPatternValidation`）：Freeze 带相位函数与旧函数、整周期位移不变，以及后备 pass 的全部像素用例（见 cpu-gpu-contracts.md）。
- 完整 Debug|ARM64 构建与相关无窗口/hidden 入口须记录退出码；实际结果以任务验证为准，本合同不宣称已通过。

### 7. Wrong vs Correct

Wrong：`视口样本 → backgroundState.Invalidate() + CancelBackgroundCandidate() → 重画`（连续平移永远追不上回执）；或 `在途期间 backgroundScene.Invalidate()`（确认时被 ConsumeDamage 吞掉）。

Correct：`视口样本 → 最新者通道（Blank 不唤醒）→ 无在途提交时 scene damage → 画 → GetDC/ULW → 发布候选 → 回执确认 Prepare 新帧 → 通道又前进则同一回调链式再画一帧 → 落定帧精确重画 → Idle`。

### 8. Common Mistakes

- 不要把相位变化当内容失效；不要在在途提交期间读通道或新增 scene damage。
- 不要做页匹配：页匹配会让相位晚于墨迹翻页至少一个 state 线程周期（≥50 ms）；样本取最新者，只丢弃更老代次。
- 不要对视口用 float `fmod`；不要让格点位置依赖累加。
- 不要在写端逐样本 `SetEvent`；不要让 Blank 主题、桌面或 PPT 唤醒 Freeze。
- 不要为新增 shader 路径复用 `0..22` 的 shape 码，见 cpu-gpu-contracts.md。
- 不要把纹理写进 L2、历史、可信快照、热前像或 composition cache；纹理不得出现在桌面/PPT 的画布像素中。

### 9. 已知限制、推断与未验证项（S8 补录于 2026-11-08）

- **主 Agent 本机实测事实（Debug|ARM64、WARP、屏外、60 Hz 节拍、无 DWM，噪声较大）**：`Whiteboard.cpp:RunHiddenBackgroundTests` 内 `RunFollowStream` 的 M1 为 22–35 新像素帧/s、47–60 回调/s；M2 最大落后 3–4 个视口样本。不能把回调率当作纹理更新率，也不能据此宣称真实桌面帧率通过。
- **推断**：60 Hz 采样下 3–4 样本约为 50–65 ms 视口滞后；约 1500–3000 DIP/s 以上预期可见错位，是否可接受需真机判断。默认仍为 Freeze；若体验不佳，按既定 `Draw3.WhiteboardModel.h:kWhiteboardPatternOwner` 切到 Drawpad/Draw3 归属后备。
- **已知限制**：Draw3 后备纹理路径使用 Host 启动时捕获的 DPI，未跟踪运行期 DPI 变化；不能把 Freeze 的 Display 失效订阅当作后备已支持动态 DPI。
- 真实平移输入经调度器唤醒与 UI3 60 FPS 节拍的端到端帧率：hidden 测试以 `Step()` 直驱回调，不含 `RenderPipeline` 批间隔、`Request` 合并与其它客户端争用；调度上限（`Scheduler::SetFrameRate` 仅接受 30/60，默认 60）只来自代码阅读。
- DWM/显示器延迟与 3000 DIP/s 以上的视觉不同步：M2 只量软件侧下界，不含 DWM 取用与扫描输出，光学滞后需外部高速摄像；上述速度下的错位/混叠风险仍属推断，没有真实显示测量（S8 软件侧实测与速度风险推断见本节前两条）。
- 4K/100% DPI 的单次更新成本（含 ULW）：仅有屏外 WARP 上 D2D+`GetDC(COPY)`、不含 ULW/DWM 的实测（噪声大、非真实 GPU）——4K 点阵 100% 约 32–39 ms/次、150% 约 17 ms、200% 约 9–10 ms，网格约 3–6 ms；这些单次离屏数据不能证明实际调度更新率；4K/100% 点阵存在超出 16.7 ms 预算的风险。含 ULW 的完整成本与真实桌面呈现成本未测。
- UI3 线程饥饿与功耗：Freeze 与 Bar/PageControl 共用 UI3 线程，每次更新整屏 `GetDC(COPY)` 与 ULW（4K 约 33 MB）；对其它客户端的影响与功耗/温度未测。
- Draw3 后备路径在真实 DComp/ULW 呈现下的视觉与 4K 合成帧耗时：仅有屏外 WARP、无 HWND/交换链的像素用例，4K 成本只记录不断言。
- Win7 WARP：D2D 路径与 Draw3 shader 路径都未在 Win7 验证。
- 平移期间跨显示器或 DPI 变化：Display 订阅会整屏失效并作废 `followState`，但运动中的行为未测；`W_f == canvas` 来自白板三窗同取主屏 `rcMonitor` 的代码阅读，未做真机/多显示器核对。
- hidden-HWND 测试只在屏外（`{-30000,-30000,-28080,-28920}`，1920×1080）运行，不证明真实 DWM、任务栏或 Office 动画。
- 未实现、待真机数据后再批准的缓解：速度前瞻（把相位外推到显示时刻）、高速淡出（超过约 4000 DIP/s 只画纯色）、单基底遍、`ID2D1GeometryRealization`（Win8.1+，Win7 须回退）；以及把开关切到 Draw3 归属。
