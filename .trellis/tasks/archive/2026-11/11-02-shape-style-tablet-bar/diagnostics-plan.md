# R2 主栏/翻页间歇点击诊断补充（已批准）

2026-11-02用户明确批准继续在现有输出中补齐信息，要求尽量详细以减少反复设备测试。本轮只增加观测，沿用Experimental.Inkeys3.ConsoleOutput.Haptics和已有非阻塞DiagnosticOutput；不新增设置，不修触摸/兼容鼠标/capture/lease策略，不接入分页振动，不commit/push或GUI。

## 变更边界与文件

问题是既有日志在Touch准入后才打印，缺少原始到达、共享lease拒绝、窗口捕获/真实命中和业务结果。必须沿实际链路观测，不拿触觉token失败代替UI拒绝。

- Bar.Interaction.cpp：主栏QueueWindowMessage/WndProc原始消息与兼容鼠标过滤、WM_TOUCH读取/逐触点选择、捕获取得/释放、共享队列生产/消费、会话取消原因、已呈现widget/主体命中与真实业务接受。
- PageControl.cpp：四个分页窗口真实role/index、输入snapshot语义/可见/locked/工作区、WM_TOUCH/笔兼容消息、租约和Session拒绝、capture/队列/worker业务结果、方向动作和长按repeat；能区分PPT/白板和左右控件。
- UiPointerInput.h/.cpp：诊断快照或事件记录共享非鼠标租约requester/owner及generation、Begin/End/Cancel/析构释放；诊断不得为了读取状态再次进入同一锁。
- 必要的小型公共诊断helper与现有无窗口测试：统一字段/限频/关闭开关/LastError保护/长接触关联。新增文件仅在现有工程不能发现时最小登记，不改工具链。

## 输出合同

1. 统一前缀及键名（保持PenHaptics输出体系，可新增ui_raw/ui_touch/ui_lease/ui_queue/ui_session/ui_action等event）。每个关键记录有stage/reason、进程单调tick、线程/窗口role与HWND、msg名称/值、来源、contact/generation，已有相关原始时间与extraInfo保留。
2. 原始Pointer Down/Up和WM_TOUCH在任何业务过滤前可见；兼容Mouse来源及被过滤原因可见。触点逐项记录device/id/flags/phase、GetTouchInputInfo失败及原始错误，primary/fallback/active匹配/producer已有状态、BeginTouch拒绝均可区分。不可二次读取或消费输入handle。
3. 能闭环22对右下Touch：窗口rect/client/屏幕点、同帧呈现的background/widget命中与revision、capture前后和票据、lease归属。只能读取现成snapshot/状态，不在日志中跨窗SendMessage/WindowFromPoint/NCHITTEST，也不对视觉对象跨线程裸读。
4. 能闭环27对Pointer：区分根本无原始Touch、GetTouchInputInfo失败、触点选择/lease/activeSession拒绝、排队失败、消费取消/语义失效和业务未命中。lease记录requester/owner标识与各自generation，End旧代或非owner忽略也给reason。
5. Down/Up/Cancel/拒绝/业务接受完整记录，Move只限频/聚合，带接触/窗口key和suppressed数量；无论是否收到Move，都不限制按住时长。不让连续PointerUpdate或capture查询刷满队列。跟踪状态有界，不新增长期线程。
6. 诊断关闭时快速返回，不做昂贵系统查询、format或分配。诊断路径保存/恢复GetLastError；锁内仅复制必要小状态，退出provider/mailbox/scene锁后再发布日志。复用现有非阻塞输出，日志丢弃保留统计，不以诊断成功决定输入业务。
7. ui_action业务接受与bar_click触觉排队含义分开，PPT Down触发/白板Up触发/长按repeat可区分；操作被拒也说明首个实际原因。正常控制台输出不默认写IDTLogger文件，继续保留stdout/stderr重定向能力。

## 实施与验证

- implement子代理负责产品与相关测试；主会话规范/文档/构建；独立check核对所有新增日志不改变分支结果、返回值、capture、session/lease和默认消息处理。
- 现有纯无窗口测试覆盖关闭开关/错误状态保护/限频边界与终态不丢、格式字段及有界性；测试诊断帮助函数或生产接缝，不复制业务状态机。
- 完整InkeysRepo.sln Debug|ARM64，vswhere找ARM64原生MSBuild，同调用移除重复PATH后恢复单一Path、MSBUILDDISABLENODEREUSE=1，总超时900s；不因环境问题改源码。
- 针对输入生产探针（实际适用的--ui3-input-worker-test等早退无窗口入口）与headless --no-window（900s）验证，git diff --check/BOM/CRLF与范围检查。明确未做GUI或高漫真机复测。
- 用户后续保持同一开关与采集方式即可。最终给出输出字段/简短对照流程，注明诊断不是已修复点击问题。
