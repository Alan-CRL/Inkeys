# Whiteboard 初版合同

本合同来源于 2026-10-15 用户批准的 Whiteboard 初版计划，覆盖白板、共享分页、Draw3 页级存储及主栏场景生命周期；实现与验证记录见 `.trellis/tasks/10-15-whiteboard-v1/`。此前 08-17 白板任务中的无保存、统一墨绿背景、退出强制折叠和左右同宽约束由本合同替代。

## UI 与线程所有权

- PageControl 是四个分页 HWND 的唯一 owner；白板继续复用 PptBottomLeft/Right。白板的标准列表设计目标为左栏 305×80 DIP、右栏 230×80 DIP；PPT/白板的可见项及专用 Drag 槽通过同源列表布局计算背景宽度和按钮目标，主体侧边沿控件尺度留 5 DIP，底部按真实 DPI 保证可见描边外沿距屏幕底边 5 屏幕 DIP，透明光影包络不参与底距。分页、更多菜单与主题面板共用主栏三态缩放基准（关闭/DPI、48DIP基础量、任务栏真实量）×`UI.Bar.Zoom2`，不继承PPT局部倍率或保存位置；画布背景纹理仍按真实DPI绘制。
- 菜单及主题面板属于左下同一 HWND，但分别绘制独立矩形：菜单在分页栏上方回弹展开，主题在菜单右侧回弹展开，节奏复用主栏属性动画。菜单行使用左图标和左对齐文字。包络、命中、damage 与呈现基于同帧动画几何；左/底锚点不移动，不新建 popup HWND。分页列表与固定属性直接调用主栏公共按钮函数，BarPageSurface 仅负责数据和目标适配。
- More、Escape、面板外点击、翻页、最小化、workspace 转换收起菜单并取消 capture。白板返回放映时保留 More 实例与列表成员，使用共享 UI3 隐藏布局收拢到 Next 下层并渐隐，不得以空目标矩形或移除成员代替隐藏；按钮退场与列表背景宽度在同一布局批次连续变化，保留按钮和整条栏保持正常透明度；退场时不可命中，中途重入时宽度、位置与按钮透明度均从当前呈现值反向；首次隐藏配置从首帧保持透明。透明边界穿透，UI 操作不落笔。背景和预览使用同一主题纹理绘制规则。
- 按钮在白板中显示关闭白板，有有效放映时显示返回放映；英文返回标签使用短文案 `Resume`，保持标准按钮字号与文字槽。白板窗口组最小化后，主栏按钮显示白板图标和“返回白板”（英文 `Resume`），点击恢复同一白板，不切换 workspace 或重新进入。最小化/恢复边沿须立即刷新按钮内容，浮动和折叠主栏同样适用。菜单关闭使用同一退出事务。后台 PPT 结束不得结束白板。
- 主题按页保存，同进程新页继承当前页；新进程首张白板页读取最后一次成功修改的颜色/纹理偏好，缺省或非法配置回退墨绿 #123B32/空白。冷加载与切换已有页不回写偏好，重复或身份失效的主题请求不触发持久化；绘制 owner 只投递既有配置 FIFO，正常退出先停止生产者再排空写入。色板按三列两排：#123B32/#172A46/#000000，#FFFFFF/#E5E7EB/#F5F1E6；空白/点阵/网格，24 DIP 间距、1 DIP 点半径/线宽。主题不改变画笔、不进入墨迹 undo，修改使页 dirty。
- UI 只发布带身份请求并消费快照，Draw3 绘制线程独占页内容/历史；I/O worker 不访问 UI、HWND 或可变 runtime。
- 首次尚未建立白板工具记忆时默认 SoftPen 绘制；Desktop/Presentation 的新工具记忆仍默认 Selection。已有工作区记忆及用户手动切换的拖动状态保持不变。

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
- 最小化是窗口组操作，不是 workspace 退出；两侧分页隐藏，主栏保持显示并回到任务栏居中底栏，恢复不重放入口；PPT 场景禁止最小化白板，菜单最小化行的图标与文字复用 UI3 `BarButtonDisabledContentOpacity=0.30` 禁用视觉，并关闭悬停/按压/点击；禁用依据是有效放映会话事实，不能由被白板遮挡的分页可见性推导，后台放映结束时须唤醒并恢复可用状态。任务栏锚点使用 exe 图标和国际化 Inkeys 白板标题。任务栏关闭或 Alt+F4 必须转为白板退出请求，沿既有保存/离场事务完成；不能直接销毁共享 Freeze 锚点及其 owned 窗口链。保持 NOTOPMOST、单一任务栏锚点及退出穿透/Z序合同。Shell 全屏通知覆盖 Freeze 与 Drawpad 两个可激活面；激活仅异步重申，最小化、退出和销毁前撤销，恢复重新发布。
- 进入白板必须等待真实 Draw3 首帧、输出/内容版本、背景提交及必要窗口事务；先发布 Active，再异步尝试一次前台激活。Freeze 的背景提交由 Window owner 读回目标 RECT、真实 visible 且非 iconic，并发布固定回执/快照；UI 和协调器不得自行调用 HWND 查询来裁决 ready；最小化期间被抑制的 SetBounds/Show 返回未提交并保留重试，不得以“未执行但成功”推进 `committedBackgroundActive`。恢复边沿重新唤醒背景提交，不需要再次点击入口。焦点分配失败仍完成后台进入，不按周期抢焦点；同 epoch 的 Entering→Active 不取消已接纳请求，退出、新 epoch、最小化、隐藏或停机必须拒绝。设置的实际 Present 不持 ImGui 输入锁，避免与窗口焦点消息互等。
- 同一次进入 epoch 的原生窗口事务失败重试只执行一次主栏收起和 capture 取消；失败后的 250ms 退避先于 Entering 发布，新意图和取消不受旧期限阻挡。后台排序保留 Bar 位置、仅下调 PPT 并核验内部 owner/band/代际/顺序，焦点许可不参与准入；纯层级失败恢复不得 Hide/Show 或重建画布策略。
- 平移缩放仍禁用；页面管理、历史 UI、手动导出、跨进程恢复不在初版内。

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
- 主题样式纹理覆盖完整70×70按钮，文字绘制在纹理上方，不能按标签槽缩短纹理区域。点阵为中心对称3×3九点，网格为三横三竖九交点；与画布共用纹理绘制规则，画布24DIP密度保持不变。
- 色板显示、选中和点击请求使用同一顺序映射，第三项黑色为#000000。

