# Technical Design

## Architecture

配置层声明 `MoreBoundary` 和新默认值；按钮集合层拥有注册类型、旧开关顺序、主栏列表与更多分组快照；`Bar.Main` 负责浮层几何、动画、绘制和命中。当前运行时只把旧开关投影成临时布局序列，不消费持久化 B 区。

## Registration And Configuration

- 增加 `BarButtonRegistrationKindEnum::{EntityButton, LayoutMarker}`；实体按钮必须有对象，布局标识必须无对象。
- `RegisterButton` 保持实体按钮入口并增加 `closeMoreAfterAction` 元数据；增加只供内部使用的 `RegisterLayoutMarker`。
- 官方 `Setting` 注册到 Extension，`MoreBoundary` 注册为 Extension 单例布局标识；插件仍不得占用 `Inkeys.` 前缀。
- `NormalizeExtensionZone` 先识别已注册标识或实体，再处理未知插件 ID。标识忽略 Visible/Size 的展示含义并输出规范默认值；未出现时不自动补齐。
- 缺失字段使用 `[MoreBoundary, Setting]` 默认值；现有字段在 UI2/UI3 并行期不由 Bar `Load()` 读取或写回。

## Runtime Projection

- `BarButtomSetClass` 保存旧组件活动顺序。首次同步按注册顺序建立；后续保留仍启用项顺序，删除关闭项，并把新启用/重新启用项追加到末尾。
- 通用规划器读取带可选标识的条目：标识前前两个可见实体进入主栏，剩余进入 `forcedOverflow`；标识后进入 `explicitMore`。
- 当前 `Load()` 输入固定为活动旧组件、虚拟 `MoreBoundary`、`Setting`。主栏输出为 A1、A1|B Divider、最多两个 B 实体、硬编码 More、B|A2 Divider、A2。
- 更多快照整体替换并与主栏列表共享注册表持有的 `shared_ptr`；硬编码 More 由按钮集合长期持有，不注册、不持久化。

## Popup Layout And Rendering

- 使用 2x2 子网格打包：70x70 单元由四个 32.5x32.5 子格和 5 DIP 间距组成；尺寸占格规则与主栏一致，保持输入顺序。
- 两组独立打包。列数按总单元数 `min(5, ceil(sqrt(n)))` 计算；分隔线使下一组从新行开始，不允许跨线共享单元。
- 显式更多组在远端，强制溢出组靠近主栏；每组末尾为最新项。面板向上/向下展开时只翻转物理行放置。
- 根面板、关闭按钮和分割线复用几何面板的 Surface/PointLight/动画风格；以 More 按钮中心锚定，保留 5 DIP 间距并限制到显示器工作区。
- More SVG 只旋转三角图形。状态切换把目标角度累加 180 度；使用现有 Cubic/Back 动画曲线同步完成收缩、轻微放大和回落，动画静止后归一化整周角度。

## Interaction And Compatibility

- `moreExpanded` 是独立目标状态，不映射 Selected。点击 More 切换并关闭绘制属性、几何及其子浮层。
- 打开绘制属性或几何、折叠主栏、点击外部或 X 时关闭 More；外部点击关闭后继续原消息处理。
- 浮层实体按钮复用现有按钮状态机和点击回调。`closeMoreAfterAction` 为 true 时先关闭再执行回调，false 时保持打开。
- 设备 epoch 重建沿用注册按钮及新增 More SVG 的现有缓存清理路径，不创建独立 D2D device/context。

## Rollback

所有行为局限于新配置 ID、Bar 按钮集合、Bar 状态/渲染和两个 SVG 资源登记点。若运行时布局不稳定，可独立回退 More 注入和浮层状态，同时保留配置默认迁移；不得回退或覆盖用户无关改动。
