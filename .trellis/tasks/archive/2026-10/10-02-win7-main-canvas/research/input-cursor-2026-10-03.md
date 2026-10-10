# Win7 输入、光标、撤销证据复核（2026-10-03）

## 证据身份与边界

- 本轮静态调查基于开始时 HEAD `d03713bdf456c9da80adb6e0df0f27ebd1399765`；之后本文件及 RTS 诊断修改为工作树增量。没有证明用户 EXE 与此提交逐字一致，不把源码事实直接称为该 Win7 二进制的执行事实。
- 只读附件 `G:/Inkeys/Inkeys-Win7-ULW-Hit-x64-Release/console output.txt`，20049 bytes，SHA-256 `663BC37352BC8A16708B0457F655A2B8D1236FD438BB3CFD402EB6CBC5D5345C`。
- 只读附件 `idt1791013766788.log`，39832 bytes，SHA-256 `0DE953F1DFF2897473F2520F7F8A0ADDBFB965481FBDB15331D989C91FC642EC`。
- Console 是换行折叠后的尾段，开始即 `seq=13129`、`mouse-state phase=after`；没有启动全量内容和此前几次操作。不能因尾段没有 Down/Up 就说整个运行没收到 Down/Up，也不能把前次附件的启动 build identifier 自动归到这次附件。

## 新证据改变了前轮概率判断

1. Console 的 event 2145–2156 全为 `WM_MOUSEMOVE (0x0200)`，`api=0 device=0` 表示来源 API 不可用；全部 `accepted=1 reason=accepted-mouse`，`promoted=0`、`known=0`、三个 Reject 均为 0。
2. `tool=3` 是 Eraser；`owner=2` 是 Mouse；`shape=2` 是 EraserGripCircle（不是第三种输入设备）。定义见 `Draw3.WindowControl.cppm:29`、`Draw3.PenCursor.cppm:17,42`。
3. `seq=13142–13144`：绘制线程读到 Mouse 样本，生成自绘橡皮视觉 `x=976 y=951 width=29 height=29 opacity=0.5 rgb=1/1/1`，然后 `present success=1 visuals=1 dirty=(959,931,995,968)`。这证明该段输入至 CPU 光标解析通畅；前轮怀疑的“Win7 未知来源鼠标被旧 Pen Hover 拒绝”不能解释这个已接受的样本。`pen=0/0/0` 也没有有效 Pen Hover。
4. `mouse=1/0` 是样本 valid/inContact；`mouseLifecycle=1/1` 实际是速度橡皮 `HasPosition()/NeedsAnimation()`，不是鼠标 Down/Up 回调计数。映射在 `Draw3.DrawingController.cpp:9949–9957`。
5. 后来的 `mouse-leave -> owner=0 -> visuals=0` 与移至其他 UI 窗口一致：`system-skip hit=0x5034C` 仅证明 WindowFromPoint 命中其他 HWND，没有足够证据给该 HWND 定具体 role；此尾部离窗不能解释之前 visual 存在却不见的报告。

## 必须区分输入的两条生产路径

`Draw3.WindowControl.cpp:1562–1742` 的 WM_MOUSE* 分支只过滤和发布 `DrawingCursorSample`，并请求光标重绘；不会调用 ContactInputCoordinator::PublishDown，也不会创建笔画。

实际墨迹 contact 来自唯一 RTS producer：

```text
RealTimeStylus plugin StylusDown/Packets/StylusUp
 -> decoder / active binding / ContactInputCoordinator PublishDown/Move/Up
 -> DrawingController processCommand / initializeStroke
 -> active modeled points / CPU stored stroke / runtime history
 -> operator layers / L2 / backbuffer / presenter
```

- `Draw3.RealtimeStylus.cpp` 修改前 `928` 将 `TDK_Mouse` 解为 MouseLeft；`2476` 的 `SetAllTabletsMode(TRUE)` 包含鼠标、笔、触摸。
- 修改前 `1137–1222`：Down 可以因无 decoder、TouchPad 来源、binding 分配失败、DecodeSnapshot 失败或 PublishDown 失败提前返回；解码成功时依据按键选择 MouseLeft/MouseRight 再发布。WM_MOUSE accepted=1 不证明这条路径同样成功。
- 修改前 `1319–1363`：Packets 无 binding/decoder、gate 读取失败或包解码失败时不发布；正常只把最后一个 packet 解为 Move 并 PublishMove。
- `DrawingController.cpp:9238–9261`：出队 contact 仍可能因 admission、Whiteboard selection 或 Presentation load suppression 被 discard。`initializeStroke` 从 `8025` 开始，另有各工具初始化/导航分支。
- 因此剩余的输入疑点在 RTS contact 到达、解码、发布、consumer admission；不应根据此鼠标 Hover 尾段猜测修改 Win7 鼠标来源过滤。

