# WindowControl 实施记录

日期：2026-10-19。实施代理范围仅 `Draw3.WindowControl.cpp/.cppm` 与本文；其他光标视觉、配置、设置和测试由对应责任方处理。

## 实施前边界

已读取 Trellis implement 代理、before-dev、当前 PRD/design/implement/manifest，及 native-desktop 的窗口所有权、Touch 临时归属、schema=2 有界诊断、RTS/Mouse 单 producer、兼容研究。默认 `python` 命中不可执行的 WindowsApps alias；未更改环境或安装依赖，主代理统一进行 Trellis/构建/测试验证。

当前明确缺口：窗口 property 与 query 共用 mask 未包含禁用 Touch UI 的 FORCEOFF；SetProp 未检查；Touch End 没有刷新请求；重复 Pen Clear 早退会跳过系统光标刷新；selectionMode/窗口重新显示没有主动收敛系统光标。当前系统诊断只有决策，没有实际 HCURSOR flags/handle。

最小修改位置为 WindowControl：首次接触前固定设置 FORCEOFF（保留现有多点与手势位），记录 SetProp 成败并在脱离/销毁时恢复本对象借用前的属性；终态保留 owner/Touch suppression，只重新排队 owner 线程刷新；选择模式在本 HWND 命中范围交还普通光标，显示/隐藏边界请求刷新；为现有决策补有界、低频的 GetCursorInfo 观测。既有文件均为 UTF-8 BOM、CRLF。

不修改输入来源过滤、未知 Mouse 位置/按键回退、设备身份、逐 Touch 橡皮尺寸/生命周期、全局系统/注册表/ShowCursor 计数。不新增 Win8 静态导入、不发送跨进程 WM_SETCURSOR/鼠标消息、不创建第二套 producer。缺乏 Win8+ 反馈故障证据，因此不扩展 SetWindowFeedbackSetting。保留 WindowFromPoint 所有权保护；静止穿透后的下层应用样式交接仍须真实设备验证。

## 实际修改与静态自查

已完成以下修改：

- 固定 `kTabletInputFlags` 加入 SDK 原生 `TABLET_DISABLE_TOUCHUIFORCEOFF`；与首触前 `MICROSOFT_TABLETPENSERVICE_PROPERTY` 和 query 返回值共用一个 mask，多点及原长按/轻拂控制不变。
- SetProp 返回值明确检查，失败记录真实 Win32 错误（无有效错误码则日志使用 ERROR_GEN_FAILURE）且继续既有运行；保存附着前的属性值。脱离/销毁通过同一幂等恢复入口，只在当前属性仍等于本对象安装值时恢复借用前值或删除本对象新增值；失败记录，宿主后来改过的值不删除。
- 附着时重置系统光标刷新去重位，避免旧 HWND 未执行的刷新阻断新代请求。
- Touch End 和重复 Pen Clear 均重新排队 UI owner 刷新；不解除 Touch suppression，不恢复旧 Hover，不变更 Mouse/Pen owner。保持真实 Mouse/Pen 接管合同。
- 主工具真实变化、选择模式变化、WM_SHOWWINDOW、窗口 SHOW/HIDE 状态变化均排队刷新。selection 只取消普通 Mouse/Unknown 的 Eraser/Laser 强制隐藏并采用系统鼠标策略，Pen/Touch 和 Touch Pan 保留原隐藏逻辑；白板选择态仍有主 Drawpad，不能用全局 `!SelectionMode()` 关闭所有隐藏。实际/旧诊断 trace 使用相同参数投影。
- `ApplyWindowCursor` 保留 WindowFromPoint/IsChild 检查；销毁阶段原先无条件恢复箭头也改为命中本画布才执行。任何 skip 都不更改其他应用 HCURSOR，不发跨进程消息。
- 现有 `system` 决策行补 selection；新增 `system-observe` 保存实际 GetCursorInfo 的 before/after 成功、错误码、flags、handle，以及 target/hit HWND 和所属线程。skip 情况记录未应用。仅生命周期刷新、决策/命中变化或至多每秒一次采样；关闭 Cursor 诊断不查 GetCursorInfo/线程、不格式化这些行，LastError 由只读观测保留。PostMessage 失败另记错误码。
- 已追踪强制 posted-refresh 观测的生产者：Touch barrier helper 只由 Touch Down（非 Move）调用；Pen/Mouse 样本仅 valid/contact/inverted 等生命周期变化排队；owner/suppression/工具开关均真实变化去重。重复 Clear 在 Up/OutOfRange/取消/停产终态，不是每包路径；常态 WM_SETCURSOR 仍限频。

静态自查结果：

- 本代理只修改 WindowControl 两文件和本文；源码 diff 为局部 hunk，无整文件格式化。
- 两源码仍为 UTF-8 BOM、纯 CRLF（cpp 2171 行、cppm 374 行，无裸 LF），严格 UTF-8 解码成功；新研究文档维持 UTF-8 无 BOM、LF。
- `git diff --ignore-submodules=all --check -- <WindowControl.cpp> <WindowControl.cppm>` 退出 0。
- 本机 Windows SDK `10.0.26100.0/um/tpcshrd.h` 中 FORCEOFF 值为 `0x00000200`，与已读官方研究一致；没有新增 Win8 导入。该 SDK 头核对是编译声明检查，不是 Win7 运行验证。
- 按 64 位句柄及 DWORD 最长值估算，新增 system-observe 行约 280 字符，低于现有诊断 320 字节 payload；不通过大行截断丢失 after handle。
- 没有编译、执行 headless/产品/GUI 或 API 实机验证；由主代理统一构建、测试和独立审查。当前 diff 是实施结果，未经运行验证不得写为 Win7 修复验收通过。

## 验证限制

没有本次 Win7 设备完整序列或可见像素证据。FORCEOFF 有官方 Vista+ 语义；不代表全部 Win7 驱动首次落指零闪或第二光标已确定属于 Windows。GetCursorInfo 是全局瞬时采样，不证明采样间隔内系统没有重新覆盖，也不观察独立 Tablet feedback 或 Draw3 自绘像素。
