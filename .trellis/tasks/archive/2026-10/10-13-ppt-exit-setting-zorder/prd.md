# PPT 退出画布交接与设置即时层级修复

## Goal

桌面/PPT 内容和工具模式切换保持画布及主栏显示连贯；主栏高亮与真实选择/绘制状态一致。设置在显示及工具变化时立即得到正确层级。

## User reproduction and requirements

- 桌面绘制保留墨迹，点击选择后进入 PPT 绘制，在画笔模式点击结束放映：退出时主栏及墨迹都暂时消失；回到桌面实际选择/穿透正常，主栏仍显示画笔高亮。
- 退出时不要主动双隐藏，不清空文档，以就绪帧切换内容；区分 Drawpad 与 clickthrough DrawpadPresentation，保持桌面和放映墨迹隔离。
- 设置已出现但被墨迹压住，等周期刷新后才恢复。绘制态设置位于两块画布上方；选择态设置保持普通 ownerless 位置。先在选择态打开设置，再切换绘制也须立即生效，返回选择立即解除。
- 用户已在调查方案和行为澄清后明确要求创建 Trellis 任务并修复。

## Requirements

R1：PPT/Desktop 及 Selection/非Selection 输出只在最新目标帧/工作区/模式/content revision 匹配时提交可见性；等待保留既有画面，不主动制造双 Hidden 空档，保留过期提交 guard。
R2：可信放映退出仍通过 expected mode revision 条件切到 Selection，不覆盖用户较新的工具意图；成功切换后刷新 Bar 样式及渲染。
R3：可信退出目标应用时即时刷新根层级，失败可重试；不等周期刷新，不改变白板 NOTOPMOST。
R4：设置 owner 提交与显示路径立即应用层级；只经根统一置顶，不增加独立永久 TOPMOST，保留可激活/任务栏语义和当前重试。
R5：无关状态重复同步幂等，不能每次宽色更新都强制根刷新；跨 owner-thread 操作不引入循环等待。

## Acceptance Criteria

- [x] 指定退出复现中模式/bridge/主栏一致，桌面原墨迹保留，PPT墨迹按原有文档隔离。
- [x] 首帧/辅助输出等待期间不主动双隐藏；正常就绪切换无可见空档，窗口提交失败仍保持安全处理。
- [x] 工作区切换同时包含 Primary↔SelectionUlw，就绪策略及过期guard经回归/审查核对。
- [x] 设置打开、Selection→Pen（设置已开）、Pen→Selection 均即时应用层级，不依赖周期刷新。
- [x] 静态审查、完整 Debug ARM64 Solution 构建和适用无窗口测试通过；GUI验证未执行。

## Out of scope and limits

无 GUI/Computer Use、无 commit/push；不重构 renderer、COM 或窗口树，不删除文档墨迹，不增加公开 mock。用户优先要求显示连贯：旧非layered表面可保留到新可穿透帧就绪，不承诺帧未就绪时已经提供系统穿透。

## Progress

2026-10-13 已实施两项修复、更新规范并通过独立审查、完整构建和无窗口测试。其余体验/实际Z序验收仍待用户确认。详见 research/implementation-verification.md。未commit/push。

## 用户验收

2026-10-13 用户明确确认问题已修复、人工验证通过，授权结束任务并提交 commit。AI未自行执行GUI；自动构建/无窗口测试与用户人工验收分别记录。不push。
