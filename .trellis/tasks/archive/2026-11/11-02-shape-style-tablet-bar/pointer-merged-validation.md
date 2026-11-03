# R3 合并版（Pointer 为根）实现与验证记录

日期：2026-11-03。状态：源码已冻结；Debug|ARM64、五个早退探针、headless、Release|x64 通过；设备验证待用户（见 pointer-candidate-capture.md）。不 commit/push。

## 实现（与 pointer-source-design.md 的偏差已注明）

- `UiTouchRouting.h / UiPointerInput.cpp`：`Touch::PointerMode()`（GetProcAddress 的 GetPointerInfo；`INKEYS_UI_TOUCH_LEGACY=1` 强制 Legacy）、`Touch::HandlePointer()`、`DispatchDecoded(..., originIsTarget)`、PointerMode 下 `HandleMessage` 丢弃 WM_TOUCH（`touch_dropped`）、测试钩子 `Testing::SetPointerQuery/ForceLegacy`。
- **偏差**：未引入新的 `Contact` 值类型；Pointer 接触合成值型 `TOUCHINPUT`（`hSource=sourceDevice`、`dwID=pointerId`、`dwTime=info.dwTime`、`x/y=ptPixelLocationRaw*100`、PRIMARY/PEN 位），下游 Bar/Page Deliver、`Context::Owns`、触觉 `BindDown/Touch` 一行不改。坐标取 Raw（与触觉观察同源，日志中与 processed 一致）。
- `Bar.Interaction.cpp`：`ObserveBarHapticPointer` 之后一行 `HandlePointer`。`PageControl.cpp`：窗口过程一行 `HandlePointer`；`ProcessNativePointer` 对活动 Touch/Pen 接触期间的真实 `WM_MOUSELEAVE` 标记 `event.touch`，worker 的 MOUSELEAVE 分支据此不清按压/长按状态（仍保留边缘光效离开通知）。
- 既有 `Probe::Swallow`（`INKEYS_POINTER_PROBE=1` 且诊断开启）仍在 `HandlePointer` 之前；启用时 UI 接触被吞、手指/笔点不动（仅诊断用）。
- 两个缺陷的对应：缺陷2（Surface 笔“更多”展开后主栏首击被取消）由“目标=接收窗，downOrigin==target”结构性消除；缺陷1（手指书写后翻页首击无效）由 MOUSELEAVE 保护修复。

## 独立检查（trellis-check）与处理

无 must-fix。已处理 should-fix：
1. 丢失终态：同设备新的主接触撤销旧主接触（不含 preparing）；Up/Update 查询失败撤销该 id 的路由。
2. 第二键拒绝前先撤销同 id 的旧路由。
3. Page MOUSELEAVE 不再整条丢弃，改为只保护按压状态。
4. 测试加强：GetPointerInfo 替身污染 GLE（成功与失败分支都验证调用方 GLE 保留）；WM_TOUCH 丢弃/Legacy 读取用诊断输出区分；`ForceLegacy` 覆盖 Legacy 契约；新增丢终态、查询失败 Up、先撤销后拒绝用例。
说明（已记入设计/规范）：`WM_POINTERCAPTURECHANGED`/`LEAVE` 暂不作为终态（避免 SetCapture 重入同步触发和窗口外拖动被误断），设备日志观察 `bar_invalidate reason=capture_changed`；设计文档“Contact 值类型”以 TOUCHINPUT 合成替代。

## 验证

沿既有 InkeysRepo.sln，vswhere 定位 ARM64 原生 MSBuild，单一 Path，禁用节点复用；脚本和原始日志在 ignored `TestResults/pointer-probe/`（merged*、merged2*、merged-release-x64*）。

| 项目 | 结果 |
| --- | --- |
| Debug\|ARM64 完整 Solution（审查修复后） | 0 错误 |
| --ui3-input-worker-test / --state-mode-production-test / --pen-width-production-test / --draw3-pen-width-test / --draw3-control-fence-test | 均 exit0 |
| InkeysHeadlessTests --no-window | exit0，`PASS animation correctness`，含新增 PointerMode 路由用例 |
| Release\|x64 完整 Solution | 0 错误，101.99 秒 |

### Release x64 产物

