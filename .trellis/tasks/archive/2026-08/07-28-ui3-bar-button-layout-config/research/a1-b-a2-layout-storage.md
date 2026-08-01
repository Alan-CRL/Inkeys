# 研究：主栏 A1 / B / A2 分区布局存储

日期：2026-08-01  
任务：`07-28-ui3-bar-button-layout-config`（重新打开，planning）  
范围：只研究，不改产品代码

## 1. 现状证据

### 1.1 配置模型

- 路径：`UI.Bar.ButtonLayout`（`Other.Config.cppm:157`）
- 元素：`BarButtonLayoutEntry { Id, Visible=true }`（`Other.Config.cppm:37-41`）
- 默认序列（`MakeDefaultBarButtonLayout`，`Other.Config.cppm:98-111`）：

```
Select, Draw, Eraser, Geometry(false), Recall, Clean, Divider, Pierce, Freeze, Setting
```

- 序列解析：整数组事务替换；任一元素 schema 错误则整字段回退默认（`Other.Config.cpp:325-340`）
- 规范合同：`.trellis/spec/native-desktop/configuration-i18n-and-assets.md`「UI3 Bar 顺序布局配置合同」

### 1.2 加载语义

`BarButtomSetClass::Load()`（`Bar.Buttom.cpp:468-499`）：

1. 读取 `ButtonLayout.Snapshot()`
2. 未注册 ID：保留到 normalized 配置，不创建 UI
3. 已注册且 `allowMultiple=false`：只取第一条，后续丢弃
4. 已注册且 `allowMultiple=true`：可重复（当前仅 Divider）
5. 用 entry.Visible 写入 `button->userVisible`
6. 把 normalized 序列 `Replace` 回内存配置

### 1.3 有效可见性

- `IsVisible() = userVisible && !hide`（`Bar.Bottom.cppm:83`）
- `hide` 是运行时上下文隐藏（选择模式等），与配置可见性分离

### 1.4 当前模型的能力与缺口

| 能力 | 现状 |
| --- | --- |
| 任意顺序 | 支持 |
| 官方按钮隐藏 | 支持（Visible=false） |
| 未知/插件 ID 保留 | 支持 |
| 官方按钮“不可消失” | **不支持**；配置丢项时不会补回 |
| A1 / B / A2 分区 | **不支持**；单序列无分区边界 |
| 固定区只排序不显隐 | **不支持**；固定项也存 Visible |
| 插件区独立策略 | **不支持**；与官方共用同一数组语义 |

### 1.5 默认布局与用户目标的映射

用户目标最终样式：`A1 + B + A2`

结合当前默认顺序，**自然切分**是：

| 区 | 默认成员 | 语义 |
| --- | --- | --- |
| A1 | Select, Draw, Eraser, Geometry, Recall, Clean, Divider | 绘制相关主按钮 + 分割线 |
| B | （空，留给插件/组件） | 可排序 + 可显隐 + 可缺席 |
| A2 | Pierce, Freeze, Setting | 定格/穿透/设置 |

> 注意：Divider 在当前默认中位于 Clean 与 Pierce 之间。若 Divider 算 A1 尾部，则 B 正好插在 Divider 与 Pierce 之间，符合“B 位于绘制相关按钮与定格穿透之间”。

## 2. 问题本质

要保证：

1. **官方固定按钮不会因配置损坏/手改/旧版本字段缺失而永久丢失**
2. **A 区只允许改顺序，不允许隐藏或删除**
3. **B 区继续用“顺序 + Visible + 未知 ID 保留”**
4. **最终渲染恒为 `A1 内部序 + B 内部序 + A2 内部序`**
5. **A1 与 A2 的分界固定**（B 的插入点固定在 A1 之后、A2 之前）

“棘手情况”不是排序本身，而是：

- 配置里缺了某个官方 ID
- 配置里多了未知官方 ID（未来版本）
- 配置里把官方 ID 写进了错误区
- 旧版单数组 `ButtonLayout` 需要迁移
- 用户/外部编辑器把 Visible=false 写到官方固定按钮

## 3. 方案对比

### 方案 S1：继续单数组，运行时按角色修补

- 仍只存 `UI.Bar.ButtonLayout`
- 注册表给每个按钮打 `SlotRole = FixedA1 | FixedA2 | PluginOrComponent`
- Load 时：
  1. 扫描配置，抽出 A1 候选、B 候选、A2 候选
  2. 对 A1/A2 做“按配置相对序 + 缺失项按默认序补回”
  3. 强制 `userVisible=true`（官方固定）
  4. B 保持现有语义
  5. 合成 `A1+B+A2` 写回