## 为何系统光标消失与呈现侧故障相容

`Draw3.PenCursor.cpp:387–390`：Eraser/Laser 总是隐藏系统光标，即使 owner Unknown、当前无有效 sample。`Draw3.WindowControl.cpp:1133` 执行 `SetCursor(nullptr)`。普通工具在配置要求系统 Mouse 光标时仍显示系统箭头。

所以“普通笔是系统光标，橡皮/激光没有任何光标”恰好与专用自绘视觉无法成为可见像素相容；它不单独证明输入被阻断。日志 `system hidden=1 tool=3 owner=2` 是实际执行该策略的证据。

## present success 的实际保证及静默 GPU 路径

- `DrawingController.cpp:7046–7060` 的 PresentFrame 返回值来自 `presentation_.Present`；`13731` 调用后 `13771` 输出 present success。该结果没有校验实际 alpha/RGB。
- `DrawTransientDrawingCursor`（`Draw3.RendererPrimitives.cpp:245–306`）返回 void；visual/资源无效、inkDataBuffer Map 失败、globalCB Map 失败均直接返回，controller 仍可能继续 PresentFrame 并报告成功。
- 上述函数调用 D3D `Draw(6,0)` 无 HRESULT。`visuals=1` 统计的是 CPU 数组长度，不是 GPU 成功绘制数量。
- 光标绘制成功提交仍可能受 GPU viewport/scissor、shader 或 MRT/blend 状态影响；本日志没有 GPU 状态或像素采样，不能据此定因。

## Undo item=3,2,1,0 证明什么

- `DrawingController.cpp:10782–10917` 先要求 runtime history 有逻辑 visible item；`hot_preimage` 只在 `RestorePreimage().restored` 时选中，之后 UndoLastVisible 成功才输出。
- `InkHistoryGpu.cpp:1288–1325` 要求 hot entry 为 committed 且 canvas、item、rasterKey、afterState、viewport、尺寸匹配；执行 CopySubresourceRegion 后置 restored=true。Copy 是 void，不提供像素有效性回执。
- 正常 contact 完成时 `DrawingController.cpp:13285` 先 CommitRuntimeStoredStrokeCpu；`13308` 明确规定“进入 runtime history 即成为有内容，GPU 呈现失败不回滚文档真值”。`CommitRuntimeStoredStrokeCpu:1814–1818` 先追加 InkStroke 再追加 history。
- 正常路径仅在 stored raster 结果与 ApplyOperatorLayers 提交结果允许时 CommitPreimage（约 `13390–13400`）；redo 的约 `11039` 也会捕获前像。所以 hot_preimage 比单纯 CPU history 更强，但仍只表明缓存/调用链接受了工作，完全透明前像同样可撤销。
- 四次 Undo 证明该页存在四个可撤销、带 committed 热前像的历史项目；不能证明四条都包含非透明像素，也不能证明它们来自本尾段鼠标、来自普通 Pen 而非 Eraser、或对应用户报告中的某次 Down。
- 它明显降低“这个运行从未产生过任何绘制/历史对象”的概率，支持先调查 shared renderer/backbuffer/ULW 连接点。

## Viewport 专项静态复核

