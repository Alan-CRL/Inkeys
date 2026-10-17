# 技术设计（已批准 A 方案）

## 基线与层边界

基线为 P2 2e3a4f26 及 UI 修正 384c5838。原 11 类已有 ToolSpec/PresetKind 与生产 Draw3 接线，新增类别尚无对应绘制能力；本任务只扩大 UI 目录，不能通过扩充业务枚举制造空实现。
类别、参数、属性、关闭按钮当前在同一面板容器内；目录数量变化后需要统一派生索引，避免 CircleCenter/SidesBegin/StyleBegin/CloseButton 和测试中的旧固定偏移冲突。只修改该面板必要声明与消费者，不重构其他主栏子树。

## UI 目录与可用性

在 Bar 层定义只读的形状目录描述：稳定的 UI 键、i18n 标签、SVG 资源键、可选的已实现 PresetKind。既有 11 类映射真实 PresetKind；新增 25 类无映射并标记禁用。UI 键不进入 ShapeObject/UInk 或业务状态。
目录是标签、资源、排列、选中与动作资格的唯一来源；只允许具备已实现映射的项进入 SetShapeToolPresetIfRevision。动作入口本身也检查资格，不能只依赖鼠标 hit 禁用。禁用项永不成为选中项，键盘导航跳过。
延用共享 UI3 disabled/主题与按钮内容机制；不增加自己绘图或逐帧资源加载。已确认 RetargetBarFixedButtonVisual 的 enabled=false 使用统一 0.30 内容透明度，Disable 经 ResolveBarButtonHitSnapshot/HitTestBarButtonHitSnapshot 投影为不可命中；Geometry 当前三个局部覆写（设置交互状态、visual enabled、发布 hit.enabled）均须接通同一目录资格，不能只改一处。圆类别按现有模式选择资源；正多边形轮播继续只作用于既有类别。

## 布局与动画

沿用 categoryList_ 与 GeometryGrid，类别目录 36 项按最多六列连续排列。下方 attributeList_ 保持独立，原有局部坐标重排、退场透明度、分隔项与同帧姿态映射不变。
面板目标高度由真实类别行数、参数区和属性区计算；沿用当前工作区 fit 与上下展开。不能仍保留两行高度，不能把关闭按钮或底部间距留在旧坐标。底部净距始终为 5 DIP × 当前面板 pose/fit scale。
已确认目录全量展开为 492.5×498.5 DIP，带参数区为 492.5×542 DIP。现有 fit 只约束整个工作区，不会给主栏预留独立的上下区域；460×420 工作区时整体 fit 约 0.8425/0.7749，类别约 58.98/54.24 DIP，属性约 27.38/25.18 DIP，且可能覆盖主栏。若再简单收紧为主栏上/下半区，可读性会进一步下降，不能以几何边界合格代替可操作性。

**用户已选择 A 方案，批准开始实施；B 仅保留为历史取舍记录：**

- A（建议）：正常空间仍六列全显；空间不足时仅图形类别 list 使用局部裁剪视口并垂直滚动，下方参数/属性和关闭入口保持可达。无分类导航或分页，不改变“更多”或其他菜单。使用现有 UI3 值动画、clip/pose、成功呈现命中；滚动范围由行高及视口确定，类别坐标通过统一局部偏移映射。支持鼠标滚轮和可拖动的滚动条供笔/触摸使用，键盘导航使已实现类别进入视口；禁用项保持占位并跳过执行。裁剪外/退场按钮不可命中。此局部交互扩展已获用户明确批准；现有 list 不预先假定具有完整滚动能力。
- B（最小改动）：维持当前整个面板 fit，不增加滚动，接受极小工作区中按钮/文字缩小以及主栏可能局部遮挡；必须保留面板关闭和既有功能的可达性，不能将该方案描述成已经解决可读性。

采用 A 时补充类别滚动范围、滚动期间输入、裁剪像素/命中、键盘可见性与触摸滚动条的离屏测试；具体视口尺寸由同帧主栏位置、可用展开侧及固定属性区计算。没有足够空间容纳一行时仍遵循既有极小工作区 fit；不承诺任意小尺寸均能保持原尺寸。两方案均禁止另起动画线程、独立绘制循环或扩大业务层。
禁用类别占据布局位置，但其可执行 hit.enabled=false；不以透明度为零隐藏，从而保持静态目录与位置稳定。

## SVG、资源与国际化

