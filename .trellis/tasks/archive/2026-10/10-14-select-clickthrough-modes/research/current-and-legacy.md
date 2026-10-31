# 当前与Inkeys2模式证据
- 实施基线为用户指定feature/mode，HEAD f499f98a；UI3唯一编译主栏，IdtFloating/IdtDrawpad为vcxproj None，不能恢复生产。
- origin/inkeys2-final:智绘教/IdtFloating.cpp穿透由penetrate.select独立切换，画笔和穿透共存、重复穿透恢复；用户明确不要此共存/切回。
- 当前IdtState::PublishDraw3State/Reconcile/SyncDraw3State以Selection投影后端selectionMode和设置owner；新增穿透必须同样处理。
- 当前Bar.Button::ExecuteClearClick/ToggleClickCoalescer已保留Accepted Clear双击延续，空Selection无动作，勿改橡皮属性Clear行为。
- Freeze模块唯一状态所有者；PPT/白板关闭并标记不可用，不恢复。
- Bar.A2.h和UpdateWhiteboardButtonStyle当前PPT保留灰色定格，本任务改隐藏。旧A2配置Pierce仍退休，穿透运行时注入。
- Setting.cpp常规tab2为目标；PPT助手提示块CardBackground+AccentText图标+TextStrong文字，按内容测量新说明高度。
- 标准2×2=70×70 DIP，label 70×25/13DIP；2×1=70×32.5，文字槽37 DIP/12 DIP。英文需局部换行/字号验证。
- 新Other.Config schema支持原子int；Setting已有QueueConfigWrite与业务FIFO。
- Python别名WindowsApps无法执行，使用bundled dependencies/python/python.exe。VS ARM64 host由vswhere动态查找。git status默认会因HiMsg子模块权限报错，用--ignore-submodules=all确认主树。

## 分支纠正
用户明确要求本工作树切换feature/mode。切换前只有本任务未跟踪文档，无源码diff；git switch成功，任务branch改为feature/mode。保留分支已有Redo popup、交互模式设置FIFO以及DeferDraw3SyncIfRendering死锁修复。
