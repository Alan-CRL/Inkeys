# Research: M6-input v1 跨窗口租约、捕获与会话失败证据

- Query: 悬浮不离开感应范围跨窗口后不可点，远离再回恢复；日志是否证明其他channel持租约/旧session/capture票据拒绝。
- Scope: internal（用户详细日志 + 当前生产源码）
- Date: 2026-11-02
- Log: Build/x64/Release/M6-input.txt
- Task: .trellis/tasks/11-02-shape-style-tablet-bar

## Findings

### 结论

本次日志**明确存在旧 Pen 会话未结束、持有 capture 和全局非鼠标租约，造成新接触被拒绝**；此前仅为风险，本次有首个 Down→Up被过滤→后续拒绝→释放恢复的闭环证据。其起点不是“硬件没发Up”：日志里 Up 已到目标窗口，但 Down 带 `0xff51572d` 被认作 Pen，Up 带 `0xff5157ad` 被认作 Touch compatibility 副本并过滤。此结论证明两段可复现路径，不将全部间歇失败都归为租约。

### 全文件可见计数（按日志记录，不补算 suppressed/dropped）

| 记录 | 数量 | 解释 |
| --- | ---: | --- |
| lease_begin/acquired | 189 | 成功获得租约 |
| lease_end/released | 189 | 最终总数平衡，不代表中间没有长时间持有 |
| lease_begin/other_channel_owns_lease | 26 | 明确跨channel拒绝，分下文5次与21次两段 |
| pen_begin/nonmouse_session_active | 37 | 已活动非Mouse会话拒绝新Pen Down；含正常Touch会话去重，不能全部计为缺陷 |
| capture_acquire/acquired | 143 | 没有看到capture_acquire/failed或down_reject/pen_capture_failed |
| down_reject/background_miss | 46 | 另一类明确失败：样本投到未命中背景的窗口 |
| capture_release_request/stale_or_missing_ticket | 11 | 这些记录owned_ticket均为0，不能据此认定旧票据仍持捕获 |
| session_accept/identity_mismatch_or_inactive | 2 | L4900/L4910，Bar会话已经active=0，不是持续active卡住的证据 |

build日志L5：`draw3-Nov  2 2026T22:01:56 arch=x64 msvc=1944`。

### 窗口/channel 对照

- Bar：HWND `0x601A4`，channel `0x2835fc12680`。
- bottom-left：HWND `0x2C0C50`，channel `0x2835fb6eaf0`。
- bottom-right：HWND `0x1101A8`，channel `0x2835fb75be0`。
- lease诊断 `owner_snapshot=before_mutation` 是操作前快照；成功lease_end日志owner仍非零不代表释放失败。需结合reason=released判断。

### 段A：pointer1272，右下Pen会话gen1卡住，5次左下租约拒绝

| 日志行号 | 可确认事实 |
| --- | --- |
| L6164–6172 | 同一位置附近Touch投bottom-left，因background_miss取消，gen42租约已释放。 |
| L6174–6180 | bottom-right收到Pen兼容Down，extra=`0xff51572d`；取得租约gen1、capture ticket26，命中hit2并入worker。 |
| L6183–6188 | pointer1272的WM_POINTERUP到bottom-right，随后WM_LBUTTONUP extra=`0xff5157ad`；raw_context仍active=1/source=pen/gen1/ticket26，L6188明确filter/touch_promoted_mouse。 |
| L6193/L6218/L6241/L6259/L6283 | bottom-left请求gen43–47，均被bottom-right gen1租约拒绝；时间tick23027265–23028015。 |
| L6285–6287 | bottom-right又收到Pen Down，因nonmouse_session_active拒绝；旧gen1继续存在。 |
| L6301 | bottom-right收到pointer1272 Leave，capture仍为右下HWND。**单个窗口PointerLeave没有清旧Pen session/lease**。 |
| L6305–6306 | 新pointer1273 Enter，旧Pen gen1/ticket26仍active。仅Enter也未恢复。 |
| L6321–6338 | 新pointer1273的WM_TOUCH Down终于投到right自身；触摸分支取消旧Pen，L6332释放capture26，L6334释放gen1，L6335重新取得Touch gen2、ticket27。 |
| L6348–6357 | Touch Up合法消费，L6352 button_activated clicked2，L6353 direction_on_up，随后capture和gen2租约配对释放，恢复被业务日志直接证实。 |

旧owner持有时长：tick23027093→23029531，约2.438秒。新pointer1273出现与用户“远离再回”描述相符，但日志不能单凭ID变化证明物理移动距离；**实际恢复边沿是新WM_TOUCH Down触发旧会话取消**，不是PointerLeave本身。

### 段B：pointer1273，左下Pen gen48卡住，21次右下租约拒绝

