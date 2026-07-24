# 颜色按钮描边样式设计

## Implementation Boundary

- 沿用 `BarUISetShapeEnum::DrawAttributeBar_ColorSelect1..11`、`BarUiShapeClass` 和原版 SVG 资源。
- 不新增枚举、状态、动画、渲染层、配置或公开接口。
- 不改变几何、布局、命中、预设颜色和输入处理。

## Selection State

- 色块初始化描边保持原版 `1px`。
- 颜色匹配时继续显示原版 SVG 勾，但描边目标由原版 `2px` 改为 `1px`。
- 取消选择时继续设置 `1px`，因此选中切换不会触发描边粗细动画。

## Theme

- `SwatchFrame` 继续作为 11 个色块共用的主题角色色，不在初始化点散落 RGB。
- 浅色主题使用 `RGB(176,176,176)`，向浅色 Surface 靠近以降低边框存在感。
- 深色主题使用 `RGB(80,80,80)`，向深色 Surface 靠近并保留黑色色块边界。

## Compatibility

- `Bar.Main.cppm`、`colorSelect.svg` 和工程文件不修改。
- 属性栏展开、收起、上下换边和点击切色继续使用原有路径。
