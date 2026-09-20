# 调查收尾实施清单

## Final Cleanup And Cursor Setting

- [x] 以 `949752a` 恢复两次诊断提交及未提交 cadence B 覆盖的生产/测试文件。
- [x] 删除 ordered ring、额外 diagnostics/metrics、Release trace、Modeler-to-Present trace 和测试 hook。
- [x] 保留 `949752a` 的 cursor wake/Move 发布顺序。
- [x] 保留独立的 writer-latch ownership 最小修复，不携带实验 ring 与 barrier。
- [x] 增加默认关闭的 `StrokeModelConfiguration` 绘制时应用光标字段。
- [x] 增加 `DrawingController` 原子 setter/getter，并在值变化时发布一次 control wake。
- [x] 让普通 Pen/Highlighter Contact 在开关开启时复用 Hover application cursor；系统 cursor 和其他工具/设备不变。
- [x] 增加默认值、Pen/Highlighter 等价和完整反例矩阵测试。
- [x] 删除 cadence A/B、raw-tail probe 的专用构建、日志、任务目录和 detached worktree，保留用户 `Vcpkg/`。
- [x] 同步 runtime spec 和最终调查报告，不再宣称 P1 是最新提交回归或问题 2/3 已修复。
- [x] 完成 Debug/Release ARM64 全解决方案构建与两套测试。
- [x] 完成撤回符号搜索、BOM/CRLF、`git diff --check`、范围和 reviewer 检查。
- [x] 用户完成最终人工核验，任务进入提交与归档。

## Historical Diagnostic Checklist (Superseded)

## Phase 1: Diagnostics Only

- [x] 为 Down/Packets 的所有目标早退增加明确 `RtsPacketResult`，不改变返回值或生产状态转换。
- [x] 记录 Down decoder ensure/generation/decode/PublishDown 与成功坐标、压力。
- [x] 记录 Packets state gate/binding/decoder/generation/property/decode/PublishMove 结果；成功采样、失败聚合。
- [x] 增加 DrawingController Down/snapshot/modeler/recycle 关联事件和 Pen/Mouse/Touch 标签。
- [x] 未知 RTS 设备记录 `Unknown`，不在 decoder 缺失时猜成 Pen。
- [x] 增加唯一 session 文件、BEGIN/END/build 标记和 `--rts-trace-output`。
- [x] Release ARM64 编译 diagnostics，但未开启时保持轻量启用分支；DrawingController 禁用时不采集额外字段。

## Phase 2: Focused Non-GUI Tests

- [x] 覆盖 session BEGIN/END、配置/架构、reason 名称与 Drawing/Modeler 汇总。
- [x] 覆盖正常 Packets 采样数量和 Unknown 设备标签。
- [x] 覆盖 timeline overwrite 后失败 aggregate 的 count、first/last callback sequence/QPC。
- [x] 覆盖独立 contact reason 行，避免长 sequence 截断 reason。
- [x] 覆盖 timeline overwrite 后 lifecycle/error auxiliary 仍以独立类别输出。

## Phase 3: Build And Static Gate

- [x] reviewer auxiliary 修复后的完整解决方案 `Debug|ARM64` 构建通过。
- [x] reviewer auxiliary 修复后的 Debug ARM64 非 GUI 测试通过。
- [x] reviewer auxiliary 修复后的完整解决方案 `Release|ARM64` 构建通过。
- [x] reviewer auxiliary 修复后的 Release ARM64 非 GUI 测试通过。
- [x] 恢复所有修改 native/XML 文件的 UTF-8 BOM + CRLF。
- [x] `git diff --check` 和范围检查通过。
- [x] 审计 callback 无新增 heap allocation、同步 IO、blocking mutex、COM query、wake 或 wait。

## Phase 4: First Hardware Session

- [x] 用户启动 Release ARM64 diagnostics 并完成真实 Pen/Mouse session。
- [x] 日志确认 RTS Success=2990，Down/decoder/binding/state gate/Publish/Drawing 初始化/snapshot/Modeler 均无失败证据。
- [x] 将下一阶段边界推进到 Modeler output -> geometry -> render -> Present -> frame pacing，不回头增强 RTS/coordinator。

## Phase 5: Modeler To Present Diagnostics

- [x] 增加活动 frameSeq、连续 frame interval、latest input age 和前三 contact frame 完整样本。
- [x] 记录真实 Modeler/real/predicted/L0 数量、L1 committed index、prediction endpoint、drawable/changed geometry。
- [x] 记录 dirty/render decision、render begin/end/duration 和 cursor/stroke/full-present 分类。
- [x] 在真实 `Present1` 位置记录 QPC、HRESULT、参数、dirty rect 和连续 begin/end interval；保留 CPU submission 语义说明。
- [x] 记录现有 deadline wait 的 requested budget、wait begin/end、目标 deadline、actual wait 和 overshoot，不修改 pacing。
- [x] 记录现有 DComp initialization Commit；静态确认没有 frame-latency waitable handle 获取/等待。
- [x] 输出 contact summary 与 Pen/Mouse/Touch/Unknown device aggregate，覆盖 Down 到首次 geometry/render/Present。
- [x] 增加 fixed-capacity、timeline overwrite、前三帧、Present interval、contact/device summary 的非 GUI tests。
- [x] 完成 Debug/Release ARM64 全解决方案构建、两套 tests、`git diff --check`、BOM/CRLF 和热路径审计。

## Phase 6: Raw RTS / Visible Tail Probe

- [x] 记录 cadence A/B 反证，停止把 ordered ring 作为 P1 产品修复扩展。
- [x] 增加默认关闭的 `--pen-latency-probe` 参数，并传入 DrawingController。
- [x] 为同步 RTS Pen 样本增加独立 latest mailbox；`WM_POINTER` 不写入该来源，Contact 不新增 wake。
- [x] 在普通 Pen contact 的 cursor transient top layer 同帧绘制 raw RTS 环与实际 L0 tail 方框，并复用旧/新 cursor dirty 清理。
- [x] 仅在 probe 开启、普通 Pen 工具且 authority 为 Pen（或 Unknown + 有效 Pen 样本）时保留系统光标；其余输入与工具继续沿用既有隐藏矩阵。
- [x] 增加来源隔离、开关关闭、contact/hover/clear 和端点选择测试。
- [x] 基于 `2520c2c` 构建独立 ARM64 Debug/Release 诊断包；不叠加 cadence B。
- [x] 运行两配置完整解决方案构建、非 GUI tests、`git diff --check`、BOM/CRLF 与 callback/wake 静态审计。
- [x] 输出肉眼与高速录像测试矩阵，明确 raw-to-visible 与 physical-to-raw 的不同证据边界。

## Validation Commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64 /t:Build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64 /t:Build
.\ARM64\Debug\inkStrokeModelerTestTests.exe
.\ARM64\Release\inkStrokeModelerTestTests.exe
git diff --check
```
