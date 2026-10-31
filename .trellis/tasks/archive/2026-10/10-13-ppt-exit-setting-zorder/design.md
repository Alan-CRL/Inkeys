# 技术设计

## Evidence and boundary

IdtState.cpp 的 true-exit selectionWaiting/hideForNewSelection 主动双隐藏，之后 ready 再显现；EndSession 在模式条件切换之前刷新 Bar，后续 SyncDraw3State 未请求 Bar 导致高亮旧帧。Window.cpp 设置 owner attach 只改 GWLP_HWNDPARENT，Show 只显示/激活，立即Z序未闭环。旧规范 true-exit双隐藏与用户最新要求冲突，应随实现更新。

产品边界：IdtState.cpp、IdtPlug-in.cpp 负责目标就绪及 mode/UI 提交；Window.cpp 负责 owner/窗口线程/Z序，Setting.cpp 仅在必要时调整显示调用顺序。相关现有测试按实际生产行为扩充，不造字符串断言。允许在现有 Draw3.PresentationState.h 提取生产就绪策略，并在 draw3_bridge_tests.cpp 测试同一策略；不新增公开mock或ABI。

## Frame handoff

从 desired 而不是旧 runtime 决定目标工作区/Selection/输出。匹配 workspace、selectionMode、requested/ready output revision、presented content revision 后提交；等待时保留既有可见帧。去除 true-exit 的提前双 Hidden 及过期场景的主动隐藏，保留 stillDesired 和 mode revision 防止迟到提交。正常切换复用 Window Service 批量显隐，不提前显示未就绪 surface。窗口API失败的安全兜底不当作正常动画路径删除。

允许短暂保留旧主表面至辅助frame ready；业务Selection先关闭绘制接收，系统clickthrough只在就绪后切换，不将 admission blocked 冒充系统穿透。PPT/Desktop文档切换与页墨迹保存逻辑不变。

## Mode and Bar

可信退出捕获 mode revision，条件 Selection 提交成功之后再同步 Bar 样式/渲染；条件失败尊重用户新工具。就绪退出交接刷新根且成功后再清 pending，以便失败重试。退出诊断增加 Bar HWND（复用现有 opt-in trace），区分 visible 与 topmost。

## Settings

owner attach 成功后在同步服务包装层协调立即根刷新，读取实际状态保证设置在画布之前；不在 Setting owner-thread 命令内部等待 overlay 命令形成互等。owner不变且已正确时不重复抬升。owned Show 时在显示/激活前确保当前链层级；ownerless Selection Show 保持普通应用语义。解除owner仍立即 NOTOPMOST；白板沿已有非TOPMOST策略。根刷新失败向上层返回可重试，不能把半完成关系写为 applied。

## Validation and rollback

窗口现有回归增加“attach/show后不额外调用刷新就已在两画布上方”、设置先开后工具切换和解除owner。现有纯逻辑目标就绪/条件mode测试用于过期guard和frame handoff；不得运行默认包含可见窗口的套件。完整Solution+--no-window，生产无GUI mode CLI若有可靠初始化可用再执行。可见PPT/RTS场景需用户另授权。

保持UTF8/BOM/CRLF中文注释和minimal diff；无配置/依赖/公开ABI变化。按上述边界独立回滚，不改变文档格式。

## Cross-thread constraints

设置 owner-thread 不能同步等待 overlay 根刷新造成 USER32 消息互等。正常生产协调/业务线程在 Setting命令完成后执行根刷新；若由 Setting owner-thread 调用且仍需跨线程刷新，应安全返回待重试，不能伪报成功。local HWND_TOP 只调整本窗同带排序，不独立设置 TOPMOST。
