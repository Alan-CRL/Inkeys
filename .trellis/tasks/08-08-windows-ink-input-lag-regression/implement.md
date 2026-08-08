# 实施清单

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

## Validation Commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64 /t:Build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64 /t:Build
.\ARM64\Debug\inkStrokeModelerTestTests.exe
.\ARM64\Release\inkStrokeModelerTestTests.exe
git diff --check
```
