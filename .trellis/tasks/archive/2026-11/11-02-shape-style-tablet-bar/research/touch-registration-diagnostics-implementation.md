# Touch 注册与 Pointer 来源诊断实现

日期：2026-11-03。当前状态：源码已冻结；实现子代理未构建或启动 GUI，统一验证由主会话执行。

## 文件和接口

- `Inkeys/IdtMain.cpp`：原 touchRegisterFuc 改为固定角色 callback factory，五窗分别标记 bar/bottom-left/bottom-right/middle-left/middle-right。每个 created 回调仍恰好调用一次 `RegisterTouchWindow(hwnd,0)`，在下一条 native 查询前立即保存失败 GLE；成功的诊断错误码明确为 0。其后原 DisableEdgeGestures、Touch adapter 登记和原流程顺序继续。
- `Input/UiPointerDiagnostics.h`：添加小范围诊断 helper `TouchRegistration`、`WindowSnapshot`、`PointerDownSnapshot`。新增 `NativeQueries` 仅包含这些旁路查询；headless 的 `testNativeQueries` 不进入业务路由。
- `InkeysHeadlessTests/ui_pointer_tests.cpp`：真实 helper 的无 HWND 查询替身回归。未修改 Bar/Page 路由、过滤、capture、lease、haptic 或其他产品文件。

## 输出字段

所有输出继续 `[PenHaptics] event=ui_input v=1` 基础格式，分为三条短记录，不拼成单条超长行。

1. `stage=touch_registration reason=registered/register_failed`：
   `registration_result` 为实际 BOOL，`registration_error`、`registration_error_applicable`、`requested_flags`、`call_tid`。role/HWND 在原公共字段内。
2. `stage=touch_window_snapshot reason=after_registration/pointer_down`：
   `call_tid owner_tid owner_pid owner_matches owner_error is_window visible enabled is_touch is_touch_flags is_touch_flags_valid is_touch_error`。
   IsTouchWindow 查询前清零 GLE，立即记录 false 后的观察值；false+0 不被称作 API 失败。成功明确错误码 0，flags_valid 表示读回 flags 是否有注册意义。输出 `error_basis=cleared_before_query received_touch=unknown`，不把“已注册”写成“已收到 Touch”。
3. `stage=pointer_info reason=queried/query_failed/api_unavailable`：
   `pointer_api pointer_ok pointer_error pointer_error_applicable requested_pointer info_pointer pointer_type pointer_flags source_device hwnd_target frame system_time pixel raw_pixel`。
   来源仅从成功的 GetPointerInfo.pointerType 得到，失败/可选 API 缺失均为 unknown，并清空可能部分填写的字段。输出 `source_basis=GetPointerInfo touch_hsource=unknown`，不把 Pointer sourceDevice 映射成 TOUCHINPUT.hSource 或 Windows Ink 驱动开关。

## 接线和兼容边界

- 通过既有 Raw 的 `entry=wndproc` 接点调用 PointerDownSnapshot，因此每个真正 WM_POINTERDOWN 均采一组；Bar 的 channel_callback 只保留旧 raw 记录，跳过新增状态/API查询。Move 不增加查询，原有 250ms limiter 保留。
- PointerDownSnapshot 不受触觉控制器、笔震动能力或 getCursor 成败约束。
- GetPointerInfo 使用 GetProcAddress 延迟解析，无 Win8+ 硬导入；API 缺失仅输出观察状态。窗口/Touch 查询均为现有 Win7 范围。
- Enabled=false 时 helper 在 query、API解析、分配、格式化之前返回；原 RegisterTouchWindow 始终执行。未增加重试、额外注册、窗口/设备枚举、Send/Post、捕获或业务输入路径。
- ErrorScope 包围查询和输出，保持调用者原 GetLastError，包括成功注册后遗留的旧值；查询自己的失败码立即保存。查询与日志不持输入/窗口/Scene 业务锁。

## 回归覆盖

仅替换新增诊断的只读 native 函数和输出 sink；假 HWND 没有传给真实窗口 API，不创建 HWND 或 GUI，调用生产 helper 而不复制算法。

- 关闭时 TouchRegistration/WindowSnapshot/PointerDownSnapshot 零查询、零输出、保留 GLE。
- Register 成功忽略旧错误，owner 与 Touch 成功读回 flags；注册失败保留调用方立即捕获的错误。
- IsTouchWindow false+0 与 false+新观察错误区分；owner 查询失败及非 owner 线程均仅记录。
- channel_callback、Move 不执行新增查询；每次 native PointerDown 均采集，不被重复 Down 限频。
- GetPointerInfo 成功的原始 ID、Pen/Touch/Mouse 类型、flags、sourceDevice、hwndTarget、系统时间、processed/raw 屏幕坐标。
- Pointer API 缺失与查询失败分开；失败输出部分字段清零、不猜来源；输出 sink 修改 GLE 后仍恢复原值。
- 三组日志正常字段未触发 truncated=1。

## 验证与局限

实现者已执行 `git diff --ignore-submodules --check`，通过；三个文件保持原 UTF-8 BOM/无 BOM 与 CRLF，无 bare LF。未执行 build、headless、GUI、commit 或 push；主会话负责完整 Debug ARM64、Release x64 和无窗口验证。没有增加 Pointer-only 业务入口，也未确认任何设备 Windows Ink 配置；待用户下一轮同设备/同驱动设置日志验证实际注册与消息链。
