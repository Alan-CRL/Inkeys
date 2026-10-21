# 已批准设计

## 基线与复用
feature/move 1b8cb4b6；已有 opencv4 4.10.0#3 静态 core/imgproc/video，不修改 manifest/triplets/features。Draw3 TranslateViewport、CanvasNavigation、L2 可信快照、屏幕热前像失效和 budgeted visible Tile recovery 为唯一平移恢复路径。Desktop AutoSave 仅 Clear/Exit，UInk v10 viewport 已双向贯通。

## Capture
MagCaptureService 提供 Start/Stop/Snapshot/帧回调；独立 owner thread 拥有 Host/Child、消息循环、持久 DibSurface。Frame 输出 BGRA、尺寸/stride、source rect、QPC 时间戳、session/seq，借用至同步回调返回。20 FPS 目标，超时跳过周期，无帧 FIFO。从同一 Inkeys.Display 快照取主屏 bounds/workArea。每帧刷新同进程顶层窗口排除表。Host 不最小化、内容不呈现、不可激活且穿透。捕获与 Freeze 共享受管理的进程 Mag runtime 寿命，独立窗口互不争用。
检查 API/尺寸/缓冲、灰度均值/方差/近黑比例/信息量，不能仅凭相同 hash 判失败。超过 1s 无完成结果，监督暂停并撤销发布。PrintWindow 可能阻塞：UI 不 join，未返回时禁止新 worker，不 TerminateThread、不提前释放其窗口/运行库/缓冲。清理完成才可重启。

## Tracker
VisualMotionTracker 仅处理图像，不依赖 HWND/Draw3/UInk/UI。Initialize/Process/Reset，输出 accepted/delta/total/confidence/reason/stats。半分辨率灰度，8x6 网格约 320 角点；goodFeaturesToTrack、LK 21x21/maxLevel3、FB check、空间计权的纯平移鲁棒一致集和内点中位数。不引入 calib3d。用位移场空间变化拒绝显著缩放/旋转/重排。起点：40 有效点、65% 内点、FB约1处理像素、残差约1.5，结合覆盖/跳变/捕获质量输出0..1可信度。接受帧才更新参考/total/补点，拒绝后仍以最后可信帧为参考；补点内容坐标=current_image_point-total，维持累计坐标。高可信零位移接受。严重捕获无效立即 Paused，普通低可信连续3帧 Paused，不自动恢复。

## Session / Draw3
ScrollFollowService 状态 Off/Initializing/Tracking/Paused/Stopping。启用先 Desktop/模式检查和准入收尾，取 Draw3 基准/场景身份，再 F0/Tracker，最后辅助呈现/穿透握手，成功才 Tracking。失败回滚原工具与窗口状态。
新增窄 Begin/ApplyTarget/End。单槽 latest-only 目标包含 session/seq/文档页及运行代次，发布绝对累计内容位移。Draw3 owner 以初始 viewport-total 计算目标，像素/DPI只转换一次，复用 ApplyCanvasContentTranslationChecked/TranslateViewport 及原恢复路径。过滤重复 seq、旧 session/scene。End 关闭发布并携带最后可信目标作为可靠屏障，完成呈现后恢复输入。临时穿透参与既有有效 selection/双窗 ready 协调，工具/笔属性不丢失。保持导航门禁 false。
平移不追加 Undo、不清 Redo、不改点/PageGuid/UInk版本。Canvas-local composition cache 复用、屏幕热前像失效，L2/Tile 恢复沿现有实现。不新增长期GPU状态。

## UI / diagnostics / compatibility
More 面板中 Setting 前加入视图调整，现有布局顺序保留，仅补缺失新按钮。定格 active 禁用；工具/Undo/Redo/Clear/Freeze 先 stop 再执行；白板/PPT/文档/几何DPI/窗口/设备变化 stop。简单选中反馈，补SVG/中英繁体及生成资源。
--scroll-follow-console / --scroll-follow-verbose（后者含console）复用唯一 DiagnosticOutput::Session。状态和错误立即入队，性能/追踪每秒汇总，verbose逐帧；[ScrollFollow]前缀含 session/seq、Mag耗时/有效性、tracked/inliers/FB/residual/coverage/confidence/relay、实际viewport/clamp/recovery和清理结果。
Win8.1+，WOW64/能力失败安全禁用；不增加 Win7 不可用硬导入，库静态沿用现有配置。

## 阶段门槛
捕获窗口不可见而真实源变化仍连续读回、排除表和重复启停验证通过，才能进入 tracker；否则在 Mag 方案调查并保留可验证成果，不换截图技术，不接入按钮。离线 tracker 通过再接 Draw3。


## 实施阶段发现（2026-10-21）

捕获初始化和采样分开监督：初始owner资源准备使用5秒上限；开始采样后保持1秒无完成结果撤销发布，UI不等待系统调用。冷启动历史真实PoC曾读到变化像素，最新check复跑55帧全NearBlack；预持Freeze运行库的bare-lease及预建真实隐藏first-owner/global-once对照均在MagSetWindowFilterList返回50。当前保持全局首次Initialize/最后使用者Uninitialize的批准策略；额外初始化/反初始化实验尚未得到安全替代。最终对齐STA初始化顺序后，同owner两个控件的真实像素实验成功，但private bytes呈增长趋势，未证明安全的长期资源边界；不擅自迁移定格owner。阶段门槛未通过，后三子任务未进入实施，后续须先解决并重跑共享场景，不能以冷启动对照代替。
