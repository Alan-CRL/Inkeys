# 技术设计（草案）

> 研究阶段草案。已采纳：三数组 + A 区非法整区重置。

## 架构

```
UI.Bar.FixedButtonsA1      // {Id, Size}[]，required Id 集合的排列
UI.Bar.ExtensionButtons    // {Id, Size, Visible}[]，插件/组件
UI.Bar.FixedButtonsA2      // {Id, Size}[]，required Id 集合的排列

runtime = A1 + normalize(B) + A2
```

### 元素结构

- 共用：`Id`（string）、`Size`（`oneOne` | `twoOne` | `twoTwo` | `oneTwo`）
- 仅 B：`Visible`（bool，默认 true）
- A 无配置 Visible；显隐默认来自注册写死值
- A 元素若误带 `Visible`：**忽略并剥离写回**，不整区重置
- Size 与现网 `BarButtomSizeEnum` 对齐；Divider 默认 `oneTwo`
- **本轮 Size 只读注册默认**：缺省/非法/与注册不一致 → 纠正为注册默认并写回；不因 Size 重置 A 区 Id 排列
- 后续开放用户改 Size 时，停止强制纠正，改为保留合法枚举值
- `PresetHoming` 对 Freeze 的临时 size 覆盖仍优先于配置基础 Size

## A 区校验（已确认：严校验）

`isPermutationOf(configured, defaultIds)`：配置 ID 多重集合 **恰好等于** 该区 required 默认多重集合。

失败（缺、多、错区、非法重复、类型错误）→ 该区 `Replace(defaultIds)`，不先过滤再抢救。

A 按钮 `userVisible` 只来自注册默认，不读配置。

## B 区

沿用并确认：
- 未知 / 已卸载插件 ID **永久保留**，不渲染
- Visible 生效
- 单例去重
- 剔除误入的官方固定 ID
- 本轮不做启动 GC

## 迁移

旧 `ButtonLayout` 按角色拆分 → A 走校验（可能重置）→ B normalize → 写新字段。

## 升级

新增 A 区 required ID → 旧配置缺项 → **该区整区重置默认**。

已确认不需要“保留用户旧序 + 猜测新按钮插入点”的迁移；自动修补无法可靠知道新按钮该放哪。

## 开放点

（无。规划决策已收敛。）
