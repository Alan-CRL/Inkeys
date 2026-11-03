# M6 / Surface 笔来源日志分析(2026-11-03)

日志:`Build/ARM64/Debug/` 下 `m6-candidate-winink.txt`(13781 行)、`m6-candidate-non-winink.txt`(11302 行)、`surfacepen-candidate.txt`(17385 行)。均由含 `pen-source` 诊断的构建产生,Surface Laptop ARM64,屏幕 2880x1920。

## 结论(按证据强度)

### 1. Windows Ink 模式 M6 不高亮:根因已确认
- `pen-source`(winink 日志 seq=188):`source=Integrated Pen recognition=2(PointerCursor) capsKnown=1 integrated=1 pointerRaw=Integrated Pen pointerMatches=1 mappedMonitor=0x0 mappedRect=(0,0,2880,1920)`。
- 即 **Windows 自己把 M6 报告为 INTEGRATED_PEN**(`GetPointerDevices` 类型 + RTS `THWC_Integrated` 两个独立信号一致),`ClassifySource` 无矛盾,按设计返回 IntegratedPen;`ResolvePrimaryDrawingCursorVisual` 的 `highlight = !screenPen` 因此为 false,外描边 1.0(=0.5×2 DPI)。该行为是正确执行了错误前提,不是代码误判。
- 统计:winink 日志 `penSource=Integrated` 163 帧,`visual outline=1.0`(不高亮)94 次;`outline=2.5`(高亮)只有 4 次,全部出现在 `penSource=Unknown/0/cid0` 的帧(见第 3 点)。
- 对照 Surface 内置笔(surfacepen,seq=18):同为 `Integrated Pen`,**`mappedMonitor=0x10001`(有效显示器句柄)**;M6 为 **0x0**。这是目前唯一观察到的差异信号。
- 用户印象“数位板曾高亮、屏幕笔不高亮”与此一致:旧版本(或其它驱动)把 M6 判为 External;当前 M6 驱动/Windows 报告 Integrated。**未验证旧版本为何不同,不下结论。**

### 2. 非 Windows Ink 模式 M6:以“驱动注入的鼠标”到达,有可用信号
- 全程 0 个 `pen-sample`、0 个 `pen-source`;WM_MOUSEMOVE 共 1213 条,`GetCurrentInputMessageSource` 均为 `device=2(IMDT_MOUSE) origin=2(IMO_INJECTED)`,`extra=0x0`(无 `0xFF515700` 笔签名)。
- 对照 Surface 日志中真实鼠标:`device=2 origin=1(IMO_HARDWARE)`(5 条);Surface 笔 `device=8(PEN) origin=1`;Ink 模式 M6 提升消息 `device=8 origin=2 extra=0xff515743`。
- RTS 仍收到 `rts-input event=Down/Up ... device=2`(TDK_Mouse 上下文),不是 Pen 上下文。
- 因此“**鼠标 + 注入(origin=INJECTED)**”可作为非 Ink 数位板的候选信号。
- 风险(未验证):远程桌面/虚拟机、AutoHotkey、屏幕键盘鼠标键、其它注入软件同样是 origin=INJECTED,会被误判。仅一台设备样本、未含“真鼠标+注入软件”负例。
- Win7/无 API:`GetCurrentInputMessageSource` 不可用时此信号不存在。

### 3. 附带发现:笔首批样本来源为 Unknown(短暂高亮闪现)
- Pointer 消息路径发布的首批悬停样本 `source` 为空(`penSource=Unknown/0/cid0`),Unknown 按固定规则高亮;随后 RTS 样本带来 Integrated 才变为不高亮。winink 日志 4 帧、surfacepen 日志 15 帧 Unknown。
- 对 B 的影响:“屏幕笔不高亮”场景会在笔进入时闪现一次高亮;需要在 B 中决定 Unknown 的显示策略(沿用上一次已知来源/等待来源/按数位板行)。

## 未知/待证实
- M6 `monitor==NULL` 是否对所有外部数位板成立、Surface 笔/Cintiq 类屏幕设备是否恒有有效 monitor:需要 `GetPointerDevices` 全表(含 `productString`、monitor)转储和更多设备(其它数位板、Wacom 驱动、Cintiq)。
- 旧版本 M6 曾高亮的原因。

## 第二轮:指针设备全表(m6-candidate-winink / surface-candidate,2026-11-03)

系统共 6 个指针设备(19 个 cursor):

| idx | 产品名 | 类型 | monitor | cursorId | 说明 |
| --- | --- | --- | --- | --- | --- |
| 0 | HIDSPI Device | IntegratedPen | 有效 | 2-3 | Surface 内置数字化器 |
| 3 | Microsoft HID RID 000D_0002 | IntegratedPen | 有效 | 63-64 | Surface 笔(Surface 日志命中 cid=63) |
| 4 | GAOMON_M6 | IntegratedPen | 有效 | 65-66 | 25401x15876(=M6 10x6.25in 有效区),本轮未产生输入 |
| 5 | Microsoft HID RID 000D_0002 | IntegratedPen | **空** | 67-68 | 38100x25400 虚拟 HID,M6 在 Windows Ink 下实际使用(cid=67) |

- 产品名无法区分真/虚拟设备(idx 3 与 idx 5 命名模式相同);**monitor 为空只出现在 M6 实际输入的设备上**。
- 规则:Integrated 且已读到映射矩形且 monitor 为空 → 归为 ExternalPen。已在 `ClassifySource` 实现并加 Headless 用例;`GAOMON_M6` 条目(idx 4)有效 monitor 且本轮没有输入,不受影响也无法验证。
- 局限:仅一台设备/一个驱动;其它品牌(Wacom、Huion)与屏幕数位屏(Cintiq)未验证。Cintiq 类带屏设备若有有效 monitor 仍归屏幕笔(符合预期)。
- 规则影响面:ExternalPen 同时改变橡皮响应模型(IndirectDip)、笔速/Eraser 来源判断,不仅是光标高亮;需要实机再验 M6 的橡皮手感。
