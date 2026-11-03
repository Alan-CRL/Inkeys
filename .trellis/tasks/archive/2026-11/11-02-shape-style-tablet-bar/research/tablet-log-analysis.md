# 高漫M6间歇点击日志分析（2026-11-02）

## 输入与范围

用户报告：数位板测试时翻页控件与主栏交替/间歇不可点击。原始日志为附件7ff6bb2f-bf6c-417a-9ffa-75387b5a0716/已粘贴的文本.txt，共653行/94544字节，Draw3构建标识Nov 2 2026 20:45:11 ARM64。原始附件不复制到仓库；本文行号为附件1-based。用户已明确全程只使用高漫M6，没有屏幕笔/手指/鼠标混用；仍未提供每次操作目标/成功失败标注。可以确认同一M6前后存在成功的Pointer→Touch→按钮接受链路。

本轮只调查，不改生产输入策略、不GUI、不构建、不提交。此前“笔兼容鼠标过滤是首要原因”须降级为未证实候选，不能覆盖本次两组控件的间歇现象。

## 直接观察

- L42-47：bar_pointer Down → bar_touch Down(token matched) → Pointer Up → Touch Up(matched_up) → product click published → bar_click accepted=1。记录明确存在完整触摸到主栏业务接受链路。
- L103-167：22对bar_touch Down/Up，0条bar_pointer、0条bar_click，坐标主要右下x2660-2790/y1810-1856；同源hSource=00000000049517FB。位置可能是翻页控件，不能仅凭坐标认定，需窗口矩形和目标命中证据。L140-155还有canvas_ingress与bar_touch同点/相近时间，需核对窗口路由。
- L542-595：pointer=1055、cursor=43、同source连续27对bar_pointer Down/Up，0条bar_touch、0条bar_click；时间18262784..18269776，约7秒。说明主栏WndProc可观测笔Pointer，不是整个程序未收到笔；缺少的是Touch接纳/业务这段证据。当前bar_touch在GetTouchInputInfo、触点选择与BeginTouch之后打印，不能写作“原始WM_TOUCH一定未送达”。
- L607-613：pointer=1057重新出现Pointer+Touch+bar_click accepted=1；L641-646再有一次正常链路。前后source/cursor一致；数字pointerId更换不等于物理设备更换。
- 全文78条bar_pointer(39Down/39Up)、69条bar_touch(34Down/34Up/1Move)、8条bar_click，8次均accepted=1。日志记录的完整Up不能证明未记录的其他窗口会话没有漏终态。
- L247..526有24条Page实际页动作，支持翻页链路并非始终失效，但不携带每次UI来源与拒绝状态，不能对应到所有点击尝试。
- attach 52次均present=0；8次click_consume sent=0。它是WinRT触觉设备获取不到的独立问题；如L47已先成功业务接受，L51才attach失败，不能据此把点击失败归因触觉。
- 输出包含少量DiagnosticOutput丢弃统计（最大报告dropped=3），不宜把一整段27对触摸阶段缺失简单解释成这几条丢行。

## 已核对代码与优先调查方向

1. 跨窗口非鼠标租约：UiPointerInput.cpp:557 BeginTouch在别的channel占用touchOwner时直接false；没有自动到期。Bar和PageControl都会使用此入口，且相关拒绝目前没有日志。主栏出现Pointer但无bar_touch，既可能原始Touch未到，也可能在此准入失败。不能仅凭日志确认租约卡住。
2. 触摸透明区域：Bar WM_TOUCH先按primary/fallback接纳并BeginTouch，再AcquireBarNativeCapture/SetCapture，worker之后才冻结按钮命中。它没有在占用前核对真正按钮/主体命中，可能收到透明区Touch也先占用lease/capture。官方WM_TOUCH文档明确不遵循WM_NCHITTEST的HTTRANSPARENT区域，因此L103-167“右下Touch却在Bar、Pointer不在Bar”的现象值得优先核实；不是已证明的目标窗口错误。
3. 缺终态恢复：若另一个窗口漏Up且无capture/cancel/隐藏/语义失效，旧会话可能长期阻止其他channel的Touch/Pen开始。当前PointerTimer主要复核桌面/语义并drain，不证明会以物理Up自动释放。已有22对完整Touch又要求避免把所有问题笼统叫漏Up。
4. 过滤保留为条件候选：本日志没有原始兼容WM_MOUSE来源/过滤日志，不能证明失败点击靠被过滤的鼠标消息才能触发；更不宜立即放开过滤或统一改用WM_POINTER。

## 最小下一步诊断范围（尚未实施）

复用现有控制台诊断，Down/Up/Cancel完整、Move限频；不更改capture/准入/去重/业务：

- Bar与PageControl的原始WM_TOUCH、WM_POINTER、兼容WM_MOUSE来源与接收HWND/role、窗口rect/当前呈现命中、point/time。
- GetTouchInputInfo结果/错误、primary/fallback选择与跳过原因。
- BeginTouch准入/拒绝，明确requester、owner、generation；EndTouch/cancel的释放或忽略原因与capture票据。
- 入队/消费、匹配会话与命中widget、业务执行/拒绝点；区分触觉拒绝和UI拒绝。

这些日志才能将L103-167和L542-595分别定位到窗口投递、租约、捕获、队列或业务，不要求用户先改变Windows Ink或重新提供一份相同粒度日志。

## 关联材料

- tablet-intermittent-session-audit.md：子代理的来源租约/取消/触觉隔离源码核对。
- main-bar-diagnostics.md：已有日志的含义与盲区。
- 官方：https://learn.microsoft.com/en-us/windows/win32/wintouch/wm-touchdown （HTTRANSPARENT说明）。

## 用户确认后的结论收敛

全程M6排除了混用设备作为两个日志分段差异的解释。M6能够通过WM_TOUCH完成主栏业务已被真实日志证实。L103-167的22对Touch均打印于BeginTouch成功之后，属于Bar确实取得过归属而非该段BeginTouch拒绝；但每对都有Up，不能据此认定租约永久卡住。L542-595的27对Pointer无Touch日志仍需原始入口/租约拒绝日志区分。优先窗口接收目标、透明区域提前capture与跨窗口非鼠标lease；不以此日志直接批准/实施改输入来源。
