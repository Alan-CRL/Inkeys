# Implement: 数位板识别调查

0. **先加诊断(我来做)**:现有 `ConsoleOutput.Cursor` 的 `[CURSOR_TRACE]` 与 `visual` 行不含笔来源。在 `TraceResolvedDrawingCursor`/`visual` 日志里补 Pen 样本的 `source.kind/recognition/cursorId`、命中的设备行(screenPen/tablet/mouse)和 highlight 判定;仅在 Cursor 诊断开关下输出,限频沿用现有去重。需要新构建交给用户测试。
1. **取证(需用户)**:在设置→实验中开启“光标调试信息”(下次启动生效)后,在 M6 上分别于 Windows Ink 开/关各录一段:悬停→落笔→抬笔→离开;同时录屏幕笔(若有)与鼠标作对照。回传日志。
2. **日志分析**:对照 `Draw3.RealtimeStylus.cpp` source 日志(kind/recognition/integrated/capabilitiesKnown/modernSourceApi),写入 `research/ink-mode-source-log.md`;判定高亮不生效原因(Integrated 误判 / Conflict / Mouse 样本路径 / 其它)。
3. **修复(如为分类问题)**:先在 `Draw3.SpeedEraser.cpp::ClassifySource` / `RealtimeStylus.cpp::SourceForCursor` 的 Headless 用例里构造该设备信号并取红,再改。
4. **非 Ink 调研**:在 `research/non-ink-tablet-recognition.md` 逐项评估 `GetCurrentInputMessageSource`、`GetMessageExtraInfo` 签名、Raw Input HID Digitizer、Wintab、坐标跳变启发式;给出可行性/误判/依赖/Win7 影响。必要时写临时诊断(不入库或仅限诊断开关)并让用户在非 Ink 模式验证。
5. **结论与落地**:可行则出子方案并补负例测试(真鼠标、触摸板);不可行则写入结论并在 B 的专页说明里提示“可把鼠标行改为笔光标”。
6. **Spec**:有逻辑改动则更新 `draw3-cursors.md`、`input-and-ink.md`。

## 门禁
- 无日志不得宣称根因;设备验收结果单列记录。

## 进展记录
- 2026-11-03:已加诊断(仅 `ConsoleOutput.Cursor` 开启时输出)。`Draw3.RealtimeStylus.cpp` 新增 `pen-source` 行(tcid/cid、source、recognition、deviceKnown/capsKnown/integrated/modernApi、pointerSources/pointerMatches/pointerRaw、mappedRect),仅输入变化时输出;`Draw3.DrawingController.cpp` 的 `frame` 行追加 `penSource=kind/recognition/cid`、`mouseSource=kind/recognition`。Debug|ARM64 完整 Solution 构建通过(exit 0),产物 `Build/ARM64/Debug/Inkeys.exe`。未跑 Headless(仅日志改动)。
- 2026-11-03(第二轮):首轮日志分析见 `research/ink-mode-source-log.md`。结论:Ink 模式 M6 被 Windows 报告为 Integrated Pen(monitor=NULL,Surface 笔为有效句柄);非 Ink 为 origin=INJECTED 的鼠标。用户决策:①先转储全部指针设备再验证 monitor 规则;②非 Ink 用“注入鼠标”信号做可选开关(默认关)。已加 `pointer-device` 转储日志(`ReadPointerSources`,仅 Cursor 诊断开启时),Debug|ARM64 构建通过,待用户再抓一轮(M6 Ink 开)。
- 2026-11-03(第三轮):设备全表确认 M6 实际输入的设备是 monitor 为空的虚拟 HID。已在 `ClassifySource` 加 `pointerUnmapped`(Integrated+有矩形+monitor 空→ExternalPen)、`SourceForCursor` 传入、`RtsSourceRoutingForTesting` 与 `speed_eraser_tests` 加用例;`draw3-cursors.md` 已记规则。Debug|ARM64 完整构建通过(首次遇 LNK1000 增量链接内部错误,重试通过),`InkeysHeadlessTests.exe --no-window` exit 0。待实机验证(M6 高亮/橡皮手感)。
- 非 Ink 注入鼠标识别(默认关的可选开关):依赖设置页,转入 B 的 PRD(R5)实现;信号为 `GetCurrentInputMessageSource` 的 device=MOUSE 且 origin=INJECTED,真鼠标为 HARDWARE。
- 2026-11-04:Windows Ink 下 M6 识别为数位板已验证(高亮、橡皮手感正常);非 Ink 注入鼠标识别已作为默认关闭的开关在 B 中落地并验收通过。其它品牌数位板/带屏数位屏未验证。任务完成。
