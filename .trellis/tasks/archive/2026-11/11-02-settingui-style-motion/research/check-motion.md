# 全范围动效检查及修正

检查范围：R1-R7的产品接入、条件布局、顶点和子窗口裁剪、滚动/输入、会话资源及材质事务。已读取PRD/design/implement、check.jsonl的六份真实上下文和trellis-check。初轮静态检查没有把平台推断当作实机证据；无GUI、无提交。

## 已复现并修复：A7底部锚点丢失

初始代码仅在EndPageMotion后SetScrollY；正文内层AutoResizeY的测高落后一帧，下一帧便不再被认为贴底。真实生产Controls.cpp + ImGui/ImFluent的独立ARM64无窗口回放：12个前置真实ButtonRow、底部两个条件Row及Footer。初始scroll=max=684，footer=272px；插入后footer依次311/343/...396px，最终scroll716、max840，15个过渡帧全失败。日志 `Build/settingui-motion-review/bottom-baseline.log`。

修正：ConfigurePageMotion在正文Begin前推进已登记的条件tween，首次目标变化帧以dt0登记目标并测量。父组用原测高加子组实际占位增量预测本帧高度；条件预测和实际高度用相同物理像素取整。正文Begin前以已提交真实内容高度+预测增量设置native ContentSize，保留贴底意图并设置本帧原生scroll目标；EndPageMotion仅更新当前实际子窗口测量，不靠晚到的scroll或全页顶点平移。用户滚动/拖动优先，中部上方条件变化仍复用原局部锚点补偿。

修正后同一回放插入和移除共30帧Footer均272px，退出归零后scroll=max=684。日志 `bottom-and-rail-pixel-fixed.log`，exit0。这是实际生产算法和真实CPU DrawData/布局回放，不是复制高度算法的模拟测试。

## 已复现并修复：不可见原生轨道接受输入

原state.bounds取ClipRect，排除了native滚动条。将其他修正保留，仅把正文取消范围恢复旧ClipRect进行对照：轨道按下时elapsed仍在入场、alpha=0，而原生scroll从684变为324，exit1。日志 `invisible-rail-baseline.log`。

改为正文宿主完整Rect；ConfigurePageMotion在原生Begin前响应真实轨道按下，原生处理该次输入时elapsed=.46、alpha=1，不吞掉或合成重放。修正后相同真实输入回放通过。重复导航键也纳入取消，防止按住方向键重复时移动焦点但入场不停。

## 已复现并修复：异步插入使入场倒退

PageBlockScope原先忽略传入id，每帧重新计算可见顺序。条件块在前方异步出现时，后项延迟增加24ms，但时钟只前进16.7ms，会使既有卡片alpha降低/位置倒退。真实ButtonRow/ConditionalBlock回放在基线捕获1次逆转，修正后0次。`async-and-rail-baseline.log` exit1；`all-fixed.log` exit0，同时覆盖底部插入/移除和不可见轨道输入。

修正只在前340ms入场期间为当前可见块按稳定window+scope ID保留alpha的单调上界；重复的通用标题/text ID使用各自出现序号。导航和结束时清理，不在稳定页面逐帧分配或保存无限历史。

## 缓存及生命周期

- BeginScrollChild最后新增可选PageMotionState*参数，仅当前调用借用；不保存“已准备页面”的跨帧裸状态指针。
- 缓存只保留实际内容高度、owner稳定ID、viewport和字体/atlas身份值、倍率、语言布局revision；不缓存字形/UV。
- body宽高、字体/atlas身份、倍率或显式layoutRevision变化使旧高度预测失效并稳定归位，导航generation不变。生产调用传I18n::revision以覆盖同字体语言热切换。
- 默认参数保持其他滚动宿主及未开启页面scope的现有测试行为。

## 持久化回归

在CheckPageAndConditionalMotion扩展真实fixture：底部可见插入/移除、嵌套条件收起（不重复计数）、宽度与倍率/语言revision失效（不重播）、不可见native轨道真实按下、按住导航键重复、异步前置块插入的后项alpha不倒退。保持原中部上方锚点、快速反向、真实操作次数和页面入场测试；100%及187.5%运行。新增断言未放宽原锚点容差。

独立probe编译和执行结果通过；完整Solution与集成fixture重建由主代理在源文件释放后完成。实际Win7 resize和Win10/11最大化模糊仍必须单列人工验收，WARP Texture/RTV替代回调计时不能证明DXGI/DWM实机性能。

## 规范同步提醒

setting-motion.md现有R7合同已描述贴底意图与Begin前处理。需更新两个签名：ConfigurePageMotion增加最后layoutRevision=0；BeginScrollChild增加最后motion=nullptr，并补充条件首帧dt0测高、真实测高+预测增量、物理像素取整、短暂稳定ID单调alpha及布局失效不重播。由主代理统一修改规范，避免并发写同文件。

## 第二轮真实集成发现与最终验证

- 完整新fixture在真实字体/187.5%下暴露尺寸比较问题：请求1312.5px会被ImGui截为1312px，旧比较把每帧当作新resize并反复清除条件预测。比较改为同一ImTrunc规则，不放宽锚点容差。
- 嵌套条件被额外BeginGroup/EndGroup包裹时，EndGroup会读取完整旧LastItemData.Rect并增加ItemSpacing，导致父组仍测到旧高度。条件分支原本不需要Group，移除包装后按真实游标占位测量，恢复原先纯if布局间距并使父子预测一致。
- 另经实际水平滚轮复现R2错误：仅补偿WorkRect时，长正文ContentSize.x652、InnerRect692、padding24，ScrollMax.x仍为8，MouseWheelH使Scroll.x0→8；日志horizontal-before.log。最终在Begin前声明当前正文宽度预算，native InnerRect/ScrollMax与同一gutter预算一致，旧尺寸宽度不再遗留一帧横向范围；其他宿主不变。
- 新增每帧ScrollMax.x/Scroll.x/ScrollbarX断言及真实水平滚轮事件。

完整原生ARM64 `InkeysRepo.sln Debug|ARM64` 构建成功（build-final.log，exit0）；实际`INKEYS_SETTING_MOTION_ONLY=1`的Headless `--no-window --setting-home-only`通过，两倍率各405帧、真实操作各一次；本次未设置INKEYS_SETTING_MOTION_SNAPSHOTS，因此未重跑20语WARP截图分支，需由主代理最终运行；targeted-final.stderr.log为空，targeted-final.log exit0。临时精确定位打印已从源码移除；主代理将进行最终全范围重建/验证。
