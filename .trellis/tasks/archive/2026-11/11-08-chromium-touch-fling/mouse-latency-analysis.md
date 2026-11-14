# Bug Analysis：Mouse位置流打断画布恢复和帧率等待

## 1. Root Cause Category

- B（跨层合同）、D（接线测试缺口）、E（隐含假设）。普通Contact Move已经不打断活动帧，但独立WM_MOUSE cursor路径仍每次RequestDrawingCursorRender→PublishControlWake。合并control marker不合并SignalWake，故仍推进恢复/帧率等待读取的generation。
- Selection/Pan显示系统箭头，不绘应用主光标。位置变化没有自绘收益，却能打断tile恢复和WaitForFrameDeadline。背景viewport先发布，Draw3随后恢复/合成/Present，二者可表现不同。
- 源码可以确定该副作用；用户提供的callback→consume多数约1ms及拖动时亚毫秒帧间隔与之相容。GPU积压、RTS之前和光学延迟没有测量证据，不能认定它是现场唯一因素。

## 2. Why Fixes Failed / 覆盖缺口

此前输入历史/释放估速修复及CPU手感矩阵覆盖ContactInput→CanvasPanInput，没有覆盖WindowController的独立Mouse cursor控制流，更没有证明Present后何时可见。此次不因timing-resets频繁就重改动力学：TimingRejected之后仍应用几何delta，不能单独解释持有拖动延迟。

## 3. Prevention Mechanisms

| 优先级 | 机制 | 本轮措施 |
| --- | --- | --- |
| P0 | 源头合同 | Pan纯Mouse位置只更新mailbox；状态边沿、clear/取消、模式与owner变化仍唤醒；非Pan仍重绘 |
| P0 | 生产接线回归 | 真实WindowController测试入口→publisher→Coordinator；1000次burst、持续流/deadline（Sleep(1)不承诺实测1kHz）、状态边沿/clear及非Pan |
| P1 | 分阶段诊断 | 现有CanvasPan gate记录恢复yield、snapshot fallback返回、合成/Present与帧间隔；关闭gate不新增诊断取时 |
| P1 | 规范 | native-desktop/canvas-pan.md记录独立cursor路径及软件/光学边界 |

## 4. Systematic Expansion

- 已核对Pen接触cursor已有避免逐包render wake的门控；Mouse owner/suppression setter有旧值比较，稳定位置流不会从它们重复唤醒。无理由扩大为全局忽略control命令。
- 未发现Mouse接触使导航整帧走idle continue。可信snapshot有效且有交集时按当前viewport重投影，不能仅因tile被抢占就宣称全部墨迹位置必然冻结。
- 本轮只补fallback实际返回值的日志，不改变Renderer失败策略或GPU后端。屏幕物理尺度、RTS硬件时间和大屏profile继续延后。

## 5. Knowledge Capture

- 已更新`.trellis/spec/native-desktop/canvas-pan.md`，研究证据见同任务research/mouse-latency-input.md与mouse-latency-render.md。
- 本仓库没有`src/templates/markdown/spec/`，不新建模板系统。
- 实施时未授权commit/push，不执行技能模板建议的自动提交。用户后续已确认当前鼠标正确并授权本次commit，但不结束任务；无push授权。GUI/真实OS消息派发未自动覆盖，不扩大用户反馈为跨设备验收；自动回归结果见mouse-latency-validation.md。
