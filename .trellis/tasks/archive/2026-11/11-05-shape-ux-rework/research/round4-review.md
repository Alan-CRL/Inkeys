# 第四轮实现与审查记录

基线 e8eb2f1b。用户批准的12项方案已实施，父任务保持in_progress；本轮不提交或推送。

## 修改边界
- 共享核心：平行四边形水平首笔/局部斜基坐标拖角、箱体底面外接框内接。没有修改存储格式、枚举、shader或完成后立体编辑入口。
- 编辑器：普通圆点材质的20DIP旋转/移动胶囊、四侧停靠、旋转/显示命中一致、粗角标、短轴镜像焦点、青色镜像双杠。
- 构造提示：叉左勾右，窄屏双列保持同排，键盘沿视觉顺序。原动作索引不变。

## 裁切证据
SelectionOverlay每次BeginFrameDraw覆盖完整窗口，ULW没有prcDirty，并非局部上传脏矩形。原clearMainBar把整个Bar HWND（含透明留白）清空，造成附近控件缺失；删除后依靠Window Service既有Z序让实际Bar覆盖编辑层。另一处窗口范围只含新snapshot handles，退场HandleVisual仍绘制，撤回远端顶点时范围提前收缩。新增离屏用例检查退场圆点四向像素与完整包络。粗角标额外覆盖半线宽和AA。

## 交叉审查修正
1. Rotate沿用UI偏移映射回原顶部柄会改变角度计算中心；改用实际显示指针并验证侧边90度轨迹。
2. 胶囊必须复用普通thumb材质公式、16DIP图标及一次整体opacity合成。首轮编辑器1289条同一像素断言发现填充不一致，按真实材质修复，没有删除断言。
3. 原外框角点1DIP窗口余量不足4/5DIP角标半线宽；改为2.5*scale+1px并验证大圆角落。
4. 新旋转回归临时替换actionSink，漏恢复导致case276后续六条动作断言失败；只修复测试夹具，保留原动作断言。

## 窗口归属与验收边界
外框/控制点/样式栏/子面板在SelectionOverlay（标题Inkeys Shape Editor）透明HWND，使用共享UI RenderPipeline/D2D并经UpdateLayeredWindowIndirect呈现。主栏与构造提示在Bar，图形本体在Draw3画布。Window.cpp在首次Show及窗口组排序时明确让编辑层位于Bar下方。

现有隐藏窗口fixture验证Host/input/actions，不验证真实可见重叠窗口的WindowFromPoint或操作系统输入投递；这些连同触控手感、动画观感仍待用户实机验收。没有启动交互式GUI或Computer Use。
