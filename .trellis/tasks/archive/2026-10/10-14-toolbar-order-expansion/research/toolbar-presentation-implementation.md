# 主栏按钮表现规范化实施记录

## 根因及红测

基线feature/mode / 2dd3b344。真实SyncLegacyExtensionButtons -> Load会把Freeze配置基础TwoTwo覆写运行时twoOne，A2内容styleKey不变跳过投影。先仅增加RegisterBuiltInComponents/Explorer开关/Sync+Update真实回归：首build因Test缺少IdtConfiguration.h声明机械修正；复build退出0，真实离屏退出1且仅一个预期Freeze/Pierce半高断言失败，其它身份/邻接/成员断言通过。红测日志由主会话保存在Build/ToolbarOrderExpansionValidation/button-presentation/red-offscreen-results.log。

## 唯一表现写入与合法例外

- BarButtonClass新增baseSize/baseUserVisible元数据，配置Load/注册只更新基准及成员；有效size/userVisible/hide/state由StateUpdate下的ApplyMainPresentationLocked一次计算应用。该入口遍历全部注册官方/组件以及稳定Divider、More、Redo、Pierce，覆盖A1/B/A2与更多中的同实例。
- A2所有几何每次按一致工具、内容、场景、模式、Freeze、历史与fold/More捕获投影，文字缓存仅用于内容；Remove外部IdtPlug-in三个UpdateDrawButtonStyle直调用，保留原UpdateRendering。
- PresetHoming只归位面板，无主栏有效尺寸/显隐写入；面板Close与轻请求在presentation锁外。More Selected不再由RenderLoop第二写入。
- Recall业务事件/拒绝点击/墙钟结算只修改RecallState与deadline，事件锁外UpdateRendering(false)；灰色Recall仍可点击延期，不能设通用Disable。三秒保护、点击前事实、popup身份和空页Selection优先隐藏保持。
- 构造默认/首次注册、独立分页与属性按钮、hover/emph/pressScale/动画SetTar是合法原owner，不迁入主栏投影。

## 一帧事实、锁与动画

- UpdateRendering(true)保留原static序列化StateUpdate/Thickness；false绕过static锁及presentation锁，仍执行有限动作封口/Notify/RenderPipeline请求，避免Draw3回调或Close通知等待状态owner。
- StateUpdate先在锁外PrepareMainPanels/PresetHoming，再进入UpdateMainPresentation短锁；Load在presentationMutex内更改基准+整段列表+同一ApplyMainPresentationLocked，迁移Write在unlock后进行。预设未初始化入口检查Draw指针后安全返回。
- RenderFrame在工具帧快照后、Submit前AdvanceRecallChromeIdle仅结算已发布state/deadline，不再Refresh事实；随后锁外PrepareMainPanels。主栏网格段短锁单次UpdateMainPresentation读取最新历史并发布RecallChromePresentation，从同一有效值抓身份/size/visible序列、算宽度/判侧/批次/SetTar/隐藏锚点；在AnchorHidden结束后、FitInitial/初置/停靠之前unlock。
- no-arg RecallChromeView只返回已发布view，供本帧颜色/弹窗共用，不重新Refresh或拼实时工具门禁。新事实若在结算之后改变目标，按原Availability策略建立新pending期限。命令入口仍用点击前真实事实判断资格。
- presentation锁内无D2D绘制/呈现、Window Service或SetWindowPos/同步owner等待。内容target加载resource、历史timer装卸和ClosePenTypeMenu->CloseAnnotationTooltip的KillTimer是现有非owner等待调用；Recall/Popup Close不在该锁内。
- geometry指纹精确序列比较，含隐藏成员身份/顺序、有效size/IsVisible；不含Selected/Disable、文本/颜色、hover/press/当前动画val。等宽变化也加入原mainBarLayoutChange，保留CanJoin、后半程重开、曲线与dirty；finite DTO/wire未改。

## 新增回归范围与边界

- 真Bar注册/Load/旧开关：0/1/2/3/4/2/1/0组件数量、合并/分离、空/有内容、Selection/Pen/Shape/Eraser/ClickThrough、desktop/PPT/Whiteboard，共480例；容量/overflow/稳定身份/重Load/重复刷新及配置基准保持。
- 同一生产MainLayoutSnapshot的等宽split/full和成员Swap失效、hover/press/color不失效；尺寸投影不改动画current，真实timeline/SetTar helper的零dt、早期可加入/后半程从current重开、120帧重复刷新后几何/内容收敛。
- 真墙钟Poll/Advance、早到、大dt、灰色OpenRecall命令延期和旧唤醒、Dismiss/绘制/工作区抑制、Load交错、有hold/无hold期限、到期不直接改有效字段、统一入口最终发布；已有空页Selection门禁/排序/隐藏锚点/第三光像素回归继续保留。
- Headless生产源码单写入口/到期宽度前/fingerprint静态合同更新，仅为实际Bar回归补充。
- 未启用Host/HWND/GUI：注入策略Accepted不代表真实产品Publish Accepted；timer测试不代表系统WM_TIMER消息投递；timeline/helper不代表运行整个私有RenderLoop。真实组件设置、鼠标/触摸、动画/PPT视觉仍人工待验。

## 验证责任

实施代理完成diff --check、八个修改源码原UTF8/BOM/CRLF核对和全写入口静态库存。未执行完整构建，避免与主会话PDB/产物冲突；冻结后由主会话统一ARM64 solution、真实Bar离屏、Headless与state-mode-production回归，Trellis检查可自修必要问题，实际结果在../validation-button-presentation.md。
无commit/push/任务结束/GUI/CU。

## 检查后最终接线与验证

独立检查修正了旧颜色拾取/粗细滑块关闭会SendMessage取消capture的两个锁内调用：同帧range/可用性与关闭判断移至grid锁前。新增frame RecallChromeView/anchorVisible在projection后锁内捕获，主栏颜色和RecallAttribute::Advance显式传递；无参getter保留给其他读取，不在frame popup重读。对应真实Advance跨全局新view回归与源码锁外合同加入。最终完整solution、Bar、Headless和state-mode CLI均退出0；详细结果及人工边界见../validation-button-presentation.md。
