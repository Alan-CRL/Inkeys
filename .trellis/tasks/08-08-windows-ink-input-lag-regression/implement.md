# 实施清单

## Phase 1: Minimal Production Patch

- [x] 修改 `WindowController::PublishPenCursorSample`：所有样本继续发布 mailbox，仅 `!sample.inContact` 请求 cursor render。
- [x] 保留 authority 更新、系统 cursor 状态刷新、Clear 以及全部 `WM_POINTER` handler 代码。
- [x] 修改 RTS `Packets`：Move publication 在 Pen cursor publication 之前，diagnostics 仍最后记录。
- [x] 保持 Move 失败时仍更新 cursor、interruption simulation、last-packet 和 state gate 语义。
- [x] 添加简短中文注释说明 Contact cursor 由活动帧读取，无需每包唤醒。

回滚点：两个生产文件各只有一个局部行为变化，可独立恢复；不触碰 deadline 或输入存储。

## Phase 2: Source And Automated Verification

- [x] 核对 `WM_POINTER` 分支、Hover、Clear、Up/Leave 未改变。
- [x] 核对 contact sample 仍进入 mailbox，只有 render wake 被条件化。
- [x] 核对 RTS 顺序为 Move -> cursor -> diagnostics，callback 无等待/分配。
- [x] 运行现有 Pen cursor、contact input、RTS decoder/binding/lifecycle/state-gate 测试。
- [x] 执行 `git diff --check`，确认只有批准范围内的生产/规范/任务文档差异。

## Phase 3: Build And Runtime Validation

- [x] 使用 ARM64 MSBuild 构建完整解决方案的 `Debug|ARM64` 与 `Release|ARM64`，单次超时不少于 5 分钟。
- [x] 运行 Debug 与 Release 的 ARM64 测试程序。
- [ ] 真机 A/B 验证 Pen 跟手性、追赶抽帧、Contact Eraser/倒转笔 cursor、Hover 与触觉。
- [ ] 验证 Mouse、Touch、Precision Touchpad 不变，成功 Present 仍不超过 120 FPS。
- [ ] 若仍复现，使用已有 RTS trace 另行调查 state gate，不扩大本补丁。

## Validation Commands

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Debug /p:Platform=ARM64 /t:Build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe' .\inkStrokeModelerTest.sln /m /p:Configuration=Release /p:Platform=ARM64 /t:Build
.\ARM64\Debug\inkStrokeModelerTestTests.exe
.\ARM64\Release\inkStrokeModelerTestTests.exe
git diff --check
```
