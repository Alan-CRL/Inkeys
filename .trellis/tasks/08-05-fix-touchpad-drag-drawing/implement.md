# 实施清单

## Phase 1: restore baseline

- [x] 对照工作树 diff 精确撤回 RTS Mouse 短路、窗口鼠标 contact 发布、双击桥接测试和错误 runtime 规范条款。
- [x] 确认 `realtime_stylus.cpp` 的 MouseLeft/MouseRight Down/Packets/Up/InAir 恢复现有 coordinator 发布路径。
- [x] 确认 `window_control.cpp` 仍只处理既有光标消息；没有任何 `PublishDown/Move/Up/Cancelled` 调用。
- [x] 保留无关的 `Vcpkg/` 和其他任务改动。

## Phase 2: first diagnostic build

- [x] 在 `draw3.diagnostics` 增加宏隔离的初始化、RTS callback 和窗口观察 API。
- [x] 使用固定 32 个 contact 槽、每槽 10 条事件和 6 条 Move/InAir 上限；Up 输出汇总。
- [x] 在 RTS 初始化每个失败/成功出口记录 HRESULT、packet order 和 DataInterest。
- [x] 在 RTS Down/Packets/InAir/Up 旁记录原始/解码坐标、线程、QPC 和发布结果。
- [x] 在窗口消息入口观察普通/双击/移动/抬起及 promoted 标记，不触碰 coordinator。
- [x] 仅主工程 Debug|Win32、Debug|x64、Debug|ARM64 定义 `DRAW3_RTS_DIAGNOSTICS`。
- [x] 仅 Debug 接受 `--rts-trace`，窗口创建完成后才开始窗口消息计数。

## Phase 3: validation before user reproduction

- [x] 使用 ARM64 MSBuild 构建完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64`。
- [x] 运行 `ARM64\Debug\inkStrokeModelerTestTests.exe`，现有控制台测试通过。
- [x] 使用 ARM64 MSBuild 构建完整 `Release|ARM64`，确认编译命令未定义诊断宏和生产路径通过。
- [x] 执行 `git diff --check`、BOM/CRLF 检查并确认只保留任务范围文件。
- [ ] 不自动启动 GUI；交付用户在实体触摸板上复现 A/B。

## Phase 4: log-driven probe

- [x] 收到 A、B 两组 `--rts-trace` 日志；用户确认 B 仍按第二次按住移动复现，但第二次动作没有形成 RTS contact。
- [x] 判断 Packets 是否到达、发布是否成功，以及 Mouse Move 是否 promoted；A 的唯一 Packets 发布成功，B 没有 Packets/InAirPackets。
- [x] 启用并复测首个 Debug-only `SetAllTabletsMode` 单变量探针；A/B 结果未改变，普通 Debug 和 Release 仍使用基线调用。
- [x] 启用并复测第二个 Debug-only `DataInterest` 探针（仅 `StylusDown | Packets | StylusUp`）；A 收到连续包但因缺少生命周期缩放初始化而全部解码失败，B 捕获到两个 contact 但同样解码失败。
- [x] 启用并复测第三个 Debug-only `DataInterest` 探针（`RealTimeStylusEnabled | Disabled | StylusDown | Packets | StylusUp`）；解码恢复，但 A 又只剩一个 `Packets`，B 仍没有第二个 RTS contact。
- [x] 启用第四个 Debug-only `DataInterest` 探针（`RealTimeStylusEnabled | StylusDown | Packets | StylusUp`，`0x382`），并完成 Debug/Release ARM64 构建与测试。
- [x] 收到第四个 `0x382` 探针日志；A 仍只有一个 `Packets`，确认仅 Enabled 兴趣已经足以触发问题。
- [x] 从生产掩码移除 `RealTimeStylusEnabled`，并验证仅排除该位的 `0x63f5` 仍无法恢复 A；首 context 缩放初始化迁移到首个 `StylusDown` 慢路径。
- [x] Debug trace 与 Release 统一使用精确 `0x380`，诊断开关不再改变输入路由；增加静态测试锁定 contact 事件集，不再要求 hover。
- [x] 用户实测 `0x63f5` 后 A 仍无中间点；将生产与 Debug trace 收敛到精确 `0x380`，并保留 `StylusDown` 懒尺度初始化。
- [x] 静态测试改为锁定精确 `StylusDown | Packets | StylusUp`，不再要求 hover/生命周期兴趣。
- [x] 根据 `rts-fix-380-A.log` 确认精确 `0x380` 仍未恢复连续绘制；每个 contact 只有一个抬起前 `Packets`。
- [ ] 用户实测 Debug-only 旧 `IdtDrawpad.cpp` `TabletFlags=0x00010309` 探针是否恢复 A 连续绘制；这是当前硬验收门槛。
- [ ] A 通过后顺手复测 B；B 若仍失败则记录结果并停止，不测试窗口鼠标桥接或组合式 Draw2 profile。

## Phase 5: final fix and regression gate

- [ ] 只保留日志证明有效的最小 RTS 改动，移除或完全隔离临时诊断。
- [ ] 复测 Pen 绘制、Touch、多点、普通鼠标、resize、失焦和关闭；精确 `0x380` 不再承诺本插件的 Pen hover callback。
- [x] 完整 Debug/Release ARM64 构建、现有控制台测试和 `git diff --check` 通过。
- [x] 将根因证据和探针结论回写 PRD/设计文档；等待 A 真机硬验收后进入任务收尾。

## Validation commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64 /t:Build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64 /t:Build
.\ARM64\Debug\inkStrokeModelerTestTests.exe
.\ARM64\Release\inkStrokeModelerTestTests.exe
git diff --check
```

用户复现命令：

```text
ARM64\Debug\inkStrokeModelerTest.exe --rts-trace
```