## 三工作区统一交接（2026-10-27）

本轮已批准合同；实现和执行证据见任务 whiteboard-first-open-visibility 的最终报告，不能由本规范推定实机通过。

- Desktop/Presentation/Whiteboard是三个稳定业务态。最新Desired、已完成Committed和仍有实际责任的Pending区分；最小化/焦点不是第四工作区。统一协调快照是业务权威，主栏稳定工作区、工具和Desktop基线按Committed投影，背景/分页可按Pending准备但不能据旧ACK开放业务。
- 接纳切换先真实关闭Host新Down，工具/场景准备和ControlWake、Frame/UI ACK、终态排空仍继续。实际目标工具/选择、匹配输出、必要背景与原生Prepare/Complete结果就绪后统一提交；foreground、Shell COM成功和全局TOP/连续性不进入完成门。
- 业务epoch复用现有奇偶模型，白板显示/最小化语义变化也退休旧ACK；普通PPT页/工具/输出变化不推进工作区epoch。业务版本不替代session/target、slot、tool、device/output/content或HWND代际。
- 白板背景复用Freeze并保持完整主屏rcMonitor，尺寸与DPI读同一Display快照；Desktop/PPT双画布底部1物理像素及旧定格/放大镜规则独立不变。BackgroundPrepared仅证明当前请求、内容失效版本、设备与 HWND 代次的真实像素及窗口回执，实际Active仍需 owner 发布的真实visible/bounds/非iconic；inactive必须真实Hide后确认，且优先于minimized暂停。
- 白板最小化仍保留页/历史/工具/基线；系统确认只确认当前视图，不另发意图让在途请求自失效。最小化后可直接关闭。后台PPT变化只更新最新Primary事实，不能抢走白板。
- 普通失败停止无限重复显示/隐藏，保留owner真实责任；只有Rejected证明owner恢复已退休后才恢复Whiteboard源背景/分页，Pending/failed和输入fence继续保留，不能用源预览伪装切换成功。背景ACK、Draw3/PPT ready及背景prepared/final active的无进展等待进入同一可重试失败预算，实际输出/内容/背景进展才重置等待。
- DComp退休采用共享有界等待，超时/忙碌禁止第二worker、ULW和新HWND；能清除样式才走原ULW，否则经完整legacy窗口链重建并禁止再次选择DComp。旧generation的帧/completion不得确认新恢复，受保护/fatal只可在完整token及真实安全清理后退场，不等待已停消费者普通帧、不清fatal或伪造ready。
- Settings/安全UI与画布恢复的阻挡按实际写集区分；新场景Show/关穿透不得越过旧恢复，旧补偿不能作用于新HWND代际。保存/加载和失败warm数据规则不变。

背景渲染遵守 [UI3 原生窗口候选与真实回执](rendering-and-ui.md#ui3-原生窗口候选与真实回执2026-10-28)：成功像素等待 native ACK 时不重复整屏绘制；Invalidate 同时退休旧内容候选，同请求版本不能绕过主题/DPI/设备失效。完整 mask、frameSerial、workspaceRevision、HWND/generation 和实际矩形全部匹配后才 Prepare/ConsumeDamage；inactive 的 Hide 也必须取得精确真实回执。

背景完成/inactive ACK 通过 NotifyProductWorkspaceProgress 唤醒原协调等待；Freeze ULW 与政策几何/回滚共用 try-only 呈现租约及 pixel revision。Prepare/Complete 为显式异步回执；新通知不作为 ready，也不加速原失败预算。参见 rendering-and-ui 的直移与工作区异步协调合同。
