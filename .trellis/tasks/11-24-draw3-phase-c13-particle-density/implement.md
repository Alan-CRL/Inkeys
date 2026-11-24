# 实施计划：阶段 C1.3 画布粒子数量上限档位

上下文按这个顺序读：`prd.md` → `design.md` → `research/clear-frame-benchmark.md` → 本文件。

文件格式要求：
- 源文件（.cpp/.cppm/.h）要保持 UTF-8 BOM + CRLF；hlsl/hlsli 保持原有编码与换行。
- 改动用 Edit，不要用 `sed -i`（会把 CRLF 转成 LF）。
- vcxproj、rc 只做接入新着色器必需的改动。

## 执行顺序（2026-11-24 开工核对后修订，优先于下文的 S 编号）

分工：Codex（`gpt-6.2-sol`，High，`service_tier=default`，workspace-write）写代码；另起 Codex 只读会话做独立审查；主 Agent 负责简报、构建、自检、读完整差异、变异验证与验收。

| 写入步 | 内容 | 对应下文 |
|---|---|---|
| W1 | 单池迁移到运行时容量（档位表只用低档）：容量与掩码、`capacityMask` 常量与 VS、环的批次上限与 `TrimLatest`、分块暂存、全部自检同步；行为不变 | S1 + S2 的第 2、5 项 |
| W2 | 拆成行进池和飘散池、`SetLimit`、逐级降档（两池各自独立降级）、四档自检 | S2 其余项 |
| W3 | `MakeDriftParticle` 直写记录：激光飘散与 CPU 清空路径；`bad_alloc` 兜底 | S3 |
| W4 | 墨迹范围查询、计算着色器与工程接入、渲染器生成与轮询、清空事务（design 8.5 第 3–5 项） | S4 |
| W5 | 配置、Host、每帧读取档位（design 8.5 第 6 项）、设置页、文案、配置自检 | S5 |

门禁：
- 每个写入步完成后，主 Agent 构建并跑相关自检，读完整差异，再做至少一次变异验证。
- W2、W4、W5 之后各做一次 Codex 只读审查，修复后复核。
- 全部完成后，Codex 和主 Agent 各做一次完整审查。

## 步骤

### S1 档位表、选项位、运行时容量（只接入低档，行为不变）
1. `Draw3.LaserParticles.cppm`：
   - 新增档位枚举与档位表、`kLaserParticleMaximumBatches = 8192`、`kLaserParticleStagingChunk = 16384`、`kLaserTrailPoolCapacity = 8192`，加上 `static_assert`。
   - 删掉 `kLaserParticleCapacity`；删掉两个参数结构体里的 `maximumSeeds`（密度字段保留不动）。
   - 新增数量上限选项位，以及 `LaserParticleLimitFromOptions`。
2. `LaserParticleActiveRing`：`Initialize(capacity)`；批次表固定上限；新增 `TrimLatest`；所有下标改用运行时掩码。
3. 着色器：`capacityMask` 放进常量缓冲区（偏移 52），VS 和 hlsli 同步修改。
- 检查点：编译通过，现有粒子自检数值不变。

### S2 拆成两个池，分块暂存
1. `LaserParticleSystem` 内部拆成行进池和飘散池（design 4.1），按 `kind` 分流；绘制、撤回、汇聚、空闲归还、`Reset` 都按池处理。
2. `staging_` 改为固定分块；`Step` 和 `KillSlots` 按块上传；`slotGroups_` 按各池容量分配。
3. 飘散池按档位创建，失败时逐级降档并记下上限，写日志；新增 `SetLimit`。缓冲区加 UAV 绑定标志。
- 检查点 G1：低档下现有所有粒子自检数值与改动前一致（拆池不改变外观）。

### S3 直写记录（CPU）
1. 合并出 `MakeDriftParticle`：`AppendDriftParticleSeed` 的计算加上 `PackLaserParticle` 飘散分支的计算，直接得到 `LaserGpuParticle`，同时累加包围盒和寿命。常量提到循环外。
2. 激光飘散 `AppendLaserDissolveSeeds` 改为直写；`Step` 新增接收已打包记录的入口。
3. CPU 清空路径（作为退回路径和自检对照）：改为直写，抽样算法（2×2 代表像素、0.5 粒/平方 DIP）保持不变。
4. 两处生成都用 `try/catch (const std::bad_alloc&)` 兜底。
- 检查点：在 S5 的本机基准上，CPU 清空每粒的耗时明显下降（记录数字）。

### S4 GPU 生成（清空）
0. 新增墨迹范围查询 `VisibleContentScreenBounds`（design 5.4），接口按 C2 页面级空间索引可接手的方式设计；用线性遍历实现。
1. 新增 `Assets/laserParticleSpawnCS.hlsl`（逐物理像素，计数遍 + 生成遍，只派发墨迹范围），与 VS 共用 `laserParticleCommon.hlsli`，把风场、哈希、碎裂哈希等公式移到这个公共头里。包含计数遍、生成遍，以及可选的清零遍。
2. 工程接入：vcxproj 的 None、读写行、Inputs/Outputs、FxCompile（全部配置，Compute，`cs_5_0`）；rc 资源和资源 ID；渲染器加载字节码。
3. 渲染器：
   - 新增 `SpawnCanvasClearParticles`：预留槽位、清零、两次派发、复制 counters 到 staging。
   - 新增 `PollCanvasClearSpawnResult`：用 `DO_NOT_WAIT` 轮询，返回实际粒子数。
   - counters 缓冲区、UAV 和 staging 缓冲区延迟到第一次使用时创建，空闲时随飘散池一起归还。
