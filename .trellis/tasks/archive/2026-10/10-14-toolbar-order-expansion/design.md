# 技术设计

## 配置与UI边界
Settings代理拥有Other.Config.cpp/.cppm、Setting.cpp、三语JSONC、生成头/快照、config_metadata_tests和设置无HWND回放。两字段固定名称IconOrder(0)、ExpandDirectionAdjustment(1)，严格JSON整数范围校验，NoUpload。
设置直接更新原子字段（只有变化），barUISet.UpdateRendering(false)唤醒而不切工具或同步Draw3，然后QueueConfigWrite冻结保存；core在同一渲染frame捕获配置并识别目标变化。
中文名称按PRD；繁体主欄圖示順序/主欄展開方向調整，英文Main toolbar icon order/Main toolbar expansion direction。选项对应Follow main button/Follow main button (reverse)/Left to right/Right to left；Screen centerline/When space is insufficient。

## 布局核心
Core代理拥有Bar代码、必要新增纯策略头和现有布局/底部停靠/离屏测试。使用内部枚举与纯helper；IconOrder规范化0..3，Direction0..1默认1。镜像X表：follow=!opensRight、reverse=opensRight、LTR=false、RTL=true，始终正序迭代且y不镜像。
在渲染布局提交中先计算完整稳定目标宽度，再选择侧，再建立统一布局/换边批次，不能因PositionUpdate旧中轴逻辑在其他frame入口把新侧覆盖。
新方向模式保留当前侧，当前侧不足且对侧可完整容纳才切；静态目标包络按像素外扩取整判断，排除pulse/动画Back超调。两侧不足保持原侧，无Root平移或自动缩放。首次方向依既有startup初始化，不以未初始化0坐标判断；fold使用完整目标宽度。
居中与白板专用锁优先；普通非居中dock适用新策略。上下PositionUpdate行为不改。排序独立变化触发布局批次和damage，不能因width/side没变漏刷新；保留隐藏Pierce/Recall等锚点与hit过滤。
检查现有finite稳定签名/根批次/缓存依赖，把新增排序/策略变化计入正确几何版本，不破坏既有协议。

## 主会话边界
主会话拥有task/spec与统一build/test；两代理不修改对方代码、不撤销他人变动。冻结后trellis-check全范围，必要局部机械修复。禁止GUI/push/task结束；commit仅按下文用户的具体授权执行。

## 延续修复边界

调查第三光源生产输入、拖动move/up/cancel到render的状态交接、屏幕/窗口/DIP坐标以及释放时几何更新；只修已确认的交接问题，不重构光源或主栏架构。优先复用已有生产入口和离屏/纯策略回归，测试不得用替代公式隐藏生产链错误。主会话拥有任务/spec与统一构建，实施代理拥有本次必要Bar源码/回归；冻结后交由Trellis检查。

## 第三光源坐标交接设计（源码研究后）

输入继续在原锁内发布物理screenPoint/serial/available，保留现有缓存与唯一生命周期。PrepareLightingAndDemand显式传入本帧layoutScreenOrigin（monitorOrigin + bottomDockFrameTransitionTranslation，来自同一稳定偶数tuple）；真实PrepareFrameLighting以该原点换算screenPoint得到物理像素局部点，不另除zoom、不读live或最后成功呈现translation。保留既有FinishLightingFrame旧新局部光源比较与damage链、主体/刚性光影逆映射，分页继续使用原共享屏幕点/半径/强度。普通stale presentation只检查既有合同，不扩大修复呈现策略。

在已有Bar真实离屏入口添加受保护数据的派生fixture，固定screenPoint和serial，在translation清零及根几何吸收前后调用真实Prepare；先在旧实现观测失败，再修正后复验。覆盖负坐标、zoom、残差、静止连续帧/取消和原门禁，共享屏幕光照保持；可行时补隔离第一光的D2D像素连续性。

## 主栏按钮统一表现边界

先库存主栏按钮运行时尺寸/显隐直接写入与组件Load调用链，单独核对Recall短期历史保留和计时器。复用现有主栏状态更新与渲染布局机制建立唯一派生计算/应用入口；配置/构造基础值可独立初始化，业务事件不得覆盖运行时派生结果。界面与渲染调用不能引入锁内重入/同步窗口等待。研究确认后补具体数据分层和触点。主会话拥有任务/规范/统一构建，实施与检查由Trellis代理完成；不改既有按压/hover/动画所有权或非主栏架构。

研究已确认组件实际入口为SyncUi3BuiltInComponents -> SyncLegacyExtensionButtons -> Load，Load写回配置尺寸后，A2业务styleKey缓存跳过再次投影。Recall事件与渲染idle也会在统一StateUpdate外应用hide/size，且idle晚于目标宽度扫描。统一派生必须绕过文字styleKey对几何的短路，并将保护期结算置于本帧presentation/目标宽度之前；保留当前3秒保护和灰色Recall点击延期语义，不能以通用Disable阻断。等宽尺寸/显隐变化也须通过几何指纹进入统一布局批次；hover/按压/纯颜色状态不属于该几何指纹。

已读两份研究，实施优先复用StateUpdate建立一个主栏计算/应用入口：注册/配置默认是独立基值，Load只改成员/基值，几何覆盖不受内容styleKey跳过。所有Recall事件只更改state/deadline后锁外Request(false)，渲染在同帧工具快照后、宽度扫描前结算到期并应用唯一投影，保持UpdateRendering既有串行锁边界。普通按钮/A2/Recall/More业务状态归同一入口；非主栏、按压/hover与SetTar保持原owner。布局指纹包含稳定成员顺序/身份、有效size/IsVisible（含隐藏成员影响），不包含颜色/文字/hover/Selected/Disable/pressScale/动画val；沿既有等宽非扩张曲线和换边批次衔接。组件列表重建与投影需一致化，避免Load期间覆写已显示有效值，且不能在未初始化预设或recall锁内重入全局StateUpdate。

具体接线：StateUpdate先锁外PrepareMainPanels，再短锁UpdateMainPresentation；Load在同一presentationMutex中更新baseSize/baseUserVisible与完整成员列表后ApplyMainPresentationLocked。UpdateRendering(true)保留static序列化，false绕过static和presentation锁，仅封口/通知请求。渲染在Submit之前AdvanceIdle只结算已发布Recall state/deadline，再锁外归位；网格段单次投影刷新历史事实并发布RecallChromePresentation，随后同锁抓MainLayoutSnapshot并提交目标，在隐藏锚点后、FitInitial/初置/停靠之前释放。no-arg RecallChromeView纯读本次发布view；新的真实事实改变目标则按既有保护策略建立新pending，不重复刷新同帧历史。

独立检查收紧：颜色拾取/粗细滑块不可用时取消capture可能SendMessage等待窗口owner，整体预处理移到presentation锁之前；主栏锁内只扫描/投影/目标提交。Recall的本帧view与对应anchorVisible在统一投影后锁内捕获，颜色和RecallAttribute::Advance显式消费同份帧值；解锁后的其他StateUpdate/Load发布不能把popup身份拼到旧主栏目标上。仅收紧历史帧视图，不建立全UI不可变快照。
