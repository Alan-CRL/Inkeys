# 主栏按钮统一表现检查记录

## 范围与上下文

Trellis check，活动任务10-14-toolbar-order-expansion，feature/mode，基线2dd3b344。已读取保存的SubagentStart hook、check.jsonl全部列项、prd/design/implement，以及两份按钮/历史研究、实施记录和相关完整规范章节；不按截断摘要代替合同。检查覆盖此次九处产品/回归源码，不改任务/规范，不启动独立构建或GUI/CU，不commit/push/finish。

## Findings (fixed)

### 1. 表现锁覆盖同步窗口捕获取消
- File: Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp；InkeysHeadlessTests/recall_attribute_tests.cpp
- Issue: 新presentationMutex从主栏投影持有到隐藏锚点处理；期间原CloseColorPicker(true)/CloseThicknessSlider(true)在活跃capture/gesture时会进入Bar.Interaction.cpp的同步SendMessage，违反不持表现锁等待window owner合同。UpdateRendering(false)避开锁不能替代移除这类持锁等待。
- Fix: 与主会话协调授权后，把range、两项可用性计算及不可用关闭条件整体移到SubmitTargetsAndLayout开头、presentationLock之前；后续目标使用同份frame工具/range/可用性，无unlock/relock混采。新增源级断言确认两项Close在锁外，锁内不含这些调用。ClosePenTypeMenu逐级仅原子字段与CloseAnnotationTooltip的IsWindow/KillTimer，没有同步SendMessage，不扩大修改。

### 2. 历史弹窗解锁后重读全局发布view
- File: Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp、Bar.Main.cppm、Bar.RecallAttribute.cpp、Bar.EraserAttribute.Test.cpp；InkeysHeadlessTests/recall_attribute_tests.cpp
- Issue: 原实现grid内统一投影发布全局view并供颜色使用，grid解锁之后并发Load/StateUpdate可发布新view/hide；稍后RecallAttribute::Advance无参getter与anchor->IsVisible组合成与已提交主栏目标不同的业务代。
- Fix: 主会话确认内部快照方案后，grid投影后仍持锁时捕获RecallChromeView及依其expanded身份选出的anchorVisible到frame；主栏颜色及Advance显式消费同份参数。Advance继续用稳定preset身份与既有动画几何，不再读取全局getter/live hide。其他面板互斥关闭继续原语义，不宣称全UI原子快照。
- Regression: 已有真实无窗口Bar fixture初始化主按钮/主栏后，对Undo/Redo两种hold捕获打开view与可见anchor，再发布新的空页Selection全局view；真实独立BarRecallAttributePanel::Advance仍用所传frame保持popup显示，传入false anchor门禁时完成退场。静态合同同步参数/身份/无getter与frame接线。该回归不执行私有RenderLoop/HWND或真实Accepted命令。

## Findings (not fixed)

没有剩余已确认、在本轮范围内的源码缺陷。真实设置组件增删、捕获取消窗口消息、鼠标/触摸、三秒计时器系统投递、PPT/白板与动画视觉属于未授权GUI人工待验；无窗口回归与源码审查不能替代。历史Accepted样本由纯策略种入，未启动Draw3 Host，不宣称验证真实Publish Accepted。

## 真实路径审查结论

- SyncUi3BuiltInComponents -> SyncLegacyExtensionButtons -> Load真实链保留。注册/配置基准与有效网格分开；Load只改base/member并结束于同一ApplyMainPresentationLocked，不短暂覆盖动态有效尺寸。重复RegisterButton在复制base前拒绝已有ID，不把动态Freeze twoOne回抄注册默认。
- 构造/注册首次初始化之外，主栏有效size/userVisible/hide/business state写入集中到ApplyMainPresentationLocked。A1/B/A2、稳定Pierce/Redo/More/Divider同属该投影；独立Surface/属性按钮与hover/press/SetTar保持既有owner。IdtPlug-in三个冗余公共内容入口已删除，其UpdateRendering保留。
- A2直接复用生产ResolveBarA2Projection，几何每次应用、文字缓存不短路；compact Selection对Eraser/Geometry/Undo/Redo优先隐藏，Clear仍只有空页Selection Disable。Pierce保持Freeze前长期成员，不因隐藏抽离。
- Recall事件/命令/关闭/绘制/工作区/idle只更改state/deadline后锁外Request；3秒steady_clock、灰色点击延期、点击前事实与拒绝命令保留。AdvanceIdle只结算已发布state，统一投影唯一刷新真实可用性，no-arg getter只读已发布view。生产due与PreparePanels在Submit之前，统一投影在宽度之前；frame view与anchor门禁只捕获一次。
- UpdateRendering(true)原static串行更新 -> 短presentation -> Recall；false绕过static/presentation等待并保留finite封口/Notify/Request。Load保存Write在unlock后，Prepare/PresetHoming面板归位在projection锁外。目标扫描锁在AnchorHidden结束、FitInitial/初置/停靠前释放，修正后不含同步owner等待/D2D呈现/SetWindowPos。
- MainLayoutSnapshot精确比较稳定成员identity/order/effective size/IsVisible，包含隐藏成员；不含Selected/Disable/文字/颜色/hover/press/动画val。等宽split/full或成员顺序变化沿现有mainBarLayoutChange、CanJoin/后半程重开/current续接/hidden anchor与dirty；扩张曲线仍按实际总宽。列表/Y不反转，旧左右策略与第三光源代码未变。
- 480组件矩阵调用真实注册/Sync/Load/Update入口；0/1/2/3/4/2/1/0数量、两模式/三场景/空有内容/五工具，另有墙钟Poll/Advance、灰色OpenRecall拒绝入口与重装旧唤醒。timeline/SetTar回归是既有生产helper级，不宣称执行完整私有RenderLoop。源码断言只是补充。

## Verification

- Lint/static: git diff --check通过；有效字段assign/store/exchange库存核对通过；九处修改源码对HEAD的UTF-8、BOM与CRLF核对通过。检查编辑时RenderLoop曾误加BOM，已在最终编译读取前恢复原无BOM，无内容变更。
- TypeCheck/build: 主会话首次实现完整ARM64 solution退出0；检查修正后最终完整InkeysRepo.sln Debug|ARM64退出0，0 errors/131既有warnings，耗时3:35，确认实际编译Bar.RenderLoop.cpp且恢复原无BOM在编译前完成。本角色不并行构建；具体警告/环境说明见主会话validation。
- Tests: 修复前真实Bar红测已记录；最终主会话确认Bar离屏退出0（componentMatrixCases=480，EraserVisual failures=0）、Headless --no-window退出0（RecallAttribute failures=0，PASS animation correctness）、state-mode-production退出0（操作模式/穿透切换/Freeze/Clear事务及FineDial过期物理两行PASS）。对应日志均由主会话保存在Build/ToolbarOrderExpansionValidation/button-presentation；本角色未重复运行。
- 未运行GUI/Computer Use或Presentation HWND fixture；上述人工边界保留。

最终确认：两项审查修正已进入最终编译/测试产物，静态与统一验证通过；主会话另以git cat-file --filters对工作树22文件检查编码/CRLF并完成diff --check。源码继续冻结，任务状态/提交由主会话按当前授权处理，本角色没有commit或结束任务。

2026-10-14收尾补记（主会话）：用户确认修改到位且人工验证通过，已记录人工验收完成；自动验证与本角色未运行GUI的证据边界保持原样。用户授权本轮提交并结束任务。
