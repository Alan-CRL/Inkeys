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
- Divider 继续作为运行时交界对象注入，但视觉只绘制其 Shape：宽高 `1x35` DIP、圆角 `0.5`、SurfaceFrame 填充透明度 `0.30`，同时保留 `oneTwo` 的两行布局占用；不加载或绘制 SVG，PointLight 关闭主光并复用几何分隔线 `0.30` 的第三鼠标光强度。
- Divider 复用上一组尾端已有的 `5` DIP 间隙，不推进主栏宽度。布局遇到 Divider 时先封闭未填满的小按钮列，把线放在间隙中心，再从新列开始下一组；最终横坐标继续走统一镜像函数。
- 更多快照整体替换并与主栏列表共享注册表持有的 `shared_ptr`；硬编码 More 由按钮集合长期持有，不注册、不持久化。

## Popup Layout And Rendering

- 使用 2x2 子网格打包：70x70 单元由四个 32.5x32.5 子格和 5 DIP 间距组成；尺寸占格规则与主栏一致，保持输入顺序。
- 两组独立打包。列数按总单元数 `min(5, ceil(sqrt(n)))` 计算；分隔线使下一组从新行开始，不允许跨线共享单元。
- 显式更多组在远端，强制溢出组靠近主栏；每组末尾为最新项。面板向上/向下展开时只翻转物理行放置。
- 根面板、关闭按钮和分割线复用几何面板的 Surface/PointLight/动画风格；根面板关闭时以 More 按钮中心为 60×30 紧凑态，使用绘制属性面板相同的默认操作时长，几何沿上下方向使用 Back、透明度使用 Sine，并先于主栏绘制以从主栏下层出现。关闭按钮放在按钮网格右侧 35 DIP 窄栏的右上角，不额外增加顶部高度；X 复用独立按钮的悬停填充与按下缩放；分割线跨过 X 侧栏并保持左右等距内边距；保留 5 DIP 网格间距和主栏锚点净空，并限制到显示器工作区。
- 浮层子内容以完整面板中心和同一个比例缩放。按钮位置始终保存为主栏局部坐标：展开时由面板中心换算，收起时缩在 More 入口下方，使按钮从 More 补位到主栏时沿用相邻起点。
- More SVG 三角略微缩小并保持固定朝向；展开状态由 More 入口的 Selected 状态和青色高亮表达。浮层网格保持 5 DIP 单元间距，主栏锚点保留独立 12 DIP 净空，并使用未截断的 Back 进度驱动面板几何以保留回弹。

## Interaction And Compatibility

- `moreExpanded` 是独立目标状态；渲染循环将其映射到硬编码 More 的 Selected 视觉态。点击 More 切换并关闭绘制属性、几何及其子浮层。
- 浮层完全隐藏时直接同步内部按钮的填充、边框、图标和文字颜色，使 Selected 青色在下次展开前已经落稳。
- 打开绘制属性或几何、折叠主栏、点击外部或 X 时关闭 More；外部点击关闭后继续原消息处理。
- 浮层实体按钮复用现有按钮状态机和点击回调。`closeMoreAfterAction` 为 true 时先关闭再执行回调，false 时保持打开。
- 主栏 Divider 在悬停推进、指针扫描和点击/按压命中入口均显式跳过；布局/动画推进会清除遗留的 hover、pressed 与缩放状态，但不借由禁用 Shape 或清零 `frameLightPct` 来关闭第三光。
- 设备 epoch 重建沿用注册按钮及新增 More SVG 的现有缓存清理路径，不创建独立 D2D device/context。

## Rollback

所有行为局限于新配置 ID、Bar 按钮集合、Bar 状态/渲染和两个 SVG 资源登记点。若运行时布局不稳定，可独立回退 More 注入和浮层状态，同时保留配置默认迁移；不得回退或覆盖用户无关改动。
