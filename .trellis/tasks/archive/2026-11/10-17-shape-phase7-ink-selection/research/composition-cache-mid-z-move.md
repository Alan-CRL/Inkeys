# Research: 合成缓存现状与未来"Z 序中间选区移动"的架构演进

- **Query**: 合成缓存怎么工作；支持圈选/框选后高帧率移动 Z 序中间的笔迹/图形还缺什么；候选设计与推荐；现在（L1 pending shape 工作）应预留什么；风险与产品问题
- **Scope**: internal（只读代码 + spec），未构建
- **Date**: 2026-11-05
- **代码基线**: feature/shape 工作区（另一 agent 正在并发修改 Draw3，行号可能漂移）

---

## 1. 合成缓存今天的真实形态

### 1.1 每个绘制项都是一个逐像素仿射算子（这是全部设计的基础）

- 每个 item 在每个像素上是 `dst' = add + retain * dst`：`add` 为预乘 BGRA，`retain` 为标量。
  - 普通笔/高光笔/图形：`add = rgb*a, a`，`retain = 1 - a`（`Assets/inkPixelShader.hlsl:626-633`，图形 `:552-559`）。
  - 橡皮（`globalOperatorKind == 1`）：`add = 0`，`retain = 1 - alpha`（`inkPixelShader.hlsl:621-625`, `:547-551`）。
  - 同一笔内部（L1/L0 两段、多材质）先做覆盖并集 MAX/MIN（type 1，`inkPixelShader.hlsl:509-514`；`spec shapes.md:18`），对外仍是**一个**算子。
- 算子复合：`Later∘Earlier = (Al + Rl*Ae, Rl*Re)`（type 14，`inkPixelShader.hlsl:443-458`；有序 L1 over L0 同式 type 2，`:515-520`）。满足结合律，可以组成线段树。
- 落到 L2 用双源混合：`SrcBlend=ONE, DestBlend=SRC1_COLOR`，即 `L2 = add + retain*L2`（`Draw3.Renderer.cpp` 约 `:641-649` 的 `operatorResolveBlendState`；apply 见 `Draw3.InkHistoryGpu.cpp:674-715`）。
- 结论：**"上层"不是一张图片，而是一个算子**。普通墨迹叠加是 `retain == 1 - add.a` 的特例；橡皮和高光笔让 `retain` 与 `add.a` 脱钩，所以必须单独存 retain 通道。现有缓存格式正好就是这样存的。

### 1.2 粒度与拓扑（CPU：`CompositionRangeTree`）

| 常量 | 值 | 位置 |
|---|---|---|
| `kCompositionTileSize` | 256 px（Canvas 空间，viewport 平移不进 key） | `Draw3.InkHistory.cppm:22` |
| `kCompositionTileBytes` | 256×256×6 = 384 KiB（BGRA8 add + R16F retain） | `:23-24`；格式见 `Draw3.InkHistoryGpu.cpp:506-509` |
| `kCompositionTilesPerPage` | 64 个 slice 组成一个 Texture2DArray（约 24 MiB） | `:25`；`InkHistoryGpu.cpp:530-546` |
| `kCompositionLeafItemCount` | 32 项一个叶块 | `:26` |
| `CompositionCachePolicy.byteBudget` | 默认 192 MiB，即 **512 个 slot** | `:34-37` |
| `kUndoTileSize`（热前像，与合成缓存无关） | 128 px，BGRA8 | `:18-21` |

- 树的形态：按 **item 下标**（= Z 序，`RenderItemId.index == items_.size()` 只追加，`InkHistory.cpp:691`）切成 32 项叶块。节点 `CompositionNodeId{firstBlock, blockCount}` 是 2 的幂对齐区间，根容量是 ≥ 块数的最小 2 的幂（`InkHistory.cpp:58-67`, `:796-831`）。也就是一棵**隐式线段树，按 (node, tile) 稀疏存储**。
- 每个 (node, tile) 的 generation 存在 `tileGenerations_`（`InkHistory.cppm:172`）。`InvalidateTiles` 对 item 所在叶块到根的每一级、每个受影响 tile 都打一个新 generation（`InkHistory.cpp:662-687`）。触发失效的有：`AppendRenderItem`、`SetItemVisibility`（Undo/Redo）、`UpdateItemGeometry`（旧、新 tile 都失效，`:728-774`）和 `SetItemBarrier`。
- `tileBlocks_`：tile 到"触及该 tile 的叶块"的有序表，供 `NodeTouchesTile` 判空用（`:841-852`）。不触及的节点直接视为单位算子，不占 slot（`InkHistoryGpu.cpp:941-942`）。
- **区间分解** `DecomposeRange(begin, end, tile)`（`InkHistory.cpp:854-928`）：
  - 完全覆盖且不含屏障的节点 → `CachedNode`。
  - 叶块被部分覆盖时，逐项输出 `OrderedItems`，相邻项合并。
  - 覆盖该 tile 的可见屏障项 → `BarrierItem`。
  - 所以一个任意区间 `[rangeBegin, rangeEnd)` 在每个 tile 上 = O(log B) 个缓存节点，加上首尾两个残缺叶块里**最多约 2×31 项逐项光栅化**。
