# Implementation Plan

1. 更新配置合同：增加 `MoreBoundary`、默认 Extension 序列，移除 A2 的 Setting，并扩展 B 区规范化对官方注册实体/标识的支持。
2. 扩展按钮注册和运行时投影：注册类型、关闭元数据、旧开关栈顺序、B 区两按钮容量、更多分组快照及硬编码 More 按钮。
3. 抽取/复用 2x2 子网格打包，增加 More 面板布局、上下方向和最多五列的尺寸计算。
4. 增加 More 面板状态、动画、绘制、命中、关闭和互斥逻辑；保持主栏左右换边不改变内容顺序。
5. 新增 `barMore.svg` 并登记资源，复用 `barCloseSmall`；补齐设备资源重建时的缓存清理。
6. 更新 `.trellis/spec/native-desktop/configuration-i18n-and-assets.md` 的 UI3 Bar 合同。
7. 检查配置迁移、旧开关顺序、分隔线条件、混合尺寸布局、上下/左右方向、关闭路径和快速动画切换。
8. 运行 `git diff --check`，使用 ARM64 host MSBuild 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64`，超时至少五分钟。

## Implementation Notes

- 配置新增 `Inkeys.Bar.MoreBoundary`；默认 `ExtensionButtons` 为 `MoreBoundary + Setting`，A2 默认/required 仅 `Pierce + Freeze`。UI3 运行时仍忽略持久化 `ExtensionButtons`，只投影旧组件开关。
- 按钮注册区分实体与无实体布局标识；旧组件活动顺序保持首次注册顺序，关闭移除、重新启用追加。主栏 B 固定最多两个普通按钮，More 入口硬编码且不持久化；快照拆分 `explicitMore` 与 `forcedOverflow`。
- More 面板复用 UI3 Surface/PointLight/关闭按钮和绘制属性的默认动画时长，几何 Back 与透明度 Sine 分离；标准单元 70 DIP、最多五列；显式组远端、强制组近端，分割线只在两组同时存在时绘制。主栏左右换边不反转逻辑顺序，上下方向只翻转物理行。
- More 交互覆盖外部点击继续分派、面板消费、右上角 X 悬停填充与按压缩放、拖出取消/抬起关闭、按钮动作前关闭及 `closeMoreAfterAction=false` 保持打开；隐藏时直接同步内部 Selected 颜色，打开 More 会关闭绘制提示、颜色选择器、粗细滑块和几何面板。三角 SVG 保持固定朝向，入口改由 Selected 状态和青色高亮表达展开。

## Verification

- `git diff --check`：已通过；所有原为 CRLF 的修改文件均已恢复 CRLF。
- ARM64 host MSBuild：`InkeysRepo.sln`，`Debug|ARM64`，最终使用 `/m:1` 已通过（仅仓库既有窄化/模块片段警告）。并行增量构建曾因 `.ifc` 输出文件占用返回 C3474，不是源码诊断。
- 未运行自动化 UI 测试；仍需在真实 UI 中手工检查折叠、上下展开、显示器边界钳制、快速连续切换和按钮动作。

## Risk Gates

- 修改 `Load()` 后先检查主栏列表对象所有权，避免注册表对象被重复释放或同一对象同时出现在主栏和浮层。
- 修改 `Bar.Main` 时逐段核对布局、绘制、命中和窗口区域，任何一层缺项都先停止继续扩展。
- 新增资源后核对 `.rc`、磁盘文件和缓存清理三处；不修改 vcxproj，SVG 继续通过 Win32 UI resource 嵌入。
- 构建失败时只修复本任务引入的问题，不清理仓库既有警告或无关代码。