4. `DrawingController` 清空分支按 design 5.3 改写：
   - 快照捕获成功、GPU 路径可用 → GPU 生成。
   - 否则 → CPU 路径（原算法，仍然先读回）。
   - 脏区与碎裂范围直接用历史查询得到的墨迹范围；异步拿到粒子数后 `TrimLatest`，0 粒时撤销这一批。
5. 计时日志：清空当帧分别记录快照、预留、派发、CPU 总耗时（Debug 级）；异步结果到达时记录粒子数和延迟的帧数。

### S5 选项链路与设置页
1. 配置字段 `Particles.Limit`；Host 的 `CanvasParticleOptions` 新增该字段；`IdtMain` 和 `Setting` 下发时带上它。`DrawingController` 检测档位变化后调用 `SetLimit`，并清理飘散批次和脏区。
2. 设置页：新增或复用 `InlineCombos`，“飘散方向”下面放“粒子数量上限”；运行 `i18n.ps1 sync` 和 `check`（需要 pwsh），重新生成 `IdtI18nKeys.g.h`。
3. 设置导航测试先只跑导航函数定位问题，再跑全量。

### S6 自检
- `--config-boundary-test`：默认值为 1；0–3 能往返保存；非法值回落到 1。
- `--draw3-laser-resources-test`：
  - 四档的飘散池容量与掩码正确；行进池固定为 8192。
  - 切换档位只清飘散池，行进池不受影响。
  - 注入创建失败后逐级降档，之后不再尝试更高档。
  - 环：非默认容量下的跨越末尾、批次上限触发淘汰、`TrimLatest`，以及最新一批不在环尾时不缩短。
  - 一批满额清空和一批满额激光飘散同时在环内，两批都完整。
  - 分块上传时跨块、跨越环形末尾的记录内容正确。
  - **GPU 与 CPU 对照（100% 缩放）**：同一块小画布（含不透明、半透明、抗锯齿边缘和多种颜色），分别跑两条路径：
    - 粒子数相等，或在统计容差内（两边的随机数一致时应当相等）。
    - 记录按 (位置, 随机数) 排序后逐粒比较：位置、颜色、释放时刻、寿命完全一致；三角函数相关的字段在容差内。
  - GPU 路径：counters 回读正确；空范围时为 0 粒；墨迹超过上限时截在上限，不超过时 = 面积 × 0.5；释放时刻等于出生像素在遮罩里消失的时刻。
  - 200% 缩放：GPU 逐像素与 CPU 2×2 抽样的粒子数相差在统计容差内（例如 ±4σ），位置分布按 16×16 分块计数对比。
  - 墨迹范围：多种内容（笔、荧光笔、图形、橡皮擦过、白板平移后）下，读回得到的范围 ⊆ 历史查询得到的范围。
  - 计算着色器不可用时，清空走 CPU 路径，结果一致。
- `--draw3-laser-raster-failure-test` 中的激光种子用例：改为按档位断言（触顶、圆点保底；密度在各档相同）；低档直写记录与原来“种子 + 打包”的结果逐字段一致（同一套公式，只是合并了）。
- 其他 Draw3 自检照常通过（参考 C1 归档清单）。`--draw3-hidden-test` 一类测试以 `0xE143001D` 退出是基线既有问题。

### S7 构建与实机
- 构建：ARM64 原生 MSBuild，`InkeysRepo.sln`，`Debug|ARM64`，`MSBUILDDISABLENODEREUSE=1`，超时至少 5 分钟。如果出现 `LNK1000`，移走 `Inkeys.ilk` 后重新链接。
- 自检用 `& exe --xxx | Out-File` 启动。
- 实机（需要用户配合）：
  - 四档各清空一次写满的白板，各做一次激光飘散，以及两者叠加。
  - 从日志读清空当帧的 CPU 耗时；从性能看板读飘散期间的帧时间。
  - 目视检查大面积涂抹清空时亮度不再骤降。

## 检查门

- G1（S2 后）：低档拆池后外观和数值不变。
- G2（S4 后）：GPU 与 CPU 对照自检通过。
- G3（S6 后）：全部自检、i18n check 和构建通过；`git diff` 无意外改动（编码、换行、无关文件）。
- G4（实机后）：用户确认观感与流畅度，才进入第 3 阶段。

## 第 3 阶段要更新的规范

- `.trellis/spec/native-desktop/draw3-shaders.md`：新增生成着色器，常量缓冲区新增 `capacityMask`，公共头的公式归属。
- `.trellis/spec/shaders/cpu-gpu-contracts.md`：粒子管线一节整体改为现状：两个池、档位表、counters 布局、预留与异步缩短、退回 CPU 路径。原来的计算着色器每帧模拟方案已经过时，整节替换。
- `.trellis/spec/native/runtime-and-rendering.md`：同步粒子相关条目。
- `.trellis/spec/native-desktop/settings-performance-and-drawing.md`：新增 `Drawing.Particles.Limit`，修正其中已过时的“粒子强制关闭”描述。
- 父任务 `prd.md` 的子任务表更新 C1.3 的状态。

## 回退

- 只回退数值：把档位表里“中”的数值改成低档，或把默认值改为 0。
- 墨迹范围查询有漏：清空改用整张画布作为范围（多一点开销，但结果正确）。
- GPU 生成出问题：强制走 CPU 路径（保留一个内部开关，供自检和排查使用），不影响其他部分。
- S1–S3 的结构改动不依赖 GPU 生成，可以单独保留。
