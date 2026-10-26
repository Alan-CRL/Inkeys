# Animation check review

日期：2026-10-24。已按最新PRD/design、10条check.jsonl及最新“主栏与控件静止字号及连续动画目标”code-spec审查。旧逐帧floor结论不适用于本轮。无GUI/Computer Use、commit/push；构建和耗时回归由主会话执行。

## Findings (fixed)

### 底栏direct-hold字体目标必须消费已解析抓手

- 文件：`Bar.RenderLoop.cpp`、`Bar.BottomDock.h`、`InkeysHeadlessTests/bar_bottom_dock_tests.cpp`。
- 问题：新字体body目标原先用`frame.bottomDockElasticOffsetDip`，但实际bodyMapping由render owner按真实屏幕抓手与本帧中心重新求得的`bottomDockSpring.positionDip`生成。输入tracker值在捕获/重基准时不保证等于这个已解析值。
- 具体反例：主体高80DIP，旧输入offset=5DIP而真实抓手解析offset=6DIP，13px字体原错误目标T=12.1875，c=12/12.1875；应用到实际12.025px轨迹后静止为11.84px，未落整数12px。
- 修复：增加小型生产/测试共用`ResolveBarBottomDockFontTargetScale(resolvedMapping,directHold)`。direct-hold只读mapping中已解析的rigidOverlayTranslationYDip（即Resolve返回的elasticOffset）；捕获下端点独立弹簧目标仍为0，不把其中间值当新字号目标；释放/恢复目标为1。RenderLoop只传实际mapping，去掉未重基准的frame输入offset。
- 回归：既有BottomDock Headless中加入0.6/1/1.5/2倍率下的真实抓手解析与上述5→6DIP反例；验证hold最终整数、stale输入确为不同错误结果、capture弹簧不污染终点及释放归1。测试直接调用生产目标策略及生产c目标helper，未复制私有目标选择实现。
- 此次目标修复只改上述3份源码，另补PageControl测试断言，保留UTF-8/BOM/CRLF；diff静态检查通过。复编与运行结果由主会话记录。

### PageControl字体独立续帧回归补齐基线idle断言

- 文件：`PageControl.cpp::RunOffscreenTests`新增fontCallback块。
- 原初始90帧settle循环未断言已Idle，其他raw动画可能掩蔽字体独立续帧。
- 已在本轮构建结束后保存baselineIdle并在政策翻转前Check；只有先成功idle，再出现多帧Continue并归idle，回归才能通过。

## Findings (not fixed)

目前没有待处理的局部源码问题；自修后的编译及回归结果由主会话确认。

## 代码路径核对

- `BarUiFontCorrectionClass`固定持有生产BarUiValueClass，不改变raw size.val/tar/start/middle/曲线。只在T/政策真正变化时计算目标；SetTar幂等。重定向保留当前动画c，同frame serial不再Advance；T=0冻结当前有效c并使下个有效T重新计算，0<T<1目标为0。禁动画及明确forceReplace直接落目标，dt=0由既有动画语义保留当前值。
- 每word固定c，dock使用bottomDockIndicatorWord，FPS用独立direct样式c，细刻度共用固定样式c；没有无界指针或tick map。格式/字体资源缓存失效不重置这些连续状态；失败重试沿原raw候选动画语义，不新增rollback或把候选值称为已上屏值。
- 主栏在BeginFontFrame清本帧active前捕获fontPending，纳入unclassifiedDamagePending和visual retry，因此c单独在途仍能越过绘制前ShouldPresent及dirty cull；落定后停止保守全脏并恢复idle。Scene同样保留上一帧fontPending进本帧active/FullDamage；PresentScene绘制后再合并新启动的font active到continueRendering。
- PageControl在bounds缓存短路前交接AnimatedBounds.targetScale，Scene setter立即更新renderer目标；Render时SetPose再按最新目标scale与目标pose组合，覆盖同now的Advance→SetBounds→Render顺序。Scene菜单/主题按父子targetPose组合；Eraser/Recall对预缩放SetDirect字号另存nominal×targetPose。
- 目标向量独立组合，不再用FLOAT当前矩阵乘double目标/当前比值抵消。通用/固定按钮在套当前矩阵前建立目标scope；主栏body/base/grip/rigid、手工按压、tooltip、预览popup及dock旁同步目标，作用域退出恢复metadata。纯X变形不进入字号高度，细刻度不重复乘面板scale。
- FPS明确提供nominalTarget=12.0，不从currentfont/currentZoom反推常量；dock明确传设计字号并使用源progress.tar，内容中点只影响当前轨迹。MeasureText继续保留原上界，设置字体渲染不变。

## 新增回归审阅

- Headless使用实际生产raw size动画和实际c类，检查13→15DIP/0.6倍率的7→9px端点、中间小数、末帧到idle一致、零dt中途改目标、重复目标和同帧重复采样、raw两段metadata保留、T=0、禁动画及forceReplace。
- D2D离屏通过真实DrawBarButtonVisual和实际FLOAT矩阵检查13px释放目标：当前press取1/.95/.96/.99/1.01/.9/1等样本，c目标保持1，能捕捉先前13变12的误floor。按压保持12px/释放13px、同帧双draw只推进一次、内容中点与Back过冲不重定向均有断言。
- 真实PageControl无HWND回调验证Continue、多次提交及最终Idle；已补baselineIdle断言，确认其他原始动画结束之后才翻转字体政策。
- 原有真实DWrite细刻度布局/缓存及DPI/PIXELS/非均匀/旋转剪切测试继续覆盖静止解析。当前新增专用连续轨迹主要覆盖raw size、真实button和PageControl政策尾段；Eraser/Recall/多层pose、popup及FPS metadata是源码路径核对，不将其称为各自均已有新增专用轨迹用例。

## Verification

- 独立运行`git diff --ignore-submodules=all --check`：通过。
- 独立运行i18n check：en-US/zh-TW各437/437，exit 0。
- 编码/BOM/CRLF检查沿用本任务约束，局部自修按原字节换行写入；最终由主会话再次核对全部变更。
- 主会话报告局部自修前完整solution已exit 0；自修后构建/Headless/离屏结果待主会话，不在此提前宣称通过。

## 编译反馈后的局部修正

首次helper自修把返回列表中的elasticOffset参数误当成同名结构成员，主会话编译报C2039。现已重新读取实际BarBottomDockVerticalMapping定义及Resolve返回列表：末成员rigidOverlayTranslationYDip存储该解析偏移。只修引用，不新增/重命名字段或改变ABI；同时补上述baselineIdle断言。已再次核对生产调用只传实际bottomDockMapping，原几何/抓手生成无变动，diff静态检查通过。修后结果待完整复编及运行，不提前宣称通过。
