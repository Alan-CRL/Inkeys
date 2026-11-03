# R2详细输入诊断验证（2026-11-02）

## 授权与边界

用户批准在现有“测试触控反馈”控制台中补齐主栏/翻页诊断，以减少反复数位板测试。当前仅增加观测：未修改触摸/笔兼容鼠标去重、lease/capture/session处理策略、业务返回或默认消息处理；未修复点击、未接入分页振动、未新增设置/线程/依赖或工程配置。本轮不commit/push。

## 已实现

- UiPointerDiagnostics.h：ui_schema/ui_input v1、统一字段、固定32槽Move限频250ms、suppressed/truncated、ErrorScope；testSink仅测试条件编译，格式实参限制为primitive。
- UiPointerInput：只读DiagnosticId，锁内小型owner快照、锁外lease Begin/End/Cancel/析构输出；owner_snapshot=before_mutation。
- Bar：callback/WndProc entry分开，原始/过滤、单次Touch读取/逐点选择/early reject、capture、冻结按钮/背景命中、queue/consume/session取消与业务结果。
- PageControl：四角色及mode/visible/locked/semantic、Touch/Pen会话拒绝、捕获票据与异步释放拒绝、队列/消费者、Down/Up/repeat触发和回调派发；来源/代次依据明确。
- WM_TOUCH原始批次不伪造Down，Pointer来源未知不假定Mouse；终态不被Move限频；右/中/X按键边沿和PointerEnter/CaptureChanged纳入诊断。滚轮本轮不新增观测流。

## 独立审查

见research/diagnostics-check.md。修正未知Move来源误标、printf类型、lease快照语义、释放票据拒绝与raw_context错误码保护。逐branch审查未发现策略/返回值变化，未见provider/mailbox/scene锁内新增格式化输出。

构建发现本轮两处UiDiag命名空间别名位于global module fragment导致C5202，已移到module/import之后并重新构建；未压制warning或修改工具链。剩余warning为既有SDK/第三方代码。

## 构建与测试

使用vswhere定位ARM64原生MSBuild；完整InkeysRepo.sln Debug|ARM64；同PowerShell规范重复PATH并保留单一Path，MSBUILDDISABLENODEREUSE=1；构建上限900s。首次完整构建exit0/28.6s；审查修正后最终完整Solution增量exit0/16.48s，新增C5202已消失。

| 验证 | 退出码 | 耗时 |
| --- | --- | --- |
| --ui3-input-worker-test | 0 | 0.72s |
| --state-mode-production-test | 0 | 0.10s |
| --pen-width-production-test | 0 | 0.13s |
| --draw3-pen-width-test | 0 | 0.08s |
| --draw3-control-fence-test | 0 | 0.42s |
| InkeysHeadlessTests --no-window | 0 | 296.57s |

Headless新增用例直接运行实际helper/MouseChannel，覆盖disabled Raw不查询假窗口、LastError穿sink保护、字段/截断、Move边界/来源隔离/终态/32槽有界、真实lease拒绝/旧End/RawCancel/析构；sink回调取得provider锁检验输出确在锁外。最终PASS animation correctness。

日志与结构化退出码在ignored TestResults/tablet-ui-diagnostics/（build-results.json/tests-results.json及stdout/stderr）。静态git diff --ignore-submodules --check通过；既有源码BOM身份保持、工作树CRLF；新header UTF8无BOM/CRLF。无无关配置、依赖升级或源代码清理。

## 交付与局限

继续在实验选项开启“测试触控反馈”并重启本轮新版，按diagnostics-capture.md保留完整输出；应有event=ui_schema v=1和event=ui_input v=1。本轮没有GUI/Computer Use或高漫M6实机复测；新增日志和无窗口通过不代表现场点击已修复。R2仍待下一次真实日志定位，不自动归档任务。
