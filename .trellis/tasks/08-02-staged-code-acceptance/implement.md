# 阶段 1 绘制性能优化实施计划

## Implementation Order

1. 在不修改生产行为的前提下增加无窗口性能/等价性测试入口，记录四类绘制固定工作负载的分配、元素处理量、命令资格和耗时中位数基线。
2. 将 renderer、dirty rect 与 controller 热点中的只读点集合改为 span；用栈上固定数组处理 dot，消除 Pen、Eraser、Laser 稳定/live 子区间临时 vector。
3. 实现 Highlighter in-place 流式去重与 primitive rebuild，复用 L0 capacity；完成路径分别绘制 committed/live primitive，补充参考实现逐项等价和无额外分配断言。
4. 扩展多 contact Laser fallback 状态，计算 stable delta 与旧/新 live dirty；renderer 增加 scratch 交集清理和 scissor 限制，并验证 Down 顺序、source-over、自交与 Bake plan 等价。
5. 将 Laser 矩形 shape 改为常量生成 quad，加入样式常量 generation/opacity 缓存；同步 CPU/HLSL shape、slot、失效和失败回退契约。
6. 合并粒子 tracker 的 prune/activity/bounds 为每帧 snapshot；用 snapshot 跳过空闲 simulation/draw，并保留最后一次旧 bounds 清理。
7. 实现单次绑定的 batched particle update/emit，保持多 contact 请求顺序、spawn cursor 和 seed；补充命令序列与粒子结果测试。
8. 将 `LaserGpuParticle` 压缩为 80 字节，改用 GPU reset 初始化，删除 shader 中无最终贡献的 white-mix 计算；增加 C++/HLSL layout 与静态数据流契约测试。
9. 运行完整静态检查、ARM64 Debug/Release 全解决方案构建、两个配置的无窗口全量测试与性能入口，记录优化前后数据和禁止执行的 GUI 验证项。

## Validation Commands

- 基线和优化后均运行 `ARM64\\Debug\\inkStrokeModelerTestTests.exe` 与 `ARM64\\Release\\inkStrokeModelerTestTests.exe`。
- 运行新增的无窗口 `--drawing-perf`（最终名称以测试实现为准）；该入口不得调用主程序启动、窗口枚举、输入注入或窗口尺寸修改代码。
- 使用 ARM64 原生 `MSBuild.exe` 对完整 `inkStrokeModelerTest.sln` 执行 `Debug|ARM64` 和 `Release|ARM64` Build，单次超时至少 5 分钟。
- 运行 `git diff --check`，并检查修改的 C++/HLSL/工程文件仍为 UTF-8 BOM + CRLF，Trellis Markdown 为 UTF-8 无 BOM + LF。
- 使用静态测试或 `rg` 核对矩形 shape 的 buffer 访问、particle stride/offset、slot 绑定/解绑、scissor 恢复和缓存失效点。

## Review Gates

- 每批先通过输出等价测试，再评估分配、复制、提交数和耗时变化；不得以性能提升为理由接受像素或顺序差异。
- `frameDirty` 必须在基础合成前闭合，所有 renderer 调用只能消费既定 dirty，不能在合成后扩大 Present 区域。
- 多 contact Laser 仍使用完整 layer 几何和 Down 顺序 resolve；scissor 只限制执行像素，不改变几何集合。
- 粒子空闲 gating 不能跳过仍可能存活的 GPU batch，也不能遗漏活动结束后的旧 bounds 清理。
- CPU/HLSL particle stride、矩形常量用途与 shader slot 必须同时修改并由测试锁定。
- 不启动主程序或任何 GUI 测试；`--benchmark` 始终禁止。
- 阶段 2、阶段 3 和未跟踪 `Vcpkg/` 不进入本轮 diff。

## Rollback Points

- 基线测试独立落地；若无法证明无窗口或数据稳定，不进入生产优化。
- span 与 Highlighter 批次可独立回滚，不影响 Laser/particle renderer 状态。
- 多 contact scissor 发生任何顺序或脏区差异时恢复原完整脏区 fallback，单 contact 增量路径保持不动。
- rect 常量和样式缓存可分别回滚；缓存上传失败必须自动失效。
- 粒子 gating/batching 与 80 字节 layout 分开验证；layout 或 reset 失败时先恢复 128 字节结构，不影响前述优化。

## Completion Record

- [x] 无窗口基线测试与数据已记录。
- [x] 四类绘制 CPU 容器/范围优化完成并通过等价性测试。
- [x] 多 contact Laser dirty/scissor 优化完成并通过顺序与脏区测试。
- [x] Laser rect pass 与样式常量缓存完成并通过 CPU/HLSL 契约测试。
- [x] 粒子 snapshot、空闲 gating 和 batched emission 完成并通过确定性测试。
- [x] 80 字节 particle layout、GPU reset 初始化和 dead shader work 删除完成。
- [x] ARM64 Debug/Release 完整解决方案构建和无窗口控制台测试通过。
- [x] 最终性能对比、静态检查、编码换行检查和未验证项已记录。

## Stage 1 Validation Result (2026-08-03)

- 固定 4096 点 Highlighter workload：优化前热循环分配 `31` 次，优化后 Debug/Release 均为 `0` 次；primitive 数保持 `4095`。
- 优化前 Debug 中位耗时：Highlighter `773.7 us`、Pen bounds `318.5 us`、Eraser bounds `316.6 us`、Laser bounds `512.7 us`。审查后最终复测为 `854.8/316.6/336.9/664.8 us`；按 PRD 只记录，不作为正确性门槛。
- 审查后最终 Release 中位耗时：Highlighter `30.1 us`、Pen bounds `11.2 us`、Eraser bounds `11.2 us`、Laser bounds `59.8 us`。
- Debug/Release `ARM64` 完整解决方案构建通过，VS/PS/UpdateCS/EmitCS 均重新编译；两个配置的全量无窗口控制台测试与 `--drawing-perf` 通过。
- `git diff --check` 通过；修改的 C++/HLSL/test 文件为 UTF-8 BOM + CRLF，Trellis Markdown 为 UTF-8 no-BOM + LF。
- 按用户限制未启动主程序、未创建或操控窗口、未运行 `--benchmark`。真实窗口视觉效果、D3D11 Debug Layer、人工多指/粒子/Resize/Present 输入验证仍待后续人工验收。
- 本记录只完成 Stage 1；Stage 2 冗余/规范验收和 Stage 3 最近一个月安全验收继续保留为未开始。
