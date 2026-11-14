# 当前批准范围：鼠标拖动画布高延迟（2026-11-08）

用户新反馈：Touch正常；Mouse拖动画布延迟很高，背景稍好；暂停尺度适配，要求修复。基线4b4edb91。现有未提交scale-stage-plan/research及prd/design/implement/jsonl/task.json属于其他阶段文档，本轮保留，不覆盖。仅本文件记录本轮scope，任务继续in_progress；实施时没有commit/push或GUI授权。2026-11-08用户后续确认鼠标正确并授权本次commit，仍不结束任务、不push。

## 证据与设计

输入日志callback-arrival→consume多数0–2ms，偶发约6ms，不能证明RTS之前无延迟或Present之后已可见。频繁timing-resets会令速度0，但几何delta仍非零，不能据此贸然更改已经正常的Touch动力学。

WindowController::PublishMouseCursorSample成功发布后每次RequestDrawingCursorRender→RequestControlWake→coordinator.PublishControlWake。白板移动模式显示系统箭头，没有应用光标位置需要重绘。Draw3恢复tile前检查pending ingress与wake generation，活动frame deadline也被控制wake打断。这是Mouse高Hz重复产生不必要控制工作的确定链；渲染饥饿/呈现延迟须由测试和新日志验证，不能把推断写成现场唯一根因。

最小修复：在CanvasPanMode下，纯Mouse位置更新仅更新cursor mailbox；valid/contact边沿仍请求render/control wake，leave/clear/cancel/模式切换/systemcursor刷新保持原路。非移动模式及Touch/Pen行为保持原合同。优先现有窗口入口而非在下游忽略全部控制命令。

增加生产gate下的导航render记录：关联sample/viewport与实际帧，记录恢复tiles/剩余/中断原因、frame work/composite/present耗时、成功与snapshot fallback；明确Present返回是软件提交而非光学显示。关闭gate无新增诊断clock/格式化，不刷空Idle帧。

## 实施/测试

1. 只读核对Mouse/Touch差异、完整Render顺序，必要时收窄/修正上述因果判断。
2. implement代理负责WindowControl.cpp/.cppm、DrawingController.cpp/.cppm；Controller私有PresentFrame可选QPC输出仅用于准确日志，主会话已授权这一最小扩展。主会话不并发编辑源码。保持BOM/CRLF，关键中文注释，minimal diff。
3. 回归必须调用真实PublishMouseCursorSample/唤醒协调器或其生产接线，验证持续位置变化不产生额外control wake、不提前破坏frame deadline；Down/Up/clear/模式切换仍可靠唤醒；非移动模式及Touch兼容来源过滤不退化。若无窗口只能fixture方式，明确未覆盖真实OS事件到达。
4. 使用现有InkeysRepo.sln Debug|ARM64+动态定位ARM64 host；同次PowerShell规范Path/PATH并禁node reuse，/m:1正常执行环境、超时至少5分钟。主会话统一构建，不并发MSBuild。
5. 新Mouse回归位于RunCanvasPanProductionProbe，由--draw3-control-fence-test调用；另跑Headless --no-window及canvas-pan-only、pan-feel、diagnostic-output。1000次burst和Sleep(1)持续流验证唤醒协议，不冒充实测1kHz或光学延迟基准。GUI禁止，不能靠旧CPU动力学测试证明渲染延迟修复。
6. check代理交叉审查，按实际结果更新小规范及本轮validation；不改屏幕尺度、算法常量、工程/依赖版本；实施阶段不commit/push，后续仅按用户本次明确授权提交。
