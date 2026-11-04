# 数位板识别与高亮调查(子任务 A)

## Goal

1. 解释并修复:数位板(高漫 M6,已开 Windows Ink)笔光标不高亮。
2. 评估非 Windows Ink 模式数位板能否被识别为“数位板”(此时它以鼠标消息到达、会显示系统鼠标光标)。

## 背景(源码已确认)

- 现行高亮规则:`Draw3.PenCursor.cpp::ResolvePrimaryDrawingCursorVisual` 中 `highlight = !screenPen`,`screenPen` 仅当 `source.kind==IntegratedPen` 且非 Conflict;因此数位板不高亮意味着其来源被判为 `IntegratedPen`,或样本不是预期路径(例如以 Mouse 样本到达,走 Mouse 分支)。
- 来源分类 `SpeedEraser::ClassifySource` 依赖:RTS `IInkTablet::get_HardwareCapabilities` 的 `THWC_Integrated`、`GetPointerDevices` 的 `POINTER_DEVICE_TYPE_INTEGRATED_PEN/EXTERNAL_PEN` 与 cursorId 匹配;二者不一致返回 Unknown(→ Conflict,视觉固定高亮)。
- 已有诊断入口:`Experimental.Inkeys3.ConsoleOutput.Cursor` 与 `Draw3.RealtimeStylus.cpp` 约 2892 行的 source 日志(kind/recognition/capabilitiesKnown/integrated/modernSourceApi/mappedMonitor)。

## Requirements

- 在用户 M6 上取得 Windows Ink 开/关两种状态下的来源日志(kind、recognition、integrated、pointer device type、sample 路径 Pen/Mouse),定位“不高亮”的确切原因(分类错误、驱动上报 Integrated、还是走了 Mouse 样本)。
- 若为分类错误:给出修正方案(例如以 pointer device type/映射矩形/`THWC_Integrated` 之外的信号辅助判定),并补 Headless 回归。
- 非 Ink 模式调研候选信号并逐项判定可行性与误判风险:`GetCurrentInputMessageSource`/`GetMessageExtraInfo` 签名、Raw Input HID Digitizer(usage page 0x0D)、Wintab 近场信号、绝对坐标跳变启发式。每项写明需要的权限/依赖(不得新增 Win7 硬导入)与结论。
- 无法可靠识别时,结论文档明确“不实现”,由 B 的设备行(鼠标光标可手动改为笔光标)承担。

## Acceptance Criteria

- [ ] `research/` 下有日志分析与结论,区分“已确认/推断/无证据”。
- [ ] Windows Ink 模式数位板按来源规则命中数位板行为(有日志或设备验收证据),或明确为硬件/驱动限制。
- [ ] 非 Ink 识别方案:要么落地并有负例(真鼠标、触摸板不被误判),要么有“不可行”论证。
- [ ] 若改动识别逻辑:Headless 覆盖 + `draw3-cursors.md`/`input-and-ink.md` 更新。

## Constraints

- 需要用户在真机运行并回传日志;未取得日志前不得宣称根因。
- 不得用固定延时/全局 hook 替代来源判定(既有 spec 要求)。
- Win7 不扩展自动物理识别的原则保持。
