# Pen离开后鼠标光标回弹（2026-10-19）

用户已确认Windows 11复现：先用屏幕笔绘制，移出感应范围，约1秒后鼠标未移动却出现选定的鼠标画笔光标；进一步确认MouseStyle=System默认光标原本正常，仅Drawing画笔光标出现此回归。要求无真实鼠标输入时持续隐藏，之后真实鼠标移动/点击仍即时接管。本轮修复已授权；此前commit授权已执行完成，不延续为本次commit/push授权，GUI/Computer Use仍不运行。

## 源码证据与待实机边界

- 基线e9e9b1f5，工作区开始时干净。PenCursor::SelectPrimarySample在Pen归属且Pen样本为空时返回nullptr，没有按时间恢复Mouse。Window::ClearPenCursorSample清mailbox并请求refresh，保留owner；因此无后续输入事件时没有主动的一秒恢复规则。
- WindowControl处理RTS后端WM_MOUSE*时调用真实FilterMouseCursorMessage；通过后清Touch抑制、设置owner=Mouse并发布Mouse mailbox。过滤器对API成功的IMDT_UNAVAILABLE+IMO_SYSTEM的WM_MOUSEMOVE仅在touchSuppressed=true时拒绝，Pen归属及OutOfRange后的空Pen mailbox不受保护。这是可达的错误接管路径。
- 当前本应用没有EnableMouseInPointer调用；普通Win32Mouse后端有独立producer，不能为修RTS的Pen归属而套用promoted过滤破坏兼容后端。
- Controller::buildDrawingCursorVisuals的primaryRuntime只选择实际工具，不改owner；Pen归属且空样本时不回退Mouse。System样式选到Mouse也不自绘，Drawing样式则会绘PenIcon，因此两样式差异与错误消息接管路径一致，但不足以单独证明现场消息来源。
- 本次没有现场Cursor日志，尚不能把用户约1秒的观测确定为固定定时器或特定驱动行为。通过生产过滤/样本/visual序列补齐可达回归，实际设备复测另列，不以headless代替。

官方依据：[GetCurrentInputMessageSource](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcurrentinputmessagesource)返回消息来源，查询成功/失败必须区分，Windows 8+可用；[IMO_SYSTEM](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ne-winuser-input_message_origin_id)明确为系统注入，IMO_HARDWARE与IMO_INJECTED另有语义。缺少API或查询失败不能当成系统注入；Win7保留既有回退。

## 最小实现边界与验证

优先仅修改Draw3.PenCursor.cppm/.cpp、Draw3.WindowControl.cpp和既有draw3_contact_tests.cpp：给共用过滤输入传当前Pen归属，扩展已存在的系统Move过滤到Pen持有期间（含清Pen样本后）；使用既有owner状态而非新增计时器或平行状态。拒绝时不改owner/mailbox，不以坐标变化或按键位绕过明确的系统来源规则。

真实Mouse/TouchPad来源、明确按键Down、新Pen Hover/Contact即时恢复；Mouse已真实接管后的系统刷新沿原行为。Unknown来源/API缺失/失败与应用注入保留既有回退，不能扩大为吞所有WM_MOUSEMOVE。原Touch/TouchPan/Pen兼容Up及Win32Mouse独立后端保持。

测试调用实际生产FilterMouseCursorMessage和mailbox/visual入口，覆盖旧Mouse样本→Pen Hover/Contact→Up/OutOfRange清样本→迟到/重复系统Move（含约1秒、长间隔、按键位、不同坐标）保持隐藏；再覆盖真实Mouse/TouchPad移动、点击、新Pen恢复、Mouse已接管与Win7/API失败。System/Drawing两种设置均验证Pen离开后无主视觉；Drawing必须在真实输入后恢复PenIcon，保留既有默认System表现。测试不能复制另一套过滤实现。若现有无HWND生产probe适合验证Window接线可复用，不新增GUI/测试入口。

按Trellis implement→check→root串行完整InkeysRepo.sln Debug|ARM64（动态ARM64 MSBuild、同PowerShell PATH规范化、600秒）、headless --no-window（300秒）执行。本机增量链接器已有稳定复现，构建前仅保留已核对的.ilk使原配置完整链接，不改项目。保持UTF-8/BOM/CRLF和关键中文注释。无需改shader/guarded、配置/UI/i18n/实际墨迹或来源分类；正式设备复现和Win7仍待验收。
