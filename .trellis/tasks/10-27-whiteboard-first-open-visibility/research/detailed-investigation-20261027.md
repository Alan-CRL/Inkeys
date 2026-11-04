# 深入调查与修复依据（2026-10-27）

## 结论及证据等级

确认存在一个与后台进入要求冲突的前置条件：当前白板窗口事务同时要求受管组内部正确和跨应用全局 Z 序连续。另一个应用的窗口穿插，即使本组 owner、非置顶和内部顺序全部正确，仍会使进入失败。恢复又复用同一条件，故障可以扩大为所有普通 overlay 命令被拒绝；状态线程随后反复收拢主栏。

这是代码和生产纯策略探针直接确认的缺陷类别。尚未进行 GUI 复现，所以不能宣称现场第一次失败必然是 continuous 检查；原生调用失败、截止或既有恢复责任还需具体 stage 记录区分。任务栏点击改变前台/全局排序，与此链相容，但现场没有相应快照，仍是需要验证的关联。

## 1. 过强的全局排序条件

- `Window.cpp:1847`：非已知 owned 辅助窗口穿插使 continuous=false。
- `Window.ZOrderPolicy.h:28`：Matches 第一项直接拒绝 !continuous，尚未检查其余正确条件。
- `Window.cpp:1943–1957`：对根降 band 后，又对每个受管成员请求 HWND_TOP；1960 统一使用包含全局连续性的 GroupMatches。
- `Window.cpp:2759`：模式样式和上述排序作为同一 Enter 提交。
- `IdtState.cpp:2645`：只有模式、Draw3 和背景完成后才异步尝试激活。

Microsoft 的 owned window 合同要求 owned 高于 owner，不承诺同一 owner 树必须在全局相邻。SetWindowPos 文档还明确将窗口置到顶部受 SetForegroundWindow 权限限制。后台进入不能把这项全局能力作为必要条件。

来源：
- https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#owned-windows
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos#remarks
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow#remarks

一个应允许完成后台进入的排列模型：Bar > 外部窗口 F > Drawpad > Presentation > Freeze > 隐藏 Mag。已知成员都非置顶且 owned 高于 owner，当前仍拒绝。

## 2. 无窗口生产策略探针

产物：`Build/WhiteboardInvestigation20261027/`。独立 CLI 复用实际 `Window.ZOrderPolicy.h`，采用当前工程 v143 原生 ARM64 编译器 14.44.35207；没有修改工程或生产源码，没有创建 HWND。

同一组 HWND 代表值、owner、全部非置顶及相对 ordinal 保持不变，只切换 continuous：

```json
{"allKnownOwnersBandsAndRelativeOrderMatch":true,"sameMembersWithForeignGapAccepted":false,"nativeWindowsCreated":0}
```

编译与探针退出码均为 0。该结果证明生产策略的拒绝条件，不等于实测了 USER32 或复现了现场窗口。

## 3. 恢复屏障把排序失败扩大

- ExecuteTracked 2724–2727 保留恢复闭包。
- RetryCommandRecovery 2559–2572 仅全部成功后清除；过期并不等于已经撤回。
- 所有普通 overlay 命令在 2684 先经过同一恢复屏障。
- RestoreCommandSnapshot 2536–2541 再次执行 CanvasPolicy 和完整 RefreshOverlayGroup。

如果持续失败项是全局连续性，回滚也重复同一条件。于是 Enter、Show、Bounds、Draw3 后端样式命令都可能被旧恢复阻挡。修复应保留物理回滚严格性，移除“其他进程不能穿插”这一不属于物理恢复的条件；不能通过超时清空屏障。

## 4. 状态重试与背景退出

- IdtState 2567/2723 每次同意图进入失败重试都 Collapse+Cancel，直接解释 More 刚开即关。
- Whiteboard.cpp 146 在 inactive 清理前因 minimized 返回 Idle；IdtState 2772 却等待 inactive ACK 后才在2789 Leave并清最小化，形成退出闭环。
- Whiteboard.cpp 149/246 两处忽略 Hide 结果就写 false ACK。仅移动 minimized 判断会留下假成功。
- 单独重读 active bool 不能区分 false→true→false；快速重入需要当前背景意图身份和实际提交身份一致。

方案细节见更新后的 design.md，实施与验收顺序见 implement.md。

## 5. 两处相关恢复边界缺口

- minimized Freeze Show/Bounds 在没有物理写入前返回 false，但通用 ExecuteTracked 已准备恢复，随后反而执行 SetWindowPos(FRAMECHANGED)/ShowWindow。应在快照前将此类未变更拒绝直接发布 Rejected，不能变回假成功。
- SetBounds(Drawpad) 原生同时移动 Primary 与 Presentation（3306–3313），快照却仅保存 Primary（2488–2494）。移动后的 policy 失败或过期可能只恢复一面。这是已证实的局部缺口，未证明是现场首开触发因素；实施应作为相关恢复测试处理，避免扩展成无关重构。

## 已排除的假设

- Enter/Topmost/Visibility 不共享 ledger intent 键，周期刷新不会直接以同键退休 Enter。
- Draw3 DComp/ULW 不绕过 Service 并发写 GWL_EXSTYLE；最终由同一 owner 队列执行。
- 现有创建 owner 链与 ExpectedGroupOwners 相符；隐藏 Mag 不是天然非法；可选 Mag 缺失回退正确。
- 默认全组快照不直接包含 Setting，第10槽为空；合法 owned Setting 已有祖先例外。
- 没有显式 CreateWindowInBand/SetWindowBand，不能把 WS_EX_TOPMOST 诊断等同于 UIAccess ZBID。
- 没有发现 NonRude/region 在正常 API 恢复健康后必然永远失败的证据。

## 验证限制

本轮只做源码、已有日志、Microsoft primary 文档和无窗口生产策略探针。未启动 Inkeys GUI、未复现实际首次点击；GUI 授权请求仍待答复。没有重新执行完整 solution/headless，因为本轮未修改产品源码，已有通过结果不能用于宣称现场问题已修复。

## 方案独立复核

方案已由独立 check 复核并修正：无 Bar 只能以下调 PPT 兄弟恢复序列；关闭 guard 必须兼容同一关闭 epoch 的 Active→Exiting，不能在首次 Hide 前自我拒绝；初始尚无背景所有权的 inactive 不应触碰桌面/PPT 共用 Freeze。恢复谓词、同 epoch 去重、真实隐藏 ACK 和版本/lifecycle 设计经复核一致。本轮没有实施产品修复。
