# Research: ink-stroke-modeler 上游状态、源码差异与可复用机制

- Query: 高速大曲率转弯立即抬笔时，平滑轨迹连接真实 Up 点产生末端弯折；Google ink-stroke-modeler 是否发布了直接修复，本地是否缺少相关补丁，已有参数机制是否值得验证。
- Scope: mixed；公开上游只读核查、本地产品头文件及原始源码仓库当前工作树只读比较。本报告不实施、不构建、不运行 GUI。
- Date: 2026-10-12（Asia/Shanghai）

## Findings

### 1. 可直接采用的结论

1. **没有找到可认定为本问题直接修复的新上游版本。** 2026-10-12 查询到的 `main` 为 `f2388813b0b25bc3e33d143d369a8367ab2e30c8`，提交时间 `2026-08-02T15:57:29Z`，内容是 Bazel lockfile。公开 releases 与 tags API 均返回空数组。[固定 HEAD](https://github.com/google/ink-stroke-modeler/commit/f2388813b0b25bc3e33d143d369a8367ab2e30c8)、[releases](https://github.com/google/ink-stroke-modeler/releases)、[tags](https://github.com/google/ink-stroke-modeler/tags)
2. **原始源码仓库当前工作树的 7 个关键源文件与该 HEAD 全文一致，仅统一 CRLF/LF 后比较。** 包括 `stroke_modeler.cc`、`params.h`、`internal/position_modeler.h/.cc`、`internal/stylus_state_modeler.cc`、`internal/loop_contraction_mitigation_modeler.cc`、`internal/wobble_smoother.cc`。所以“升级这 7 个源文件以取得新修复”目前没有差异可应用。
3. 产品已带有 loop contraction mitigation 与角度自适应细分的接口和当前实现；两项在产品当前配置都未启用。启用已有机制是候选试验，不等于“上游已经修复、只需升级”。
4. `ModelEndOfStroke` 仍保持局部过冲检查，`kUp` 仍先执行普通线性输入插值，再执行结束收敛。没有新增按真实末端切线修正运动阶段外偏的策略。
5. **源码比较不能证明 `merge.lib` 的内容。** 本地 README 的 `8d04529` 是原始 Inkeys3-Draw3 仓库 revision，不是 Google 上游 SHA。当前工作树、该历史 revision、固定静态库之间尚未完成可复现构建 provenance 闭环。

### 2. 文件与版本依据

为避免长路径重复，本报告定义 `O = D:/Project/Inkeys/inkStrokeModelerTest/Inkeys3-Draw3/inkStrokeModelerTest/additional/ink_stroke_modeler`。以下 `O/...:行号` 都引用该**当前工作树**实际文件。

| 文件 | 说明 |
|---|---|
| `NOTICE:27` | 声明 Google ink-stroke-modeler 及 Apache 2.0 上游来源。 |
| `Inkeys/additional/ink_stroke_modeler/README.md:5` | 原始源码仓库位置；`:6` 写项目来源 revision `8d04529`；`:8` 说明固定架构静态库。 |
| `Inkeys/additional/ink_stroke_modeler/params.h:47` | spring/drag、loop mitigation、采样和防抖参数。 |
| `Inkeys/additional/ink_stroke_modeler/internal/position_modeler.h:96` | 结束笔画模板实现随产品头文件提供。 |
| `O/stroke_modeler.cc:55` | 对 raw-polyline 投影结果与 spring 结果做融合，并产生速度反馈。 |
| `O/stroke_modeler.cc:233` | `ProcessUpEvent`；普通插值与终态收敛的顺序。 |
| `O/stroke_modeler.cc:282` | `ProcessMoveEvent`；先 wobble smoothing 再建模与投影。 |
| `O/internal/position_modeler.cc:18` | 弹簧加速度与阻力；`:26` 为采样步数估算；`:64` 为积分。 |
| `O/internal/stylus_state_modeler.cc:86` | normal 投影、左右候选与不回退限制。 |
| `O/internal/loop_contraction_mitigation_modeler.cc:19` | speed 平均值到融合权重的映射。 |
| `O/internal/wobble_smoother.cc:37` | 按速度混合时间加权平均位置与输入位置。 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp:230` | 产品参数入口；`:236` spring/drag，`:244` 开启 normal projection，未配置 mitigation 或角度细分。 |

比较结果：

| 文件组 | 比较方法 | 结果 |
|---|---|---|
| 上述 `O` 中 7 个核心文件 | `gh api contents?...ref=<固定 SHA>` 获取原始 UTF-8；内存中全文比较，统一 CRLF/LF | 全部相同。未修改本地源码。 |
| 产品 `ink_stroke_modeler` 下全部 18 个 `.h` | 本地去 BOM、统一 LF 后计算文件内容 blob SHA-1，与该 SHA 的 tree API 比较 | 17 个全文相同；唯一差异 `numbers.h`。 |
| 产品 `numbers.h:20` | 人工核查唯一头文件差异 | 本地有 `__cplusplus` 条件与 MSVC/旧标准 fallback 常量；上游直接用 `std::numbers::pi`。不是末端轨迹修复。 |

原始源码 `stroke_modeler.cc` 347 行、`params.h` 324 行、`position_modeler.h` 149 行、`position_modeler.cc` 74 行、`stylus_state_modeler.cc` 246 行、`loop_contraction_mitigation_modeler.cc` 58 行、`wobble_smoother.cc` 89 行，与固定 HEAD 行数及内容一致。没有把网页工具抽取后的行号当源文件行号。

### 3. 已合并的相关修复与本问题边界

下表日期为上游 commit 的 UTC 日期；使用提交链接作为版本锚点，PR 状态已单独核查。

| 日期 / 提交 | 实际变化 | 与本问题关系 |
|---|---|---|
| 2024-09-10 / [fa329844](https://github.com/google/ink-stroke-modeler/commit/fa329844bb2a19a2f1c2f0a607daf84b8a87b738)（[PR #138](https://github.com/google/ink-stroke-modeler/pull/138)） | 增加 loop contraction mitigation；默认关闭。 | 从运动阶段把建模轨迹拉向输入折线，属于相关能力；不能据标题推定 Up 弯折已消除。 |
| 2024-09-20 / [11d3c656](https://github.com/google/ink-stroke-modeler/commit/11d3c65612d9d53e7792ebe7d07aa87a50891b45) | 修正 stroke normal 计算。 | 支撑 projection；当前本地接口/源码已具备。 |
| 2024-09-23 / [11abc9c1](https://github.com/google/ink-stroke-modeler/commit/11abc9c18dcc0d6074106fe14b3bd5eddd28559b)（[PR #141](https://github.com/google/ink-stroke-modeler/pull/141)） | 修正急转弯时投影选择错误；当左右都有交点时根据加速度选择转弯外侧候选。 | 修的是投影对应关系；不是把 spring 轨迹外偏整体消除。随后采样窗口机制又演进。 |
| 2025-04-03 / [03db1ed3](https://github.com/google/ink-stroke-modeler/commit/03db1ed37b8b10b47d62ed0fa142d198a3861689)（[PR #158](https://github.com/google/ink-stroke-modeler/pull/158)） | 投影顺序不得沿输入折线回退，减少紧密 loops 的错误。 | 当前本地 `stylus_state_modeler.cc` 已具备。 |
| 2025-06-17 / [748f84a3](https://github.com/google/ink-stroke-modeler/commit/748f84a30335b773e073fd1cf31c591be0a6b77e)（[PR #162](https://github.com/google/ink-stroke-modeler/pull/162)） | 投影通过的旧 segment 才丢弃；mitigation speed 窗口仅按时长，移除对离散样本数量的错误解释。 | 当前本地已具备；不建立终点切线约束。 |
| 2025-08-21 / [1ebc40f2](https://github.com/google/ink-stroke-modeler/commit/1ebc40f220d4b8f916f3ec4d6c93e8628044a635)（[PR #169](https://github.com/google/ink-stroke-modeler/pull/169)，merged） | 同一 segment 的回退过滤提前到候选加入前，修正过滤过晚的问题。 | 当前本地 `stylus_state_modeler.cc:117` 已含；启用 mitigation 的必要可靠性基础，非直接终态修复。 |
| 2025-08-22 / [707ced88](https://github.com/google/ink-stroke-modeler/commit/707ced88e211779264cf513c9c50d8cf1815020d)（[PR #171](https://github.com/google/ink-stroke-modeler/pull/171)） | normal projection 与输入折线直接相交时接受该候选。 | 本地 `stylus_state_modeler.cc:132` 已含。 |
| 2025-08-27 / [2cd45e86](https://github.com/google/ink-stroke-modeler/commit/2cd45e8683025c28fa2efcf672ad46607e8af869)（[PR #172](https://github.com/google/ink-stroke-modeler/pull/172)，merged） | 角度估算包含 drag，使急转弯/离散方向翻转不至于漏掉所需细分。 | 当前本地 `position_modeler.cc:35` 已含；但只在设置正数角度阈值时才增加细分。 |

2025-08-27 之后公开 main 的变化中，没有发现替换上述末端位置算法的提交。2026 年有依赖、构建、版权和 README 变化；不应为本缺陷无目的地升级第三方二进制。

### 4. 容易误认成新修复的 PR / commit

| 记录 | API / 实码事实 | 研究判断 |
|---|---|---|
| [PR #159](https://github.com/google/ink-stroke-modeler/pull/159)（2025-04-03 建立） | 描述提及半圈至少 8 个点与尖曲线末端裁角；`state=closed`、`merged_at=null`。 | 可启发 `pi/8` 的实验阈值，但不能称作已发布默认修复；main 的角度阈值仍为 `-1`。 |
| [PR #174](https://github.com/google/ink-stroke-modeler/pull/174)（2025-08-22 建立，2026-01-21 关闭） | `state=closed`、`merged_at=null`。部分累计 diff 与 #172 已落地内容重合。 | 不能只读标题推定存在新的已启用 spring model 配置；main 的公开算法与本地相同。 |
| [PR #157](https://github.com/google/ink-stroke-modeler/pull/157)（2025-04-01 建立） | 题述保留前一 normal，减少速度和加速度同为零时 discontinuity；`merged_at=null`。 | 当前 main 仍调用 `GetStrokeNormal(tip_state, prev_time)`，零速度/零加速度返回 `nullopt`，没有该持久 normal 机制。该提案也不是高速转弯末端外偏修复。 |
| [PR #161 / fe79520c](https://github.com/google/ink-stroke-modeler/commit/fe79520c9ad7d2d445d26d3c59fda6fc54eb4d5c)（2025-06-06，merged） | 标题称始终使用 raw input 的 position modeler variant，但本仓库实际 commit 只修改 `params.cc`，允许两类采样时长为零。 | 不能据共享导出提交标题，声称本库新增公开的 raw position model 选择器；当前公开 `StrokeModelParams` 仍是相同 spring 配置。 |
| [PR #38](https://github.com/google/ink-stroke-modeler/pull/38)（2022-04-14 建立） | 描述处理零时差 kUp 的重复平滑点；`merged_at=null`。 | 不是本问题的直接修复；当前 Up 代码在结果为空时仍保留当前 state。 |

`merged_at=null` 只说明该 PR 没有通过 GitHub merge 动作合并；Copybara 导出或其他提交可能包含部分相同改动。判断能力是否可用，以**固定 HEAD 的实际代码**及本地差异为准，不以“Closed”或 PR 标题为准。

### 5. Loop contraction mitigation 的机制与可验证价值

源码依据：[固定 HEAD params.h](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/params.h#L58)、[ModelStylus](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/stroke_modeler.cc#L55)、[speed modeler](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/ink_stroke_modeler/internal/loop_contraction_mitigation_modeler.cc#L19)。本地对应行号相同。

- `P` 为 `StylusStateModeler::Project` 的输入折线投影结果，`Q` 为 spring 结果，输出为 `R = (1-alpha) P + alpha Q`。`alpha=1` 保留 spring，`alpha=0` 采用投影；参数里的 strength 是**spring 保留权重**，不是补偿强度。
- `use_stroke_normal_projection=true` 时优先沿 modeled stroke normal 找交点，防止向过去 segment 回退；左右都有交点时按加速度选外侧，缺少合法候选时沿用前一 projection。零 normal 的 fallback 是最近点投影。这不等价于“每份结果投到距离最小的 raw 点”。依据 `O/internal/stylus_state_modeler.cc:107`、`:144`、`:159`、`:200`。
- 字段名虽叫 raw polyline，Move 实际存入的是 **wobble-corrected input**（`O/stroke_modeler.cc:289-290`）；Up 插入物理 Up 位置（`:268`）。高速超过 wobble speed ceiling 时 corrected 与真实 raw 相同，低速或过渡区不能这样假定。
- **注释与实现的 speed 来源不同。** `params.h:61` 说 raw input speed，但实际 `ModelStylus:71-72` 使用前一融合结果 `result.back().velocity`，speed modeler 再取 magnitude 和时间窗口算术平均（`:22-32`、`:39`）。该反馈既含 spring velocity 又含投影插值得到的输入 velocity；不是独立真实原始速度。该点在当前上游未改。
- 每个结果先用上一份权重生成，再更新权重用于下一份结果；结果样本已被 upsampled，不是每份 raw 输入等权平均。窗口越大，权重变化越平滑，也越迟响应急转弯与停止。
- 融合的是整个 `Result` 的 position/velocity/acceleration 等字段；其 velocity 不保证等于变化权重后 position 的几何导数。后续终点 settled 判据或预测如依赖这些字段，需要同时验证。
- `Predict` 拷贝 stylus/mitigation modeler 再走相同 `ModelStylus`（`O/stroke_modeler.cc:180-186`），不提交预测状态。能够维持库内 Update/Predict 的机制一致性，但不能自动替代产品的 L0/L1/Stored 与物理 Up 时间合同。

**有依据的推断：** 对无自交、投影能匹配到正确 segment 的曲线，normal 方向位置融合可以缩小模型相对输入折线的法向偏差，因此虽命名为 loop contraction，也值得测试是否改善转弯外偏。不过它主要纠正法向对应，不能保证消除沿轨迹的时间滞后，也没有约束真实末端切线。运动期偏差若已落入稳定前缀，此机制不会回写过去结果。

**启用约束：** 必须同时设置 `is_enabled=true`、合法 speed bounds、两个 `[0,1]` 权重、非负 sampling window，并保持 `use_stroke_normal_projection=true`。不能只开 bool 而保留其他默认 `-1`。上游 validation 要求 lower speed <= upper speed，lower-speed spring 权重 >= upper-speed spring 权重；因此这是速度自适应的全程位置机制，不能直接表达“只在高曲率启用”。`O/params.cc:48-82`、`:230`。

**建议实验而非落地值：** 用真实轨迹速度分布选 bounds，比较多个有限 spring 权重和短时间窗口；不要先把 `alpha=0` 当产品答案。权重太低会暴露 raw 折线棱角/噪声、提高输入稀疏度敏感性，投影在紧密 loops、自交、反向和没有合法交点时可能停住或跳到另一段。需同时量化转弯前 tracking lag、Up 前法向偏差、Up 后切线转角及 projection 单调性。

### 6. 其余已有机制为什么不能单独宣称根治

**角度自适应细分。** `params.h:136-141` 的 `max_estimated_angle_to_traverse_per_input` 默认 `-1`；产品未赋值。启用后 `O/internal/position_modeler.cc:47-54` 会把时间步数与预计转角步数取较大值。`pi/8` 可以作为来自 #159 描述的试验起点，但它只提高离散模拟分辨率，不提供缺失真实采样或重建稳定前缀；连续 spring 惯性偏差依旧可能存在。必须和“只增加 min_output_rate”分开比较，并保持 `max_outputs_per_call` 上界。

**Wobble smoother。** `O/internal/wobble_smoother.cc:71-76` 在低速混合时间加权平均位置，在速度超过 ceiling 后回到输入位置。因此对高速曲率问题，关 wobble 或缩小 timeout 的收益需先测 actual blend；不能把低速平滑造成的 lag 与 spring 本身惯性混为一谈。Up 没有再过 wobble，仍可能从 corrected 最后 Move 转向真实 Up anchor。

**Spring/drag。** `O/internal/position_modeler.cc:18-22` 和 `:64-68` 明确是带惯性的 spring-drag 积分。产品默认 `mass/spring=11/32400`、`drag=72`；连续模型近似阻尼比 `72*sqrt(11/32400)/2 ≈ 0.663`，仅说明存在欠阻尼机制，不能用该数值替代实际离散回放。减小 mass/spring、重新配 drag 可减少 lag/回摆，但改变全程平滑、噪声抑制和预测；这是参数权衡，不是上游新增修复。

**ModelEndOfStroke。** 产品头文件 `position_modeler.h:103-125` 对下一步 candidate 检查：若在 previous→candidate segment 上 nearest-to-anchor 的比例 `<1`，恢复上一 state 并将时间步减半；微小位移/距终点很近/迭代预算用尽时停止。它约束逐步接近 anchor，不约束 raw 最后切线，也不倒回消除运动期外偏。

**kUp 普通插值未受这个 guard 保护。** `O/stroke_modeler.cc:251-255` 先把最后 corrected input 到 raw Up 做 `UpdateAlongLinearPath`，其每个积分结果直接入 buffer；之后才 `ModelEndOfStroke`。因此不能把终态 guard 误认为整批 Up 输出已有全程不越界或切线约束，更不能认为设置更小 stopping distance 就能消掉末钩。

### 7. 公开检索覆盖与后续验证边界

- 通过只读 `gh api` 查询固定 HEAD、完整分页 commits 标题、完整分页公开 issues/PR 元数据与标题，详查上述相关 PR 的 body、merged_at、merge SHA 和关键 diff。
- 全部普通公开 issues 共 10 条：`#1/#2/#3/#30/#63/#64/#73/#75/#151/#152`；主题是安全策略、构建、JS/Python/mobile 或讨论入口，未发现题述曲线立即 Up 缺陷的公开普通 issue。该结论不涵盖 discussions、私有 issue、Google 内部 bug。
- 提交标题含“不公开描述”的记录没有仅凭标题排除；当前固定 HEAD 核心源码与本地全文比较，补足了只搜标题的局限。未对每条历史 commit 的所有 diff 或全部 PR 评论作穷举。
- 使用 web 浏览了官方仓库、固定源码和相关 PR/commit；GitHub API 请求一度超时，最终直接字面量 `gh api` 查询成功，核心证据没有因临时网络问题缺失。所有链接指向原始公开来源，没有使用第三方对修复状态的转述。
- [上游 README](https://github.com/google/ink-stroke-modeler/blob/f2388813b0b25bc3e33d143d369a8367ab2e30c8/README.md) 现称 Google Ink 已迁移到新的 input-smoothing 实现；2026-03-10 的 [README commit](https://github.com/google/ink-stroke-modeler/commit/da42d439389c90ec7574f0381ec53e7f5be0c2eb) 能支持“另一个实现存在”，不能支持“换 Google Ink 可直接修复本缺陷”。本次未扩大到那个库的迁移研究。

建议后续只做有对照的无窗口轨迹回放：固定输入与真实 Up，baseline / angle-only / mitigation-only / 两者联合分组；位置指标使用 raw 与 modeled 数据，避免模拟笔锋半径影响中心线判断。同时保留 Up 同位/异位、突然反向、自交、稀疏包、批量包、短暂停再转弯这些边界。必须观察 Up 之前的运动轨迹；最终点误差小或末端局部 curve 看起来平滑都不足以证明根治。

## Related Specs

- `.trellis/spec/native/runtime-and-rendering.md:417-428`：prediction 不推进真实模型；终点接纳保护整批；严格位置/速度 settled；`:424` 明确大曲率运动阶段外偏及 Up 拐动仍未解决，不能把终态 cubic 或末点到位当完整修复。
- `.trellis/spec/native-desktop/draw3-integration.md`：主产品与独立 demo 的边界；原始库仓库上的源码现状或 demo 行为不自动证明主产品 static library 行为。
- `.trellis/spec/native/index.md`：第三方来源、最小差异与纯文档验证边界。
- `.trellis/spec/native-desktop/index.md`：证据等级、主产品生产链与验证归属。
- `.trellis/workflow.md` 与本任务 `prd.md`：本次限定 planning/research，研究必须持久化，不能进入实现。

## Caveats / Not Found

- “未发现公开直接修复”不能证明未来、私有或其他仓库实现不存在修复；结论锁定查询日期与 SHA。
- 原始源码普通文件可读，但主会话补查 `8d04529` 的只读 Git 命令返回 `exit 128 / Permission denied`；未确认该 revision 的 7 文件是否与当前工作树一致。本研究角色禁止任何 Git 操作，因此没有另行绕过该限制。
- 未解析/重编/运行产品 `merge.lib`，没有声称本次完成了二进制行为和源码版本核对。17/18 头文件一致也不能独立证明二进制 ABI 和实现来源。
- 没有执行 GUI、构建或轨迹参数试验；上述改善判断均是机制推断和待验证候选。仅新增本研究文件；产品、第三方、tests、工程和历史任务未改。
