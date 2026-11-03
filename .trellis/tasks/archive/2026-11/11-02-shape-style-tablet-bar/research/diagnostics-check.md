# R2 diagnostic-only independent review

## Findings (fixed)

- UiPointerDiagnostics.h: 无样本Move限频占位原本默认Source::Mouse，可能把未知过滤消息误报物理鼠标。现显式unknown，并新增真实helper回归。
- UiPointerDiagnostics.h: truncated布尔和touch_count的WORD提升类型与%u不匹配，已显式转unsigned；Log增加仅接受算术值/裸指针的编译期检查，防shared_ptr/IdtAtomic直接传给snprintf。没有新增业务字段转换。
- UiPointerDiagnostics.h: lease owner/owned是在锁内复制的变更前快照，明确输出owner_snapshot=before_mutation，避免acquired被解读成新owner。
- PageControl.cpp: 异步释放capture票据被原分支拒绝时补requested_ticket/owned_ticket/channel日志，ShouldReleasePageCapture仍只求值一次；raw_context新增LastError保护范围。
- UiPointerDiagnostics.h: Raw补右/中/X Down/Up/DoubleClick可见性及正确phase，不会因默认Move而吞终态；补PointerEnter/PointerCaptureChanged名称。仅诊断识别，不改变原WndProc/兼容鼠标过滤；本轮不新增滚轮观察。ui_pointer_tests直接测试实际RawMessageKind/InputMessage/MessageName。

## Review evidence

逐个业务diff核对：复合条件仅把原Session::Begin结果保存到局部诊断变量，原短路、求值顺序与返回值不变；lease/capture/default处理未修改策略。主栏callback日志区分回调接受与触觉反馈；Page按下/抬起/长按触发和实际方向callback派发分开。

LeaseEvent先于provider锁guard构造，因此函数返回时先释放provider锁，再在诊断析构里format/输出；测试sink调用RawMouseAvailable可检验无同锁重入。未发现mailbox/scene锁内新增format/输出。调用点额外窗口查询都在Enabled分支且有ErrorScope；关闭路径不新增GetTouchInputInfo、WindowFromPoint或Pointer API查询。Raw只读取一次GetMessageExtraInfo，输入WM_TOUCH句柄仍沿原路径一次解码和关闭。

Down/Up/Cancel不被MoveLimiter限频；逐触点阶段保留真实终态，原始WM_TOUCH阶段标batch而不伪装Down。状态有界32项，Move带suppressed，格式截断和现有输出队列丢弃分别可见。角色/通道/lease归属、capture票据、入队/消费与真实呈现hit可关联22组Touch与27组Pointer缺口。

## Findings (not fixed)

没有发现需要改动输入策略的确定缺陷；本轮仅日志，不宣称点击已修复。诊断可指出第一个失败阶段，但无本轮新M6现场日志，不据此断言驱动/Windows消息派发根因。

## Verification

- git diff --check --ignore-submodules=all PASS。
- 修改既有文件使用原UTF-8 BOM身份与CRLF；新header UTF-8无BOM/CRLF。
- 首轮完整Debug ARM64主会话已通过；本轮审查修复后重新build/五个生产探针/headless由主会话统一执行，最终结果见其validation记录。
- 本代理未build、未运行GUI或真实设备测试，未commit/push。

## 主会话最终验证回填

module别名位置修正后最终完整Solution Debug|ARM64 exit0/16.48s；五个生产探针全部exit0；headless --no-window exit0/296.57s。详情见../diagnostics-validation.md。未GUI/commit/push。
