# 技术方案（已批准，实施中）

## 最终范围与任务划分
用户已确认：主栏保留PPT进入/退出及现有PPT专用贴底规则；另对Drawpad和DrawpadPresentation两个画布规定Desktop/PPT始终留底1物理像素且明确非全屏，Whiteboard取消间隙并保持全屏。
范围确认之后，用户已明确批准实现；父任务及两个子任务均已start为in_progress。

| 子任务 | 独立交付与责任 |
| --- | --- |
| 10-19-canvas-edge-fullscreen-policy | Window Service双画布底行区域、非全屏属性、白板事务与恢复，不依赖任务栏显隐采样 |
| 10-19-taskbar-dock-follow | Display任务栏观察、主栏贴底线/Y动画、Bar底行保护，保持PPT主栏规则 |

两个子任务没有功能依赖。批准后建议先完成画布策略，再完成主栏适配，最后父任务整体验收；各自可单独验证，避免同时改动窗口和显示环境交接。父任务不重复实现子任务。每个子任务有独立PRD/design/implement及context manifests。

## 主栏贴底策略
沿同一纯函数解析目标线，并保持场景优先级：

| 场景/该屏任务栏实际状态 | 主体边框外沿的目标Y |
| --- | --- |
| PPT或白板专用场景 | 保持现有monitor.bottom-round(5×真实DPI/96) |
| 桌面，顶部/左右任务栏 | monitor.bottom-round(5×真实DPI/96) |
| 桌面，明确无任务栏 | 保持有效workArea/monitor回退 |
| 桌面，底部常驻任务栏 | 稳定任务栏top，可靠workArea作为兼容路径 |
| 桌面，底部自动隐藏且Raised | 稳定任务栏top |
| 桌面，底部自动隐藏且Hidden | monitor.bottom-round(5×真实DPI/96) |
| Unknown/短暂查询失败 | 同屏同几何下最近可信目标；首次无法判断沿现有workArea/monitor保守回退 |

边框下沿=centerScreenY+(bodyHeightDip+strokeWidthDip)×最终UIzoom/2。5DIP仅按真实屏幕DPI换算；taskbar top本身为物理坐标。不能按含阴影/透明余量的HWND边缘计算间隙。
统一供稳定渲染、输入tracker、blocked-release、捕获/恢复、普通/居中底栏和CenteredBottomBar启动预览消费。不伪造workArea影响浮动布局或弹窗；两角浮动启动不改。顶部/侧边按追加要求与Hidden分支共用5真实屏幕DIP目标，维持统一消费及回归，不无必要重写。

## 任务栏观察与快照
复用Display现有采样与DisplayObserver owner消息线程，不新增常驻线程。逐屏快照扩展可靠edge、autoHide模式、规范化Hidden/Raised/Unknown及稳定dock anchor。完整taskbarThicknessPx与显隐状态独立，Shell动画不使UI缩放基础量变化。

以ABM_GETAUTOHIDEBAREX的owner/edge、主任务栏API和兼容Shell窗口采样为基础。配置autoHide不等于正在升起，IsWindowVisible不等于实际屏内暴露。归属本屏的矩形/可见带判定实际状态，微小露出条归Hidden；升起目标按完整厚度求稳定top，不逐帧追逐Shell动画rect.top。门限与迟滞抽为可测状态策略。

优先采用非注入WinEvent通知，按Shell进程并再次按缓存HWND、OBJID_WINDOW/CHILDID_SELF过滤。回调只排队，owner线程合并约16–33ms burst；不在回调、UI帧或鼠标Move枚举EDID/任务栏。仅对已确认底部autoHide的缓存窗口启用约200ms轻量矩形兜底，原2秒结构复查负责设置变化/Explorer重建及漏事件恢复；无相关任务栏时停快速采样。