优点：迁移简单，磁盘结构几乎不变。  
缺点：磁盘上仍允许脏数据；边界（谁属于 A1/A2）只活在代码里；后续设置 UI 不易表达“不可隐藏”。

### 方案 S2：三序列分存（推荐）

配置改为：

```json
"UI": {
  "Bar": {
    "FixedLayoutA1": [ { "Id": "Inkeys.Bar.Select" }, ... ],
    "PluginLayout":  [ { "Id": "...", "Visible": true }, ... ],
    "FixedLayoutA2": [ { "Id": "Inkeys.Bar.Pierce" }, ... ]
  }
}
```

或对象形态：

```json
"UI": {
  "Bar": {
    "ButtonLayout": {
      "A1": ["Inkeys.Bar.Select", ...],
      "B":  [{ "Id": "Plugin.X", "Visible": true }],
      "A2": ["Inkeys.Bar.Pierce", ...]
    }
  }
}
```

A1/A2 元素可以只存 `Id`（不存 Visible），B 继续 `{Id, Visible}`。

优点：

- 存储语义与产品语义对齐
- 固定区丢失恢复规则局部、可测
- 设置 UI 可对 A1/A2 禁用隐藏控件
- 插件区独立演进不影响固定区

缺点：

- 需要从旧 `ButtonLayout` 数组迁移
- schema / codec / spec 合同都要更新

### 方案 S3：双序列（Fixed + Plugin）+ 固定分界点

- `FixedLayout` 存 A1+A2 的相对序
- `PluginLayout` 存 B
- 另存或硬编码 `pluginInsertAfter = Clean/Divider` 之类的锚点

优点：字段更少。  
缺点：A1/A2 仍混在一个序列里，跨界拖动、丢失补回、UI 分区都更绕；不直接表达 A1/B/A2。

## 4. 推荐方案

**推荐 S2：三序列分存 + 角色注册 + 规范化补全。**

### 4.1 角色模型

注册表扩展（概念）：

| 字段 | 含义 |
| --- | --- |
| `id` | 稳定字符串 ID |
| `role` | `FixedA1` / `FixedA2` / `PluginOrComponent` / `Decorative` |
| `allowMultiple` | 当前仅 Divider |
| `defaultVisible` | 仅 B 有意义；A 固定 true |
| `required` | A1/A2 官方按钮为 true |

默认角色建议：

- FixedA1 required：Select, Draw, Eraser, Geometry, Recall, Clean, Divider
- FixedA2 required：Pierce, Freeze, Setting
- PluginOrComponent：未来插件/组件按钮

> Geometry 虽默认“产品上较少使用”，但用户要求 A 区不允许隐藏；因此 Geometry 也应是 required 固定按钮，只允许改 A1 内顺序。若产品仍要“默认不出现”，那与“不允许隐藏”冲突，必须单独决策（见文末问题）。

### 4.2 规范化算法（核心）

对 **A1**（A2 同理）：

输入：配置数组 `configured`，默认数组 `defaults`，注册表中本区 required 集合 `requiredIds`

```
seen = empty ordered list
used = empty set

for entry in configured:
  if entry.Id not in requiredIds of this zone:
    // 错区 / 未知官方 / 插件误入固定区
    drop or migrate by policy (见下)
    continue
  if entry.Id in used and not allowMultiple:
    drop duplicate
    continue
  append entry.Id to seen; mark used

for id in defaults:          // 默认序作为“补回锚点”
  if id not in used:
    insert id into seen at the relative position implied by defaults
    // 具体：按 defaults 顺序重建，配置中出现过的保持相对序，缺失项插回默认邻居之间

result = seen
force Visible=true for all result entries
```

更稳妥、易测的实现方式是 **相对序归并（order merge）**：

1. 从配置中过滤出本区合法 ID，去重，保留首次出现相对序 → `userOrder`
2. 从默认本区序列取出完整 `defaultOrder`
3. `result = stable merge`:
   - 先放入 `userOrder` 中所有仍合法的 ID
   - 再把 `defaultOrder` 中缺失 ID 按默认相对位置插回  
     （常用算法：以 defaultOrder 为骨架，把 userOrder 当作对骨架的排列约束；或：缺失项插到其默认前驱之后）

推荐缺失项插回策略：