新增 25 份独立 SVG，名称采用现有 barShape*。函数图标使用可辨识的曲线轮廓；反比例与双曲线分别表达 y=k/x 与双支圆锥曲线，不能共用同一 SVG。空间图形使用可辨轮廓及细隐藏虚线。主描边沿用 2.6 与当前颜色占位语义，不使用位图。
登记 Inkeys.rc 的既有 UI 资源块；仅当当前工程显式列资源时按现有方式登记，不新增构建链。三语 GeometryAttributes 标签同链补齐，使用现有 i18n sync/check 生成头与快照，不手工维护生成映射。

## SVG 图意清单

| 类别 | 独立资源名（拟定） | 图意 |
| --- | --- | --- |
| 折线 | barShapePolyline | 不闭合的折线与顶点 |
| 多边形 | barShapePolygon | 闭合不规则轮廓，与正多边形区分 |
| 圆弧 | barShapeArc | 清晰的局部圆弧 |
| 扇形 | barShapeSector | 两半径加圆弧 |
| 角度 | barShapeAngle | 两射线加短角弧 |
| 平行线 | barShapeParallel | 两条平行粗线 |
| 垂直线 | barShapePerpendicular | 垂直线与直角标记 |
| 长方体 | barShapeCuboid | 长宽不等的透视盒，细虚线隐藏边 |
| 正方体 | barShapeCube | 等边透视立方体 |
| 圆柱 | barShapeCylinder | 顶部椭圆、侧边与底部圆弧 |
| 圆锥 | barShapeCone | 顶点、两侧线和椭圆底面 |
| 棱柱 | barShapePrism | 三角棱柱，区别于盒体 |
| 棱锥 | barShapePyramid | 多边底面与公共顶点 |
| 数轴 | barShapeNumberLine | 单轴、箭头与刻度 |
| 直角坐标系 | barShapeCartesian | 相交的两轴、正方向箭头与刻度 |
| 网格 | barShapeGrid | 均匀交错方格 |
| 坐标点 | barShapeCoordinatePoint | 简化坐标轴中的强调点 |
| 抛物线 | barShapeParabola | 开口抛物线 |
| 反比例 | barShapeReciprocal | 第一、第三象限的双支曲线 |
| 双曲线 | barShapeHyperbola | 水平开口双曲线，区别于反比例 |
| 正弦 | barShapeSine | 过原点的正弦周期轮廓 |
| 余弦 | barShapeCosine | 纵轴处波峰的余弦轮廓 |
| 正切 | barShapeTangent | 分段正切曲线，避开渐近线连接 |
| 指数 | barShapeExponential | 单调上升的指数轮廓 |
| 对数 | barShapeLogarithmic | 单调上升且变缓的对数轮廓 |

资源名需先检查与既有正多边形编号资源等不冲突；SVG 在实施时绘制并嵌入，当前不先行修改产品资源。

## 验证设计

扩展既有 Bar.GeometryAttribute.Test.cpp 的真实生产场景，不另建仅能画图的假面板。目录数、行数、标签、正式资源、像素可见性与禁用动作检查由目录描述驱动；保留既有 11 类工具和属性动态序列的独立断言，防止因泛化索引弱化回归。
对新增 25 类逐个验证绘制存在、禁用视觉存在、鼠标/键盘/动作入口不发布命令；失败呈现保留上次成功快照。覆盖三语/主题/缩放/上下展开/小工作区、资源重建、稳定休眠。现有宽度/状态/橡皮测试与 Headless 覆盖相邻子树。

## 兼容、风险与回退

不变更业务模型或保存格式，回退此 UI 目录/资源/i18n/test 补丁即可回到 11 类，原文档与绘制路径不受迁移影响。风险集中于目录扩容后的索引、面板高度、缩小后的可读性、禁用动作漏检和新增 SVG 在低 DPI 的抗锯齿。仅测试本机离屏不能代替 Win7/10 实机与真实笔触摸验收。

## 已实施接口与首轮反馈

生产修改限定在 Bar.Main.cppm 与 Bar.GeometryAttribute.cpp。新增 ShapeCategoryDescriptor/ShapeCategoryCatalog/ShapeCategoryLabel；Geometry* 常量统一派生 59 控件。Presentation 增加 categoryClip、scrollTrack/Thumb、scrollOffset/Maximum、categoryViewportHeight/ContentHeight、categoriesAcceptInput、below 及工作区事实 workArea。沿用 Pointer 接收已有滚轮链，不新增窗口消息入口。
滚动请求通过原子值由 worker 发布，render owner 使用共享 BarUiValueClass 推进，successful presentation 同批提交 clip/hits/workArea。原属性 list/透明度/5 DIP 合同保留。
首轮离屏仅发现工作区尺寸变化但轨道恰好未变时旧拖动仍继续。修复将同帧规范化 workArea 写入快照、changed 判定及 drag 资格，不修改测试期望。构建已再次通过，最终回归结果写入 validation.md。
