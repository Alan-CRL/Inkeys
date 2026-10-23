# Scroll Follow 实时可视化诊断窗口交付记录

基线：feature/move 980dc39b。实现、完整构建、定向离线检查及符合启动顺序的非交互实屏验证已完成。控件按用户明确选择交付人工验证；其余未覆盖项见下表。用户已授权本次提交；不 push 或创建 PR。

## 启动与操作

产物：`Build/ARM64/Debug/Inkeys.exe`。正常启动添加 `--scroll-follow-debug-window`，控制台 `--scroll-follow-console` / `--scroll-follow-verbose` 可独立使用。只在启动时打开一次，关闭诊断窗不退出 Inkeys、不停止跟随，当前进程后续 ViewAdjust 不重新打开。

窗口左侧显示真正提交 Tracker 的 WorkArea 输入，右侧为可拖动分隔线的滚动统计面板，底部为最近30条数值事件。鼠标滚轮缩放预览、拖动平移、双击或 Reset view 复位。Points/Flow/Grid/Bounds 和1×/2×/4×只改变绘图。没有新增功能快捷键。

Freeze 固定图像、标记和该帧统计，LIVE 状态/摘要继续更新；恢复直接消费最新完整帧。Save Debug Snapshot 对应顶部 Export...，固定点击时数据，再选择父目录；独立任务创建新的 scroll-follow-* 子目录，输出 raw.png、preview.png、snapshot.json。raw 是原尺寸输入，preview 是完整 ROI 1:1 标记图（不裁成交互视野），JSON 保存交互选项。默认不保存图像，一次只有一个导出任务；取消/失败可能保留本次新建的部分目录，并明确显示状态。

## 实际修改文件

| 文件（相对仓库） | 修改目的 |
| --- | --- |
| Inkeys/Inkeys/CV/VisualMotionTracker.h/.cpp | 默认空指针的只读快照、原检查位置记录、旧参考身份和候选/实际结果 |
| Inkeys/Inkeys/Business/ScrollFollowDebug.h/.cpp（新增） | 三槽 BGRA/数值快照、30条摘要、Freeze/关闭代次和实时状态 |
| Inkeys/Inkeys/Business/ScrollFollow.cpp | 复制实际 ROI、原位 CV 快照、发布/ownerApplied 分离、首暂停原因及原捕获故障事件 |
| Inkeys/Inkeys/UI/ScrollFollowDebug/ScrollFollowDebug.h/.cpp（新增） | GDI 窗口、映射/标记/面板/Freeze、异步目录选择与共享 PNG/JSON writer |
| Inkeys/Inkeys/Graphics/FreezeCapture.h/.cpp | 只读采样/故障/质量有效性字段和同帧统计，不改调度与决定 |
| Inkeys/Inkeys/Window/Window.cpp/.cppm | 新角色、独立设置线程路由、句柄及统一退出生命周期 |
| Inkeys/IdtMain.cpp | 启动参数、严格单参短测试派发、导出有序退出 |
| Inkeys/IdtMagnification.h/.cpp | 当前 HWND 排除、可靠原生错误、复用正式 Mag 的短像素夹具 |
| Inkeys/Inkeys/Drawing/Draw3/Draw3.ProductLiveTest.inl | 既有真实产品夹具的有限性能/GUI模式；未改 Draw3 生产渲染逻辑 |
| Inkeys/Inkeys.vcxproj、.filters；InkeysHeadlessTests/InkeysHeadlessTests.vcxproj | 登记新源码；UI 同名 cpp 使用独立 .obj 名，避免对象覆盖 |
| InkeysHeadlessTests/animation_tests.cpp、visual_motion_tracker_tests.cpp、window_tests.cpp、scroll_follow_debug_tests.cpp（新增） | 严格短入口、CV差分、通道/捕获/导出/窗口合同 |
| .trellis/spec/native-desktop/index.md、scroll-follow-tracking.md、scroll-follow-capture.md、scroll-follow-debug-window.md（新增） | 当前接口、有效性、所有权和验证边界 |
| .trellis/tasks/10-23-scroll-follow-debug-window/ | PRD/design/implement、调查、上下文、审查和本轮验证记录 |

## 窗口、Mag 与线程所有权

