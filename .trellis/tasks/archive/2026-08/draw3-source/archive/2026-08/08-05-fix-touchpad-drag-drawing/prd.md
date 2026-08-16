# 以现行 RTS 为基线定位触摸板绘制问题

## Goal

定位并修复触摸板连续绘制失败，覆盖两种复现方式：

- A：触摸板直接按下并移动，必须恢复连续绘制。
- B：双击后第二次按住并移动；仅在同一最小 RTS 修复自然覆盖时一并修复，不阻塞 A 的交付。

RTS 的 `StylusDown`、`Packets`、`StylusUp` 仍是唯一绘制输入来源。窗口 `WM_MOUSE*` 只能作为观察数据，不能发布 contact，也不能作为正式绘制入口。

## Evidence

首轮实机 trace 已回传，但尚未足以证明最终根因：

```text
A: StylusDown -> Packets（仅一次，接近 Up） -> StylusUp
B: StylusDown -> StylusUp
```

A 拖动时抬起后可见落点和一段连线，B 只有落点。窗口同时可能收到持续的 `WM_MOUSEMOVE`，但这只是系统提升/兼容消息，不能据此改变生产路由。Draw2 代码仅作只读事实参照，不复制初始化配置或输入桥接。

日志细节：A 的 contact 为 `device=MouseLeft`，`packetsCallbacks=1`、`inAirCallbacks=0`，唯一 `Packets` 解码成功且 `published=1`，发生在 `StylusUp` 前约 35.6 微秒；因此当前证据不支持 coordinator 发布失败或 generation 丢失。B 文件只捕获到一个 `StylusDown -> StylusUp`，没有 `Packets`/`InAirPackets`；用户已确认实际操作仍是轻点两下并在第二次保持按住移动，但第二次动作只留下窗口 `WM_LBUTTONDOWN/UP` 和后续大量 `WM_MOUSEMOVE`，没有形成第二个 RTS contact。

初始化日志同时证明基线 `put_HWND`、`SetAllTabletsMode(TRUE)`、完整 `DataInterest` 和 9 项 extended packet description 均返回成功。首个探针跳过 `SetAllTabletsMode` 后 A/B 结果未变。第二个探针让 trace Debug 进程仅返回 `StylusDown | Packets | StylusUp`：A 曾收到 139 个连续 `Packets`，但因没有 `RealTimeStylusEnabled` 而未建立像素缩放，所有包解码失败；B 已出现两个 `StylusDown -> StylusUp` contact，同样因缩放未初始化而解码失败。随后迁移首个 `StylusDown` 的缩放初始化，精确 `0x380` 的最新 A 日志仍显示每个 contact 只有一个抬起前 `Packets`，所以 `0x380` 不是已确认的充分修复。原始 `IdtRts.cpp` 与 Downloads 副本哈希一致，并额外使用五项 `X,Y,Pressure,Width,Height` packet description；这些 RTS 变量已不再由当前 trace 组合切换。当前 Debug `--rts-trace` 改为只切换旧 `IdtDrawpad.cpp` 的 Tablet Pen Service flags `0x00010309`（`PRESSANDHOLD/PENTAPFEEDBACK/TOUCHUIFORCEON/TOUCHUIFORCEOFF/FLICKS`），DataInterest、packet description、RTS 路由和绘制入口保持生产基线。

上一轮工作树中的错误修改已经精确撤回：RTS Mouse 回调恢复正常发布路径，窗口过程恢复只更新光标，测试工程不再编译窗口桥接测试。

## Requirements

1. 首轮只增加 Debug 诊断，不改变 RTS 初始化调用顺序、`SetAllTabletsMode`、`DataInterest`、packet 属性集合、`CS_DBLCLKS` 或 Tablet Pen Service flags。
2. Debug 配置显式定义 `DRAW3_RTS_DIAGNOSTICS`；Release 不编译临时 trace 状态、trace 字符串或 trace 输出。
3. Debug 程序接受唯一新增选项 `--rts-trace`。不增加 Draw2 profile 或组合式初始化模式。
4. `WM_MOUSE*` 观察必须不调用 `ContactInputCoordinator`，不创建、不更新、不关闭 contact。
5. 诊断必须能关联 QPC、线程、`tcid/cid`、设备类型、包/属性数量、原始坐标、解码坐标和发布结果。
6. RTS 热路径不得为诊断分配内存、执行 COM 查询或产生无界逐包日志。每个 contact 使用固定容量槽位；Move/InAir 只保留前若干条，Up 输出一次汇总。

