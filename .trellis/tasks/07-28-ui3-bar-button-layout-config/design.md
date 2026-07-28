# 技术设计

## 配置序列

- 将标量 traits 提升为通用 JSON value codec，并为顺序序列提供统一适配入口。
- 配置序列内部持有 `shared_mutex` 和有序值集合；对外提供快照与事务替换。JSON 写入在共享锁下取得一致快照，JSON 读取先解析到临时集合，再在独占锁下替换。
- `BarButtonLayoutEntry` 保存 `Id` 和 `Visible`。元素必须是对象且 `Id` 为非空字符串；`Visible` 缺失时取 `true`，类型错误时判定整个字段无效。
- schema 默认值包含当前官方布局，Geometry 保留在 Eraser 后但设为隐藏。

## Bar 注册与加载

- `BarButtomClass` 增加稳定 ID 和独立用户可见状态；现有 `hide` 继续表示运行时上下文隐藏。
- `BarButtomSetClass` 维护 ID 到预设按钮及 `allowMultiple` 的注册信息。官方 ID 使用 `Inkeys.Bar.*`；Divider 允许重复，其余当前按钮不允许。
- `Load()` 从配置快照按顺序创建 UI 项。未知 ID 保留在配置但跳过 UI；不可重复 ID 从第二次出现起跳过，并从规范化后的配置序列删除。
- 所有显示、布局、动画、命中和锚点判断使用统一的有效可见性，避免用户隐藏与上下文隐藏互相覆盖。

## 数据流与兼容

1. `IdtMain` 先执行 `config.ReadAll()`；缺键或无效布局保留 schema 默认值。
2. 既有启动写入会把缺失布局写入 `main.json`。
3. Bar 初始化预设和 ID 注册后执行 `Load()`，构造运行时列表并把去重后的完整布局替换回内存配置。
4. 因启动写入发生在 Bar 加载前，重复项会在后续任意 `config.Write()` 时清理，未知 ID 始终保留。

## 边界

- 空数组是有效布局。
- 未注册 ID 不参与重复判定。
- 新增官方/插件按钮必须显式提供稳定 ID、重复策略和默认/迁移策略。
- 本次不实现插件注册表或设置 UI。