- **按默认前驱插入**：找到缺失 ID 在 `defaultOrder` 中的前一个已存在 ID，插到它后面；若前驱都不在，则插到最前/按默认索引插入。
- 这能在“中间丢了一项”时尽量回到自然位置，而不是一律甩到末尾。

### 4.3 B 区算法（沿用现语义）

对 **PluginLayout / B**：

1. 保留未知 ID（插件未加载）
2. 已注册 plugin 按钮：按序加载，应用 Visible
3. 单例重复：保留首条
4. 若误含官方固定 ID：从 B 移除（避免同一按钮出现两次）
5. 不因“缺了某个插件”而补默认——插件本就可缺席

### 4.4 合成与写回

```
runtimeList = normalize(A1) + normalize(B) + normalize(A2)
```

- 渲染/命中只消费 `runtimeList`
- 规范化后的 A1/B/A2 分别写回各自配置字段
- 下一次 `Config::Write()` 落盘，完成自愈

### 4.5 旧配置迁移

若磁盘仍是旧单数组 `UI.Bar.ButtonLayout`：

1. 读取旧数组
2. 按 ID 角色拆分：
   - FixedA1 IDs → A1（保留相对序）
   - FixedA2 IDs → A2
   - 其余 → B（保留 Visible）
3. 对 A1/A2 跑补全算法
4. 写入新三字段
5. 删除或停止写入旧 `ButtonLayout`（建议读兼容一个版本，写只写新结构）

若旧数组完全无效：三区都回各自默认。

### 4.6 “丢失”场景决策表

| 场景 | 推荐行为 |
| --- | --- |
| A1 缺 Geometry | 按默认前驱（Eraser 后）补回，Visible 强制 true |
| A1 为空数组 | 使用完整默认 A1 |
| A1 字段缺失 | 使用默认 A1 |
| A1 含插件 ID | 移出 A1；若像 plugin 则并入 B，否则丢弃 |
| A1 含 Pierce（本属 A2） | 移到 A2，保留相对意图困难时按 A2 默认位置 |
| A1 重复 Select | 保留第一条 |
| B 缺某插件 ID | 不补；插件卸载后可选择保留或 GC（见问题） |
| B 含官方固定 ID | 删除，避免双份 |
| 官方固定 Visible=false（旧数据） | 忽略，强制 true |
| 整个 UI.Bar 损坏 | A1/A2/B 均默认 |

### 4.7 为什么不靠“单数组 + 禁止隐藏”就够

单数组即使强制官方 Visible=true，仍无法表达：

- B 必须夹在 A1 与 A2 之间
- 用户不能把 Setting 拖进绘制区左侧却让插件跑到 Setting 右侧之外的分区约束
- 固定按钮缺失补回时，插入点依赖分区默认骨架

分区是产品结构，不只是校验规则。

## 5. 对现有代码的影响面（实施时，非现在）

| 区域 | 影响 |
| --- | --- |
| `Other.Config.cppm` schema | 替换/拆分 `ButtonLayout` |
| `Other.Config.cpp` codec | A1/A2 可简化为 Id 列表；B 保留 entry |
| `Bar.Buttom.cpp` Load | 三分区 normalize + 合成 |
| 注册表 | 增加 role / required |
| spec 合同 | 更新 UI3 Bar 布局合同 |
| 设置 UI（后续任务） | A1/A2 只排序；B 排序+显隐 |
| 迁移 | 旧 `ButtonLayout` 读兼容 |

不需要改渲染主循环的消费方式：仍遍历 `buttomlist` 顺序即可。

## 6. 建议的配置形状（具体草案）

```json
"UI": {
  "Bar": {
    "FixedButtonsA1": [
      { "Id": "Inkeys.Bar.Select" },
      { "Id": "Inkeys.Bar.Draw" },
      { "Id": "Inkeys.Bar.Eraser" },
      { "Id": "Inkeys.Bar.Geometry" },
      { "Id": "Inkeys.Bar.Recall" },
      { "Id": "Inkeys.Bar.Clean" },
      { "Id": "Inkeys.Bar.Divider" }
    ],
    "ExtensionButtons": [
      { "Id": "Plugin.Example", "Visible": true }
    ],
    "FixedButtonsA2": [
      { "Id": "Inkeys.Bar.Pierce" },
      { "Id": "Inkeys.Bar.Freeze" },
      { "Id": "Inkeys.Bar.Setting" }
    ]
  }
}
```

说明：

