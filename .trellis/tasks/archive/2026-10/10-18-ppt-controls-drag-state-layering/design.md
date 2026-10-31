# PPT 控件问题调查与修复设计（1–3项已批准）

## 1. 状态与范围

2026-10-18用户明确批准并开始修复1–3，按本设计实施；第4项窗口层级只保留调查/回归。GUI/Office、commit/push未授权。基线13783fe7，反馈版本未经确认，可能20261010b或20261014a；实际症状是只有被拖控件停住、主栏仍可操作。

本轮按同一PageControl链路组织一个任务，不拆独立子任务：位置交接、吸附和局部拖动停滞共用tracker/geometry；页码与层级只核对已有修复及回归边界。实现时也必须按故障证据分段，不能将四项强行归为一个根因。

## 2. 证据与处理分级

| 现象 | 当前判断 | 拟处理 |
| --- | --- | --- |
| 抬手微移 | fallback渲染仍会位置缓动，drag提交仅核版本/mask而未证明实际位置到位；Up不采最终坐标是另一项输入缺口 | 修正pair最终位置交接，直接移窗与render fallback统一位置语义 |
| 侧竖控件中轴吸附 | 当前拖动只有Clamp/碰撞约束，没有中轴snap | 新增20DIP中轴及侧边双门槛吸附和短促UI3过渡，保持边界/碰撞约束 |
| 仅拖动控件停住 | 尚未复现；旧presentationMutex死锁已由既有try_lock处理；候选回退、单client Retry/inputLocked等仍待区分 | 先保留约束，补必要的限频数值诊断及故障注入验证，修复能证实的状态缺口 |
| SetCapture条件性重入 | Down持renderTransactionMutex时调用SetCapture，若旧capture为另一个PageControl可重入相同锁 | 作为独立可达风险在批准后最小处理，不宣称是本次局部停住的实机根因 |
| 翻页 `-/-` | 当前已有0eb1c7c5同场busy保留可信数字及Basic控制修复；首次未知/换场次未知仍可合法显示 | 优先回归现有修复，不再无依据更改COM或放开墨迹门禁 |
| 控件位于Inkeys画布下 | 8a25cfcd已将两个画布嵌套，两反馈候选版本均包含 | 核对/回归现有owner链，不逐节点置顶；外部PPT穿插单独诊断 |

所有代码路径、历史祖先与限制分别见research/drag-release-and-snap.md、drag-hang-locks.md、page-state-and-layering.md、history-and-baseline.md。源码机制/条件可达不等于故障设备已复现。

## 3. 拖动最终位置合同

路径：owner WndProc发布绝对候选 -> 可行性约束 -> pair tracker -> 能直移则移动两HWND -> 失败/锁忙请求pair render -> 两窗真实提交 -> 松手持久化/释放layout ownership。

- 已完成直接移窗时，Up不得因旧snapshot、整数往返或重新缓动把窗口拉回。
- 正在拖动和已released但最终candidate仍pending的整个pair，普通位置采用候选最终几何，吸附Y修正按第4节有界过渡；不能只以当前surface.dragging判断，因为同组另一面从未置dragging、Up后该标志也已清除。
- 该例外只约束拖动位置，不顺手关闭出现/消失、hover、模式切换等其他动画。
- tracker只应在两窗实际成功提交且各自位置与同一候选一致后，宣布最终候选complete；中间动画帧不得当作最终几何提交；几何比较采用既有像素舍入/实际presentation bounds语义，不能用不相等的浮点精确比较制造永久pending。
- Up按该事件屏幕坐标处理最终候选一次（同一DPI/monitor快照），随后释放capture；避免漏掉最后Move，也避免Cancel/过期gesture将别的手势位置写回。已在dragging时直接采终点；仍为Page/background pending时复用同一系统阈值判定，Up才越阈值则完成一次最终drag并取消原preview，阈值内维持原click/preview。Arrow/Whiteboard/透明命中不因此变成拖动。
- released pending保持所有权，最终成功后才持久化并释放；API失败/Retry保留同一latest-wins候选，由后续明确请求的pair帧收敛。

状态采集沿用dragCommitMutex -> snapshotMutex顺序，不能在SnapshotForRender已有snapshotMutex内反取dragCommitMutex。纯动画/候选策略不取锁、不访问HWND；真实RetargetBounds/AdvanceBounds/ConfigureSurface的验证扩展PageControl.cpp现有无窗口离屏入口，避免只测布尔helper或tracker字段就声称余移已修复。

不新增拖动线程，不移植主栏完整拖动引擎，不重写renderer。

## 4. 侧边中轴吸附合同（用户追加要求，覆盖初版40DIP）

2026-10-18用户在首轮自动验证完成后，明确要求将吸附距离改为20DIP，并且距屏幕侧边扣除既定间隙后的额外距离也必须<=20DIP，才允许竖直方向吸附；吸附加入少量UI3动画。本节为当前已授权合同，旧研究中40DIP是历史方案。

