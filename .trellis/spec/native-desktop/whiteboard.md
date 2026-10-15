# Whiteboard 初版合同

本合同来源于 2026-10-15 用户批准的 Whiteboard 初版计划，覆盖白板、共享分页、Draw3 页级存储及主栏场景生命周期；实现与验证记录见 `.trellis/tasks/10-15-whiteboard-v1/`。此前 08-17 白板任务中的无保存、统一墨绿背景、退出强制折叠和左右同宽约束由本合同替代。

## UI 与线程所有权

- PageControl 是四个分页 HWND 的唯一 owner；白板继续复用 PptBottomLeft/Right。左栏增加标准 2×2 More 后为 305×80 DIP，右栏 230×80 DIP，主体距侧/底各 5 DIP。
- 菜单及主题面板属于左下同一 HWND，但分别绘制独立矩形：菜单在分页栏上方回弹展开，主题在菜单右侧回弹展开，节奏复用主栏属性动画。菜单行使用左图标和左对齐文字。包络、命中、damage 与呈现基于同帧动画几何；左/底锚点不移动，不新建 popup HWND。共享 BarSurfaceScene 行为而非复制按钮状态机。
- More、Escape、面板外点击、翻页、最小化、workspace 转换收起菜单并取消 capture。透明边界穿透，UI 操作不落笔。背景和预览使用同一主题纹理绘制规则。
- 按钮在白板中显示关闭白板，有有效放映时显示返回放映；菜单关闭使用同一退出事务。后台 PPT 结束不得结束白板。
- 主题按页保存，新页继承；默认墨绿 #123B32/空白。色板 #123B32/#FFFFFF/#F5F1E6/#E5E7EB/#25282D/#172A46；空白/点阵/网格，24 DIP 间距、1 DIP 点半径/线宽。主题不改变画笔、不进入墨迹 undo，修改使页 dirty。
- UI 只发布带身份请求并消费快照，Draw3 绘制线程独占页内容/历史；I/O worker 不访问 UI、HWND 或可变 runtime。

## 存储、历史与内存

- 每进程一个逻辑多页 UInk 白板，独立 AutoSave/whiteboard 命名空间；忽略桌面保存开关。同进程关闭重开保留，新进程新建，旧文件保留供未来历史浏览，不自动恢复。
- 保存只在离页/离开白板/正常退出安全点触发。主题、新空页、Undo/Redo/Clear 都是 mutation；无变化不写盘，无定时保存。退出扫描 active 和 parked dirty 白板。
- 流式扫描目录、按页 decode、流式复制未修改页面并原子提交新版本；读取、自校验、hash 及保存均不得全量物化多页墨迹。文件版本与页偏移必须绑定；先 durable UInk 后原子索引。
- 版本化 extra 保存页主题及卸载前可用的完整 CPU 撤销/重做信息，标准 content 保持可见画面语义。GPU 纹理/缓存不得序列化。不扩大既有跨 Clear 规则。
- 非当前页仅在保存成功且 identity/generation/revision 仍匹配时卸载笔迹、历史及CPU/GPU缓存。正常稳态只保留当前页、轻量目录和有限在途数据；同页待保存快照合并。
- 保存失败可继续翻页，失败页 warm 并显示部分页面未保存；后续安全点重试。stale completion 不得释放新修改。冷加载锁定绘制/破坏命令，成功成对切主题/内容/页码；失败保留原页而非创建空白覆盖。

## 场景与底栏

- 首次 Desktop→PPT/Whiteboard 记录桌面主栏快照。只调整已有底栏贴屏底 5 DIP，保持是否居中；浮动、折叠不强制展开。PPT↔Whiteboard 不重拍基线，不重复折叠/复位；最终回 Desktop 恢复基线。
- 白板主屏，PPT 放映屏；同屏连续，跨屏适配显示快照/DPI，原显示器消失按既有回退。
- 最小化是窗口组操作，不是 workspace 退出；两侧分页隐藏，主栏保持显示并回到任务栏居中底栏，恢复不重放入口；PPT 场景禁止最小化白板。任务栏锚点使用 exe 图标和国际化 Inkeys 白板标题。保持 NOTOPMOST、单一任务栏锚点及退出穿透/Z序合同。
- 平移缩放仍禁用；页面管理、历史 UI、手动导出、跨进程恢复不在初版内。

