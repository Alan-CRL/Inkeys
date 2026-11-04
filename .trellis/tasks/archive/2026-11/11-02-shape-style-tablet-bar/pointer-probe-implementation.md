# 阶段1探针实现记录

日期：2026-11-03。状态：源码已冻结；Debug|ARM64、五个早退探针、headless 与 Release|x64 已通过（见文末验证），设备采集待用户执行。

## 改动（均默认关闭）

- `Input/UiPointerDiagnostics.h`：新增 `Diagnostics::Probe`。
  - `Requested()`：读环境变量 `INKEYS_POINTER_PROBE=1`（进程内一次，保护调用者 GLE）；`Active()` 另需“测试触控反馈”诊断开启。
  - `Swallow(role,hwnd,msg,wParam)`：仅处理 `WM_POINTERDOWN/UPDATE/UP`，用可选 `GetPointerInfo`（已有 `PointerInfoQuery`）确认 PT_TOUCH/PT_PEN；`UPDATE` 需带 `POINTER_FLAG_INCONTACT`，悬停和 PT_MOUSE 不吞；API 缺失/查询失败不吞。返回 true 时调用方直接 `return 0`。
  - 追踪每个 pointerId 的按下窗口（16 槽，mutex 保护），在 `probe_contact` 记录 `known_down/same_window/down_window/in_receiving_rect/in_down_rect` 及累计 `swallowed/legacy_touch_seen/legacy_signed_mouse_seen`。
  - `CountLegacy`：在既有 `Raw(...,"wndproc")` 内统计 `WM_TOUCH` 与带 `0xFF5157xx` 签名的左键消息；`Announce`：在 `TouchRegistration` 末尾对每窗输出一次 `probe_mode`。
  - 诊断关闭或未设环境变量：零查询、零分配、零格式化。
- `Bar.Interaction.cpp`：`ObserveBarHapticPointer` 之后一行 `if (UiDiag::Probe::Swallow("bar",...)) return 0;`（触觉旁观保留）。
- `PageControl.cpp`：签名鼠标过滤之前一行 `if(!dispatchedPointer&&UiDiag::Probe::Swallow(SurfaceName(index),...))return 0;`。
- `InkeysHeadlessTests/ui_pointer_tests.cpp`：真实 `Probe` helper 回归——开关关闭/诊断关闭零查询、announce、非接触阶段与 PT_MOUSE/查询失败/API 缺失放行、finger/pen 吞下、GLE 保护、跨窗 Up 的 `same_window=0`、Up 后退休、派生计数。

## 边界

不接入任何业务；不改变 Touch 注册、过滤、lease、capture、haptics 业务；悬停消息仍交 DefWindowProc；不新增硬导入。探针启用时手指/笔点击无响应是预期。

## 验证结果

沿既有 InkeysRepo.sln，vswhere 定位 ARM64 原生 MSBuild，单一 Path、禁用节点复用；脚本与原始日志在 ignored `TestResults/pointer-probe/`。构建命令曾被自动模式分类器连续拒绝，用户在会话中重试放行后才执行；未改项目、依赖或权限配置。

| 项目 | 结果 |
| --- | --- |
| Debug\|ARM64 完整 Solution | 0 错误，44.46 秒；Inkeys.exe 与 InkeysHeadlessTests.exe 均重新链接 |
| --ui3-input-worker-test / --state-mode-production-test / --pen-width-production-test / --draw3-pen-width-test / --draw3-control-fence-test | 均 exit0，0.28–0.74 秒（bash 直接运行的真实退出码，见 probes-exit.txt） |
| InkeysHeadlessTests --no-window | exit0，`PASS animation correctness`，含新增 Probe 回归 |
| Release\|x64 完整 Solution | 0 错误，422.45 秒 |

说明：用 PowerShell Start-Process 重定向运行测试时，ui3-input-worker 与 draw3-control-fence 曾显示 120 秒超时、其余 exit 为 null，属脚本等待重定向句柄的问题；同一命令在 bash 直接运行均秒级 exit0，以后者为准。

### Release x64 产物

- Build/x64/Release/Inkeys.exe，2026-11-03 15:37:30，39,884,800 字节，PE Machine=0x8664。
- SHA256：F0DC0EFBC9AC7D7E098F903D5C3916FB7FB1A8E5ED7F704B667B3962984D71A5。
- 标识存在：`INKEYS_POINTER_PROBE`（UTF-16）、`probe_contact`、`probe_mode`、`legacy_signed_mouse_seen`、`ui_schema`、`touch_registration`。
- dumpbin /imports：无 `GetPointer*` 硬导入（770 行，user32 正常导入），可选 Pointer API 仍为 GetProcAddress。
- 元数据：TestResults/pointer-probe/release-x64.artifact.json、release-x64.imports.txt。
- 未启动 GUI，未在设备上运行；这是探针包，不是修复版。

## 下一步

用户按 pointer-probe-capture.md 采集（M6 板、旧板、手指各一轮，有设备者即可），按文末判定表决定阶段 3 采用“不转交 DefWindowProc”还是“类型排除”兜底。