- 中轴仍为主体中心Y对齐当前PPT布局屏幕水平中线，middlePairHeight=0即居中，不添加阴影或边距补偿。
- 同时满足abs(middlePairHeight)<=20*normalizedDpi与middlePairWidth<=20*normalizedDpi才尝试吸附，均含等号。middlePairWidth正是主体距对应屏幕侧边扣除既有PageControlGapDip*实际controlScale间隙后的额外距离；不得用带阴影的presentation HWND或重复扣间隙。先Clamp，再用当前实际拟合倍率检查中心碰撞。
- 阈值复用Bar.BottomDock.h的BarBottomDockThresholdDip=20；两个20均为屏幕DIP，仅乘同一布局屏幕DPI一次，不乘middlePairScale；96/144/192 DPI对应20/30/40像素。Bar的40DIP中轴常量和底栏自身行为不修改。
- 中心不可行时保留可行自由候选，仍不可行则保留最后feasible，不推动另一组；不为达到吸附删除边界/碰撞约束，也不把X强制归零。
- 只有吸附引起的Y修正使用短促UI3过渡，140ms，复用已有曲线/动画路径；普通自由拖动、锁忙/窗口失败的非吸附fallback及Up最终采样仍直接稳定。入口从两面当前实际Y连续过渡（大跨度输入直接进带也只运行同一次140ms），X及时跟随指针，两面共享开始时刻和单调采样进度，owner已推进的动画不能被较早render批次时间回拉；重复Move、同场页码发布与Up不得反复重启计时。
- released pending在吸附过渡完成前继续持有候选；两面真实几何到位且呈现/窗口提交成功后才commit，不把中间帧当最终位置。过渡有界结束后后续无输入帧稳定。
- 吸附动画不设置workspace/inputLocked门禁。离开任一阈值、Cancel、失效session/epoch、新手势和模式切换应能安全打断，保持latest-wins与既有锁序；实际开始/打断策略需在实现记录说明并以真实生产离屏断言验证。

不新增设置项、线程或外部动画依赖，不调整第4项窗口层级和COM合同。

## 5. 局部停住与capture重入

用户反馈不支持直接宣称全进程死锁。必须分别记录候选被拒绝、边界/碰撞约束、inputLocked、可见性、pair pending与两窗commit mask、Retry原因和时间；按状态变化/限频输出数值，不输出文稿内容/路径。若已有日志足够则复用，不扩成通用埋点工程。

不为“能继续移动”删除碰撞规则或采纳无效候选。若最新候选可行而某面长期未提交，保证pending仍持有且有明确续帧；只修复证实的丢请求/错误清退，不凭现象重写solver。

SetCapture独立风险的最小结构是短锁内判断/准备Down，锁外完成capture转移，短锁内复核gesture/session并建立新tracking。须先处理旧窗WM_CAPTURECHANGED造成的旧tracker清退，再建立新tracker。不能盲目把SetCapture挪到整个Down尾部，现有long-press决策依赖GetCapture，必须在capture完成后判断；同pair换capture不能让旧Cancel覆盖新tracking。WM_CANCELMODE/WM_CAPTURECHANGED/Up仍按既有锁外ReleaseCapture和取消合同清退。

## 6. 页码和窗口层级边界

- 先回归0eb1c7c5的同场busy保留旧显示、新场次未知清空、Basic数字与InkReady分离、pageAckEpoch防旧回执；不改C# COM访问合同或DLL/TLB。
- `-/-`只代表数字未提供/为负，不能单凭显示断言COM损坏或墨迹ready；合法未知不伪造数值，旧数字仅可作为同场稳定显示。
- 当前owner链Freeze -> DrawpadPresentation -> Drawpad -> Bar/四PPT必须维持；四PPT与Bar同级。不得改成独立TOPMOST或将两个画布恢复为兄弟。
- “点不了”需区分被覆盖、WM_NCHITTEST因inputLocked/targetVisible返回HTTRANSPARENT、以及业务请求因session/inflight被拒绝。
- 外部PPT放映窗与Inkeys画布不同；先前只读层级研究记录于workspace/codex/journal-2.md:457。当前缺少故障时Z序/owner/style/可见性快照，不能将外部穿插归为owner链修复未生效。

## 7. 最小改动范围与兼容

主要预期文件：PageControl.cpp/.cppm、page_control_tests.cpp。确有新增PPT facade策略/测试边界才改Ppt.cpp/.cppm和ppt_ui_tests.cpp；未发现当前COM/Window链新缺陷前不修改PptCOM、IdtPlug-in、Window Service或项目配置。主栏只作为吸附常量证据，避免无关格式/布局改动。

保留配置JSON、位置存储键、pair/session/epoch隔离、DPI策略、旧PPT/WPP支持声明、动态COM优先与确定释放。本轮不触及程序集和ABI，也不以GC/强制结束Office辅助验证。

## 8. 验证与审批

验证矩阵及执行顺序见implement.md。纯逻辑测试必须覆盖真实最终位置仲裁和released pending，不能仅重复tracker内部字段。完整产品编译复用InkeysRepo.sln Debug|ARM64、ARM64原生MSBuild和同invocation PATH处理；无窗口验证使用现有--no-window。真实输入、窗口capture/Z序/Office验收需用户另行授权GUI后执行，未执行部分明确NOT VERIFIED。

2026-10-18用户已明确批准1–3；据此task.py start并派实施/检查代理。第4项新层级修改仍不在实施范围，真实GUI/Office仍需另获授权。
