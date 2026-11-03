# Setting 页面动效、滚动布局和材质连续性

## 1. Scope / Trigger

修改设置页面入场、条件条目显隐、标题栏开屏可见性、正文滚动或resize/背景材质时适用。仍由唯一共享渲染线程处理ImGui/D3D，HWND线程处理原生框架；不改变字体、20语索引、设备/交换链模式、配置归属或Win7系统能力。

## 2. Signatures

- `ConfigureRowAnimations(delta, enabled, rate)`：现有设置时钟和全局动画偏好；禁用时当前动效立即归位。
- `ConfigurePageMotion(PageMotionState&, uint64_t routeKey, bool opening, bool reset=false, uint64_t layoutRevision=0, bool exiting=false)`：实际route/subpage状态，正文Begin之前调用。
- `BeginPageMotion(state)` / `EndPageMotion()`：实际正文owner范围，不保留跨帧裸窗口指针。
- `PageScrollbarAlpha(state)`、`BeginScrollChild(..., resetToTop, settleScrollbarLayout, stableBodyLayout=false, visualAlpha=1, PageMotionState* motion=nullptr)`：正文使用稳定布局，其他宿主默认行为保持。
- `PageRoute{page,subpage}` / `PageTransitionState::Resolve` / `Opacity()` / `Holding()`：目标与实际绘制路由分离，旧页125ms渐隐后一次提交最新目标；仅活动子页参与身份比较。
- `SuppressDepartingPageInput(state)`：按旧正文owner清理Active/Nav/hover及待消费scroll，不清空全局输入，不改可用控件配色。
- `PageBlockScope(id)` / `Finish()`：标准行/卡片/文本自动接入，自绘hero/网格等显式接入；不重复对嵌套内容变换。
- `ApplyGroupOpacity(window, opacity)`、`ImGui_ImplDX11_OpacityGroupBegin/End`、`ImGui_ImplDX11_SupportsOpacityGroups()`：块/条件块/旧页透明度的唯一出口；backend哨兵之间的命令按整块合成。
- `ConditionalBlock(id, visible)`、`operator bool()`、`RetainText(key,current)`：真实占位和退出文案，body只能包含纯UI及受真实控件事件守卫的动作。
- `HomeEntryChromeVisible(HomeMotionState)`、`ResumeSkippedPageOpening()`：字标遮盖层结束后恢复四项标题栏入口；几何跳过与用户取消区分。
- `SessionState::QueueResize`、`ResizeSettingPresentation`、`SettingBackdropState::Apply`：复用现有serial/资源/窗口owner边界，不另造队列。

## 3. Contracts

