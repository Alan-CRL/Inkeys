# 方案B：本轮设计与改动边界

基线af22b4a6，feature/eraser；用户确认可直接实现。主Agent顺序执行。原validation.md和既有截图/测试日志保持历史，新增scheme-b记录及Build/eraser-b输出。

## 行为缺口
当前为16/32/64、清空靠左、尺寸方底座/胶囊、半透明圆、拆分底色与单纯淡入位移。目标为24/32/40不透明圆选项 | 居中清空 | 一体自动按钮；菜单收为标题/齿轮和三段选项；两个surface使用DrawAttribute同款回弹。

## 几何与契约
- 中轴固定清空，左右外部分配宽=max(圆组需求,自动整体需求)，对称分割线和共享按钮/间距Metrics。默认三圆边缘间距16 Bar逻辑单位，约358逻辑单位宽；不能用70方槽凑对称。
- 清空/主面板/主栏擦除中心正常重合，工作区不足整体避让；菜单对齐完整自动按钮中心，独立避让。倒转只交换侧组，不反转图标/文字及组内内容。
- 纯layout给出真实圆、有限命中区、内容裁剪和自动整体矩形。真实DIP通过DiameterToCanvasPx/frameZoom转换；稳定pose严格为1。
- 提取面板局部motion辅助，复用BarUiValueClass/BarUiTimelineClass、EaseOutBack/EaseInBack与独立Sine透明度。仅状态变更Retarget；反向从当前值开始；按已有半程规则加入父时间线。父panel的pose复合子menu的pose，子锚点跟随父当前自动按钮。Bounds覆盖实际pose与光影；pose统一正向解析成当前Bar坐标的几何，绘制与命中共用成功呈现快照，窗口坐标仅除该帧zoom。SVG使用contentScale复用缓存，不额外给D2D叠加父缩放或重置全局光源状态机。
- 尺寸选项删除矩形背景/胶囊，白色实体、灰纹；选中轮廓向内加粗并走主题Accent PointLight。共享ERASER_GRIP_OPACITY不改，画布Hover不变。
- 自动只用一个BarButtonClass作视觉（包括按压/hover/frame）；保留body/arrow两个动作区并锁存Down归属。专用barAutoEraser.svg采用原斜橡皮语汇加路径A。菜单仅两行，齿轮禁用。
- 开栏原Up没有panel Down票据，不触发清空；复用原Touch兼容Mouse过滤与取消规则，不改画布输入队列。

## 配置
BaseSize=24/32/40，集中预设表；Restore按16→24、64→40、32不变、24/40幂等、非法→32。ResolveSizes继续B/2,B,5B,Touch=B/2,Fixed=B。五入口、penResponse、敏感度增益、Touch面积测量/保护及非Touch控制器算法不改。Clear事务完全保留。

## 文件边界
SpeedEraser.h/.cpp仅预设/迁移；EraserAttributeLayout、Main内局部声明、新motion模块、EraserAttribute绘制/交互、RenderLoop接点；新SVG与现有rc/vcxproj/filters登记；现有headless/offscreen/hidden测试。任务及本功能spec更新。保持编码/BOM和各原有行尾，不提交/推送/归档。

## 顺序与验收
1. 先补预设迁移、方案B中心/间距/紧凑菜单断言，运行旧实现得到失败证据。
2. 实现配置与几何，再接生产motion、圆和整体按钮。
3. 生产offscreen增加多帧采样、像素检查、反向/父子关闭/速度与禁用动画、所有权和跨区点击测试；保留未变更回归。
4. 当前ARM64 MSBuild完整InkeysRepo.sln Debug|ARM64；--no-window、--bar-eraser-offscreen-test、--draw3-hidden-test、--draw3-eraser-hidden-test全部重新执行。
5. 查看深浅色/状态/多帧真实组件图片，记录实际通过与实机GUI未覆盖项。
