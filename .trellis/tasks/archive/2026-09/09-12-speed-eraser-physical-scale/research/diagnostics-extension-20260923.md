# 输入与橡皮控制台诊断扩展：现状与边界

- 设置页目前的 `SettingsUI/Experimental/ConsoleOutput/TouchArea` 文案只称触摸面积，持久化 `Experimental.Inkeys3.ConsoleOutput.TouchArea`；启动时 `IdtMain` 建控制台并设置 `DevelopmentOptions::touchAreaTrace`。该键已存在，不应重命名造成已保存开关失效。
- Host 的 `PumpDisplayScale` 已在非 packet 路径读取 Display 快照的 `MonitorInfo`，含 EDID 解析状态、原始厘米、有效物理厘米及失效原因、活动像素分辨率和有效 DPI。需要明确活动分辨率并非 EDID 原始时序；映射不可靠时仍报告原始 EDID 与不可用理由。
- `ObserveEraserDiagnostics` 现有 250ms 输出包含面积、来源、阈值、速度、目标和尺寸，但缺坐标；未活动时会早返回。`DrawingController` 的最终 Touch 光标直径已归属当前诊断 contact，可在同一位置补坐标；笔/鼠输入和固定橡皮需避免使用默认 32 DIP 冒充测量。
- 限频快照可帮助比较普通/清扫动作，但不是 RTS 原始逐点包，也不能推出硬件回报率或精准重放。保持显示元数据在低频入口读取，RTS 路径不增加 I/O。