- 每次真实页面/子页/返回播放，重复当前页不重播，各页原有scroll保留。路由、条件块和窗口ID不依赖翻译文案。字体/语言/DPI/resize只影响布局，不算导航。产品传入I18n::revision，atlas/字体/倍率/正文尺寸失效使旧预测高度落定并重测，不改变导航generation。
- 速率1时旧页125ms smoothstep渐隐，完全消失后才提交新页。新块上浮12 DIP、位置310ms三次ease-out、透明度450ms smoothstep，约27ms顺序且首末延迟最多108ms；最后位置418ms落定而alpha到558ms才完全恢复，再用110ms渐显thumb。单独的PageMotionTiming为入场时序权威，Exit引用PageTransitionState::Duration；沿用现有开关/速率，不叠加旧整页contentReveal。
- 同一物理排一起、分区标题随第一项，按当前viewport内可见内容排序。短入场阶段按稳定window/块ID保留位置/alpha各自单调上界，异步插入不得让后项倒退；完成后清空有界记录，稳定页不逐帧分配，离屏行数不拖长总时长。
- 离页/快速返回时冻结当前块姿态再组合整页opacity，不先跳成完整终态。旧页正文switch每帧只运行一次，淡出时保持实际旧page/subpage，不能立即换新页再假装旧页淡出。最新请求替换pending而不重启淡出；回到仍显示的源页从当前opacity反向。仅inner正文内容及轨道组合alpha各一次，背景/导航/标题栏保持稳定；初次Show、字标开屏、reset与禁动画直接提交。
- 退出正文使用NoInputs及owner输入屏障，不用BeginDisabled使正常控件先跳成灰色；嵌套child不继承全部NoInputs，须清理NewFrame遗留的旧hover/nav/scroll，侧栏和标题栏保持可用。
- 整块透明度：块内（含本帧child树）先按原图元/材质alpha合成，再整体乘透明度一次，禁止逐顶点乘alpha或单改toggle配色。UI侧在位移完成后把child列表按ImGui提交顺序并入块所在DrawList、清空原child列表，并在真实IdxOffset边界插入Begin/End哨兵；同范围条件透明度乘入所属块，嵌套条件块/旧页淡出形成嵌套组。opacity为1不插哨兵（普通命令、可被FrameSubmission去重），为0或无合成backend/分通道时退回逐顶点（0时与整块等价）。DX11 backend按同一列表顺序配对，求顶点外接框∩scissor的整像素矩形；每层嵌套一张与目标同尺寸RGBA8离屏层，只清零并合成该矩形，组内保持SRC_ALPHA/INV_SRC_ALPHA得到预乘RGBA，合成用默认PS、颜色(o,o,o,o)、最近点采样及ONE/INV_SRC_ALPHA；全部四边形一次上传。资源随InvalidateDeviceObjects释放、尺寸变化重建、空闲2s归还；创建/Map失败经LastResult拒绝Present；中途失败恢复调用方RTV，异常哨兵退回普通绘制而不丢内容。
- 动效只变换该块新提交的顶点与本帧child绘制，不改变终态ItemSize和业务事件身份；首个正文点击/滚轮/对应键盘触摸操作在items提交前落定并正常执行原事件。取消区域覆盖完整正文宿主（含原生轨道），不能只用排除scrollbar的内容ClipRect。
- 字标开屏总长1.98s（等待.14/书写.90/停留.32/归位.44/落定.18，HomeEntryTiming为唯一权威），快照/恢复合同不变，完成或用户取消不追加页面入场。未播放字标或导航返回主页使用普通入场；首次真实布局因包络不安全Skipped时可切回普通入场，不能先闪终态再盖住。四项版本/主题/反馈/调测在准备及遮盖层淡退期间无绘制和可激活item，原生命中快照同源；旧在途帧不得覆盖新Hide→Show的命中屏障。
- 正文轨道完整8 DIP、padding2 DIP，与thumb alpha分离；使用既有右侧gutter补偿，在任何item提交之前统一InnerRect、WorkRect/ContentRegionRect与ScrollMax预算，重基准旧横向scroll；不能只隐藏横条却留下8DIP隐式水平滚动。真实是否溢出仍由native决定，短页无虚假thumb或额外空白列；导航、Card/Details、flyout、Combo默认轨道动画不无关改动。
- 条件块初次按真实条件初始化；后续显隐约180ms，首个目标变化帧以dt0登记并测高，后续在正文Begin前预推进已有条件，以真实测高加嵌套增量预测本帧总高；占位采用统一物理像素取整，viewport比较使用ImGui实际截断后的尺寸，不能把187.5%下1312.5→1312误判成每帧resize。真实插值高度推移后项，反向从当前值续接，退出/未展开内容禁交互。退出保留必要文案，不保留已失效的业务权限；无条件副作用不能置于退出body。零高度末帧、嵌套高度及间距只计一次；纯if显隐不额外引入BeginGroup/EndGroup，避免完整LastItemData.Rect和额外ItemSpacing污染父级测高。
- 条件裁剪按真实IdxOffset分界，必要时拆分合并的ImDrawCmd，不能按旧命令数量误裁前块。变化在viewport上方时保持稳定内容锚点；真实贴底意图跨布局修订保留，滚动/拖动优先，不能仅帧末SetScrollY而让下一帧AutoResize追赶。
- 色彩插值保留两端alpha权重；不能把每帧取整U32重新当作下一步起点。以起点/目标/时间求当前色，并在有限时间到达准确端点，避免高刷新率残留alpha；正常选中态保留。
- 同尺寸resize不新增serial；已有有效同尺寸RTV复用。缺RTV、未知实际尺寸、失败及新epoch仍重试，旧serial完成不消费更新目标；Size持续渲染，Move原有Idle保持。布局优化不能触发普通resize的字体/图片重建。
- HWND owner仅缓存成功应用的材质；等价请求无disable/re-enable，主题直接更新既有accent。真实composition/能力/高对比度/销毁仍失效重验，失败撤销并走Solid，可重试。帧内backdrop/palette/clear使用一致冻结快照，几何边框/圆角继续原路径，不关闭系统动画掩盖问题。