- A1/A2 也可进一步简化为纯字符串数组；但为了复用 `ConfigSequence` 与未来扩展，保留 `{Id}` 对象成本很低。
- A 区不写 `Visible`，从类型上减少误用。
- 字段名可用 `FixedButtonsA1` / `ExtensionButtons` / `FixedButtonsA2`，避免继续叫 `ButtonLayout` 造成旧语义混淆。

## 7. 风险

1. **Geometry 语义冲突**：当前默认 Visible=false；新规则若 A 区禁止隐藏，Geometry 将始终显示，改变现有默认 UI。
2. **Divider 归属**：若允许 Divider 被用户拖出 A1，分区边界会乱；建议 Divider 固定属于 A1，且 allowMultiple 仅在 A1 内。
3. **迁移一次失败**：旧数组含大量未知 ID 时，全进 B 可能让插件区膨胀；可接受。
4. **插件 GC**：长期保留已卸载插件 ID 会让配置变脏；需要策略（永不删 / 启动 N 次后删 / 仅手动清理）。

## 8. 结论

1. 现有单序列 `UI.Bar.ButtonLayout` **不足以**安全表达 A1/B/A2 与“官方按钮不可丢失”。
2. 推荐 **三序列分存**：A1 固定序、B 扩展序、A2 固定序。
3. 固定区用 **“用户相对序 + 默认骨架补缺失 + 强制可见”** 解决棘手丢失问题。
4. 扩展区继续现有插件友好语义。
5. 旧配置做一次只读迁移；规范化后写新结构。

在确认 Geometry/Divider 产品语义后，可进入 `prd.md` / `design.md` 收敛。

## 9. 用户简化决策（2026-08-01）

### 9.1 决策

1. **三个数组分存**：A1 / B / A2。
2. **A 区（A1、A2）**
   - 配置**只保存顺序**，不保存/不接受用户可改的 `Visible`。
   - “隐藏”只来自**代码注册时写死的默认**（例如 Geometry 在注册时 `userVisible=false`），配置层不能改。
   - **不允许丢失按钮**：加载时做完整性检查。
   - 若发现缺项（或更广义的非法 A 区配置），**不做精细插回**，直接将该区**重置为默认顺序**。
3. **B 区**
   - 继续顺序 + `Visible` + 未知 ID 保留（插件友好）。

### 9.2 评价

这是更简单、更稳的工程选择，**推荐采纳**，理由：

| 点 | 说明 |
| --- | --- |
| 实现成本低 | 校验 = “是否恰好覆盖 required 集合的一个排列”，失败则 `Replace(default)` |
| 语义清晰 | 损坏配置不半修半就，避免补洞算法插错位置 |
| 与产品一致 | A 的显隐不开放给配置；排序才是用户可定制点 |
| 自愈强 | 缺 Select / 手改丢项 / 半截迁移失败 → 整区回默认，主栏不会缺官方按钮 |

### 9.3 建议的 A 区合法性定义（在“缺项重置”之上稍作收紧，仍保持简单）

把 A1/A2 看成**固定集合的排列**：

合法当且仅当（对每个 A 区各自判断）：

1. 是数组且元素可解析为非空 `Id`（建议 A 区元素无 `Visible` 字段，有则忽略或整区非法——二选一，推荐**忽略多余 Visible** 以兼容误写）
2. 过滤后 ID 多重集合 == 该区 required 默认多重集合  
   - 当前均单例；若 Divider 仅 A1 且 allowMultiple，则默认里有几条 Divider 就要求几条
3. 不含 B 区/另一 A 区/完全未知 ID

任一不满足 → **该区重置为默认顺序**（只重置出问题的 A1 或 A2，不动另一侧与 B）。

B 区仍独立：缺插件不补、未知保留、官方固定 ID 误入则剔除（或整段规范化），**不对 B 做“缺官方就重置”**。


### 9.3.1 非法判定已确认（2026-08-01）

用户确认采用 **严校验（选项 A）**：

A1/A2 合法 **当且仅当** 配置 ID 多重集合 **恰好等于** 该区 required 默认多重集合（即该集合上的一个排列）。

下列任一情况 → **仅重置出问题的那一区** 为默认顺序：
- 缺 required ID
- 多未知 ID / 错区 ID（插件进 A、A2 的 ID 进 A1 等）
- 非法重复（破坏多重集合相等）
- 字段缺失、非数组、元素无法解析为非空 Id

不做“先丢垃圾再抢救剩余顺序”。

### 9.4 主要代价（可接受，但要写进合同）

