# schema=2 用户日志调查

附件：`4056a1f7-f313-4a13-b73f-5debb0e05389/Pasted text.txt`。

## 已确认的证据

- 398 条连续记录，无 dropped/truncated，60 个鼠标消息的来源、过滤与前后状态配对完整；1 次单触点接触，无 Pen 样本。58 条查询结果为 device=4 origin=1，2 条为 device=0 origin=4；所有查询 api=1 ok=1 error=0。
- `event=51`：seq=288 来源 device=0 origin=4，wparam=1；此前 event=50 是相同坐标 (2034,811)、相同消息 tick 的 Touch 兼容 Move。
- seq=289 before：owner=Touch、suppressed=1、touchCount=1、mouseValid=0。seq=290 filter：same=1、sourceReject=0、positionReject=0、buttonBypass=1。
- seq=291–295：触摸抑制清零，归属变为 Mouse，并发布 contact=1 样本。seq=303–312：primary 和 touch 各一枚，present success=1。
- seq=333 Touch End 后，兼容 Mouse Up 被正常过滤；seq=345–347 仍呈现错误的按下主光标。
- `event=60`：seq=348 来源同样 device=0 origin=4，wparam=0，在抬起约 3.11 秒后到达最后触点 (2359,762)；seq=352 被接受，seq=354–356 呈现 opacity=0.5 的悬停主光标。
- 记录中的系统光标决策仍为 hidden=1，额外圆环来自主自绘光标。最终 mouse-leave 清样本后视觉归零。

## 来源解释与代码根因

本机 SDK 与微软文档均定义 `origin=4` 为 `IMO_SYSTEM`（系统注入）。因此本次两个 source=0 不属于 API 缺失或查询失败，来源分类已明确。

文档：https://learn.microsoft.com/en-us/windows/win32/api/winuser/ne-winuser-input_message_origin_id

现有 WindowControl 已采集 originId，但接管过滤只接收 deviceType。未知设备的系统注入 Move 被当作 Mouse 接管候选；调用处 `!buttonDown` 又放行触摸期间的按键态消息，导致错误 Mouse 样本跨越 Touch Up 保留。状态位不是新的物理鼠标 Down 证据。

已核对编译中的 Draw3.Product::DrawpadMsgCallback → ForwardProductMessage → Host::ForwardMessage → HandleExternalMessage，转发为直接函数调用；未在这些入口发现为该消息重写来源或通过 SendMessage 重新注入。系统为何恰在这两个时刻生成消息，当前日志不能确定；修复光标归属不必先推断这一触发原因。

## Raw Input 的证据边界

seq=28 注销，seq=397 恢复注册；故障期间无 Raw Input 覆盖。此前三条 raw-mouse 都是空设备句柄、absolute/virtual-desktop 标记及触摸兼容 extra 签名；不能仅凭出现 raw-mouse 行证明发生物理鼠标操作，也不能凭故障时无记录证明没有操作。

## 下一步可评审的修复方向（本轮未实施）

在触摸仍拥有光标视觉归属时，API 成功识别为 `device=IMDT_UNAVAILABLE + origin=IMO_SYSTEM` 的 Move 不应创建 Mouse 样本或解除触摸抑制；这一判据不依赖按键位、末触点相等或固定延时。保持明确 Mouse/TouchPad/Pen 的既有接管通路，避免笼统拒绝所有 source=0 或所有注入输入。

完整接管判定应放入实际生产调用的可测试入口，覆盖两条系统消息、Touch Up、真实 Mouse/TouchPad/Pen 接管及来源 API 不可用分支；不能再次只测一个被调用处条件绕过的位置 helper。当前日志足以定位本机故障路径，但不构成多指/混合设备/Win7 验收。Win7 无该 API，需要单独检查并验证其兼容来源路径。

调查使用现有 verify_cursor_trace.py 检出 seq=294 → 312 → 347 → 356，退出码 1 表示发现需复核的异常链。本轮没有修改产品代码或新增 commit，任务继续进行中。

## 后续实施

用户随后批准修复，2026-09-25 已实现统一生产过滤入口并通过 ARM64 Solution 与无窗口回归。上述“本轮未实施”描述调查当时状态；当前实现和验收边界见 ../validation.md 的系统来源修复记录。