## 4. Validation & Error Matrix

| 边界 | 必须结果 |
| --- | --- |
| 快速导航/同页点击/返回 | 最新目标唯一，scroll保留，不重复动作或拖长离屏顺序 |
| 开屏准备/淡退/结束/取消/几何跳过 | 四项绘制、ImGui输入与Win32命中一致；取消帧保留原防点穿屏障 |
| 条件反向/归零/上方变化/贴底/阈值 | 占位与总高度连续，锚点稳定，clip不污染其他块，不丢贴底或覆盖用户滚动 |
| 语言/字体/DPI/epoch/Hide | 不复用陈旧测量/绘制指针/UV，不补播旧动效，字体恢复保持现有事务 |
| resize失败/缺RTV/新epoch | 不把失败缓存为完成，同目标可重试，最终尺寸准确 |
| composition/材质失败 | 真实失效重验，失败走既有Solid，不假发布新状态 |
| 整块透明度：深色toggle、半透明目标、嵌套组、设备对象失效 | 与“无块背景/完整块”逐通道插值误差≤2；嵌套先合成内组；不透明帧无哨兵；重建后像素一致 |

## 5. Good / Base / Bad Cases

- Good：新长页首帧正文已是最终宽度，thumb稍后渐显；点实际按钮立即落定且只产生一次动作；条件退出仍保留可见文字，但不能触发恢复/保存操作。
- Base：全局动画关闭即刻到位；Win7/8/8.1仍不透明，Win10/11仍统一BlurBehind；字标及20语字体机制保持。
- Bad：逐顶点乘块透明度让toggle轨道透过滑块；只在父列表插Begin/End而child列表仍在组后绘制；用straight-alpha混合离屏预乘结果造成暗边；用0-alpha但可点击的标题栏/轨道隐藏入口；用宽度补间挤压正文；同帧重复渲染业务switch做测量；静止贴底时只发晚到的scroll target；用普通RGBA插值替代原alpha权重；把WARP回调计时外推为Win7/DXGI性能。

## 6. Tests Required

完整原生ARM64 MSBuild构建`InkeysRepo.sln Debug|ARM64`和Headless `--no-window`。真实页面/控件/DrawData回放验证真实旧页保持至退出、latest目标/返回反向、位置先落定而alpha仍有尾段、旧正文禁操作但不变灰及新页Tab可达，以及alpha、位置、clip、width、真实命中、业务次数、保存scroll、条件嵌套/反向/归零/上方锚点及贴底。颜色覆盖Solid/半透明、浅深和30/60/120/240Hz，透明端点alpha必须为0。资源/材质测试经过生产事务，不能只复制算法；整块透明度由RunSettingHomeWarpProbe中的真实backend像素回放验证（浅/深×不透明/半透明目标，opacity .1/.2/.5/.9，嵌套.5×.5，并强制旧逐顶点路径证明可检出透出）；开屏测试时刻用HomeEntryPhaseTime按阶段映射，不能硬编码旧总时长；同时保留i18n资源/格式化和字体生命周期测试。

`INKEYS_SETTING_MOTION_ONLY=1`配合`--no-window --setting-home-only`用于定向页面/条件回归，不替代完整集成；可再设`INKEYS_SETTING_MOTION_SNAPSHOTS=1`用真实DX11/WARP保存入场、插入/移除及普通首页动画样张，无HWND。真实WARP离屏只证明渲染及资源路径，替代回调的Texture2D/RTV计数不证明实际DXGI ResizeBuffers、DWM或Win7卡顿已修好。无GUI授权不启动窗口；真实Win7 resize及Win10/11最大化/还原材质观感须单列人工验收结果。

## 7. Wrong vs Correct

```cpp
// Wrong：条件消失立即跳过所有绘制，后项瞬移；反向也丢掉中间状态。
if (show) RenderRow();

// Correct：只保留纯UI的退出外观；原业务动作仍由真实可交互事件守卫。
if (ConditionalBlock block{"##stable-visibility", show}; block) {
    if (RenderRow()) QueueExistingAction();
}
```

```cpp
// Wrong：逐帧用取整后的当前色作为下一步起点，可能永远差几个alpha单位。
current = LerpColorU32(current, target, perFrameFraction);

// Correct：从固定起点按累计时间求当前色；保留原alpha权重并有限到达目标。
current = LerpColorU32(start, target, progressFromElapsed);
```
