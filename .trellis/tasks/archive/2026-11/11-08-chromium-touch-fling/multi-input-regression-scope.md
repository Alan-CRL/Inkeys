# 多设备漫游真机回归修复范围

> 2026-11-12 最新范围取代此前“继续修 DM flags/状态”的方案：同 HWND RTS→DM Activate 已复现 E_ACCESSDENIED，反序 RTS Enable 同样失败；DM 失败回滚后原 RTS Disable 异常。独立 DM HWND 虽可初始化，但主 HWND 的 PT_TOUCHPAD 跨窗 SetContact 返回 0x80070490，不能交付。用户选择先停用 PTP 扩展，本轮唯一产品行为变更禁止 DM owner/Activate 接触 RTS 主 HWND，恢复原接触输入；不动其它设备、动力学、配置/UI 或 RTS 后端。详见 research/dm-rts-coexistence-probe.md。

用户报告前轮交付后Mouse/Touch/Pen/PenTail都无法拖动，PTP双指仅偶尔可用，已明确要求修复。既定功能/默认/hover规则不变；本轮直接修复回归，不重新询问实现许可。任务in_progress，不GUI、不commit/push/归档。

## 现有证据

附件 `C:/Users/alan-/.codex/attachments/3244ff61-0772-49b3-af29-e52e9e8162c8/已粘贴的文本.txt` 为2692行，原文件约816KB；可解析911个render、893个pan-step、872个touchpad事件，pan-register/input ordinal/rts-batch均0。日志从seq328中途开始，不能单凭缺行证明所有设备尝试都在截段内。

源码发现两项待用生产边界测试核实：Contact在SetContact成功后直接发布Running而非等待系统确认，可能伪held并阻挡其他输入；DM仅MANUALUPDATE及DISABLEPIXELSNAPPING，未显式保护其他输入，非PT_TOUCHPAD hit-test落默认处理。Microsoft已说明MANUALUPDATE只控制更新、默认自动输入可被DM消费，运行中EXPLICITHITTEST决定新WM_POINTERDOWN是否交应用。完整日志与primary source证据写research/multi-input-regression-log-analysis.md。

## 最小边界

- 恢复原RTS/Mouse接触到Controller，PTP只接收明确触控板手势，不抢占笔/触摸/鼠标。
- 区分“已关联/待识别/需要manual pump”与“系统确认Running持有”，不能用SetContact成功伪造held，也不从静默超时猜Up。
- 保留实际Running/Inertia/Ready、epoch/admission revision、关闭惯性末点、停止证明、现有帧节拍；不改UI/默认、摩擦、屏幕尺度、缩放、输入后端或工具链。
- 文件预计WindowControl cpp/cppm、TouchpadNavigation或小型输入策略helper、必要Controller活动调度及真实生产probe、contact tests。每项改动必须对应证实入口/生命周期缺口。
- 前轮无窗口回归没有激活真实COM viewport，不能只重喂bridge宣称修复；新增实际生产setup/dispatch/状态routine可注入测试，验证输入隔离及未识别→识别/结束/失败全过程，不创建HWND。

## 验证

静态/格式及独立check；动态ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，规范PATH、/m:1 /nr:false、至少五分钟预算；生产control-fence/pan-feel及Headless contact/pan/完整--no-window按受影响范围执行。设备真机/PTP/RTS共存仍不自动GUI；结果必须区分已验证的生产契约与用户现场反馈。前轮通过不能充作本轮修复通过。