- `InkRenderer::SetScreenSize`（`Renderer.cpp:129–134`）同时写 viewportWidth/Height 与 RSSetViewports；InitializeResources（约 `347`）先设置全屏尺寸。
- WarmUpShapeShaders（`RendererPrimitives.cpp:223–242`）先 RSGetViewports 保存、切零视口，再在函数尾恢复；相关 draw helper 的提前失败仅从被调用函数返回，不跳过外层恢复。
- WarmUpLaserShaders（`RendererLaser.cpp:435–484`）同样保存/恢复。DrawingController Run prewarm 在 `11941`，graphics recovery 在 `12111` 再执行。
- DrawStoredStroke（`StrokeGeometry.cpp:638–642`）按目标尺寸 SetScreenSize，tile replay 可暂时改变视口；InkHistoryGpu::FinishHistoryOperation（`612–621`）恢复 canvas 全屏尺寸。RestoreComposition 的空目标、无效 excluded item、fallback 失败及正常结束均调用 FinishHistoryOperation（`1381/1392/1500/1509`）；MaintainComposition 后也恢复（`1536`）。
- RestorePreimage 仅 unbind/copy，不更改 viewport。
- 没有在已读路径发现“warmup 永久零视口”或“history 正常出口遗留 tile 视口”的确定漏洞。实际 viewport 仍需实机日志/像素结果证伪，不能以静态路径正确宣称 Win7 渲染正常。

## RTS 日志缺席的诊断缺口与本轮最小增量

全仓含隐藏文件的 C++/project/props/targets 搜索确认：`DRAW3_RTS_DIAGNOSTICS` 只由 `inkStrokeModelerTest.vcxproj` 明确定义；`SetRtsTraceEnabled` 的应用调用只在 demo main.cpp。产品 callback RecordCallback 整体由该宏包围，产品没有 enable caller。`ConsoleOutput.Draw3/Cursor` 不是宏开关，不能使产品出现旧 StylusDown/Packets 行。

本轮只改 `Draw3.RealtimeStylus.cpp`，不改接口、Host、工程、consumer 或输入行为：

- 新 `rts-input event=Down` arrival 在 decoder resolution 前记录；结果细分 invalid-argument/no-decoder/touchpad/binding-failed/decode-failed/publish-failed/published。
- Up arrival 与正常发布、坏包取消或无 decoder 取消结果。
- Packets 最多每 plugin 每秒一次；包含 packetCount、propertyCount、tcid/cid、decoder device、decoded/published 和坐标或拒绝原因。
- 复用 `CursorDiagnosticsEnabled/RecordCursorDiagnostic` 有界队列，独立于旧宏；不开启时不读 packet throttle clock、不格式化、不入队。
- Packets/InAir 允许并发读 state gate，故限频成员使用 atomic tick CAS，避免数据竞争与并发旧 tick 导致的无符号下溢；输入 gate、decoder 和 coordinator 调用次序不变。
- `device` 表示 decoder 设备类别；如需区分右键路由，要结合鼠标消息或 consumer，不能把该字段当 routed device。`decoded=0` 时日志 x/y 为占位 0，不能解读为真实原点。

## 下一次 Win7 的判别顺序

1. 使用明确记录 EXE SHA/build ID 的候选，开启 Cursor 诊断，将 stdout/stderr 从启动写入文件，保留全量日志，避免控制台只复制尾部。
2. 书写模式先移动 Eraser 光标，再普通 Pen 按下画线、抬起；观察 `rts-input Down reason=published` 及 Up，必要时结合 consumer admission。没有任何 Down arrival 才支持回调缺失；有 arrival/no-decoder 或 decode-failed 就进入对应输入分支。
3. 若 Eraser CPU visual 存在、RTS published 也成立，优先读取 presenter 上游 backbuffer 及最终 DIB 同一区域 alpha/RGB 摘要。两者都透明指向 renderer/shared state；backbuffer 非透明而 DIB 不一致指向 readback/copy；二者相同且有 alpha、ULW 成功却不可见才继续 HWND/compositor/dirty-ULW。
4. Dirty 全区与 force-full 需记录是否实际改变；API success 不能代替 Win7 可见结果。不要无证据全局改过滤、swapchain 或系统光标 fallback。

## 本轮验证

- 完成日志与源代码只读追踪、实际宏定义与调用点搜索；未运行 GUI/Computer Use，未写入 G:、未 commit/push。
- RealtimeStylus.cpp 原 UTF-8 BOM 和 CRLF 保留；`git diff --check -- <该文件>` 通过，局部 diff 为 71 新增/5 删除，无全文件格式变化。
- 尚未构建/运行；按 main-session 分工由主代理执行完整 Solution 与必要 headless 检查。此补丁是诊断，不宣称问题已修复；真实 Win7 contact 与可见像素待实机反馈。