- 屏障 `compositionBarrier = !affineOperator`（`InkHistory.cpp:942`）。运行时所有 `AppendStroke` 调用都传 `true` 或默认值；只有白板 JSON 恢复时会按持久化的 `item[2]` 设置（`Draw3.DrawingController.cpp:911`）。这是给未来非仿射混合（例如逐通道 multiply 高光）预留的，**当前没有生产者**。

### 1.3 GPU 侧（`InkHistoryGpuCache::Impl`）

- Key：`FullCompositionKey{canvas(pageGuid, device), rasterKey(device, scale, format, pipelineGeneration), tile, node, tileGeneration}`（`InkHistoryGpu.cpp:219-234`）。查找方式是对 `residentNodes` 线性扫描（`:844-849`）。
- 构建：`GetOrBuildNode` 递归。命中就 pin 后返回；否则先建两个子节点（子节点在父节点写完前保持 pin），再 Copy 或 Compose。叶节点用 `BuildNodeByOrderedItems` 逐项 `RasterItem` 进 tile 局部算子，再用 3 个 scratch slice 乒乓复合（`InkHistoryGpu.cpp:820-842`, `:936-1052`）。
- 淘汰：`CompositionCachePlanner` 是带 pin 计数的 LRU（`InkHistory.cpp:1304-1444`）。降预算时 `RepackCompositionStorageToPolicy` 重排 slice（`InkHistoryGpu.cpp:866-927`）。
- 恢复：`RestoreComposition(request)`（`InkHistoryGpu.cpp:1374-1549`）。
  - 逐 tile：可选先 `ClearL2Tile`，再按 pieces 把节点 `ApplyOperator` 到 L2，或逐项 `ReplayRange`。
  - 失败时整 tile 回退到 `ReplayRangeDirect`（全屏 L1 逐笔 apply）。
  - 参数：`rangeBegin/rangeEnd`、`excludedItem`（只能排除一个）、`clearTargetTiles=false` 叠加模式（`InkHistoryGpu.cppm:118-135`）。
- 维护：`compositionMaintenance` 是上限 4096 的 deque（`DrawingController.cpp:11958-11959`）。
  - 每笔完成后，把该笔所在**叶块**触及的全部 tile 入队（`:18690-18716`）。
  - 空闲帧逐个 `PrimeCompositionNode` 预建（`:17818-17844`），每个 tile 之间先检查输入。
  - 视口恢复时，不可见 tile 预建 root（`:17556-17562`）。
- 整体丢弃 `DiscardCompositionCache()` 的场景：Clear、尺寸变化或 pipeline generation 回绕、设备重建、策略修改失败，以及**每次图形编辑预览/提交、Undo/Redo Replace、构造提交**（`ShapeRuntimeActions.inl:107`, `:120`, `:315`, `:321`；`DrawingController.cpp:15581`, `:15591`）。

### 1.4 已有的"中间项替换"原型：`PreviewReplacement`

位置：`InkHistoryGpu.cpp:1597-1752`，调用方 `ShapeRuntimeActions.inl:150-202`。PRD R2 要求撤回它。它其实就是单个 item 版本的"上下夹心"：

