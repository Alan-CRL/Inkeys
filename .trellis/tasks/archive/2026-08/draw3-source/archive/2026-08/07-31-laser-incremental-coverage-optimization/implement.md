# 激光笔增量覆盖实施计划

## Implementation Order

1. 抽取可测试的 Laser 稳定边界/增量 range 计算，扩展 `LaserStrokeLayer` 状态和批次快路/回退决策。
2. 新增可选 `laserLiveCoverage`、选择时 Ensure、Resize/Release/Clear 生命周期和 t9 解绑。
3. 扩展 HLSL shape 13：MAX 合并 t7/t9 后单次 `ResolveLaserMaterial`；补充 renderer resolve 与零像素预热入口。
4. 重排 controller：先更新 coverage/dirty，再合成 backbuffer；接入粒子 dirty 下的稳定 coverage 重放、单 contact Bake 和多 contact 锁定回退。
5. 加入临时资源创建、SlowFrame 和批次 Summary 输出，确保日志阈值化且不写 metrics JSON。
6. 新增独立 Laser 增量测试并加入测试工程；同步 CPU/HLSL slot、shape 和资源解绑静态契约。
7. 完成全解决方案 ARM64 Debug 构建、测试、diff/编码检查与人工验证说明。

## Validation Commands

- 使用 ARM64 原生 `MSBuild.exe` 构建完整 `inkStrokeModelerTest.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64 /m`，超时至少 5 分钟。
- 运行 `ARM64\Debug\inkStrokeModelerTestTests.exe`，确认新增与既有测试通过。
- 运行 `git diff --check`，核对自研 C++/HLSL/工程文件保持 UTF-8 BOM + CRLF，Trellis Markdown 保持 UTF-8 LF。
- 人工测试粒子开关、长笔画、自交、压力、prediction、多 Touch 回退、Resize/Clear、Hold/Fade，并收集 `[LaserPerf]` 输出。

## Diagnostic Output

临时诊断只写标准输出，不扩展长期 metrics JSON：

```text
[LaserPerf] resource_create available=true|false cpu_ms=<number>
[LaserPerf] resource_resize available=false fallback=full_redraw
[LaserPerf] SlowFrame workMs=<number> presentMs=<number> presentOk=0|1 dirty=[l,t,r,b] mode=incremental|fallback particles=0|1
[LaserPerf] Summary mode=incremental|fallback particles=0|1 layers=<n> max_points=<n> stable_points=<n> live_points=<n> full_equivalent_points=<n> dirty_pixels=<n> peak_dirty_pixels=<n> active_frames=<n> coverage_calls=<n> coverage_cpu_total_ms=<number> coverage_cpu_avg_ms=<number> coverage_cpu_max_ms=<number> fallback=<reason>
```

`coverage_cpu_*` 表示绘制线程的 CPU command submission 时间，不代表 GPU duration；资源创建只在首次尝试输出，批次汇总只在最后一次 Up/Bake 输出，慢帧才输出 `workMs`、Present 和 dirty bounds。

## Review Gates

- `frameDirty` 在 `CompositeLayersToBackBuffer` 前闭合，render 函数不得在基础合成后扩大 Present dirty。
- shape 13 只对同一笔稳定/live coverage 取 MAX；不同 contact 仍按现有 source-over 顺序处理。
- 快路不清除稳定 coverage 的矩形，不因自交误删旧几何；只清独立 live coverage。
- 新资源未创建或失败时所有公开行为与当前 HEAD 一致。
- Resize 后 coverage 为空时稳定提交索引必须 reset/rebuild，禁止跳过前缀。
- 粒子、Pen、Highlighter、Eraser 与 presenter 源码不做无关重构。

## Rollback Points

- Renderer/HLSL 完成后先构建 shader，shape 13 或资源绑定失败则不进入 controller 改造。
- Controller 接入后保留完整重绘函数作为运行时回退，不删除旧路径。
- 临时诊断独立于正确性状态，可在用户回传日志后单独删除。

## Implementation Status

- [x] 单 contact 使用时间保护边界提交稳定 coverage delta，并独立重绘 live/prediction coverage。
- [x] 第二个 Laser contact 将当前批次锁定到既有完整重绘，最后 Up 保持 Down 顺序 Bake。
- [x] `laserLiveCoverage` 在绘制线程首次观察 Laser 时按需创建，Resize/Clear/Release 与 t9 解绑已同步。
- [x] shape 13 对 t7/t9 逐通道 MAX，并只调用一次 `ResolveLaserMaterial`。
- [x] coverage/粒子/cursor dirty 在 backbuffer 基础合成前闭合，增量 resolve 会在最终 frame dirty 内重放稳定 coverage。
- [x] `[LaserPerf]` 记录资源创建耗时、coverage SlowFrame 与批次汇总，不扩展 metrics JSON。
- [x] 测试工程中的 focused 断言覆盖时间边界、连接点重叠、重建 reset、多 contact 锁定和 coverage MAX。
- [x] ARM64 Debug 完整解决方案构建成功；VS、PS、UpdateCS、EmitCS 均成功编译并完成资源链接。
- [x] 同帧 `Up -> Down` 的旧批次 Bake dirty 在基础合成前消费；coverage clear/upload/resolve 失败不会推进稳定游标，会清理 t7/t9 并锁定当前批次完整重绘。
- [x] 用户已人工验证长笔画、自交、压力、prediction 回缩、粒子/cursor dirty、Resize/Clear 和多 Touch 视觉。
- [x] 最新 ARM64 Debug/Release 无窗口控制台全量测试通过；此前 30 个 Laser 粒子默认参数断言已随现行默认配置同步修正。

## Latest Validation Record (2026-07-31)

- `[x]` 最终 `inkStrokeModelerTest.sln` `Debug|ARM64` 强制 Rebuild 成功；VS、PS、UpdateCS、EmitCS 均重新编译，主程序和测试程序均链接。
- `[x]` `inkStrokeModelerTestTests.exe --laser-incremental-only` 通过：`All laser incremental coverage tests passed.`
- `[x]` `git diff --check` 通过；新增/修改的 C++、HLSL 和工程文件保持 UTF-8 BOM + CRLF。
- `[x]` 后续 ARM64 Debug/Release 无窗口控制台全量测试通过，旧的 30 项粒子默认参数断言失败记录已失效。
- `[x]` 用户已完成真实窗口视觉、多 Touch、Resize/Clear 和 Present 恢复相关人工验证；D3D Debug Layer 不作为本任务归档阻塞项。
