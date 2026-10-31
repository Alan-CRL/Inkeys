# 实施代码独立检查（2026-10-19）

## 检查结论与范围

当前最终生产快照未发现需要阻塞交付的已确认新增缺陷。此结论仅表示源码审查结果，不代替构建、无窗口测试或真实系统输入验收。

检查者按 check.jsonl 加载了 PRD、design、implement、最新决定、研究记录及相关规格，检查范围包含新增且尚未跟踪的生产辅助头和测试文件。生产源码保持只读；本次唯一写入为本检查记录。

- MouseHook 回调、专用 jthread 消息泵及安装/卸载/停止顺序；固定原子鼠标键缓存；RoutePublisher 原子载荷和 fence 配对；单次快照尝试；SPSC 有界提交、入队后的抑制及失败链传；owner 心跳及代次退休。
- Bar owner 顶层与 Seek、颜色和粗细嵌套交互的有限批量轮询；栏外点击候选、100ms期限、栏内原生 Down 及触摸 Down 取消；成功上屏可见区域；模式、dock、设置和工作区变化退休候选。
- PPT 全局和原生本地入口的门禁与独立通道；唯一 owner 的两类累计状态；细分和多刻度处理；expected session token 原样进入最终 native business request；最终消费者复验；EndShow pending/in-flight 门禁及旧完成隔离。
- 实际 accepted stroke begin 的发布时机和本笔工具类别，包括临时右键/笔尾橡皮、形状与激光；续接/拒绝不重复通知；Host 生命周期、逻辑场景及 PPT identity 验证；短笔已结束后仍可消费起笔事件；原聚合 drawingActivity 合同保持。
- 三态策略、独立默认值、Floating 与普通/居中底栏判断、两种样式共用；Seek 内本手势 fold 基线、抓点和形态 barrier；外部取消判断保持。
- Inkeys3 Config 严格整数读取、缺失和非法字段默认、复制与冻结保存路径、NoUpload、旧配置隔离；设置四行下拉、三语文案与按字体测量的布局；工程项和无窗口测试接线。

## 已撤回的推断

初查时曾仅依据 InvokeDirection() 存在白板回调分支，推断改用 EnqueueLocalWheel() 会使白板滚轮翻页失效；此推断已撤回，不能作为本次发现。

实际准入证据：Inkeys/Inkeys/UI/PageControl/PageControl.cppm 的 ResolveWorkspaceInputPolicy(WorkspaceMode::WhiteboardExpanded) 返回 { true, false, false, false, false }，第四项 wheel=false。原生 WM_MOUSEWHEEL 先读取 state.targetVisible、!state.inputLocked 及该 workspace wheel 策略；因此稳定白板态的旧入口原本也不会调用 InvokeDirection()。新 token 路由拒绝白板，避免过渡中把旧 PPT 事件重新解释为白板命令，未证实稳定态功能回归。

## 新规格复核

已读取 .trellis/spec/native-desktop/mouse-hook-and-autocollapse.md。当前未发现需要修正的已确认实现夸大：生产入口、容量、250ms owner 租约、单 owner 累计、配置默认值及 accepted stroke 生命周期等陈述均有对应实现。该文 Tests Required 是验证要求，不是本检查者已完成这些运行验证的声明；交付记录应继续区分生产辅助逻辑测试与真实 Hook、Office/WPS、UIPI、Win7、触摸/触摸板运行。

## 验证责任边界

检查者没有执行编译、无窗口测试、系统 Hook 安装、GUI/Computer Use、Office 或设备测试，没有 commit/push。构建仍由主代理负责，实际命令、退出码、测试结果及环境限制应由主代理另行追加，不从本源码审查推定通过。

## 编译接线修正复核

主代理首次实际编译后报告并修复了受保护状态的访问位置。检查者已只读复核最终差异：原 owner 轮询函数迁为 BarUISetClass::PollGlobalMouseInput()，仍由 TryReadBarPointerMessage() 的既有单一交互 owner 调用；可见区域命中迁为 protected 成员 IsAutoCollapsePointerHit()。原受保护字段未公开，快照、候选、累计、accepted begin 和拖动处理逻辑未随此调整改变，未发现新增问题。

新增 auto_collapse_tests.cpp 的 _CRT_SECURE_NO_WARNINGS 仅作用该测试翻译单元，与工程既有 config_metadata_tests.cpp 处理共用传统头的方式一致；没有更改生产宏或全局告警策略。git diff --check --ignore-submodules=all 未报告空白错误。本追加复核没有运行编译、测试或 GUI；实际成功与否仍以主代理后续输出为准。
