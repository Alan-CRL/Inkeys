# 阶段 C1.3 实现与验收记录（2026-11-24）

## 分工
- Codex：`gpt-6.2-sol`，High，`service_tier=default`，用 workspace-write 写代码；另起只读会话做独立审查。
- 主 Agent：写简报、构建、自检、读完整差异、做变异验证、裁定审查意见、验收。
- 简报、报告、差异快照都在任务外的 `%TEMP%\inkeys-codex\phase-c13\`。

## 写入步骤
| 步 | 内容 | 主 Agent 验证 |
|---|---|---|
| W1 | 单池改为运行时容量，常量缓冲区加 `capacityMask` | 构建与自检通过；把 VS 掩码改回固定值后，用例失败 |
| W2 | 拆成行进池（8192）和飘散池（按档位），四档表，逐级降档 | 极致档 64 MB 在 WARP 上实际创建成功；去掉降档注入后 5 条用例失败 |
| W3 | 飘散粒子直写 64 字节记录（激光飘散、CPU 清空） | 与“种子 + 打包”逐字段一致；改动一个哈希常量后，3 组对照失败 |
| W3b | 生成前先确定实际档位；飘散池不可用时走退路；飘散池单独归还；行进池故障注入 | 把接纳结果改为恒真后，用例失败 |
| W4a | 计算着色器 `laserParticleSpawnCS.hlsl`（清槽、计数、生成）、工程接入、批次身份 `TrimTail`、异步回读 | 1x 时 GPU 与 CPU 整数字段完全一致；浮点误差只出现在 burst.x/y、birthWind.y，相对误差最大 8.1e-4，绝对误差 ≤1.6e-5 px |
| W4b | 历史墨迹范围查询；清空事务接入 GPU 生成；最坏行程上界 | 把范围内缩 3 px 后，6 类墨迹的用例失败 |
| W5 | 配置 `Drawing.Particles.Limit`（默认 1）、选项位 bit6–7、每帧应用档位、设置页 `InlineCombos`、20 种语言文案 | i18n 20 种语言 100%；把 Limit 上限改成 4 后，配置自检失败 |
| W6a | 回读 0 粒时撤销对应脏区；先查范围再建池；CPU 退路的碎裂范围用历史范围 | 去掉撤销逻辑后，2 条用例失败 |
| W6b/W6c | 生成飘散前再同步一次档位；测试夹具改用独立 WARP 设备（原夹具改动了共享视口） | 5 条被污染的用例恢复通过 |
| W7 | 墨迹范围逐项与查询区求交后再合并；待上传数组超过 16384 时回收容量；修正注释 | 去掉逐项裁剪后，6 条用例失败 |
| 主 Agent | 同一帧里已有入队的飘散批时，换档推迟到下一帧帧首（最后一轮复核的 P2） | 构建与自检通过；没有自动化用例覆盖，需实机确认 |

## 审查
- 开工核对 1 次（`research/kickoff-check.md`），分段审查 3 次，终审 1 次，复核 1 次，均未发现 P0/P1。
- 采纳并修复的 P2：降档时生成预算与池不一致；飘散池不可用时的退路；飘散池单独归还；回读 0 粒时的脏区；先查范围再建池；同帧内切档（两次）；视口两侧墨迹的合并框。
- 不采纳、记为既有问题：
  1. 新的清空在清页成功之前就复用了旧碎裂的快照，清页失败时旧过渡随之丢失。改动前就是这样。
  2. 平移恢复还没完成时清空，只能复制到部分重放的 L2，屏幕上尚未写回 L2 的墨迹会不经碎裂直接消失。改动前就是这样，属于 C2 的范围。
  3. 触顶时按期望值抽样，实际数量可能略少于上限，与 CPU 原算法一致。
  4. 同一个池里不同 DPI 的存量批次共用最近一次的 dipPerPixel。改动前就是这样。
  5. Host 的档位唤醒可能被输入屏障推迟到触点结束，这是现有的下发机制。
  6. 行进粒子种子分配失败会退出绘制循环。行进粒子的生成逻辑本次没有改动。
  7. 粒子 UAV 创建失败时不降档，而是退回 CPU 路径，行为正确。
  8. 极致档满额预留时要写约 2 MB 的分组表。

## 回归
- 自检：config-boundary、parked-desktop-exit、laser-resources、laser-raster-failure、renderer-upload/pixel/commit-failure、canvas-debug、whiteboard-pattern、ulw-pipeline、shape-renderer、whiteboard-storage、setting-* 全部通过。hidden 和 auxiliary-resource-lifecycle 以基线既有的 0xE143001D 退出，日志里没有 FAIL。
- InkeysHeadlessTests 只剩 1 项已知的既有失败：`closing minimized whiteboard clears iconic anchor without restoring canvas`。
- 构建环境：本机可用内存紧张，并行编译多次出现 C1060。改用 `-m:1 -p:CL_MPCount=3` 后构建稳定通过。

## 待实机验收
见 prd.md 的验收标准最后一条。另外还需确认：同帧切档与清空交错、汇聚过程中切档、高 DPI 细线和低透明度边缘、白板与 PPT 恢复视口、设置页的收起与窄窗口排版。Win7、WARP、兆芯与 Win32 留到父任务最终集成时验收。
