# 设计：阶段 C1.3 画布粒子数量上限档位

## 0. 总体结构（2026-11-24 用户确认）

- **数量上限档位**：只决定飘散池容量和单次上限，极致档飘散池 64 MB。密度保持原算法（清空每平方 DIP 0.5 粒，激光每 DIP 3 粒）。
- **清空飘散改由 GPU 生成**：计算着色器逐物理像素读清空快照纹理，写出 64 字节的 `LaserGpuParticle` 进飘散池；之后照旧由现有顶点着色器按“出生参数 + 时钟”求值，再由像素着色器绘制。去掉整屏读回、CPU 扫描、生成、打包和上传。计算着色器不可用时退回 CPU 路径。
- **激光飘散 CPU 直写**：直接写记录，不再生成 88 字节的中间种子。
- **行进粒子单独成池**。
- **墨迹范围由 CPU 从页面历史得到**，做成可复用的查询，供 C2 的页面级空间索引接手。

B2 第四轮去掉计算着色器，针对的是每帧模拟，那个结论不变：每帧仍然只由顶点着色器求值。这次的计算着色器只在清空那一刻运行一次，用来出生粒子。粒子系统本来就要求特性级别 11_0，计算着色器 5.0 一定可用。

## 1. 现状要点

| 位置 | 现状 | 本任务 |
|---|---|---|
| `kLaserParticleCapacity = 32768`（C++ 与 hlsli） | 编译期常量；VS 用 `& (容量 - 1)` 取下标 | 改为运行时容量，掩码由常量缓冲区下发 |
| `LaserParticleSystem` 单池 | 三种粒子共用；`Draw` 的两遍都遍历整个活跃区间 | 拆成行进池和飘散池 |
| `AppendCanvasClearParticleSeeds` | 整屏读回 → CPU 两遍扫描 → 种子 → 打包 → 上传 | GPU 生成；CPU 版本只作退回路径和自检对照（改为直写，算法不变） |
| `LaserDissolveParameters::maximumSeeds`、`CanvasClearParticleParameters::maximumSeeds` | 都是 16384 | 改为从档位表读取；密度字段不动 |
| `staging_`、`slotGroups_`、`batches_` | 都按池容量分配 | 暂存区分块；批次表固定上限 |
| `canvasClearCrumbleBounds` | 用 CPU 读回得到的墨迹范围 | 改用页面历史的可见内容范围 |

## 2. 档位表（唯一数据来源）

```cpp
enum class LaserParticleLimit : uint32_t { Low = 0, Medium = 1, High = 2, Extreme = 3 };
struct LaserParticleLimitTier
{
	uint32_t poolCapacity; // 飘散池槽数，2 的幂
	uint32_t maximumSeeds; // 单次上限 = poolCapacity / 2
};
// 低：与改动前相同；中（默认）8 MB；高 32 MB；极致 64 MB。
{ 32768, 16384 }, { 131072, 65536 }, { 524288, 262144 }, { 1048576, 524288 }
```

- 用 `static_assert` 约束：每档容量是 2 的幂；`maximumSeeds * 2 == poolCapacity`；`poolCapacity * 64 B` 不超过 64 MB；各档单调递增。
- 粒子数 = 墨迹量 × 原密度，超过 `maximumSeeds` 时同比变稀（现有算法）。所以档位越高，能容纳而不变稀的墨迹越多：极致档约 105 万平方 DIP，相当于 1080p 屏幕的一半。
- 行进池固定 8192 槽（512 KB）。

## 3. 选项下发

- 配置 `Drawing.Particles.Limit`（int，默认 1，校验 0–3）→ `CanvasParticleOptions::limit` → `LaserParticleOptions` 的 bit6–7（`LimitShift = 6`）→ `latchedLaserParticleOptions`。用 `LaserParticleLimitFromOptions` 取出，越界时返回 Medium。
- 设置页、`IdtMain.cpp` 启动时的下发都要带上它。

## 4. 粒子池

### 4.1 拆成两个池

- `LaserParticleSystem` 内部持有两个 `ParticlePool`，各自有缓冲区、SRV、活跃环和 `slotGroups_`：行进池（8192，只放 `Trail`）和飘散池（按档位，放激光飘散和清空飘散）。
- `Draw(UnderBody)` 只画飘散池，`Draw(OverBody)` 只画行进池，各自只遍历自己的活跃区间。VS 不变，常量缓冲区里的 `activeStart` 和 `capacityMask` 按池下发。
- 撤回、汇聚、`Reset`、空闲归还都按池执行；`Step` 收到的记录按种类分流。

### 4.2 运行时容量、分块暂存、批次上限

