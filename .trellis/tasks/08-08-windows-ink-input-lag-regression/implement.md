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

- [ ] 用户启动 Release ARM64：`inkStrokeModelerTest.exe --rts-trace`。
- [ ] 用户先用真实 Windows Ink Pen 画几笔，再紧接着用 Mouse 画几笔，然后正常退出应用。
- [ ] 读取控制台打印的本轮唯一日志路径，只分析该 session。
- [ ] 按 Down -> decoder/binding -> PublishDown -> Packets/state gate -> PublishMove -> Drawing snapshot -> Modeler 重建 Pen 时序。
- [ ] 与紧接着的 Mouse 事件比较，并把 Decoder miss、StateGate drop、Publish failure、Drawing/Modeler 等假设标为支持/削弱/已排除/证据不足。
- [ ] 信息不足时只提出下一轮必要诊断；任何生产修复必须等待用户明确进入修复阶段。

## Validation Commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64 /t:Build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64 /t:Build
.\ARM64\Debug\inkStrokeModelerTestTests.exe
.\ARM64\Release\inkStrokeModelerTestTests.exe
git diff --check
```