1. 首次触及的 tile：用 `RestoreComposition` 恢复 `[0, item)`，然后把这块 L2 拷进一张**全屏尺寸**的冻结纹理 `below`（`:1686-1697`，纹理在 `:1632-1648` 创建）。
2. 之后每帧把 below 拷回 L2（`:1699-1704`）。
3. 把候选画进全屏 L1，用有序 apply 叠到 L2（`:1706-1724`）。
4. **每帧**用 `rangeBegin = item+1`、`clearTargetTiles = false` 的叠加模式，从缓存补回上层项（`:1726-1742`）。上层项没有冻结，每帧都要走一次 DecomposeRange（缓存节点 + 残缺叶块逐项光栅化）。

会话 key 包含 `historyRevision` 和 viewport，任一变化就重新冻结（`:1615-1629`）。

---

## 2. 支持"中间选区高帧率移动"还缺什么

记选中集合为 `S = {s1 < … < sk}`，平移 `T`。选中项有 g 个连续段（run）`R1…Rg`，相邻段之间的非选中区间为 `G1…G(g-1)`。

精确合成式（每像素 p）：

```
L2(p) = U(p) ∘ R_g(p - T) ∘ G_{g-1}(p) ∘ … ∘ G_1(p) ∘ R_1(p - T) ∘ B(p)
  B = 合成[0, s1) 作用于透明  —— 可存为 BGRA 图像（下层永远从 0 开始）
  U = 合成(sk, N)              —— 必须存为算子（add+retain），不能存图像
  G_j = 合成(段 j 末尾, 段 j+1 开头) 之间的非选中项 —— 不动的算子
  R_j = 选中段 j 的复合算子      —— 随 T 平移
```

缺口如下：

1. **没有"区间 → 算子纹理"的出口 API**。`RestoreComposition` 只会把结果写进 L2，不能把 `U` 或 `G_j` 冻结成一张 per-drag 算子 tile。`PreviewReplacement` 因此每帧都要重放上层，成本 = O(log B) 次 apply + 残缺叶块最多约 31 项光栅化/tile/帧。
2. **只能排除一个 item**（`excludedItem` 是 `optional<RenderItemId>`，`InkHistoryGpu.cppm:132`）。没有"排除集合 / 按段切分"。
3. **交错段的处理**：`G_j` 不动、`R_j` 在动，所以不能把选中项预先复合成一张图。每帧每 tile 需要 2g+1 次 apply。可以按 tile 做优化：若 `G_j` 在目标 tile 上恒等（`NodeTouchesTile` 为假），相邻两段可以合并。但这个判断依赖 T，要每帧做。
4. **缓存容量撑不住全屏任意区间**。512 slot 对 4K 屏的 135–160 个 tile，每 tile 一个任意区间约要 2·log2(B) 个节点（2000 项时 B=63，约 12 个）。160×12 远大于 512，会 LRU 抖动。所以 per-drag 结果必须冻结成独立资源，树只用于拖动开始时构建。
5. **key 唯一性**。`generationClock_` 是每个 `CanvasRuntimeHistory` 实例内的计数器（`InkHistory.cppm:174`）。编辑和 Undo 路径会整份复制 runtime 作为候选（`ShapeRuntimeActions.inl:86`；`DrawingController.cpp:658`, `:15569`），候选和正本的时钟会分叉。相同的 (node, tile, generation) 可能对应不同内容，所以现在每次编辑都整体 `DiscardCompositionCache`。多项移动提交后如果也整体丢弃，4K 下就是冷重建。
6. **journal 是单项操作**：`CanvasOperation{kind, item, before, after}`（`InkHistory.cppm:179-185`）。一次移动 k 项需要一个复合 Replace 撤销步。
7. **橡皮语义未定义**（见 §4）。按当前文档模型，橡皮是 Z 序里的全局算子，公式中的 `U` 会擦掉移动到新位置的选中内容，被它擦过的旧部分则重新露出来。
8. **高光笔**：仍是仿射算子（`retain = 1 - a`），可以安全冻结。若将来改成逐通道 multiply，单个标量 retain 就表示不了，只能走 `BarrierItem`：`U` 或 `G` 中含屏障的 tile 不能冻结成算子，只能每帧逐项重放。
9. **亚像素 / 旋转缩放**：对预光栅化的算子做双线性采样，结果仍是合法算子（凸组合），但会软化抗锯齿。逐帧重画矢量则精确，但成本和选区复杂度成正比。

### 2.1 4K 内存估算（3840×2160；256 tile：对齐时 15×9=135 个，非对齐最坏 16×10=160 个）