ScrollFollowDebug 是 Window Service 的普通可调整窗口、TOPMOST、owner=null，不加入画布 owner 链；通过设置 owner 线程和现有命令队列创建/关闭/销毁。独立 UsesSettingThread 判断不扩大原 Setting 样式或业务规则。WM_CLOSE 不 PostQuitMessage，统一退出隐藏/销毁会清理句柄、timer、GDI、字体和 Channel。

每次 Source 更新前 BuildCurrentFilterList 查询当前有效调试 HWND，保留原有角色并沿原有效句柄/进程/非child/去重验证。导出选择器在自己的 STA 通过 IOleWindow 发布当前 HWND，供同一列表验证，返回/取消时清除；Mag 不访问 COM，不缓存旧句柄。列表合同不等于实际像素排除；实际结果单独记录。

原 Mag 回调仍串行调用原 Tracker。仅窗口打开时，Business 将实际 WorkArea BGRA 按行复制到自有紧凑缓冲，连同同帧几何/质量/统计、windowGeneration/businessSession/captureSession/sequence/displayGeneration 发布。三个槽复用 capacity，生产者/消费者各自持有槽，atomic middle 交接，消费者落后覆盖旧预览；30条摘要竞争时 try_lock 丢数并计数，不等绘制。关闭封闭旧代，在途 lease 只保活至回调结束，不能写入新代；Context 不永久保留大图像。

导出在点击时深拷贝稳定 front 及 live/options，worker 不持 Channel/UI 指针；独立 STA、唯一 owned thread、同线程取消目录选择。普通关窗发取消不 join，主退出在 Window Service 停止后 ShutdownExports 取消并 join，避免跨线程 COM/悬空窗口或 CRT 析构竞态。

## 标记与统计来源

| 显示 | 含义 |
| --- | --- |
| 绿色 | 主共识内点；不代表整帧必然接受 |
| 灰色 | 近静止证据 |
| 橙色 | 生产实际评估的最强竞争运动证据 |
| 红色 | 失败/排除点 |
| 青色 | 补点（committed 与候选分开） |
| 方形/三角/圆形/x/加号 | Forward成功/中间阶段/FB有效/失败/已提交补点 |

图像、点、光流、8×6网格及范围共用同一映射。每轴用实际 floor 半尺寸比例还原，适配奇数尺寸，不固定乘2或叠加DPI。anchor、delta、total 已是原图/内容像素。网格明确是参考坐标证据，retained/added 是当前格。范围名 Main Motion Support Bounds，表示支持证据，不能解释为滚动客户区。

所有检查来自原生产位置，公开71,008字节固定快照，不增加 OpenCV 估计或补跑未到达阶段。保存 reached/completed、evaluated、原 measured/comparison/threshold/condition 及 firstFailure；联合 AND/OR 保留原义，未执行显示 N/A/JSON null。condition 不等于通用通过标志。主共识、最强竞争、符合双半径条件的 runner-up 与实际拒绝竞争候选分开，接受后参考序号与本次实际使用的旧参考序号分开。

完整展示原 MotionStatistics：输入/参考/接力代次；attempted/forward/backward/fbValid/inliers；reference/occupied/inlier/interior cells；added/retained/retired/initialSurvivors；连续拒绝；point/spatial/survival ratio；coverageX/Y/runnerUpRatio；FB/residual median/P95；affine variation/residual；preprocess/opticalFlow/fit/refill耗时。阶段有效性限制默认零解释；没有实际计算的 main competitor-only 数值为 N/A/null。

候选估计、MotionResult、Target发布成功、ownerAppliedSequence/Viewport 是四类不同事实。Capture Accepted/Rejected/API 与 Tracker Accepted/Rejected、实际采样FPS、preview刷新率分开。业务 AwaitTracking/捕获失败/ROI无效/视口钳制/超时不伪造 Tracker 拒绝。暂停保存首因和触发动作前的捕获故障事件，随后 End/Requested 不抹掉原证据。

qualityObserved 只在真实 Analyze 后有效，API失败的默认Quality不显示为分析结论。原生错误仅对应API文档保证时有效。Capture Read 在 Backend.Read 返回后、Analyze前结束，包括后端几何/PW/GdiFlush/alpha修复；不能当成纯PW耗时或桌面内容新鲜度。采样间隔、有效样本年龄、Stale/Capture Failure 单独显示。totalTrackerMs 是启用诊断时围绕真实CV调用的测量，copyMs 单独记录。