- 每个池有自己的 `capacity_` 和 `capacityMask_`，所有取模改为 `& capacityMask_`。
- `LaserParticleActiveRing::Initialize(capacity)`；批次表固定 `kLaserParticleMaximumBatches = 8192`，满了先淘汰最旧批次，哨兵值改用这个批次上限。新增 `TrimLatest(count)`：最新一批是环里最后一批时，把它缩短到 `count`，光标同步回退。
- `staging_` 固定 16384 槽（1 MB），两个池共用；`Step` 和 `KillSlots` 按块上传。
- 飘散池缓冲区的绑定标志为 `SHADER_RESOURCE | UNORDERED_ACCESS`；UAV 只在生成那一次绑定。

### 4.3 档位切换与降档

- `SetLimit(limit)`：档位不同时，清掉飘散池并归还它的资源，下次按新档位创建；行进池不受影响。`DrawingController` 同步清掉脏区跟踪器里的飘散批次，并把原粒子覆盖的区域加入脏区。
- 飘散池的缓冲区、SRV 或 UAV 创建失败时，逐级降档，并记下 `limitCeiling_`（本次设备会话不再尝试更高档），写日志。低档也失败，飘散池就不可用。着色器创建失败沿用现有的 `Release()`。
- 生成粒子时用飘散池实际生效的档位。

## 5. 清空：GPU 生成

### 5.1 逐像素，2×2 怎么办

- 原 CPU 实现按 `step = floor(dpiScale)` 每 step×step 像素取一个代表像素，命中概率乘以 step²，这样每平方 DIP 的密度不变。这只是为了减少 CPU 扫描量（4K、200% 缩放时扫描量降到四分之一）。
- GPU 上每个物理像素一个线程，不需要这个抽样：每个墨迹像素的命中概率 = `0.5 × (1 / dpiScale²) × 变稀系数`。
  - 200% 缩放时，每个像素约 0.125；4 个像素合计的期望是 0.5 粒，与原来一个格子以 0.5 的概率出 1 粒相同。
  - 100% 缩放时，每个像素就是 0.5，与原算法完全一致。
- 每粒颜色和出生位置取自它自己的像素（像素内随机抖动），比原来取格子左上角那个像素更准，细线和抗锯齿边缘不会因为代表像素落空而漏掉。
- 密度 ≤ 0.5 粒/平方 DIP，每个像素至多 1 粒，不需要一个像素出多粒。

### 5.2 两遍派发

- 新着色器 `laserParticleSpawnCS.hlsl`（`cs_5_0`）：
  - **计数遍**：每个线程处理一个像素，透明度 ≥ 48 的像素做 `InterlockedAdd(counters.inked, 1)`。
  - **生成遍**：每个线程处理一个像素，读出 `inked`，算出 `wanted = min(maximumSeeds, inked × 0.5 / dpiScale²)` 和命中阈值 `min(1, wanted / inked)`（与原算法相同）。用像素哈希决定是否命中；命中则 `InterlockedAdd(counters.written, 1)` 申请槽位，序号到达本批预留数（≤ `maximumSeeds`，见 5.3）就丢弃。写入 `particles[(batchStart + 序号) & capacityMask]`。
- `counters`：16 字节的 `RWByteAddressBuffer`，存放 inked 和 written，计数遍之前由 CPU 清零。前后两次 `Dispatch` 对同一个 UAV 的写读由 D3D11 自动排序。
- 只派发墨迹范围（第 5.4 节）内的像素，不必扫整屏。
- 出生参数：把 `AppendDriftParticleSeed` 与 `PackLaserParticle` 飘散分支合并成 `MakeDriftParticle`（CPU），HLSL 实现同一份公式，放在 `laserParticleCommon.hlsli` 里与 VS 共用（风场、哈希、碎裂遮罩的像素哈希）。随机数仍用 `Hash((x ^ y * 0x9E3779B9) + seedBase)`，与原来一致；100% 缩放时两条路径应逐粒相同（三角函数相关字段允许误差）。
- 释放时刻 = 出生像素在碎裂遮罩里消失的时刻（原算法）；`kindAndBatch = Dissolve`，批次号为 0。

### 5.3 CPU 侧交接（`DrawingController` 清空分支）

1. 效果开启且飘散池可用时：
   - 先从页面历史拿到墨迹屏幕范围（第 5.4 节）；范围为空就不出粒子。
   - 然后 `CaptureCanvasClearSnapshot()`，再调用 `renderer_.SpawnCanvasClearParticles(范围, front, dpiScale, wind, seedBase, limit)`：
     - 在飘散池预留 `min(maximumSeeds, ceil(范围面积 / dpiScale² × 0.5 × 1.05) + 64)` 个槽作为一批。范围面积不小于墨迹面积，再留一点余量吸收抽样的随机波动。到期时刻 = 现在 + 最长释放延迟 + 最长寿命。
     - 把预留区间清成空槽（分块清零，或加一个清零入口，实现时选便宜的）。
     - 两次派发，然后把 `counters` 复制到 16 字节的 staging 缓冲区。