## Diagnostic contract

- `LogRtsInitializationState`：记录线程和 HWND、窗口 style/Tablet flags、`put_HWND`、`SetAllTabletsMode`、Flicks、MultiTouch、packet description、free-threaded marshaler、plugin、Enable 的 HRESULT，以及实际 `DataInterest` 和选中的属性顺序。
- `RecordRtsCallback`：记录 QPC、线程、事件名、`tcid/cid`、设备类型、包数、属性数、原始/解码坐标和 `PublishDown/Move/Up` 结果。固定 32 个 contact 槽，每槽最多保存 10 条事件，其中 Move/InAir 最多 6 条；Up 输出计数、丢弃数和事件序列。
- `RecordWindowMouseObservation`：记录 `WM_LBUTTONDOWN/DBLCLK/MOUSEMOVE/UP`、右键和中键对应消息、按键位、坐标、promoted 标记和有限计数。它只写诊断槽位。

## Probe decision tree

首轮日志已收到；后续一次只改变一项，顺序固定为：

1. `SetAllTabletsMode(TRUE)` 是否实际调用并成功。
2. `DataInterest` 保持完整事件集，或仅比较 `StylusDown | Packets | StylusUp`。
3. desired packet 属性集合及顺序。
4. RTS 初始化调用顺序。
5. 只为 B 比较 `CS_DBLCLKS`。
6. 最后才单独比较 Draw2 的 Tablet Pen Service flags。

每个探针必须输出明确名称和实际生效值，禁止组合 profile。分流规则：有 Packets 但发布失败时查 metadata、generation 和 coordinator；发布成功但消费者无数据时查 wake/snapshot/consumer 生命周期；无 Packets 时只继续测试 RTS 上游，绝不把窗口消息转成 contact。

## Acceptance criteria

- [x] 错误的窗口鼠标绘制桥接、测试工程扩展和错误规范条款已撤回。
- [x] Debug 主工程定义 `DRAW3_RTS_DIAGNOSTICS`，Release 未定义。
- [x] `--rts-trace` 只在 Debug 可用，首轮不会改变输入路由。
- [x] 三类诊断均有固定容量和有限输出，包含 A/B 所需字段。
- [x] 完整 `Debug|ARM64` 解决方案构建通过，现有控制台测试通过。
- [x] 用户提供 A、B 两组 `--rts-trace` 日志，并确认 B 仍按第二次按住移动复现；第二次动作没有形成 RTS contact。
- [ ] A 在旧 `IdtDrawpad` Tablet Pen Service flags 探针下恢复连续绘制；当前探针只使用 `0x00010309`，RTS DataInterest、packet description 和输入路由保持生产基线。
- [ ] B 若被同一候选自然修复则一并验收；否则记录为未修复但不阻塞 A。
- [x] 首轮诊断阶段已完成 Debug/Release ARM64 构建和现有控制台回归测试；最终人工输入矩阵仍待根因修复后执行。

## Out of scope

- 不把 `WM_MOUSE*` 变成绘制入口，不模拟 Draw2 的鼠标兼容方案。
- 不修改触摸板手势、系统双击设置、Pen/Touch 多点语义或绘制模型。
- 未取得实机日志前不宣称根因，也不制作组合式初始化切换。
- 本阶段不自动启动 GUI 或替用户进行硬件复现。

## User test

当前 Debug 构建包含生产修复候选，请在程序旁运行：

```text
inkStrokeModelerTest.exe --rts-trace
```

优先复现 A，并保留从 `[RTS_TRACE][probe]`、`[RTS_TRACE][init]` 开始到 contact `Up` 汇总结束的完整输出。初始化行应包含生产基线 `dataInterest=0x000063f5`、`selectedProperties=9`、`packetOrder=X,Y,Pressure,XTilt,YTilt,Azimuth,Altitude,Width,Height` 和 `probe=TabletFlags value=IdtDrawpad-0x00010309`，窗口观察中的 `tabletFlags` 也应为 `0x10309`。本轮优先复现 A；若仍只有一个 `Packets`，说明旧窗口 flags 不是根因，再进入下一个单变量探针，不把窗口鼠标变成绘制入口。

## Closure Note

本任务的代码修复、诊断清理、Debug/Release ARM64 构建和现有控制台测试已经完成，生产代码提交为 `d417cba`。A/B 触摸板以及 Pen/Touch/Mouse 的实体硬件复测由用户后续执行；本任务在不伪造硬件验收结果的前提下结束，后续 RTS decoder 热路径优化另立任务。
