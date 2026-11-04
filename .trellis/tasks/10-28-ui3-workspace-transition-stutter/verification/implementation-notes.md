# 实施记录（进行中）

## 上下文与规范

已读本任务prd/design/implement、清单及研究；从原规范补读公共按钮与统一状态、idle时钟、共享串行调度、PageControl同源、变化脏区、共享WARP/光影、批次中点及收起换边、workspace/Window事务、连续字体目标；白板合同、canvas-freeze的场景准入/窗口期限/恢复写集、PPT session/UI-ready、C++编码/边界和构建合同。未以hook截断前缀代替后段合同。

## S0接缝

仅给既有--ui3-workspace-centering-test增加环境INKEYS_UI3_TRANSITION_PERF=pilot/full的无窗口真实Scheduler模式，复用WorkspaceCenteringFixture和公共PageControl回调。所有意图/动画对象只在render owner变更，主线程仅等待atomic完成。

六向顺序D→P→D→W→P→W→D，不添加属性菜单/按压负载，默认动画/双光保持。每方向700ms最短观察并等生产主栏Idle，完整默认30轮×三倍率×三轮，pilot2轮×100%。没有真实Window Service/Draw3/Freeze全屏呈现，metadata明确freezeCovered=false/nativeUlw=false，不把该模式当request→Committed业务延迟或全产品验收。

统计只采用成功present且真实barCommitStamp在本callback内的帧；无提交/Idle/换phase断开interval。保留raw metadata/batches/callbacks及phase范围，末尾sealed且掉样=0才可接受；不拿空Continue凑有效提交。

原计划完整冷/热全链矩阵需要真实Window Service/Freeze接缝，当前先以轻量fixture验证已明确的Bar/分页转场成本，不为填满矩阵制造第二个窗口框架。Initialize仍使用原居中基线预热，不能据此称fresh capacity；首次resource cold与完全fresh capacity另行区分。

## S0新增根因：粗粒度系统睡眠

Pilot零掉样样本中，251个前批次耗时<15ms的相邻batch，实际开始间隔P50=31.1341ms/P95=34.5268ms，较16.666667ms截止时间超出P50=14.4674/P95=17.8601ms。共享Scheduler使用sleep_until，而Draw3仅在绘制态提高系统timer period；离屏/Selection没有该保证。这是独立于Bar绘制/窗口等待的可测调度瓶颈，不得通过在测试中调用timeBeginPeriod掩盖。

具体最小设计：Scheduler复用已存在FramePacingThreadTimer（高分辨率CreateWaitableTimerExW→兼容timer）等待剩余deadline，失败才回到原sleep_until，保持batch开始+16.666667ms期限、pending合并、idle无限等待与Stop语义。直接Wait不走通用helper的Legacy自旋回退，不新增spin/全局timer period/线程。先独立timer候选A/B，再考虑容量。

## 容量候选的具体边界

Pilot的P→D新宽度实际target=2006×946→2012→2022→2026，有3次段内增长；其它方向无增长仍慢。初始heuristic无条件预约Draw/Geometry/More等所有隐藏面板，GetDC必须处理远大于约1000×150 viewport的target。下一候选仅去掉这份无条件初始预约及current×1.06，把既有current/稳定实际曲线predicted viewport、dock交互域与ResolveBarWindowCapacity(requiredViewport)作为最终保守容量来源；仍保留高水位、对称锚点、固定held/release surface、source包含与full replacement，不新增CPU staging。候选前后另运行确定时间几何fixture并读取实际viewport BGRA，对比像素/命中及失败恢复。

## 失败候选与最终收敛

44FF2B…的全横向紧缩方案在真实Scheduler下暴露纵向154→218多次扩容；补锚点后的DEE0/DD93…仍会在display/retained viewport耦合中横向逐帧追扩，已撤回其横向模型，不视为已完成修复。最终保留原横向安全预约，只用稳定真实曲线极值，收紧主要开销的隐藏面板纵向预约；给display/停靠纵向动画保留完整已知段。最终A02209…pilot原生断言通过：首次pending placement1次增长，其余为0；完整矩阵进行中。

两份大规范的注入截断已按地图实际补读。初次构建/m多node恰好300秒握手失败、0源码错误；改完整/m:1后C1902由PDB IPC沙箱边界造成。同参数正常用户环境/MSBuild native ARM64构建通过，未改Zi、PDB、依赖或项目配置。所有最终A/B均正常用户环境串行运行，旧sandbox数据仅作pilot。

首次最终Headless完整stdout已到PASS animation correctness，但240s wrapper过早结束、接管原PID后ExitCode=null；只可称内部套件输出通过，OS exit未经保留。待最终性能完成后只重跑一次direct console invocation取得真实退出码，不把null当0。

## 最终保守方案与资源断言

最终不再追求横向最小位图：沿用原对称安全预约及已有207.5等横向余量，仅将Back的1.06作用于完整实际曲线宽度极值而非逐帧current。主要收益来自取消未展开Draw/Geometry/More的无条件纵向大预约。纵向driver使用现有displayCenterY真实物理段范围，与mainButton当前锚点独立求reach后取max；固定拖拽/释放surface不走新预约。

A02209…保守候选在150%首P→D仍有290→292两像素晚增长，完整矩阵原生exit1，保留converged-final反例。最终仅在独立plannedHeight真正突破历史时添加既有viewportPadding*2的两侧栅格余量，绝非每帧history+padding；overflow检查保留。3B27D5…完整矩阵exit0。随后恢复requestedWidth>history的capacityEpochChanged扫描守卫，避免禁动画/瞬时加宽丢失visible bounds，最终源码由此冻结并独立重测。

新fixture确定性资源门禁：每个稳定方向目标段首帧之后不得增长；唯一许可是fresh fixture的第一D→P pending placement第二帧真实重定向一次。默认动画/双光仍启用，断言不因性能抖动而放宽。纯容量映射变化仍full paint/full submit，但不把late重绘误当全量Observe；已知根布局移动则在BeginFrame后Observe前ForceFullDamage校准稳定子项。headless新增组/子项之后隐藏的旧像素边界回归。

## 矩阵成本与覆盖取舍

计划30cycle的六向×三倍率×三轮单版本最低1134秒（每段700ms，未计初始化/settle），三候选至少56.7分钟。pilot后完整实际使用5cycle，同54方向/倍率/轮组合、270次转场、最低189秒/版，原始数据保留全部冷首cycle与热后4cycle。该取舍保留三轮和倍率，但每组样本量减小，不能冒充30cycle完整计划。每个fixture仍Desktop基线预热；首cycle是其转场资源冷态而非全新设备容量。共享Bar/PageControl已覆盖；原生Window/Freeze/Office/Draw3 ready→Committed端到端延迟未覆盖，未修改其ACK/fence/失败恢复。

本转场fixture没有挂TLS Ui3SvgProbeScope，所以raw可分compute/draw/GetDC但无SVG parse/raster/upload独立占比。没有因此改SVG或声称已排除该热点。full replacement完整保留，不以删除正确性保护换性能。
