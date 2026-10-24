# 技术方案：静止字号目标与连续动画修订

## 修订依据与范围
- 本修订承接首轮commit aeb178bc，替代其每帧在最终绘制入口floor的策略；任务继续，不自动commit/push。
- 范围仍仅UI3主栏、属性面板、PPT/白板分页及已有独立文字入口。设置字体、图标/几何、配置key和默认值不变。
- 新调查见research/font-animation-targets.md、research/font-animation-scales.md；早期rendering-chain调查保留历史依据。

## 动画内部目标投影
- 保留既有BarUiValueClass的原始字号val/tar/start/middle、曲线和幂等语义；不能直接把size.tar改成已截断值，也不能靠派生类重载漏过BarUiValueClass&调用。
- 每段文字使用固定、有界的字号修正动画状态，复用既有动画类/帧dt、速度和动画启用策略，不建立独立时钟或无界指针map。
- 原始最终静止物理字号T合并名义字号目标、frameZoom目标及外层目标Y倍率；关闭时有效字号目标为floor(T)，修正系数目标cTarget=floor(T)/T；开启时cTarget=1。目标为0时允许最终无字，不反向钳制到1px。
- 只有稳定目标或策略真正改变时才重定向c；由当前动画c连续走向新目标，重复提交同目标不重启。必要时首次/强制替换直接初始化到目标。
- 实际绘制保留原始字号、contentScale、按压、弹出和D2D几何轨迹，再乘连续cCurrent；绝不对动画当前字号floor，也不在动画结束追加一次floor。所有源动画与c落定后实际字号为floor(T)。
- 原始轨迹可能叠加不同曲线，修正动画只连续补偿字号尾数，不量化图形几何；中途反向/改目标必须沿当前c继续。

## 稳定目标元数据
- DC矩阵只能给出当前倍率，无法推断动画终点；从拥有者显式提供文字目标frameZoom/外层Y倍率及必要名义字号，默认静态入口仍可按当前context解析。
- 动画终点使用独立的绝对目标Y向量/倍率，不能用当前FLOAT矩阵乘double目标/当前比值来抵消：例如13×float(0.95)/0.95=12.999999836…会误截断为12；不得用epsilon或round掩盖。
- 公共按钮必须包含pressScale.tar，按住不放的终点亦为整数；内容切换1→middle→1只将当前contentScale乘入轨迹，不把中点作为每帧新目标。
- Eraser/Recall等逐帧SetDirect(nominal*currentPose.scale)保留几何轨迹，同时提供nominal*targetPose.scale。Scene菜单/主题frameZoom与父子姿态按相同方式传当前/目标。
- 主栏显示缩放使用displayUiBaseScale.tar等明确目标；popup使用源progress.tar，不能从已派生rw.tar猜终点。底栏弹性恢复Y目标为1；直接拖住的静止形变按其实际抓手目标，不无限跳过量化。
- dock/FPS/细刻度等独立文字使用固定样式状态或复用既有word状态，并接入同样的目标/连续修正。细刻度布局缓存继续按实际字号区分。

## 帧、缓存与失效
- 修正动画纳入既有active/damage/present重试，源几何先结束后修正仍在途时也必须持续推进，完成后恢复idle。
- 同一word/样式每帧只推进一次，复用共享帧时间；测量/预热或重复绘制不能重复推进。隐藏恢复和设备/字体缓存重建保持正确策略。
- MeasureText继续保留原未截断上界，避免动画中重排。
- 实验卡标题及配置key不变；三语说明改为“静止字号向下取整，动画保持连续”，不改设置文字的渲染方式。

## 验证与回滚
- 更新旧离屏断言：静止终点为整数，而动画中间允许小数；必须验证轨迹连续、最后一帧衔接、重复目标/中途重定向和动画关闭直达目标。
- 覆盖源字号、press按住/释放、内容切换、panel当前pose/目标pose、多个DPI/倍率、独立文字及修正单独在途的active/damage。
- 使用既有Headless和无HWND离屏入口；完整Debug|ARM64 solution沿用ARM64原生MSBuild、PATH规范化、至少5分钟预算。已知沙箱依赖扫描故障通过同配置沙箱外运行验证，不改工具链/工程。
- 开关开启恢复原始字号轨迹；不改变抗锯齿。真实设备观感不由数值测试代替。