2. 批次脏区 = 墨迹范围扩大最坏行程（常量上限）。`canvasClearCrumbleBounds` = 墨迹范围。都立即可用，不需要等 GPU。
3. 之后每帧用 `Map(DO_NOT_WAIT)` 轮询 staging：拿到 `written` 后，若这一批仍是环里最后一批，就 `TrimLatest(written)`；`written == 0` 就撤销这一批。这只是省掉空槽的顶点着色器开销，拿不到也不影响正确性。
4. “画布仍可见”的发布时机不变：范围非空、快照成功就按“有粒子”发布。
5. **退回 CPU 路径**：生成着色器、UAV、counters 任一不可用，或快照失败时，走现有 CPU 路径（读回 + 原 2×2 抽样 + 直写），范围仍用历史查询的结果。

### 5.4 墨迹范围：从页面历史查询（为 C2 做准备）

- 现有数据：`CanvasRuntimeHistory` 里每个绘制项都有 `RenderItemState::pixelBounds`（页面像素坐标，半开区间），以及 `visible` 标志。原来的整屏读回就是在找这个范围。
- 新增查询 `VisibleContentScreenBounds(history, viewportX, viewportY, width, height) -> RECT`：
  - 把所有可见项的 `pixelBounds` 合并，减去视口偏移，裁到画布。
  - 四周放宽 2 像素，覆盖抗锯齿边缘。
  - 本任务用线性遍历实现。
- 与 C2 的关系：C2 要做“页面级空间索引（某块区域里有哪些笔画）”。这个查询的接口按“给定区域 → 可见内容范围 / 项集合”来设计，C2 只需替换内部实现。本任务不建索引结构。
- 正确性要求：范围必须覆盖 L2 上所有不透明像素。自检用“读回得到的范围 ⊆ 历史查询得到的范围”验证，用例包括白板平移后的情况和图形。

### 5.5 失败处理

| 情况 | 处理 |
|---|---|
| 生成着色器或 counters 创建失败 | 记一次日志，本次设备会话的清空都走 CPU 路径 |
| 设备移除 | 沿用 `GetDeviceRemovedReason` → `Release` |
| 异步结果一直没到 | 批次保持预留长度，到期照常结束 |
| 预留时飘散池里粒子太多 | 按先进先出淘汰最旧批次（现有行为） |

## 6. 激光飘散：CPU 直写

- `AppendLaserDissolveSeeds` 改为调用 `MakeDriftParticle` 直接写 `LaserGpuParticle`，同时累加包围盒和最长寿命。风向的 cos/sin、`dpiScale` 的倒数等常量在循环外算好。
- `Step` 新增一个直接接收已打包记录的入口；行进粒子仍走种子入口。
- 生成过程用 `try/catch (const std::bad_alloc&)` 兜底。

## 7. 着色器与工程

- `laserParticleCommon.hlsli`：删掉 `LASER_PARTICLE_CAPACITY`，加入公共公式。`LaserParticleDrawConstants` 用原 `padding[0]` 的位置放 `capacityMask`（偏移 52，仍 64 字节），VS 同步修改。
- 新增 `laserParticleSpawnCS.hlsl`。工程接入按现有着色器的做法：vcxproj 的 None 项、UTF-8 转 ASCII 读写行及其 Inputs/Outputs、FxCompile（全部配置，Compute，`cs_5_0`），rc 资源和资源 ID。`.cso` 不提交。

## 8. 设置页

用 `InlineCombos`（或已有的等价组件）让“飘散方向”和“粒子数量上限”两个下拉在同一块内纵向排列，之后是分割线和三个子项。文案键 `SettingsUI.Draw.Laser.Particles.Limit`、`LimitLow/Medium/High/Extreme`；“中”加推荐后缀；所有语言补全。

## 8.5 开工核对后的修订（2026-11-24，优先于前文）

核对报告见任务外的 `%TEMP%\inkeys-codex\phase-c13\kickoff-report.md`。主 Agent 已逐条读码确认。

