# Work 1：当前状态写入者、读取者与第一改动单元

本表依据 2026-09-27 H0 生产工程和当前工作区；历史 IdtDrawpad.cpp 是 vcxproj None 项，不能把它的 GetMemory/键盘 hook 当当前入口。完整行为与跨线程时序尚需测试，以下“竞态”仅指源码存在未同步的并发访问条件。

| 状态/命令 | 类别 | 写入者与线程 | 读取者/副作用 | 当前合同与风险 |
| --- | --- | --- | --- | --- |
| stateMode.StateModeSelect / Target / Echo | authoritative / requested / echo 各自保留 | IdtState.cpp ChangeStateModeTo* 在 stateModeTransitionMutex 内；Bar 交互线程和 PPT 线程可调用 | Bar 渲染/按钮、MouseHook、Draw3 bridge、窗口 owner | 不可合并三态。锁只保护写与 PublishDraw3State，RenderPipeline 独立线程及部分按钮读为普通读取；data race 条件成立 |
| stateMode.Pen.ModeSelect、laserActive | 工具子型和 Laser 记忆 | Bar.Interaction.cpp:5229、5272–5275、5319–5322、5365–5368 在独立 interactionThread 裸写；PPT 条件接管在 IdtState.cpp:907–920 持模式锁写 | Bar.RenderLoop、Bar.Button、IdtState GetPen*/CurrentDraw3Tool | 同字段跨线程裸写与锁定写冲突；Bar 写未递增 revision，旧 PPT 回调可能覆盖较新用户子型。须同一次语义事务更新子型+顶层模式+revision |
| stateMode.Shape.ModeSelect | 当前形状子型 | Bar.Interaction.cpp:3500 直接写并调用 SyncDraw3State | Bar 渲染/按钮、Draw3 tool snapshot | 裸写与 render 读并发；旁路顶层模式语义入口，需独立形状子型命令并保持面板/模式合同 |
| Pen/Shape width、color | 绘图参数及记忆 | IdtState.cpp SetPenWidth/SetPenColor 普通写；旧 GetMemory 在当前未编译的 IdtDrawpad.cpp 被调用 | Bar 预览、Draw3 PublishDraw3State、SetMemory | SetPen* 并未持 stateModeTransitionMutex，和模式切换/renderer 读可能并发；SetMemory 是现有同步副作用，不应由新统一入口额外重复触发 |
| stateModeTransitionRevision | 条件结果版本 | IdtState.cpp 顶层模式/PPT 接管持锁递增 | IdtPlug-in.cpp:497–505,660–664,797–800,867–875 | PPT 真退出的 ChangeStateModeToSelectionIfRevision 必须保留版本条件，不能换成无条件 setter |
| settingOwnerDesiredState / AppliedState | requested / applied | IdtState.cpp SyncDraw3State 发布并锁外应用 | Window Service owner、retry | 双状态不可合并，旧版本提交不能覆盖新期望 |
| Draw3 Bridge ProductState | 不可变提交快照 | IdtState.cpp PublishDraw3State 持模式锁序列化 | Draw3 Host/绘图 owner | UI/业务只发布；document、GPU 与成功 Present 不在统一入口同步处理 |

## 生产线程证据

- Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp:184 启独立 interactionThread；RenderPipeline.cpp:408–440,575–614 启独立 Scheduler jthread。Bar.RenderLoop.cpp:635–676、1437 等直接读 stateMode。
- PPT 回调由 IdtPlug-in.cpp 经业务线程进入 ChangeStateModeToPptAnnotation（IdtState.cpp:903–922），按 expected revision 条件写同一笔型字段。
- Bar.Button.cpp:847–929 的静态 mutex 保护 styleKey 更新，不保护其之前从 stateMode 读取的字段。

## 第一改动单元（待实施）

行为缺口：Bar 的 Laser/软笔/硬笔/荧光笔选择把普通子型字段先裸写，再调用顶层 Pen 入口；PPT 异步条件入口则在锁内写同字段。拟在 IdtState.h/.cpp 提供一个明确 Pen 子型命令，锁内原子地处理子型、顶层模式和 revision，锁外沿现有 SyncDraw3State 发布副作用；Bar.Interaction.cpp 四个调用点迁移。PPT 仍使用原条件接口，不丢 expected revision。形状子型、参数与跨线程 render 读取另做独立单元，不能把“裸写换 setter”误算总体收口。

预计只改 IdtState.h/.cpp 与 Bar.Interaction.cpp；主 agent 独占三文件。先搜索所有 Pen 子型调用者，保持已记忆的软/硬笔共用 Brush1 宽/色、Laser 独立宽/色、UI/光标/窗口副作用。需要跨入口和迟到回调真实生产逻辑测试；若当前严格无窗工程不能链接 IdtState，明确列测试缺口并提供受控复现，不复制一套算法伪测试。改动前后复用 Release 子链基准判明显成本退化；它不能替代真实 Bar/GPU 验收。
