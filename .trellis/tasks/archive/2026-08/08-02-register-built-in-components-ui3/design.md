# 技术设计

## 总体边界

本阶段保留两套配置职责：

```text
UI2 setlist.component.shortcutButton.*
        | 读取 / WriteSetting 持久化
        v
内置组件注册表（稳定顺序）
        | 运行时投影，不写新版配置
        v
UI3 runtime B buttons

UI.Bar.FixedButtonsA1  ----------------------> runtime A1
UI.Bar.ExtensionButtons  -- 本阶段忽略 ------> 不参与 runtime B
UI.Bar.FixedButtonsA2  ----------------------> runtime A2
```

最终运行时布局仍为 `A1 + boundary + derived B + boundary + A2`；当 derived B 为空时沿用现有单条边界分割线行为。

## 注册契约

扩充 `BarButtonRegistrationClass`，让一个注册项同时描述运行时按钮与设置来源：

- `Id`：非 `Inkeys.` 的稳定点分字段 ID。
- `button`：由注册表持有的按钮模板。
- `zone` / `defaultSize` / `defaultUserVisible`：沿用现有布局元数据。
- `legacyEnabled`：无参数读取器，返回对应 `setlist` 开关状态；固定按钮可为空。
- `categoryName` / `settingsName`：设置页目录元数据。
- 注册顺序：单独保存 ID 序列，禁止依赖 `unordered_map` 遍历顺序。

公开只读查询和同步入口：

- 按 ID 查询完整注册结构。
- 按稳定顺序取得扩展组件目录快照。
- `SyncLegacyExtensionButtons()`：重新读取全部 `legacyEnabled`，构建并替换运行时 B 区。

## 生命周期与运行时替换

- 注册表使用 `shared_ptr<BarButtomClass>` 持有固定按钮和组件模板，`preset[]` 继续保留非拥有型指针。
- 内置组件当前为单例注册项；每个旧布尔字段最多产生一个运行时按钮。
- 两条边界分割线使用由 `BarButtomSetClass` 长期持有的独立实例，避免运行时切换空/非空 B 区时销毁仍可能被渲染或交互线程观察到的对象。
- `BarButtomListClass` 增加一次性替换整个有效列表的接口；在内部锁中交换 `shared_ptr` 序列，再发布新的 `tot`。列表中的对象均由注册表或边界实例继续持有，因此旧 raw pointer 不会立即悬空。
- 同步只重建运行时列表，不修改 `Inkeys::config.UI.Bar.ExtensionButtons`。

## PNG 图标

`BarButtomClass` 保留现有 SVG 图标控制状态，并增加图标种类与 PNG 载荷：

- SVG：沿用当前 `BarUiSVGClass icon` 的完整路径。
- PNG：公共布局/透明度动画仍由按钮图标控制状态驱动，绘制前把当前尺寸、透明度和启用状态同步到 `BarUiPNGClass`，调用现有 `BarUIRendering::Png`。
- PNG 不参与 SVG 主题颜色替换；按钮背景、文字、按压倍率和显隐动画保持一致。
- 设备资源丢失时重置活动按钮的 PNG 位图缓存。

## 内置组件与动作复用

在 `IdtFloating.h/.cpp` 定义稳定的组件动作枚举与统一执行函数。传统浮窗原有 if/else 分支改为选择枚举后调用该函数；UI3 注册按钮的 `clickFunc` 捕获同一枚举。

注册顺序沿用设置页：

1. 应用：文件资源管理器、任务管理器、控制面板
2. 系统：显示桌面、锁屏
3. 键盘：ESC、Alt+F4
4. 随机点名：IslandCaller 1、IslandCaller 2、SecRandom 1、SecRandom 2、SecRandom 2 兼容、NamePicker
5. 联动：ClassIsland 设置、档案编辑、快速换课

图标复用 `CustomizeIco1` 至 `CustomizeIco11` 的现有 PNG 资源映射。

## 启动与设置页数据流

启动：

1. 初始化官方预设按钮。
2. 注册 16 个内置组件。
3. 读取和规范化 A1/A2。
4. 忽略持久化 B，调用 `SyncLegacyExtensionButtons()` 读取 UI2 开关并构建完整运行时列表。

设置页：

1. UI3 下取消组件导航的隐藏条件。
2. 继续显示现有分类和 toggle，不新增“已添加数量”。
3. toggle 变化时先更新 `setlist` 并调用 `WriteSetting()`。
4. 若 `useInkeys3UI`，随后调用 `SyncLegacyExtensionButtons()` 并请求主栏重新计算/渲染。
5. UI3 隐藏旧的“只能容纳 1 个组件”提示；UI2 分支不变。

## 兼容与回滚

- 不改变旧 JSON 字段及 `WriteSetting()` 行为，UI2 可直接回滚继续使用。
- 不删除新版 `ExtensionButtons` schema，只在 UI2/UI3 并行阶段绕过其运行时消费。
- 若同步失败，旧 UI2 配置仍是完整权威数据；重启后可再次投影。
