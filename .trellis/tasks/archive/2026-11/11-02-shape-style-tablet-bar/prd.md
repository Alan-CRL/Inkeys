# 共享样式与数位板UI输入修复

## 目标与阶段

任务最初调查20261028a的形状属性和数位板点击问题。共享样式已实现、经用户确认生效并提交cbd2294d；R2详细诊断已完成并通过验证，Release x64测试包已交付。当前用户明确要求规划R2修复：五个UI窗采用一致的WM_TOUCH规则、各自独立状态，拒绝Pointer生成的兼容鼠标。用户已明确批准最新五套独立Touch状态方案并要求开始实现；R2功能现已实现，最终Debug ARM64构建、五个生产探针与完整headless回归通过；Release x64已构建并核对架构/诊断标识。2026-11-03追加旧版本日志后确认仍有未覆盖的Pointer+兼容Mouse且无可见Touch场景，分页还有输入入口退化风险；当前等待补充方案，不将自动验证通过视为设备兼容完成。

## 需求与证据

### 已完成共享样式（保留回归合同）

- R1a/R1b：硬笔/软笔/形状（实线、虚线、矩形）同一Brush组，颜色和完整宽度Selection完全共享，不采用进入复制。旧独立槽调查证据：IdtState.cpp调查基线:1585/1740/1805/2128，见research/shape-style.md。
- R3：Brush颜色Primary桌面+PPT、浅白板、深白板三份；浅首次现有红、深首次白，手改后按类别记忆恢复。
- R4：Brush/Highlighter/Laser各自全局Selection粗细，跨场景不回滚，保持Preset/ExplicitDip身份、double精度和显示Context规则。
- R5：Highlighter和Laser各自全局颜色，互不合并、不并入Brush，保留默认色和画法。
- R6/R7：工作区仍记工具身份、子类型/激光选择；活动笔划Down锁存，旧墨迹不变；不启用停用的跨进程启动恢复或新增颜色schema。原工作区恢复证据IdtState.cpp调查基线:1483/1267，见research/state-lifecycle.md；页主题证据见research/style-scope-and-whiteboard-theme.md。

### R2原方案已实现；追加输入场景待处理

- R2a：Bar及四个PPT/白板分页窗口各自拥有TouchContext，复用同一份WM_TOUCH解码/选择/生命周期规则；只有薄层跨窗转交，不合并成全局业务Touch状态；真实Mouse/触摸板及原可选Raw保留。所有带Pointer来源签名的兼容WM_MOUSE一致拒绝，不再以Pen/Touch低位分类分拆交互。
- R2b：Touch即使进入旧窗口，也按真实屏幕点与当前可输入的实际呈现目标接纳；一次Down有唯一业务owner，Move/Up锁存该owner；无UI目标不捕获/抢会话，不因按住移入UI而开始新点击。
- R2c：真实Up/Cancel、隐藏/销毁/注销、工作区/页语义失效、桌面/显示变更、capture取消、队列溢出完整退役相关路由/会话/捕获。旧代次不能影响新手势；不靠超时或全局按键猜Up，不伪造成功点击。
- R2d：PPT保持Down立即翻页及原开关控制的触摸长按重复，白板保持有效Up单次。Bar全部属性面板、拖栏/吸附、连续选色/FineDial和原遮挡语义保留。
- R2e：命中必须覆盖实际已呈现主体、圆角、菜单/浮窗与阻挡状态；透明包络不抢输入，隐藏/locked/不属于五窗的native目标不误执行UI；不向外部/画布转发合成输入。
- R2f：保留Bar可选笔触觉与原接触元数据，分页不新增振动。保留现有v1详细诊断，记录原接收窗与逻辑目标、route代次及首个取消/拒绝。
- R2g：保留Win7合同，不依赖Win8+Pointer业务API。用户选择统一Touch入口，本轮不添加仅产生兼容鼠标、完全无Touch的驱动回退；规划时两份设备日志均有Touch。新增第三份旧版日志无可见Touch，当前过滤会使Pointer-only路径无业务入口，此限制已成为明确待处理项，见research/additional-log-coverage.md。