| 资源 | 按 tile 计（160 块） | 按全屏纹理计 |
|---|---|---|
| B（BGRA8 图像） | 40 MiB | 31.6 MiB（与现 `below` 相同） |
| U（BGRA8 + R16F 算子） | 60 MiB | 47.5 MiB |
| 每个 G_j | 60 MiB | 47.5 MiB |
| 选区算子（按选区包围盒，最坏全屏） | ≤ 60 MiB / 段 | ≤ 47.5 MiB |
| 现有合成缓存预算 | 192 MiB（512 slot） | — |

只冻结"原 footprint ∪ 当前 footprint"扫过的 tile（懒冻结，做法同现 `frozen` 列表）。典型拖动远小于全屏；最坏情况无交错时约 80–110 MiB 额外显存。

---

## 3. 候选设计

### A. 每次拖动做"下 / 选区 / 上"三层夹心（逐 tile 懒冻结）

- 首次触及 tile t 时：B_t = 合成[0, s1) 冻结为图像；U_t = 合成(sk, N) 冻结为算子；交错时 G_j,t 冻结为算子。这些都用现有树构建。
- 每帧：copy B_t → apply R(T)（逐段）→ apply G → apply U_t。
- 优点：每帧成本只和 g、tile 数有关，与文档项数无关。橡皮/高光精确（U 是算子）。是 `PreviewReplacement` 的直接推广。
- 缺点：首帧冻结有尖峰（冷缓存时残缺叶块要逐项光栅化）；显存见 §2.1；history 或 viewport 一变就作废（与"焦点"规则一致：平移/缩放会先结束会话）。

### B. 选区每帧画进 L1 类算子层，上层从缓存每帧重放

- 本质就是现在的 `PreviewReplacement` 加多项支持。
- 优点：无额外冻结 U/G 的显存；选区是矢量重画，旋转/缩放/亚像素都精确。
- 缺点：每帧都要 DecomposeRange 上层（O(log B) apply + 残缺叶块最多约 31 项光栅化/tile），每段 `G_j` 也一样。遇到 LRU 抖动时（§2 第 4 点）会退化成逐项重放；高项数页面上帧率不稳。

### C. 完整合成树 / 增量线段树（拖动中每帧 UpdateItemGeometry）

- 每帧把选中项的几何写进树，旧、新 tile 的祖先链全部失效。每 tile 需要重建叶块（≤32 项光栅化）+ log B 次 compose + apply root。
- 优点：不需要新的冻结概念，结束时缓存已经是新状态。
- 缺点：每帧成本最高（例如 30 tile × 32 项）；每帧都会污染或挤出长期缓存；还依赖 §2 第 5 点的 key 唯一性。只适合用在**提交时**。

### 推荐：A，选区层自适应，树只用于冻结与提交

1. 拖动开始（或首次触及 tile）时，用树构建 B_t / U_t / G_j,t 并冻结进独立的 per-drag 资源池，不占长期 LRU slot（或者 pin 住）。
2. 选区层：
   - 默认每帧把选中段矢量重画进 L1 类算子层（与 R2 的 L1 绘制路径同构，旋转/缩放/亚像素精确）。
   - 选区笔画很多、且只是平移时，降级为"拖动开始时光栅化一次选区算子 + 每帧按 T 平移采样"。
3. 每帧写进 L2 的受影响 tile（同 `PreviewReplacement` 方式，L2 在拖动期间标记为非权威，可信快照作废），不改 present 管线。
4. 松手提交：多项 `UpdateItemGeometry` + 一个复合 Replace 撤销步。按树做局部失效，只对受影响 tile 做权威 `RestoreComposition`，不整体丢弃缓存（前提是 §2 第 5 点已修）。
5. 屏障项或资源失败时，回退到逐项 `ReplayRangeDirect`（现有兜底）。

### 现在（R2 L1 pending shape 期间）应做的低成本预留

1. **"L2 不含哪些项"的状态写成集合 + 不变量**，不要硬编码成单个 shape。例如：
   ```
   L2Exclusion { pageGuid, std::vector<RenderItemId> items, Mode::SuffixOverlay }
   ```
   断言 `items` 是可见 Z 序的后缀（今天恒为最上层那一个）。只有后缀才能用 "L2 + L1 叠加上屏"；中间项必须改走 A 的夹心模式，不能复用 overlay 上屏。
