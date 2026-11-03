# R2诊断实施交接（2026-11-02）

本轮只增加日志，未改变输入过滤/Touch选择/租约准入/capture/Session/翻页返回结果，也未接入分页震动。共享样式代码保持本轮已提交基线，未再次修改。

## 修改文件与接口

- Input/UiPointerDiagnostics.h：新增header-only诊断helper，沿现有HapticDiagnostics::Enabled和DiagnosticOutput固定异步输出。event=ui_schema报告v1/build/开关与限频配置；event=ui_input统一tick/tid/role/HWND/stage/reason/msg/source/phase/contact/generation/device/sample_tick/time/point/suppressed/truncated。detail768/line1536，超长detail显式truncated。ErrorScope保护额外系统查询和输出的LastError；关闭时不查询窗口、不格式化、不分配。
- Input/UiPointerInput.h/.cpp：新增只读DiagnosticId用于channel到role/HWND映射；BeginTouch/EndTouch/析构/Cancel/Detach记录requester、旧owner、请求和持有generation，明确other_channel_owns_lease、stale_generation、not_owner、raw_cancel_does_not_release_touch等原因。LeaseEvent在原锁外析构输出；通用Session的Begin/Accept拒绝补充session地址与活动身份。
- Bar.Interaction.cpp：channel_callback与wndproc分别标entry。raw Pointer/Touch/promotedMouse可与原bar_pointer/bar_touch日志对照；单次GetTouchInputInfo成功/失败、逐触点phase/flags/mask/primary/fallback/producer/device/选择或拒绝、lease拒绝完整。capture票据、已有presented maps里的真实按钮/背景命中和revision、Push结果、消费/旧cancelSerial/来源拒绝、revision/fold/desktop取消均可见。business_accepted与callback_not_accepted独立于旧bar_click触觉队列结果。
- PageControl.cpp：四个SurfaceName和index/模式/visible/locked/semantic/channel/session/capture可关联。保留Touch兼容过滤、Pen兼容分支、原Touch决策；记录其首个拒绝及capture后的语义/region/session准入。worker显示真实命中与变更、release_decision/button_release，direction_on_down/direction_on_up/long_press_repeat，最终direction_callback_dispatched或no_direction_callback。日志在既有snapshot/callback/scene/provider锁释放后输出。
- InkeysHeadlessTests/ui_pointer_tests.cpp：复用RunUiPointerTests添加真实helper与MouseChannel用例。testSink只在现有DRAW3_CONTACT_TESTING编译条件下存在，普通产品不开放替换日志出口。

## 限频与关联

- 每个线程固定32槽，无动态接触表；按window/source/device/contact/generation/stage隔离，Move250ms摘要，Up/Cancel不受限并带suppressed。
- WM_TOUCH原始消息没有phase，入口标batch并限频，在既有一次GetTouchInputInfo解码后真实Down/Up逐点完整。touch_read成功也摘要；读取失败完整带原GetLastError。没有第二次读取/消费触摸handle。
- batch序号按线程单调增长；应按tid+hwnd+batch关联原始与解码记录，lease用channel标识映射回role/HWND。主栏HiMsg callback和WndProc不是重复OS投递，entry字段明确区分。
- Pointer原始source标unknown，不以WM_MOUSE的ExtraInfo签名推断Pointer类型；既有bar_pointer保留其动态API核验细节。原始位置/time标明GetMessagePos/GetMessageTime来源，wParam/lParam/extra完整保留。未新增Pointer API硬导入、WindowFromPoint或跨窗同步消息。

## 检查与测试接入

- git diff --ignore-submodules --check通过。六个源码/测试/新header均UTF8无BOM、CRLF，BareLF=0。未修改工程配置/依赖/设置/线程。
- 新用例接入既有headless RunUiPointerTests（InkeysHeadlessTests.exe --no-window），覆盖禁用时假HWND快返、LastError跨输出sink保护、关联字段/可见截断、250ms边界、不同source隔离、终态不抑制、32槽有界，以及真实lease成功/拒绝/旧End/RawCancel/析构。
- 测试sink在输出时调用真实RawMouseAvailable取得provider锁；用例完成同时验证lease日志输出未持provider锁。没有复制输入状态机公式。
- 代码已通知主会话冻结，完整Debug|ARM64构建、headless及--ui3-input-worker-test等适用早退探针由主会话统一执行；此交接不冒充已运行通过。无GUI/高漫M6真机复测，无commit/push。

## 下次日志如何闭环

对某次M6 Down/Up：先看raw/bar_pointer是否到目标HWND→touch_read/逐点touch_select→lease_begin成功或owner拒绝→capture+presented_hit→queue→consume/session/worker_reject→action或button_gesture取消。只有business/action接受才说明UI实际接受，bar_click accepted仅说明可选触觉排队。批次摘要有suppressed时不能把缺失Move当未到达；丢日志仍沿既有DiagnosticOutput丢弃统计判断。
