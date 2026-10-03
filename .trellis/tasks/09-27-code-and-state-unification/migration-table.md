# 状态与 UI 语义入口迁移表

状态依据 research/state-ownership.md；保留 authoritative、requested、applied/echo 的不同含义。当前只对已确认旁路做最小迁移，未完成项不能记 PASS。

2026-10-03 已按 `0a19182c` 实码补正下列旧复审标签；详见 `closeout-20261003.md`。源码复审与正常可见交互已有证据，但既有严格无窗测试不直接执行 IdtState 条件交错，原第二项验收仍未全部通过。

| 业务意图 | H0 旧入口 | 规范入口/当前处理 | 调用点 | 状态与例外 |
| --- | --- | --- | --- | --- |
| 顶层选择/笔/形状/橡皮 | Bar.Button.cpp 直接调用 ChangeStateModeTo* | 原接口保持，内部 revision 与 SyncDraw3State | Bar.Button.cpp:261–406 | 已有统一语义；未强迁 |
| Laser/硬笔/软笔/荧光笔子型 | Bar.Interaction 裸写 laserActive/Pen.ModeSelect，再 ChangeStateModeToPen | ChangeStateModeToPenTool：同一模式锁内提交子型/顶层状态/revision，锁外 Sync | Bar.Interaction.cpp 原 5229、5272–5275、5319–5322、5365–5368 | 源码复审完成，正常工具切换已有 GUI 记录；跨入口/重复/迟到回调的生产确定性运行待补。Laser 宽色独立、软硬笔共用 Brush1 |
| PPT 批注 Pen/Laser/荧光笔接管 | expected revision 条件判断后锁内写 | ChangeStateModeToPptAnnotation 保留条件，复用锁内 Pen 顶层提交步骤 | IdtPlug-in.cpp:497–505；IdtState.cpp:907–922 | 保留旧结果不得覆盖新用户选择；本轮未改调用前提 |
| PPT 真退出切选择 | 读取并传 expected revision | ChangeStateModeToSelectionIfRevision | IdtPlug-in.cpp:660–664、797–800、867–875 | 条件切换必须保留，不能无条件 setter |
| 形状子型 | Bar.Interaction 直接写 Shape.ModeSelect + SyncDraw3State | SetShapeModeSelect/IfRevision 锁内提交子型及意图版本、锁外同步 | Bar.Interaction.cpp 原 3500–3501 | 源码复审完成；主模式和面板规则保留，按压跨代取消的确定性运行待补 |
| 笔/形状粗细与颜色 | SetPenWidth/SetPenColor | 原入口锁内写状态；长手势用 SetPenWidthIfRevision，SetMemory 和 Draw3 发布仍在锁外 | IdtState.cpp；Bar.Interaction | 源码复审完成，正常粗细操作已有 GUI 记录；FineDial 跨代候选/取消的生产运行待补。同步磁盘写是既有行为，本轮未重复增加 |
| 橡皮输入偏好 | SetGlobalEraserPreference/SetEraserInputPreference | 保留既有偏好与 Draw3 bridge 发布 | IdtState.cpp:580–643 | 适用范围按五入口合同核对，当前不做无关合并 |
| 设置 owner | SyncDraw3State 发布 desired 后锁外 apply | requested/applied 双版本保留 | IdtState.cpp:647–664 | 不把 applied 回声当 authoritative |
| Bar/Hook/配置读取 | 跨线程直接读取 stateMode 字段或重复取宽色 | GetStateModeSnapshot / VersionedSnapshot；Bar 每帧同一值，交互每消息刷新，SetMemory 拆色一次快照；UpdateRendering 同一值投影按钮与粗细 | Bar.RenderLoop、Bar.Layout、Bar.Button、Bar.Interaction、MouseHook、IdtConfiguration | 源码复审完成；正常主栏/属性可见交互已有记录，条件交错与锁等待尾延迟未由此证明；后续验证使用自动操控 |
| 旧 Draw2 GetMemory/低级键盘 hook | IdtDrawpad.cpp 为工程 None 项 | 不迁移旧入口；实际产品为 IdtDrawpadFacade 和 UI3/Draw3 | Inkeys.vcxproj:895、905 | 不适用；不以历史源码推断现行功能 |

后续每次新增或替换调用点都需更新本表，并同时检查 UI 高亮、光标、Draw3 快照、窗口显隐/穿透、配置写入和迟到回调。当前状态不等于产品整体统一已完成。