2. **"焦点"抽象成通用的画布焦点会话**，不要只做 ShapeEditor 专用。例如：
   ```
   EndCanvasFocus(reason)   // 所有落定触发统一调用（PRD R2 那张清单）
   ```
   未来"选区会话"直接成为另一种焦点持有者，复用同一张触发清单（新 Down、平移缩放、翻页、切工具/页/工作区、Clear/Esc、读 L2 的路径、设备/尺寸变化）。
3. **L1 一直作为算子层（add + retain）**，用 `ApplyOperatorLayers(Ordered)` 落定，不要退化成普通图像 over。这样同一段绘制代码未来能直接插进"R 段"。另外 L1 的绘制入口要同时接受 `ShapeObject` 和 `InkStroke`（`PreviewReplacement` 的 `:1713-1721` 已经这样做）。
4. **撤回 `PreviewReplacement` 时保留它的底层能力**：`CompositionRestoreRequest` 的 `rangeBegin` + `clearTargetTiles=false` 叠加模式、`excludedItem`、以及 `MapTileToScreen` / `ApplyOperator` 这些 helper。在 spec 或本文件里记下被删实现所在的 commit，供 A 方案参考。
5. **修 key 唯一性**（独立且便宜）：
   - generation 改用进程级单调时钟，或在 `FullCompositionKey` 里加 history epoch / 实例 id。
   - 之后编辑提交、Undo Replace 就可以停掉整体 `DiscardCompositionCache`，只做局部失效。
   - 这也是 R2 "不清空合成缓存" 验收的一部分。
6. **不变量写进 spec**：Z 序 == `RenderItemId.index`，只追加。"调整层序"不在本模型内，未来要做需另行设计（树按下标切块）。新代码不要假设一个撤销步只对应一个 item。

---

## 4. 风险与开放的产品问题

1. **橡皮在移动后的语义（最关键）**。橡皮是全局 Z 序算子，不属于某一笔。若严格保持"Z 序不变、橡皮不动"：
   - 被擦掉的部分会跟着选区移走后重新露出来；
   - 原本没被擦的地方，移到橡皮轨迹下会出现新洞；
   - 橡皮本身仍在擦它下面的其他内容。

   这在数学上精确（设计 A 天然如此），但大多数用户会期望"擦痕跟着笔走"。可选模型：
   - (a) 保持全局算子语义：最简单，但所见即所得反直觉；
   - (b) 移动时给选中项生成"附着裁剪"：每个 item 记录影响它的橡皮引用或遮罩。需要改文档格式和 UInk 降级；
   - (c) 选区自动吸收相交橡皮：错误，会让其他未选项的擦痕也跟着动；
   - (d) 提交时把"选中项 + 作用其上的橡皮"烘焙成新几何或位图：有损。

   需要用户决定。另外还有：橡皮之下几乎被完全擦掉的笔画还能不能被圈选命中（命中测试是否要考虑覆盖度）？
2. **首帧尖峰**：冷缓存下冻结 B/U 要逐项光栅化残缺叶块。可以让维护队列（`compositionMaintenance`）预建选区边界所在叶块的 tile，或者对可见 tile 分帧冻结。
3. **精度**：add 是 BGRA8，复合会多一次量化，预览像素与权威重放可能不逐位一致（现有缓存路径已经接受这一点）。验收"落定逐像素一致"时必须以权威重放为准。
4. **显存**：4K 最坏情况额外约 80–110 MiB（无交错），交错段每段再加约 48–60 MiB。需要上限：段数超过阈值时退化为 B 方案或逐项重放。
5. **屏障 / 非仿射混合**：若将来引入 multiply 等逐通道混合，U/G 不能冻结为算子，帧率会退化。
6. **会话失效**：Undo/Redo、自动保存的冷卸载、设备丢失、尺寸变化、PPT 翻页都会改变 history revision 或资源，必须先结束选区焦点（与 R2 同一套规则）。
7. **调整层序**（置顶/置底）是独立需求，与按下标切块的树冲突；本研究假设 Z 序不变。

## Caveats / Not Found

- 没有运行或构建，内存和帧耗是按常量推算的估计，没有实测。
- `ApplyOperatorLayers` 的混合状态行号（`Draw3.Renderer.cpp` 约 641-649）是在并发修改中读到的，可能已漂移。
- 没有找到任何现有的选区、圈选或多项移动代码，也没有"附着橡皮"的文档字段。