1. **自定义顺序的脆弱性**：A1 里只要丢 1 个 ID，整个 A1 自定义顺序清零。损坏罕见时可接受。
2. **版本升级加官方按钮**：若新版本 A1 required 集比旧配置多 1 个 ID，旧配置会被判缺项并**重置顺序**。  
   - 缓解（仍简单）：发版时若只是“新增 required”，可做一次**专用迁移**把缺的新 ID 按默认位置追加/插入后再校验；日常损坏路径仍走重置。  
   - 不需要上完整相对序归并算法。
3. **Geometry**：不再靠配置 `Visible=false`；改为注册/初始化写死 `userVisible=false`。配置无法把它打开——若将来设置页要“显示几何”，那是产品能力变更，不是布局配置能做的。


### 9.4.1 升级重置已确认（2026-08-01）

用户确认：官方后续新增固定按钮时，**接受 A 区因 required 集合变化而整区重置为默认顺序**。

理由：
- 用户若已打乱顺序，自动补洞无法可靠判断新按钮应插入何处。
- 重置到官方默认比“猜一个插入点”更可预期。
- 不需要为升级路径单独维护复杂相对序归并。

因此发版策略简化为：
- 日常损坏 / 缺项 / 错区 → 该 A 区重置默认
- 升级新增 A 区 required ID → 旧配置缺项 → **同样整区重置默认**
- 不为“保留旧自定义序 + 插入新按钮”做专用迁移（除非未来产品明确要求）

### 9.5 与先前“精细补洞”方案的关系

| | 精细补洞（前稿） | 缺项整区重置（本决策） |
| --- | --- | --- |
| 丢 1 项 | 插回默认邻居，保留其余顺序 | 整区回默认 |
| 实现 | 较高 | **低** |
| 可预测性 | 中（插回位置需约定） | **高** |
| 升级加按钮 | 可自动保留旧序 | 需可选小迁移，否则重置 |

**结论：采用三数组 + A 区集合校验 + 失败整区默认重置。** 不做日常路径上的逐项补洞。

### 9.6 修订后的配置形状草案

```json
"UI": {
  "Bar": {
    "FixedButtonsA1": [
      "Inkeys.Bar.Select",
      "Inkeys.Bar.Draw",
      "Inkeys.Bar.Eraser",
      "Inkeys.Bar.Geometry",
      "Inkeys.Bar.Recall",
      "Inkeys.Bar.Clean",
      "Inkeys.Bar.Divider"
    ],
    "ExtensionButtons": [
      { "Id": "Plugin.Example", "Visible": true }
    ],
    "FixedButtonsA2": [
      "Inkeys.Bar.Pierce",
      "Inkeys.Bar.Freeze",
      "Inkeys.Bar.Setting"
    ]
  }
}
```

- A1/A2：纯 ID 数组（顺序排列）最简单。
- B：保持 `{Id, Visible}`。
- 旧 `ButtonLayout`：启动读入时拆分；A 侧校验失败则该侧默认；成功则写入新字段。

### 9.7 Load 伪代码（概念）

```
a1 = read(FixedButtonsA1) or empty
if not isPermutationOf(a1, defaultA1Ids): a1 = defaultA1Ids
a2 = read(FixedButtonsA2) or empty
if not isPermutationOf(a2, defaultA2Ids): a2 = defaultA2Ids
b  = normalizePlugin(read(ExtensionButtons))  // 未知保留、去官方、Visible

for id in a1: place registered button; userVisible = registrationDefaultVisible(id)
for e  in b:  place if registered; userVisible = e.Visible
for id in a2: place registered button; userVisible = registrationDefaultVisible(id)

write back normalized a1/b/a2
```

## 10. 配置元素结构体（2026-08-01）

### 10.1 用户决策

A / B 都使用对象结构，而不是 A 用纯 `string[]`。

概念字段：

| 字段 | A1/A2 | B | 说明 |
| --- | --- | --- | --- |
| `Id` | 必填 | 必填 | 稳定字符串 ID |
| `Size` | 有 | 有 | 对应 `BarButtomSizeEnum`：`oneOne` / `twoOne` / `twoTwo` / `oneTwo`（分割线） |
| `Visible` | **无**（不持久化、配置不可改） | 有 | 插件/组件用户显隐 |

示例：

```json
"FixedButtonsA1": [
  { "Id": "Inkeys.Bar.Select", "Size": "twoTwo" },
  { "Id": "Inkeys.Bar.Divider", "Size": "oneTwo" }
],
"ExtensionButtons": [
  { "Id": "Plugin.Example", "Size": "twoTwo", "Visible": true }
],
"FixedButtonsA2": [
  { "Id": "Inkeys.Bar.Pierce", "Size": "twoOne" },
  { "Id": "Inkeys.Bar.Freeze", "Size": "twoOne" },
  { "Id": "Inkeys.Bar.Setting", "Size": "twoTwo" }
]
```