## 验证要求

- 生产 Scene 离屏像素/命中/包络测试，覆盖六色三纹理、动画反向/DPI/设备重建/透明区域；不能仅以公式测试代替。
- 真实存储与Controller测试覆盖多页 roundtrip、Undo/Redo/Clear 分支、主题空页、foreign session、写/索引/读失败、快速重入与stale completion。
- 100 非空页保存遍历后非当前页内容/历史/CPU/GPU释放；统计流式buffer峰值，不用失败warm页假冒正常回收通过。
- 完整 InkeysRepo.sln Debug|ARM64 与相关无窗口测试。GUI验收必须另获明确授权；编译/headless不能代替真实PPT/任务栏/视觉验收。

## 截图反馈回归约束

- 非活动白板入口图标为标准 2×2；分页文字在模式转换后仍必须呈现。
- 色板和纹理预览均为 70×70 DIP 圆角方形，选中只显示中央勾，复用主栏按钮悬停/按压反馈，不添加外围选中高亮。
- 居中白板主栏换向使用屏幕边缘，不把分页控件当作可用屏幕边界。

- StandardTwoTwo 的标签必须使用 primaryText/主文字槽。secondaryText 的字号与槽位在共享 metrics 中为零，不能用它呈现分页标签；回归需检查真实 Scene 标签像素。

## UI3 统一菜单与主题合同（覆盖截图轮旧尺寸）
- 三项菜单操作使用共享按钮交互，140×32.5 DIP（4×1），左图标/文字，无子菜单箭头。菜单中心锚定 More 按钮中心；面板、行、预览间距均为5 DIP共享常量。
- 主题面板的背景颜色、背景样式标题和分割线沿用主栏 More 的视觉能力；预览是70×70 DIP标准2×2按钮，不能只套一层自绘hover/press。空白/点阵/网格文字在按钮内使用主栏相同标签槽和字重。
- 选中叠加已有 color/colorSelect SVG，30 DIP（主栏画笔色板15 DIP的两倍），不得绘制替代勾或增加外圈选中效果。点阵和网格相位相对于预览内容中心对称，保持24 DIP标尺。
- 弹出节点的父触发分别为 More 和 Theme；面板及内容继承共享UI单一pose，分页业务层不另行easing。原有PageControl屏幕bounds过渡不得给菜单再叠加第二套时钟。
- 纯文案/主题/编辑状态更新不应重新启动面板展开、重置输入锁或让静态Scene持续绘制。动画遵循全局速度和禁用设置。

### 共享动画与设备资源防回归
- 通用 Scene 的 popupAnchor/parentPanel 直接使用已有 EraserSurfaceMotion、ResolveEraserSurfacePose、ComposeEraserPose 和 FrameAnimationClock。标准按钮完整绘制/命中由 DrawBarButtonVisual 与共享 metrics 负责，业务 drawContent 仅画背景纹理。
- logical backing 用共享曲线极值和父子组合范围分配，保持高水位缓冲；实际 presentation 取当前背景与可见 pose 矩形并集及阴影。渲染/脏区/光源/presentation→logical 命中均使用同一 X/Y 原点。
- 读取动态包络前 Advance(frameTime)，同一 frameTime 的 Render 对所有动画值幂等。纯内容用 UpdateContent，不再次展开或延长输入锁；关闭只依据 popup motion，不被 hover 的5秒淡出延长。
- 同一控件的 icon/content opacity 每帧只能有一个目标；无图标/未选中或隐藏内容不得先后写1→0或0→0.3导致永不落定。
- 预览缓存画刷/圆角裁切，按压通过共享矩阵变换稳定几何；所有设备缓存 reset 路径释放回调保留的旧 context。PPT 初始隐藏按钮转换为 popup 时同样应用 panel/divider chrome。
- 回归需从生产 PPT Configure 切入白板，比较共享 motion 中间帧，覆盖父子同时展开、倍率/禁用/反向、当前包络包含可见像素、坐标映射和内容更新不重启；仅最终矩形或 EndDraw 成功不足以证明这些合同。