## 验证结果

最终完整 `InkeysRepo.sln Debug | ARM64` exit0，29.790秒、0错误、3条既有 hashlib++ C4267 warning。vswhere 定位 ARM64 原生 MSBuild，同一 PowerShell 先规范 PATH、禁节点复用；未升级依赖/SDK/工具链。早先 sandbox stall/C1902 与 vcpkg pwsh.exe 消息分别记录为环境现象，正常宿主完整构建和实际测试通过。源码/工程22文件的UTF-8、原BOM/CRLF、XML与完整diff/新增文件whitespace检查通过；原LF规范仍保持LF。

| 验证项 | 实际结果与范围 |
| --- | --- |
| 算法结果不变 | PASS：两份Tracker的全部30个非计时统计及顶层结果精确比较；初始化、正常/反向、真实FB、Coverage短路、两种AmbiguousMotion、NonTranslation联合门、Paused、跨拒绝参考与短接力。部分异常/Forward低点/Jump/Anchor专门负例未强制触发，只静态核对。 |
| 提前返回、身份与寿命 | PASS：未执行有效性、旧/新参考、奇数401×303→200×151、padded stride/非零ROI、借用复用、消费落后；实际双线程400帧与关闭/新代竞争。 |
| Freeze/Paused | 模型与业务合同PASS；实际出现三连拒绝、参考/最后可信total保持、TrackerPaused首因与Capture接受分离。实际Freeze按钮和最终失败帧的人工视觉复核待做。 |
| 导出 writer | PASS：同一共享renderer/writer、克隆后源清空、raw全像素、独立坐标GT、两PNG尺寸、完整JSON/有效性/非有限值、写盘前取消零文件。目录选择/实际取消/关窗中选择器由人工验证。 |
| 窗口生命周期 | PASS：屏幕外重复创建销毁/owner线程/统一退出/当前句柄；最终实屏真实close释放publisher及旧Channel存储，继续原follow，Off/on/off不重开。 |
| 完整UI的Mag排除 | PASS（正式开窗顺序）：独立两图案28帧覆盖ROI；最终产品四次256×192实际Tracker输入均49152 pixels、最大通道差0，窗外87/87、顶栏9/9。最小P2原型单独像素验收未完成，不能追溯改判。 |
| 产品行为 | PASS范围：专用2880×1920灰度源、空画布、真实正32/反16 tagged wheel、原Window ACK/Off/drain、最终位移+16、关闭后跟随。真实墨迹/定格、Edge/Word、触控笔待人工。 |
| 严格入口 | 三个短入口exit0：Headless --scroll-follow-debug-only 2.049s；Headless --exit-visibility-gate-test 0.253s；产品 --scroll-follow-protocol-test 1.025s。16组畸形argv全部exit2/零输出。未运行长OpenCV、--no-window或UInk全量。 |

最终产品实屏：`Build/ScrollFollow/p4-product-final.exit.json`，pid27944，真实exit0；stdout末`bounded_case=PASS`，全部worker/service/capture清理完毕。日志明确`controls_manual_deferred_by_user=1`、`interactive_controls_tested=0`，这个PASS不包括控件人工操作。

## 有限性能观察

Windows11 ARM64、Debug、专用2880×1920静态灰度内容/空画布，每段1秒热身及约15秒观察；没有图像历史录制。absent使用business/capture(1,1)，真实Off/反向ACK/drain后开窗并重启，open/closed使用(2,2)。专用源的窗口层级、关闭后的+16姿态及特征存量不同，属于非因果顺序观察，不能据此归因开窗使FPS增减或外推Word/Release/其它分辨率。

| 状态 | 秒数 / accepted | Capture FPS | 实测采样间隔均值/最大ms | Capture Read均值/最大ms | Tracker分段均值ms（预处理/LK/拟合/补点） |
| --- | --- | --- | --- | --- | --- |
| 未打开 | 15.009 / 92 | 6.130 | 163.874 / 212.251 | 79.281 / 123.270 | 1.981 / 7.462 / 1.566 / 3.955 |
| 打开 | 15.001 / 94 | 6.266 | 159.925 / 210.054 | 73.557 / 108.308 | 2.452 / 7.165 / 1.510 / 3.804 |
| 关闭后 | 15.006 / 101 | 6.730 | 147.892 / 187.529 | 63.582 / 88.404 | 1.565 / 6.253 / 1.436 / 1.501 |