## R2现场事实与定位

- 首次高漫M6/Win11日志653行：用户确认全程M6，存在完整Pointer→Touch→业务成功、22组右下Touch入Bar与27组Pointer无Touch接纳，原粒度不能判lease首因。见research/tablet-log-analysis.md与main-bar-diagnostics.md。
- 新详细日志来自另一块未具名数位板，文件仍叫M6-input.txt；8677行/3295110字节、x64 build Nov 2 2026 22:01:56。用户复现持续in-range悬浮跨窗后失效，离开感应范围再回来恢复。
- R2b证据：附件L5664-5681的Pointer到Bar、同点Touch却到左下并background_miss，主栏兼容鼠标被过滤。contact1263/1272/1273的Touch接收窗分别在该范围内保持Bar/左/右，而Pointer在三窗移动。
- R2a/R2c证据：L6174-6188和L7104-7117中Down 0xff51572d被接纳为Pen，Up 0xff5157ad被Touch过滤，旧capture/lease滞留，5+21次跨窗拒绝。Message.cppm:33/42已有“低位分类不稳定、仅诊断”合同，PageControl.cpp:2816/2863/2902违反。不是OS没发Up。
- 路由/恢复机制、HiMsg排除及完整线号见research/tablet-hover-root-cause.md、pen-hover-window-routing.md、tablet-v1-lease-log-audit.md。原日志已保存在ignored TestResults并校验hash；底层为何锚旧窗不归责于未经证实的驱动内部机制。
- 初始测试包版本与源码映射见research/version-baseline.md（调查起始HEAD6df7f43f、发布dc8d0a13），不把当前x64日志误标为20261028a二进制。

## 验收标准

已完成AC1-AC8（R1/R3-R7）：同组双向共享、Brush三范围记忆、三组全局宽度/颜色隔离、工具身份/过期提交、主题首笔同步/失败、保存/启动边界、完整ARM64与无窗口验证，见research/validation.md。R2诊断已通过同类检查与Release x64构建，见diagnostics-validation.md、release-x64-build.md。

修复阶段新增：
- T1 → R2a/b：五套窗口Touch状态互不覆盖，同一笔感应范围内，主栏↔左/右分页反复点击，不需OutOfRange才能恢复；接收Touch的旧窗不决定业务owner。
- T2 → R2a/c：72d/7ad及反向副本均被一致去重，不建独立Pen兼容会话；一次业务触发、一次终态收尾，无旧lease滞留和重复翻页。
- T3 → R2b/d：Hover不迁移正在按住的owner；按住跨窗仍由Down目标结束，PPT无Move长按持续、移出/Up/取消按原规则停止，白板Up一次；Bar拖动与属性手势不退化。
- T4 → R2e：全部Bar面板与Page菜单/主题的真实命中/遮挡、圆角和透明边界正确；外部/设置/画布native候选不抢占，可见locked背景不穿透到另一UI。
- T5 → R2c：SetCapture同线程重入、隐藏/销毁/注销/overflow、语义/显示变化正确取消和释放；旧route/capture/registration代次不结束新Down。
- T6 → R2a/c：primary/fallback、多点批次、不同hSource相同ID、ID复用/迟到终态按原互斥规则，无新增多指并发功能；Mouse/精密触摸板/Raw不可用仍可正常操作。
- T7 → R2f/g：Bar触觉凭据不丢、不阻UI，分页不加振动；无Pointer硬API依赖、无Hook/Draw3输入行为变化，保留可读诊断。
- T8：实际生产router/适配/worker确定性回归、完整Solution Debug|ARM64和headless通过；生成Release|x64供用户一次同路径实机验收，无窗口结果不冒充真机完成。

## 范围外与约束

不新增形状、手势/多点功能、全桌面命中引擎、跨进程恢复、分页震动或通用UI重构；不修改系统/驱动配置。保持minimal diff、中文关键注释、原BOM/CRLF。现有诊断脏改动、用户确认的共享样式、其他任务和两个CSO变更均不可覆盖/清理。默认不GUI/Computer Use、不commit/push。

