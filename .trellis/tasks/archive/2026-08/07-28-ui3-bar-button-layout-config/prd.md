# UI3 Bar 按钮布局配置（固定区 / 扩展区分存）

## Goal

把 UI3 主栏按钮布局升级为 **A1 + B + A2** 三数组模型：官方固定区只定制顺序且不可丢按钮；扩展区承载插件/组件的顺序与显隐。研究阶段不改产品代码。

## Background

### 已确认事实（代码）

- 当前：单数组 `UI.Bar.ButtonLayout`，`{Id, Visible}`；缺官方 ID 不会补回。
- 默认序：Select, Draw, Eraser, Geometry(false), Recall, Clean, Divider, Pierce, Freeze, Setting。
- `IsVisible() = userVisible && !hide`；`hide` 为运行时上下文隐藏。

### 用户目标与已拍板简化

- 最终样式恒为 **A1 B A2**。
- **三个数组分存**。
- **A1/A2**：
  - 配置只存顺序，**不通过配置改显隐**。
  - 隐藏若需要，来自**代码注册写死**的默认（非配置）。
  - **不允许丢按钮**；校验发现缺项/非法时，**该区直接重置为默认顺序**（不精细逐项补洞）。
- **B**：
  - 位于绘制相关区与定格/穿透区之间。
  - 可排序、可 Visible、未知 ID 保留（沿用现写法）。

### 建议分区成员（待最终确认）

| 区 | 默认成员 |
| --- | --- |
| A1 | Select, Draw, Eraser, Geometry, Recall, Clean, Divider |
| B | 插件/组件（默认可空） |
| A2 | Pierce, Freeze, Setting |

## Requirements（草案）

- R1：配置分存 A1 / B / A2 三个序列。
- R2：A1/A2 仅表示 required 固定集合上的排列；不持久化用户可改 Visible。
- R3：A1/A2 加载时做**严校验**：配置必须是该区 required 多重集合的恰好排列；缺项、多余/错区 ID、非法重复、字段类型错误均导致**该区重置默认顺序**并写回。不先过滤再抢救。
- R4：A 区按钮的默认 `userVisible` 来自注册/初始化写死值（如 Geometry 默认 false）；配置不能改。
- R5：B 区支持顺序、Visible；**未知/已卸载插件 ID 永久保留**且不渲染；误入官方固定 ID 剔除；不因缺插件重置 B；本轮不做启动 GC。
- R6：运行时列表 = A1 + B + A2。
- R7：旧 `UI.Bar.ButtonLayout` 可拆分迁移；迁移后的 A 区仍走 R3 校验。
- R8：发版若新增 A 区 required 按钮，旧配置因缺项触发该区重置默认；不实现“保留旧序并猜测新按钮位置”的迁移。
- R9：布局元素使用结构体：`Id` + `Size`（对应 `BarButtomSizeEnum` 四值）；B 另含 `Visible`；A 不持久化 Visible。
- R9b：本轮 `Size` 仅作结构占位与注册默认镜像；加载时纠正到注册默认并写回；非法 Size 不触发 A 区整区重置。后续设置 UI 可开放用户改 Size。
- R9c：A 区元素若误带 `Visible`，忽略并在写回时剥离；不触发整区重置。
- R10：实施时更新 spec 合同；设置页拖拽编辑可另开任务。

## Constraints

- 研究阶段不改产品代码。
- 保持 `IsVisible()` 为唯一消费入口；运行时 `hide` 不变。
- Redo 不进布局。

## Acceptance Criteria（规划阶段）

- [x] 任务重新打开为 planning。
- [x] 完成现状审计与方案文：`research/a1-b-a2-layout-storage.md`。
- [x] 采纳用户简化：三数组 + A 缺项/非法整区重置。
- [x] 确认 A1/A2 成员集合与 Divider 归属（A1 含 Divider；A2 = Pierce/Freeze/Setting）。
- [x] 确认升级新增官方按钮时整区重置，不做猜插入点迁移。
- [x] 确认 A 区非法判定：严校验（恰好排列，否则整区重置）。
- [x] 确认 A/B 使用对象结构体：`Id` + `Size`；B 另有 `Visible`；A 无配置 Visible。
- [x] 确认 Size：本轮只读注册默认并写回；缺省/非法/非默认纠正；不因 Size 重置 A 区；Freeze 运行时覆盖仍优先。后续再开放用户改大小。
- [x] 确认 A 误带 Visible：忽略并剥离，不整区重置。
- [x] 确认 B 未知/已卸载插件 ID：永久保留，不启动 GC。
- [ ] 定稿 design/implement 后用户批准再编码。

## Out of Scope

- 当前不写产品代码
- 设置页拖拽 UI
- 插件注册中心全设计
- 日常路径上的“缺谁补谁、保留其余自定义序”精细归并

## Decisions Log

| 决策 | 结论 |
| --- | --- |
| 存储结构 | 三数组 A1 / B / A2 |
| A 丢项策略 | 不做精细补洞；该区重置默认 |
| A 显隐 | 配置不可改；仅代码注册默认 |
| B 语义 | 顺序 + Visible + 未知保留 |
| 升级新增官方固定按钮 | 旧 A 区配置缺新 ID → **整区重置默认**；不做“猜插入点”迁移 |
| A 区非法判定 | **严校验**：必须是 required 多重集合的恰好排列；缺/多/错区/非法重复均重置该区 |
| A/B 元素形态 | **对象结构体**：`Id` + `Size`；B 另有 `Visible`；A **无**配置 Visible |
| Size 策略（本轮） | **字段持久化但用户不可改**；权威=注册默认；缺省/非法/非默认均纠正为注册默认并写回；不因 Size 整区重置 |
| Size 后续 | 设置 UI 可开放改按钮大小；届时保留合法非默认 Size |
| A 误带 Visible | **忽略并剥离**；不触发整区重置 |
| B 未知/已卸载插件 ID | **永久保留**不渲染；不做启动 GC |
| 按钮 ID 命名 | 官方 `Inkeys.*`；扩展为非 `Inkeys.` 的点分 ID（`xxx.xxx` / `xxx.xxx.xxx`） |
| A1 默认成员 | Select, Draw, Eraser, Geometry, Recall, Clean, Divider |
| A2 默认成员 | Pierce, Freeze, Setting |

## Open Questions

（规划决策已收敛；实施前仅需用户审阅最终 PRD/design/implement。）

## Research Pointer

`research/a1-b-a2-layout-storage.md`（含 §9 简化决策评价）