原始移动样本留在观察器内部，只有逻辑状态、稳定目标线或保护策略变化才进入SemanticallyEquals/generation，避免Shell一段动画触发几十次Bar重定向和PPT刷新。去重/合并不积压；同线程安装/卸载hook；WM_DESTROY对称清理timer/hook；Shutdown与Explorer换代拒绝旧HWND/迟到回调。旧Shell不支持EX/事件时降级，无新Windows版本硬导入、私有注册表或系统任务栏修改。

## 主栏Y动画与输入所有权
ApplyDisplayTransition锁定贴底时SetDirect，稳定路径每帧也SetDirect，仅加SetTar会被覆盖。新增明确的同屏桌面任务栏目标线过渡状态，复用displayCenterY、BarUiAdvanceAnimation及既有调度，沿用PPT贴底0.4秒及相同动画开关/速度语义。

只改变Y，保留普通底栏X及居中底栏水平锁；不调用会重置X/居中/弹簧的PPT CenterDock。同目标不重播，反向从当前已显示位置重定向，完成后恢复统一稳定约束；动画关闭则即时到位。

新目标沿pending/presented偶数serial完整环境元组发布。拖动、Absorbing和恢复期间遵守既有输入owner及成功呈现屏障，新增自动动画不能成为第二个Y写者；稳定BottomDocked后从当前呈现Y收敛最新线。浮动栏不被强制吸附。PPT/白板优先替代桌面过渡，退出恢复最新桌面目标，不先播放过期中间态。

## Bar底部1物理像素
保护限于桌面底部autoHide任务栏对应屏幕，包含Bar主体、描边、光影及弹性动画。名义5屏幕DIP不能证明光影永不触底：现有保守外扩为6×UIzoom+3px，两种倍率并不相同。

在最终呈现viewport按物理屏幕坐标裁剪，下界exclusive为monitor.bottom-1，计算包含最终directTranslation；source/size变化沿既有完整ULW替换清旧区，之后维持脏区优化，无CPU读回或每帧region更新。SetWindowPos直移也检查新矩形；会触保护行时回到既有渲染提交，不全局禁用直移。不以HTTRANSPARENT或透明padding单独证明Explorer跨进程穿透。

Bar的上述条件策略与下面画布的无条件Desktop/PPT策略分别实施，不能混用开关或把Bar的PPT贴底规则改掉。

## 双画布场景策略

| 场景 | Drawpad及DrawpadPresentation实际可见/输入区域 | Shell策略 |
| --- | --- | --- |
| Desktop，绘制/选择预览/穿透 | 始终排除所覆盖目标屏幕最底1物理像素 | 两窗显示前设置NonRudeHWND；既有overlay显式全屏为false |
| PPT，绘制/选择预览/穿透 | 与Desktop相同，不依赖任务栏设置或PPT自身全屏 | 与Desktop相同，只约束Inkeys画布 |
| Whiteboard | 取消底行排除，完整覆盖 | 移除两窗本任务的NonRudeHWND；保持Freeze作为既有全屏/激活锚点 |
| 白板退出、进入失败回滚 | 恢复Desktop/PPT的1px区域 | 恢复NonRudeHWND，清既有overlay显式全屏标记 |

采用Window Service owner线程的双window region + NonRudeHWND组合。region真正排除可见与输入底行；官方NonRudeHWND明确阻止Shell将窗口自动识别为全屏。MarkFullscreenWindow(FALSE)只撤销显式标记，不能代替非全屏属性。

1px是物理屏幕行，不乘DPI/UI倍率。region以window外框局部坐标表达，在实际屏幕RECT与monitor底行求交后排除。这里让出的是实际覆盖形状，GetWindowRect/GetClientRect仍为完整画布；保留client原点、RTS标尺、Host/backbuffer/document viewport和持久化笔迹，不通过height-1触发Draw3 resize或丢弃底行墨迹。

首次创建两窗时在首次Show之前落实策略；白板mode事务显式包含两窗，不能附在只处理Freeze/主Drawpad的style分支而漏过selection。必要场景切换暂时隐藏受影响窗口，设置/移除属性与region并核对，恢复原有可见性/互斥surface语义后才提交mode。普通Desktop↔PPT及任务栏升降不改变画布策略，不发生重复hide/show、SetWindowRgn或Host重建。