- Build/x64/Release/Inkeys.exe，2026-11-03 18:00:26，39,887,872 字节，PE Machine=0x8664。
- SHA256：30CE46A2C3EE733AA7DA4A588F4413E86656A09CEF386122755B8ABE94EE5959
- 标识：`INKEYS_UI_TOUCH_LEGACY`（UTF-16）、`pointer_route`、`touch_dropped`、`pointer_mode_derived`、`lost_primary_cancelled`、`stale_owner_cancelled`、`probe_contact` 均存在；dumpbin /imports 无 `GetPointer*` 硬导入（770 行）。
- 未启动 GUI，未在设备上运行。

## 设备验证关注点

- 在 `WM_POINTERDOWN` 内 SetCapture 的副作用（探针版未做）：看 `bar_invalidate reason=capture_changed` 是否出现在 Down，笔震动是否仍每次点击一次。
- 窗口外拖动抬起（未实测）。
- 缺陷1/2 的复测、长按（PPT 翻页重复）、手指与笔的悬停。
- 异常时用 `INKEYS_UI_TOUCH_LEGACY=1` 回到旧来源并回传两份日志。

## 第二轮候选（震动丢失修复 + 白板翻页转圈）

- `Bar.Interaction.cpp`：`ObserveBarHapticPointer` 的“非有效 Pointer”分支改为 `Invalidate(id, true)`，笔抬起后紧接着离开感应范围的 UPDATE 不再作废已完成、待消费的点击凭据（候选日志 20 次点击丢 2 次震动）。
- `Bar.BusyRing.h`（新）、`Bar.Scene.cpp`、`Bar.Rendering.cppm`：白板 `switching` 超过 0.3 秒后在页码按钮右侧显示旋转圆环（复用 `DrawProgressRing`，经新增的公开 `DrawRing` 包装；28% 弧段、0.9 秒一圈）；`switchingSince/switchingTimed` 在 `SetContent` 的 switching 切换处记录，`Advance` 在翻页期间保持帧推进并在圆环可见后每帧 `IncludeButton(3)`，`Render` 在分页按钮之后绘制。纯函数 `BusyRingVisible/BusyRingAngleDegrees` 有 headless 测试。
- 验证：Debug|ARM64 0 错误；五个早退探针与 headless exit0（含圆环延迟/角度用例）；Release|x64 0 错误，Build/x64/Release/Inkeys.exe 2026-11-03 19:07:33，39,889,408 字节，PE 0x8664，SHA256 015E5D361224F014AE654D1C2C44C7DF0C63B3C3AB3DF831A8B8BDAE4D4A5EAC，无 GetPointer 硬导入。
- 未验证：圆环的实际位置/大小/颜色和旋转流畅度（无 GUI），需设备上目视；翻页控件震动按用户决定暂不做。

## 最终用户 M6 验证（2026-11-03）

日志：M6-input(1).txt（5198 行，原件已归档到 ignored `TestResults/pointer-probe/m6-final-user-capture.txt`；build 19:06:10，即 SHA 015E5D36… 的包）。用户反馈无问题。

- Pointer 来源：77 次 PT_PEN（bar 69、左下 6、右下 2），`WM_TOUCH` 0 条（读取/丢弃均 0）；75 次路由 Down 全部 accepted，Up 与之一一对应，0 拒绝、0 route_cancel、0 worker_reject、0 down_reject、无 lease 异常。
- 无重复输入：411 条带签名兼容鼠标全部被过滤，无普通鼠标 Down 进入业务；Bar 65 次点击 `business_accepted`、翻页 8 次激活（左下 6、右下 2），每次接触恰好一次业务。1 次 `cancel_or_release_outside` 与 1 次 `callback_not_accepted` 是按下后拖出再抬起的测试，符合预期。
- 触觉凭据：Bar 69 次 Down 全部 matched，68 次 Up matched，64 次 product 发布（该机器无震动后端，discrete 发送为 0）；`bar_invalidate` 仅 cancel_mode/hidden 各 1。
- 画布书写正常（canvas_ingress 118）。

结论：与最终用户同款数位板（只发 Pointer 与兼容鼠标、无 WM_TOUCH）在 Pointer 模式下主栏与翻页均可点击，无重复；R3a–R3d 验收满足，R3f 的设备验收完成。