| 日志行号 | 可确认事实 |
| --- | --- |
| L7103–7109 | bottom-left收到Pen兼容Down extra=`0xff51572d`，获得gen48租约、capture ticket34，hit5，已入worker。 |
| L7112–7117 | pointer1273 WM_POINTERUP及WM_LBUTTONUP都已到left；Up extra=`0xff5157ad`，L7117被touch_promoted_mouse过滤，left仍active=1/source=pen/gen48。 |
| L7123–7563 | 21条other_channel_owns_lease：right请求gen26–46，被left gen48拒绝，tick23034437→23038406。Touch样本contact始终1273。 |
| L7571–7578 | 又一次PointerUp和Touch-signature鼠标Up被过滤；随后pointer1273 Leave，raw_context仍active Pen gen48/ticket34。 |
| L7592–7594 | Pen Down extra=`0xff51572d`依旧因旧Pen active被拒。 |
| L7595–7602 | 随后出现 **extra=`0xff51572d`的Pen分类WM_LBUTTONUP**，终于按gen48接受；L7599释放capture34，L7600 reset，L7601释放gen48租约。 |
| L7604–7607 | 该Up进worker但hit0/no_button_click：它解除卡住，不应被写为成功激活按钮。 |
| L7631–7638 | right可再取得gen47租约（L7632）；因实际点位在其窗口外background_miss立即取消释放。这证明租约已解锁，但路由问题仍在。 |

旧owner持有时长tick23034265→23038718，约4.453秒。该段pointer1273在释放前并未换新ID，恢复由可被Pen兼容路径接受的Up触发；因此不能写成“必须换ID才恢复”或“离范围自动EndTouch”。

### 源码与该闭环一致

- `Message/Message.cpp:68` IsTouchGeneratedMouseMessage 要求Pointer signature且TouchFlag存在；`:74` Pen要求该flag不存在。`0x72d`与`0x7ad`尾字节相差0x80，当前分类确实不同，不是日志作者凭空命名。
- `PageControl.cpp:2817` 在笔兼容处理之前过滤Touch签名的鼠标消息；因此已有Pen按压的Touch-signature Up不能到后面的Session::Accept/EndTouch。`:2861` 起Pen分支仅接Pen分类消息，`:2871` 非Mouse会话活动时拒绝新Pen Down。
- `UiPointerInput.cpp:568` BeginTouch若其他channel仍为owner直接false；这正对应26次provider明确拒绝。共享lease本身没有超时回收，捕获/真实Up/取消负责结束。
- `PageControl.cpp:2832` / `:2841` 的timer仅按可见/semantic/桌面等状态轮询，不会用“收到了PointerUp”替兼容会话结尾。当前Pointer消息观察也不自动结束Pen兼容会话。
- 原生Touch Down替换旧Pen session的取消路径会走ResetNativePointer→EndTouch；段A新Touch归投自身后因此恢复。

### Capture旧票据：有记录，但不是上述两段的首个原因

- L4333、4491、6365、6388、6614、8317、8319、8328、8330、8634、8635共11条stale_or_missing_ticket，全部owned_ticket=0。`PageControl.cpp:2806` 按票据保护拒绝旧取消，避免释放新捕获；日志不是“owned旧票据仍存在却无法释放”。
- 段A/B卡住时capture分别确实保持ticket26/34，但它们是**Up被入口过滤后未走正常收尾的结果**。有对应恢复日志证明真实Up/取消可正常释放，没有证据需先修改票据算法。
- L6352–6357等成功链后迟到旧取消被拒符合设计。不能把所有stale日志都计为故障。

### 同时存在的路由失败，不能由租约一因全包

L7025–7035 pointer1273 PointerDown在Bar；WM_TOUCH却投到bottom-right，点 `(787,1834)` 远离right窗口rect `(2390,1729,2890,1929)`。right获得gen23租约后以background_miss拒绝并立即释放。这种接触不需要旧lease卡住也会失败。更早L703–734段为right PointerDown、Bar Touch Down/Up，Bar background=0但先capture/lease，完整Up又释放。它们与用户“不离范围悬浮跨窗口”规律一致，但OS/注册来源的具体路由归属根因由主会话继续综合，不能仅靠本报告给出驱动断言。

### 触觉错误不构成UI失败原因

GetFromPointerId present=0只发生在可选触觉设备Attach/Play消费链，不参与UiPointer租约、capture或Session准入。上述闭环依靠明确ui_input filter/active/lease/activation日志建立，无需也不应以重复触觉失败解释。

### Files found / Related specs / External references

- `Build/x64/Release/M6-input.txt`：详细现场日志，本文全部L行号指此文件。
- `Input/UiPointerInput.cpp`、`Input/UiPointerDiagnostics.h`：lease真实拒绝与before_mutation诊断语义。
- `UI/PageControl/PageControl.cpp`、`Message/Message.cpp`：不对称兼容来源准入、会话/捕获收尾。
- `.trellis/spec/native-desktop/ui-pointer-session.md`：来源/代次、真实终态及旧取消合同。
- 无外部资料。

## Caveats / Not Found

- 仅新增本研究文件，未改产品/spec，未执行Git/GUI/build/commit。
- 计数是可见日志记录；文件另有DiagnosticOutput dropped与Move suppressed，不能证明每个API事件都完整。本文关键Up/拒绝/释放有正向记录，不以缺失日志单独推断。
- 用户换板仍复现及范围规律属于用户现场报告，不证明某厂商协议或Windows内部行为；本文不将两段闭环扩大为所有失败唯一根因。