快照记录原region、属性归属/值、可见性、HWND代际及已应用策略。任一窗失败按逆序恢复整对状态，失败不提前ack成功，回滚失败保留未收敛状态供现有循环重试；不新增通用事务框架。SetWindowRgn成功后HRGN归系统，回滚用复制/重建快照，失败释放本方HRGN；销毁前移除本任务属性。Bounds改变、重建、白板重入及surface切换核对最新代际策略，相同值no-op。

白板仍由既有Freeze/root锚点调用MarkFullscreenWindow，避免对每个画布再加一组显式TRUE。当前COM调用是best effort、无Shell结果回传；本次保留该既有边界，不宣称函数返回已证明Shell分类成功，不为此重构整个Topmost/COM链。新region/property步骤本身的Win32失败必须可检测、回滚和重试。

已有输入capture可继续接收区域外消息；不为弹出任务栏取消在写笔迹。底行保护针对新命中/实际覆盖，不承诺按住拖笔时Shell一定升起。

## 研究优先级与验证边界
- 最新需求以PRD及本design为准；research/canvas-scene-policy.md是画布最终研究。bottom-edge-hit.md内旧的可选A/B只保留调查过程，不再是待确认项。
- 微小露出条与Shell窗口动画不是公开API的完整保证；无窗口状态机测试不能替代真实Shell验收。Win7兼容、DComp/ULW、UIAccess、多屏均在验证矩阵中注明实测状态。
- 无窗口验证覆盖几何/状态机/失败回滚/代际/同值去重，生产helper作为测试入口；不以源码字符串断言充数。
- 完整Debug|ARM64 solution构建及InkeysHeadlessTests --no-window在批准实现后执行。GUI/Computer Use须用户另行明确授权。实施按既有solution与无窗口入口验证；不GUI、不commit/push。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。

## 用户追加调整（2026-10-19，已授权直接实施）
1. 明确顶部/左侧/右侧任务栏对应的桌面主栏贴底线为monitor.bottom-round(5*真实DPI/96)，普通/居中底栏共用；底部autoHide Hidden和PPT原5DIP规则保留，普通底部/autoHide Raised仍贴任务栏top。
2. UI.Bar.UseTaskbarHeightAsScaleBase在缺省时仅设备“默认分类”为大屏时默认true，其余false；采用现有默认设备判定，而不是用户在deploy.json中覆盖的PaintDevice。明确保存的true/false仍优先。构造/ReadMini/ReadAll/Reset默认规则一致，保持NoUpload/严格bool及Zoom2语义。
3. “未来PC UI后再都默认开”记录为后续方向，本轮不实现PC UI或新增样式开关。启动默认判定的采样、缓存与Mini时序需由研究确认最小接线，再交实现代理；不得以早期无完整显示快照而让Mini与正式默认互相矛盾。
4. 继续最小diff、原BOM/CRLF、无GUI、无commit/push；完成后完整solution Debug|ARM64及适用无窗口/配置边界验证。此前validation为追加调整前结果，不能当作本轮新修改验证。

## 设备缺省接线补充
仅READY且含integrated/external touch时，在Mini前调用一次现有Display::Initialize并冻结主屏physicalSize分类；无触控保持原枚举时序。原完整初始化点复用提前结果，传统paintDevice/liftStraighten默认与Config schema缓存取同一分类，随后用户PaintDevice覆盖不回写此缓存。面积阈值1200cm²、物理信息不可用的大屏回退与现有策略一致，不新增尺寸猜测。缓存常量初始化为普通设备，正式启动seed后才构造Mini；全局config在ReadAll重新应用同一schema默认。未来PC UI统一默认开不在本轮实现。
触控启动物理枚举可能增加预览出现前等待；不提前创建observer/窗口，不重复枚举，不把硬件查询放进Config默认表达式。细节见research/followup-device-defaults.md。