### 10.2 代码侧现状（Size 证据）

注册时写死（`Bar.Buttom.cpp` PresetInitialization）：

| 按钮 | 默认 Size |
| --- | --- |
| Divider | `oneTwo` |
| Select/Draw/Eraser/Geometry/Recall/Clean/Setting | `twoTwo` |
| Pierce/Freeze | `twoOne` |

运行时覆盖：

- `PresetHoming()` 在选择模式下把 Freeze 改为 `twoTwo`，否则 `twoOne`（`Bar.Buttom.cpp:523,539`）。
- 布局/绘制按 `temp->size` 四分支消费（`Bar.Main.cpp`）。

因此：**配置中的 Size 若被采纳，只能作为“基础/用户尺寸”；运行时上下文仍可能改写（至少 Freeze）。** 合同需写清优先级。

### 10.3 结构体设计草案

可共享基字段，B 扩展 Visible：

```text
BarButtonLayoutItem
  Id: string
  Size: enum (oneOne|twoOne|twoTwo|oneTwo)

BarExtensionLayoutItem : BarButtonLayoutItem
  Visible: bool = true
```

或单一结构 + 分区解释：

```text
BarButtonLayoutEntry
  Id: string
  Size: enum
  Visible: bool = true   // 仅 B 读写；A 加载忽略且不写回
```

第二种实现更省 codec 分叉，但 A 的 JSON 合同应声明 **不包含 Visible**（写回 A 时剥离）。

### 10.4 开放：Size 的权威来源

尚未最终确认（见 PRD Open Questions）：

1. A/B 的 Size 是否允许用户自定义并持久化？
2. 缺 Size / 非法 Size 时：用注册默认，还是整区重置？
3. 与 `PresetHoming` 运行时改 Freeze 尺寸如何叠加？

### 10.5 Size 策略已确认（2026-08-01）

用户选择 **B：结构体带 Size，当前阶段用户不可编辑**。

规则：
1. A/B 元素均持久化 `Size` 字段（与 `Id` 并列；B 另有 `Visible`）。
2. **当前产品不提供改 Size 的配置入口**；权威来源是按钮注册时的默认 Size。
3. 读取时若缺 `Size`、类型非法、或不在四枚举内 → **纠正为该 Id 的注册默认 Size**，不因此触发 A 区整区重置。
4. 读取时若 `Size` 与注册默认不同 → **当前阶段纠正为注册默认并写回**（为后续“用户可改 Size”预留字段，但不保留非默认值，直到设置 UI 上线）。
5. 后续任务可开放用户修改按钮大小；届时再把“非默认 Size 保留”写入合同。
6. 运行时 `PresetHoming` 对 Freeze 的临时尺寸覆盖仍优先于配置基础 Size。

说明：选项 B 与“后续用户可改”的衔接方式是 **字段先占位、语义先只读默认**；开放编辑时只需停止“强制纠正为默认”，改为信任配置中的合法 Size。

### 10.6 A 区误带 Visible 已确认（2026-08-01）

用户确认：**忽略并剥离**。

- A1/A2 元素若含 `Visible`：不读取、不写回。
- 不因此判定整区非法。
- Id 集合仍走严校验；Size 仍走注册默认纠正。

### 10.7 插件未知 ID 保留已确认（2026-08-01）

用户确认：**永久保留（选项 A）**。

- B 区未注册 / 已卸载插件 ID 原样保留在配置中。
- 不创建 UI、不参与渲染。
- 插件重新安装后，顺序与 Visible 仍在。
- 本轮不做启动 GC / 延迟 GC；清理可留待设置页“重置扩展按钮”等后续能力。

### 10.8 按钮 ID 命名规范（2026-08-01）

用户补充并落地：

- 官方按钮 ID 必须以 `Inkeys.` 开头（当前 `Inkeys.Bar.*`）。
- 扩展/插件/组件按钮不得使用 `Inkeys.` 前缀，且必须为点分 ID：`xxx.xxx` 或 `xxx.xxx.xxx` 等（至少两段，无首尾点、无空段）。
- `RegisterButton` 与 B 区 normalize 强制该规则；非法扩展 ID 与 `Inkeys.*` 误入 B 时剔除。

