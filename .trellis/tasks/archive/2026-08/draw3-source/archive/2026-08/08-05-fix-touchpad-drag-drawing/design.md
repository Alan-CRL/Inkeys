# 诊断阶段设计

## Boundary

```text
RTS COM callback
  -> StylusSyncPlugin 解码
  -> ContactInputCoordinator PublishDown/Move/Up
  -> DrawingController consumer

Win32 WM_MOUSE* callback
  -> WindowController 光标观察
  -> RecordWindowMouseObservation（仅 Debug trace）
```

两条路径共享 QPC 和输出前缀，便于按时间关联，但窗口路径没有 coordinator 指针访问。生产 contact 的 identity 和 generation 仍完全由现有 RTS/coordinator 契约管理。

## Diagnostics module

诊断 API 放在现有 `draw3.diagnostics` 模块，并由 `DRAW3_RTS_DIAGNOSTICS` 包围：

- `ConfigureRtsTrace(bool)` 在 Debug 参数启用时清空固定槽位；重复设置同一值不会重复清空。
- `RtsInitializationTrace` 只保存定长标量和静态属性描述名称。初始化函数在每个失败出口和成功出口调用 `LogRtsInitializationState`，因此失败步骤的 HRESULT 不会被后续清理覆盖。
- `RtsCallbackTrace` 只复制指针、整数、浮点和静态事件名。`RecordRtsCallback` 用一个互斥锁保护 32 个预分配槽位；不执行 COM 查询、不分配容器、不在每个 Move 上写流。
- 每个槽最多保存 10 条事件，Move/InAir 最多保存前 6 条；总回调、包数、解码失败、发布成功/失败和丢弃数始终累计。`StylusUp` 复制槽位并在锁外写一条汇总。
- `WindowMouseObservationTrace` 记录线程、QPC、消息、按键位、坐标和 promoted 标记。进程级最多输出 96 条窗口观察，后续只累计数量，防止系统 Hover 刷屏。

输出使用 `[RTS_TRACE][init]`、`[RTS_TRACE][callback]`、`[RTS_TRACE][contact]` 和 `[RTS_TRACE][window]` 前缀。QPC 原始 tick 不换算，保证跨线程直接比较。

## RTS instrumentation

`StylusSyncPlugin` 在现有解码和发布调用旁记录：

- `StylusDown`：记录单包原始/解码坐标及 `PublishDown` 返回值；坏包也记录 `decoded=0`。
- `Packets`：记录 callback 的 `packetCount`、属性数和最后一个包；只报告最新包的解码与发布结果。
- `InAirPackets`：只记录最后一个包。诊断器在没有活动 Down 槽时忽略悬停数据，Down 到 Up 期间限量保存。
- `StylusUp`：即使解码失败也先保留现有闭合语义，再记录 `PublishUp` 结果并输出汇总。
- Enabled/Disabled、TabletAdded/Removed 和 Error 只输出有界事件行，不改变回调返回值。

初始化记录保留当前实现的调用和 fallback 语义，仅把每次 HRESULT 保存到不同字段；extended/stylus/required packet description 的实际成功集合和顺序以静态名称输出。

首轮日志显示 A 只有一次成功发布的 `Packets`，B 样本没有第二个 RTS contact；用户已确认 B 仍按第二次按住移动复现，窗口 `WM_MOUSEMOVE` 均为 `promoted=0`，不能作为绘制入口。首个探针跳过 `SetAllTabletsMode(TRUE)` 后没有改变结果。第二个探针返回 `StylusDown | Packets | StylusUp` 后，A 曾收到 139 个 `Packets`，但缺少 `RealTimeStylusEnabled` 导致 Draw3 的全局缩放门槛未满足。第三个 `0x386` 和第四个 `0x382` 探针都恢复解码但再次只收到一个 `Packets`；最新精确 `0x380` 日志在迁移首个 `StylusDown` 缩放初始化后仍只有一个 `Packets`，所以 `0x380` 尚未证明充分。原始 `IdtRts.cpp` 与 Downloads 副本一致，使用五项 `X,Y,Pressure,Width,Height` packet description。当前 Debug-only 探针已改为只比较旧 `IdtDrawpad` 的 Tablet Pen Service flags `0x00010309`；RTS DataInterest、packet description、初始化顺序和窗口输入路由保持基线。

## Window instrumentation

`WindowController::HandleWindowMessage` 在 switch 之前观察鼠标消息，包括 `WM_LBUTTONDBLCLK`/`WM_RBUTTONDBLCLK`。观察调用只构造诊断标量；既有 switch 仍只维护光标、tracking 和窗口控制请求。promoted 标记只读取既有 `GetMessageExtraInfo` 签名，不影响 `ShouldIgnoreMouseCursorMessage` 的现有行为。

## Build isolation

- 主工程三个 Debug 配置加入 `DRAW3_RTS_DIAGNOSTICS`；三个 Release 配置不加入。
- `RealTimeStylusInput::SetRtsTraceEnabled`、`WindowController::SetRtsTraceEnabled` 和 `--rts-trace` 均在宏内声明/实现。Release 的类布局、命令行解析和模块导出不包含临时 trace 状态。
- 测试工程不启用该宏，也不编译窗口控制器；现有测试源和工程范围保持不变。

## Probe rollout

首轮日志只回答三个问题：

1. RTS 是否收到 `Packets`/`InAirPackets`。
2. callback 是否成功发布到 coordinator。
3. 窗口 Mouse Move 是否只是并行的 promoted/兼容观察。

只有问题 1 的答案为“否”时才进入上游探针。`SetAllTabletsMode` 探针无效果；`0x380` 曾在不同配置下出现连续包，但最新固定候选仍只有一个包，不能视为根因。当前 Debug-only 下一探针只使用旧 `IdtDrawpad` 的 `Tablet Pen Service flags=0x00010309`，Release 保持当前窗口 flags；两者都保持生产 RTS DataInterest、九项 packet description、初始化顺序和窗口输入路由。诊断开关只改变窗口 flags，并输出实际属性值。

## Compatibility

诊断不调用新增系统 API，不改变 Win7 动态加载路径、RTS COM 生命周期、窗口 style 或 Tablet Pen Service 属性。输出为 Debug 临时行为，Release 生产路径保持当前语义。
