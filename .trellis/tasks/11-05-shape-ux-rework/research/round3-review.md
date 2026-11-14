# 第三轮跨层审查

使用 trellis-check，结合第三轮PRD/design/implement和native/native-desktop shapes规范审查当前完整diff。构建与自动测试由root集中执行，本审查未启动GUI或重复构建。

## 已确认与修正

- 私有协议统一v7，精简SolidConstruction、旧箭头和开发期迁移路径删除同步。未知/旧开发版本走标准几何降级并报告UnsupportedVersion；v7损坏仍拒绝。复合部件计数保留闭合虚线8分段规则，标准直线矩形读取仍存在。
- 构造点编辑采用独立Begin/Preview/Commit/Cancel动作，Down票据和session/object/scene贯穿；预览合并不跨终结屏障，Up携最终位置。运行期禁止普通画布contact与pointGesture并行。
- 图四的参数重叠已修为约束在前、边数在后；prism/pyramid只显示3–8，正多边形3–14。会话状态分成三份均6且不写启动配置。
- 检查发现平行四边形固定模板系数未保留原单位模板比例，已修为q=-0.125d+0.375perp(d)，并更新核心预期。
- 检查发现Circle/RegularPolygon/Arc/Sector没有旧Rotate句柄，自动胶囊漏掉这些外框图形。经root确认后启用这些平面Rotate，核心句柄矩阵同步；立体仍仅Move。
- 旧核心测试错误假设向下圆锥也必须有隐藏底环和至少4部件；固定上视角下允许完整实环+2轮廓线，已按几何意义修正。Cuboid旋转外框测试改等比缩放平移，因为新规则禁二维旋转。

## 已协调的交付门槛

- DrawEditorInputCoverage对对象bounds铺1alpha、退场控件仍有alpha，已确认存在覆盖主栏Undo/几何入口的缺陷。UI负责主栏矩形Hit排除及Draw末尾清alpha，并增加真实像素/命中回归；不能只用Hit返回未占用代替跨线程窗口输入穿透。
- 几何入口未关闭Recall会使属性面板每帧被关闭，UI已定位并处理。完成后Undo立即可用由runtime用现有无窗口完整Host/Run测试验证，不能仅依赖predicate测试。
- 等腰双斜杠当前只在编辑层显示；首笔Triangle走普通活动预览，需补瞬态绘制提示链，禁止把符号加进保存/导出的ShapeObject。已报root协调实现。
- root确认辅助点隐藏时保留原圆心/焦点符号、显示时四向箭头，沿用第二轮切换合同，仅替换手形。主栏覆盖修正不单独作为全部Undo延迟根因结论，仍等待完整SF时序验证。

最终构建、逐帧、存储及这些门槛完成状态以root最终验证记录为准；本记录不将尚在进行的修复声明为通过。

## 最终复核（2026-11-14）

- 主栏覆盖修复已包含同一屏幕矩形的Hit排除与Draw末尾alpha清零，含退场与弹层。离屏测试验证不透明控件恰与主栏重叠时，输出像素alpha为0且命中拒绝。
- 完成/构造历史可用性改为读取当前会话，与Undo/Redo命令分派一致。publishHistoryAvailability只通知observer并同步白板副本，不修改文档revision、对象或保存数据。root报告最新完整SF测试均0失败，桌面/白板立即Undo约10–11ms；不把单独覆盖修复等同全部时延结论。
- 首拖等腰提示现通过drawingPreview只读快照传递：只复制活动对象及3个顶点，UI不生成控件、样式栏或输入coverage，绘制双斜杠后返回，未改变ShapeObject/存储几何。Window.cpp的CreateGroup(false,threadId)把Drawpad、Bar、SelectionOverlay放在相同窗口owner线程，WM_NCHITTEST的HTTRANSPARENT可透至画布。
- 最终审查发现首拖→编辑的缓存漏洞：published快照若与最终Up对象相等，原复用条件会复用drawingPreview的3点及空frameRect，漏外框与旋转柄。已交runtime在缓存复用条件排除drawingPreview，并补最后Move与Up同坐标的SF47回归；此修复最终验证由root统一记录。
- 本次复核没有发现drawingPreview写入持久化、额外历史提交或publishHistoryAvailability递归发布的问题。构造中已有接触的可用性沿既有命令优先处理构造的规则，未扩展到普通笔历史。

收口结果：缓存复用已排除drawingPreview，同坐标Move/Up的SF47通过；最终UI两项矩阵均0失败，完整记录见round3-verification.md。