1. **槽与批次分别取掩码**：环的槽位用池容量掩码，批次表用批次上限取模；`latestDissolve_` 的哨兵取批次上限。行进池的发射预算 `maximumSpawnPerFrame` 改为对照行进池容量校验。
2. **拆池后的分流**：现有 `Step` 假定种子末尾的 N 条属于激光飘散，普通批里可能混有行进与清空。拆池后：
   - 行进种子进行进池；清空种子作为普通批进飘散池；激光飘散作为飘散批进飘散池。
   - 汇聚以及 `KillSlots(LatestDissolveRange)` 只作用于飘散池里的激光飘散批。
   - 结束时刻、`Prepare/WarmUp`、空闲归还按池分别处理；着色器与时钟共享；`Reset` 同时清两个池。
   - 两个池各自独立降级：行进池创建失败不影响飘散池，反之亦然。
   - `DrawingController` 的“画布仍有粒子”判断取两个池的并集；脏区跟踪按现有方式登记，只把批次登记到正确的池。
3. **GPU 批次的簿记**：
   - 清空批的分组为 `kCanvasClearParticleGroup`，批次号为 0，登记为普通批（不可汇聚）。
   - 预留区间的 `slotGroups_` 用 `std::fill` 整段写成清空分组（不逐粒处理）。
   - 异步回读的结果要绑定“设备代次 + 批次身份”：结果到达时，如果设备已重建、批次已被淘汰或已不在环尾，就丢弃该结果，不缩短任何批次；下一次清空开始前，未读完的旧结果直接作废（staging 缓冲区按批次复用前先放弃旧结果）。
   - 缩短时取 `min(written, reserved)`；`written == 0` 时同步撤销这一批，并更新结束时刻与可见性。
   - 绑定顺序：派发前解除粒子缓冲区在 VS 上的 SRV 绑定，以及快照纹理在其他阶段的绑定；派发后解除 CS 的 UAV/SRV；然后再复制 counters。下一次绘制照常绑定 VS 的 SRV。
4. **墨迹范围的口径**：
   - 历史项的 `pixelBounds` 已包含线宽与抗锯齿余量；参数化图形要使用它自己的范围（核对时确认的位置：`InkHistory.cpp:602–715`）。
   - 视口偏移取清空前的 Canvas viewport（白板平移、PPT 恢复的存档视口、桌面平移都要覆盖）；页面坐标减去视口偏移后向外取整，再裁到画布。
   - **退路**：如果当前页处于“历史与 L2 可能不同步”的状态（还在等待重放或恢复、栅格代次不一致等，具体用现有状态标志判断），范围改用整张画布，不使用历史查询的结果。
5. **清空事务的顺序**：
   1. 先确定范围，并捕获快照；
   2. `clearCurrentPage` 成功之后，才提交 GPU 生成请求（预留槽位、淘汰旧批）；
   3. 清空失败时释放快照，不改动粒子池。
   - GPU 生成请求与 CPU 种子走同一条通道：在原来上传 `laserParticleSeeds` 的那次 `Step` 里执行。这样粒子出生时刻与碎裂起始帧保持一致（清空命令可能在当帧 `Step` 之后才被处理）。
   - 重做清空不播放，沿用 `command.type == Clear` 的现有判断。
6. **档位的读取**：粒子选项的锁存只在激光为 Inactive 时刷新。数量上限不走这个锁存，每帧直接从 `GetLaserParticleOptions()` 读取，变化时立即调用 `SetLimit`（满足 R4“立即生效”）。
7. **分步重排**（implement.md 同步修改）：
   - W1：在单池上迁移到运行时容量（档位表只用低档），同时修改 CPU/VS 合同与全部自检，行为不变。
   - W2：拆池、档位、降档。
   - W3：直写记录。
   - W4：GPU 生成与墨迹范围。
   - W5：配置、设置页、文案。
8. **验收口径的修订**：
   - “低档与改动前一致”指单批生成的粒子（数量与每粒参数）一致。拆池以后，溢出淘汰不再与行进粒子竞争，满池时的淘汰结果允许与改动前不同。
   - GPU 与 CPU 逐粒对照只在未触顶、100% 缩放时进行，按 (位置, 释放时刻) 匹配（GPU 记录里没有随机数字段）。触顶时只比较数量等于预留上限；200% 缩放下做统计对照。
   - 高档触顶的自检需要更长、更多的笔迹样本。

## 9. 风险

| 风险 | 处理 |
|---|---|
| GPU 公式与 CPU 有偏差 | 共用公式头；100% 缩放下逐粒对照，200% 缩放下统计对照 |
| 历史查询的范围漏掉某些内容（例如图片或特殊图形） | 自检要求“读回范围 ⊆ 历史范围”；发现漏掉时，那类内容按整张画布处理 |
| 兆芯、WARP 的计算着色器或 UAV 行为异常 | 退回 CPU 路径；实机验收留到最终集成 |
| 极致档每帧要求值约 52 万个实例 | 实测；行进粒子拆池后不再额外遍历；默认是中档 |