三段Tracker接受92/94/101，Tracker拒绝、Capture rejected/API均0。20Hz是原调度目标，不是本轮实测结果。原Read时长不含Analyze，不是隔离PrintWindow。

| 状态 | 实测Tracker总均值ms | 复制均值ms | 显示年龄均值ms | Preview Hz | Private MiB（始→终） | Working set MiB（始→终） | GDI/USER（始→终） |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 未打开 | N/A | N/A | N/A | 0 | 523.99→524.45 | 518.79→498.20 | 113/79→114/79 |
| 打开 | 15.351 | 1.225 | 157.163 | 9.133 | 594.46→595.71 | 589.84→591.07 | 121/81→120/80 |
| 关闭后 | N/A | N/A | N/A | 0 | 534.09→534.16 | 529.65→531.82 | 117/78→117/78 |

打开阶段消费6.266Hz，发布94/覆盖0；总Tracker时间仅对实际显示packet取样（85个样本），不能与所有帧分段和当作同一个平均量。关闭后Enabled=false且旧Channel存储释放、预览发布为0；进程内存/GDI的有限差异不表示长期零缓存或零泄漏。原始逐段日志和摘要在 `Build/ScrollFollow/p4-product-final.stdout.txt`、`p4-performance-summary.json`。

## 未验证项与保留的失败证据

用户已明确选择控件交付人工验证。实际工具后来能枚举诊断窗，但读取状态被应用授权检查拒绝：`Computer Use was not approved to use inkeys`。没有取得GUI截图或执行控件输入。正常启动的Freeze/恢复、缩放/平移/复位、标记/倍率、滚动统计/事件、目录选择/取消/关闭中导出，按 `research/manual-validation.md` 第1–10步验证。

选择器首次可见时的真实Mag像素排除仍未验证，不能由主窗口启动前排除或共享writer测试代替。真实Rejected/Paused的最终画面、实际画布墨迹与定格、Edge/Word/硬件笔、长期资源以及旧Windows/其它架构/Release、真实OOM/COM/文件系统故障未验证。

旧夹具在活跃捕获中动态Open主窗口曾出现真实覆盖像素失败（seq113/max_error225）；第四/六轮也分别有真实三次FB/InsufficientFeatures拒绝并Paused。这些FAIL保留在validation.md，最终PASS不追溯改写。源码确认正式Open早于StateMonitoring及MagnifierThread启动，没有中途Open入口；这是可达性差异，不能证明旧225差的具体根因。最终夹具以真实Off/drain遵守启动前置；未来若增加重开路径，需要重新评估在途读回和首次可见排除。

完整调查、独立审查和运行记录：validation.md、research/p1-check.md、p3-model-check.md、p4-check.md。任务保持待审，用户已授权提交本任务改动；不推送或创建PR。

## 可直接支持的后续算法研究

同帧原始输入、各阶段点轨迹/FB错误/出生锚点、参考网格票与单位格权重、主/竞争支持范围、实际门限及首失败，可用于分析重复纹理别名、固定区域多数、竞争运动、覆盖不足、特征退休/接力和早退来源。候选/返回/发布/实际应用分离有助于区分CV误估与捕获/业务/Viewport问题。本轮只提供这些证据，没有改变算法参数或承诺自动选择滚动客户区。

## TOPMOST 最终补充

用户确认后诊断窗口改为独立TOPMOST，owner=null。完整Debug|ARM64 solution构建exit0（16.392秒，3条既有warning/0errors）；诊断短矩阵exit0（1.881秒），屏幕外窗口合同exit0（0.263秒），包含TOPMOST、空owner、overlay降级/恢复、隐藏再显示、关闭重建及退出。未打开GUI；此前实屏和性能是非置顶版本，不追溯为新样式的GUI验收。详见research/topmost-check.md。用户已授权提交本次全部改动，不push。