## 规划状态

用户输入方式与范围已明确，无待用户决定的产品问题。当前修复设计见touch-fix-design.md，实施/风险与验证见touch-fix-implement.md；design.md/implement.md保留已完成阶段并指向当前方案，manifest配置到新研究材料。最终五套状态规划摘要已获用户明确批准，本次R2功能修复已实现，代码与自动验证记录见touch-fix-validation.md，设备复核步骤见touch-fix-capture.md；任务等待用户实机复核，不GUI、不commit/push。

## 追加日志审查（2026-11-03）

用户暂时无测试环境，提供D:/Personal/Downloads/M6-input.txt旧版日志。本轮仅做覆盖性检查，产品/测试/产物未改。分析见research/additional-log-coverage.md和独立check；Pointer-only缺口需补设计，保留原五窗独立状态与去重目标，不把现有Release x64标成已覆盖此设备。

## 用户最新调查优先级（2026-11-03）

用户确认追加旧日志来自另一块数位板，要求先调查WM_TOUCH为何全无，并提出Windows Ink可能关闭。先核应用注册/消息链与设备协议，不把关Ink当已证实，也不直接推进Pointer业务改造。结果见research/no-touch-cause-investigation.md及no-touch-application-path.md：注册调用存在但BOOL/IsTouchWindow未记录；完整PT_PEN并不等于驱动复选框状态，两份设备消息路径不同。产品/测试/产物未改，现场状态仍待后续可用环境补证据。

## 当前实施：补齐Touch注册诊断（2026-11-03，用户已授权）

用户要求补调试输出后再测试。按touch-registration-diagnostics-plan.md实现五窗注册返回值/错误/线程与IsTouchWindow读回、Pointer Down时状态复核和源信息；只观测不改变输入策略，构建Release x64供后续测试。此前Pointer-only业务缺口仍待另行处理。

当前诊断增量已完成：Debug ARM64源编译通过，链接器本机内部错误通过保留并重建单个增量缓存恢复；五生产探针与headless --no-window exit0/260.41秒，Release x64 exit0/332.72秒，PE/新标识/可选API导入边界核对通过。结果见touch-registration-validation.md，用户下轮测试步骤见touch-registration-capture.md；未GUI、未commit/push，业务缺口及实际注册结果仍待现场证据。


## R3：Pointer为根的输入来源仲裁（2026-11-03，用户已同意）

M6日志（research/touch-registration-log-analysis.md）证明：该数位板只发PT_PEN Pointer，无WM_TOUCH，统一WM_TOUCH后笔点不动；注册无问题。用户要求以不依赖时间的方式防止重复输入，并保持原有交互、触摸长按和笔震动。设计见pointer-source-design.md，实施计划见pointer-source-implement.md。

验收标准：
- R3a：PointerMode下手指/笔仅由Pointer接触进入业务；WM_TOUCH与带签名兼容Mouse按类型排除；对同一物理接触至多一个来源产生业务Down/Up，无任何时间窗去重。
- R3b：M6板（仅Pointer+兼容Mouse）与旧板（Pointer+WM_TOUCH+兼容Mouse）主栏和分页均可点击且无重复触发；Win7/无Pointer API保持现有WM_TOUCH行为。
- R3c：按钮、拖动、选色、FineDial、白板单击、PPT按下翻页与触摸长按保持；拒绝长按右键/第二键/笔桶键；真实鼠标/触摸板/Raw、Draw3、Hook无变化。
- R3d：Bar笔震动每次点击凭据至多一个，凭据缺失只省略震动；分页不新增震动。
- R3e：先以探针验证“接触阶段不转交DefWindowProc”假设；验证失败则降级为类型排除并由用户决定。
- R3f：真实生产helper的来源矩阵测试、完整Debug|ARM64与headless通过；Release|x64供一次实机验收，无窗口结果不冒充真机完成。
