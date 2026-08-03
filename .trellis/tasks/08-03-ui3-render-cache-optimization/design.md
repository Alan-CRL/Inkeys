# UI3 渲染缓存与 SVG 旋转设计

## Architecture

### SVG

`BarUiSVGClass` 保留 SVG 文本、目标尺寸和颜色状态，并新增角度值及缓存元数据。缓存分为两层概念：

1. 内容层：当前 SVG 字符串与语义颜色组合；内容/颜色变化时清空 raster bitmap。
2. Raster 层：按一个稳定的、设备相关的基准像素尺寸生成 `ID2D1Bitmap`。绘制时的 `contentScale`、按压缩放、强调缩放和角度只影响目标矩形或 D2D transform。

为避免尺寸动画中逐帧重建，渲染函数维护“当前缓存尺寸”和“请求尺寸”的比较：

- 缓存缺失或内容/颜色/设备代际不匹配：立即生成。
- 请求尺寸不超过缓存尺寸的质量阈值：复用 bitmap，通过 `DrawBitmap` 插值缩放。
- 请求尺寸显著大于缓存尺寸：标记待更新；动画进行中按节流/阶段策略至多生成一次，动画结束的首个稳定帧补齐最终尺寸。
- 缩小不触发重建；避免为缩小动画创建更小位图。

实现必须保留当前中点替换语义：`ApplyContentDirect()` 只在内容切换中点或最终收尾提交内容，并使内容缓存失效一次。`contentScale/contentPct` 变化本身不失效缓存。

### SVG rotation

仿照 `BarUIRendering::Png()`：计算未旋转的 `tarX/tarY/tarW/tarH`，保存 `ID2D1_MATRIX_3X2_F originalTransform`，在需要时设置以目标矩形中心为 pivot 的 `Matrix3x2F::Rotation(angle) * originalTransform`，绘制缓存位图后恢复原 transform。角度清洗为有限值并归一化判断是否需要设置 transform。`w/h`、继承坐标和缓存尺寸均保持未旋转值。

SVG 的 `GetWeigetRect`/脏区策略应与现有 PNG 规则对齐：若采用旋转包围盒，则只扩张脏区，不缩小原布局范围；命中测试继续使用未旋转布局矩形，避免改变现有按钮输入契约。

### Superellipse

在 `BarUIRendering` 内增加一个单槽路径缓存，保存：

- `ID2D1PathGeometry`（局部原点坐标，宽高对应有效像素尺寸）；
- 有效宽度/高度、`n`、分段数、必要的缩放/DPI 量；
- 设备代际通过 `DiscardDeviceResources()` 统一清理。

路径生成逻辑从 `Superellipse()` 提取为内部 helper，只用局部坐标生成 `pts/beziers` 并写入 geometry。绘制时通过平移矩阵得到当前屏幕位置：优先使用 `ID2D1TransformedGeometry` 或在 DC 上设置局部平移 transform，并在调用 PointLight、填充、边框、clip 和 bounds 查询时传入同一平移后的几何。若创建变换几何失败，回退为当前帧直接生成的路径。

缓存键不含 `inh.x/inh.y`；因此面板/主栏移动、换边动画和光源移动只复用形状路径。缓存键必须包含分段策略，保证宽高改变导致的采样精度变化不会误用旧路径。

## Data Flow

1. UI3 布局更新 `x/y/w/h/n/contentScale/angle`。
2. `Svg()` 读取未旋转布局尺寸，执行缓存键/质量阈值判断，必要时调用 `CacheBitmap()`，随后应用 content 目标矩形和 angle transform。
3. `Superellipse()` 读取有效尺寸与 `n`，命中局部 path cache 后只创建/使用平移视图，继续原有填充、frame、PointLight、clip 和 targetRect 更新。
4. 设备代际变化调用 `DiscardDeviceResources()`，释放 SVG 位图、超椭圆路径及可能的 transformed geometry，不释放可重新计算的文本/数值状态。

## Internal Contracts

- `BarUiSVGClass::angle` 默认值为 `0`，与 `BarUiPNGClass::angle` 使用相同角度单位和有限值处理。
- `CacheBitmap()` 的公开可见性和现有调用签名尽量保持；新增缓存元数据/辅助方法为 UI3 Bar 内部实现，不增加对外 API。
- 路径缓存 helper 必须返回可用于 `DrawGeometry`、`FillGeometry`、`GetBounds` 和 PointLight 的几何对象，且对象属于当前 D2D factory/device。
- 所有缓存字段的失效入口集中在 `ResetCache()` / `DiscardDeviceResources()`，内容变化不得遗留旧颜色或旧 device 资源。

## Trade-offs

- 缩放动画期间复用较低分辨率位图会有短暂插值模糊，但避免了逐帧 SVG 解析和上传；最终稳定帧补建保证待命质量。
- 仅缓存单个超椭圆形状可控制资源占用，未来若控件并发量增加再扩展为小型 LRU；本任务不引入复杂缓存管理器。
- 先保留颜色烘焙，避免改变 SVG 着色管线；颜色动态 GPU tint 作为后续优化。

## Device And Rollback

- 新缓存资源创建失败时沿用现有直接绘制/重建路径，不能让 Bar 整体空白。
- 任何 `D2DERR_RECREATE_TARGET` 或设备 epoch 改变都必须清理新旧缓存并在下一帧懒重建。
- 回滚点集中在 SVG 缓存判断、SVG 旋转 transform 和 Superellipse helper；不改变 Draw2 状态机或资源文件。
