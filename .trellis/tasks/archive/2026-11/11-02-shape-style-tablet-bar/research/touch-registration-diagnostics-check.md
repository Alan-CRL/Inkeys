# Touch注册与Pointer来源诊断独立审查

日期：2026-11-03。仅审查当前三文件新增注册诊断，保留之前共享样式、五窗Touch修复和详细日志dirty基线。未build、GUI或commit；产品源已冻结，由主会话统一验证。

## Findings (fixed)

UiPointerDiagnostics.h 的 WindowSnapshot 原本给任何状态快照都传Down样本，导致after_registration/message0被打印为输入phase=down。现仅WM_POINTERDOWN附输入样本，启动读回传nullptr，输出source=none/phase=none。ui_pointer_tests.cpp补真实TouchRegistration/WindowSnapshot输出断言，防启动日志制造输入边沿；未改Pointer Down的真实phase或来源。

## Verified contracts

- IdtMain的五个created回调仍每次恰好一次RegisterTouchWindow(hwnd,0)，只保存原BOOL，失败紧接GetLastError，随后诊断并继续原DisableEdgeGestures流程；没有重试或以注册结果改变输入处理。角色标签与原Raw的bar/bottom-left/right/middle-left/right一致。Haptics开关初始化位于3062附近，早于3444注册点。
- 成功注册强制registration_error=0且applicable=0，不把旧GLE冒充失败；helper保存恢复调用后GLE。失败输出调用点即时捕获的GLE，不被后续查询覆写。
- WindowSnapshot清零GLE后读owner和IsTouchWindow，owner0时pid归零；IsTouchWindow false/flags_valid0/error0与false/观测error非0分开，不把普通未注册认定成查询失败。is_touch并不等同received_touch，后者明确unknown。
- 新窗口/Pointer查询均在Enabled检查后；关闭路径不新增这些query、格式化或输出，原注册仍照常执行。
- Raw内新PointerDownSnapshot仅对entry=wndproc且WM_POINTERDOWN执行；Bar channel_callback、Move不会重复新增query。传原wParam取pointer ID，在原native消息处理期间查询，无Send/Post输入或捕获改变。
- GetPointerInfo由user32可选导出动态解析，没有Win8+硬导入，不依赖触觉controller是否存在或振动能力。缺API与查询失败分别标识；失败清部分info并unknown，不据兼容extra低位猜source。
- registration/window/pointer分为短行，保留v1基础字段；格式实参均为primitive及匹配类型，源device/target/type/flags/time/两套坐标均在pointer行，已有1536/768缓冲与truncated标记保留，现测试明确无关键字段截断。
- 查询替身只替换诊断读取；真实helper回归覆盖off零query/输出、GLE保存、注册成功旧GLE无效、失败、IsTouch false正常/观测错误、非owner标识、channel/Move跳过、每次Down可见、可选API缺失/失败清值，以及来源独立于compat签名。不创建真实HWND。

## Findings (not fixed)

未发现其余确定的本轮诊断正确性或输入行为变化。Pointer-only业务入口仍未补，本轮日志不声称修复该缺口；GetPointerInfo也不提供驱动Windows Ink复选框状态。

## Verification

- git diff --check --ignore-submodules=all PASS。
- 既有文件BOM与CRLF保持；本轮只修helper及对应测试，IdtMain既有标签改动保留。
- 完整ARM64构建、五生产探针、headless与Release x64由主会话执行；本代理未自行运行，不提前声明运行通过。最终证据由主会话的本阶段validation记录承接。

主会话最终验证：Debug完整Solution在保留单个本地ilk并重建后exit0/8.27秒（首次为链接器LNK1000内部错误，不改源码或工程绕过）；五生产探针全0，headless --no-window exit0/260.41秒；Release x64 exit0/332.72秒，PE/标识/无GetPointerInfo硬导入核验通过。设备仍待用户采集，见../touch-registration-validation.md。
