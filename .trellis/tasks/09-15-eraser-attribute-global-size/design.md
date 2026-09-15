# 设计

## 最小行为缺口与边界
当前普通擦除已是 ConfiguredEraser → ByEntry，五入口经 IdtState → ProductState → Host → WindowController 发布并由 RuntimeStroke 锁存。缺少的是全局基础尺寸/灵敏度、统一命令与 UI3 属性面板。

## 配置与模型
- 在 InputSettings 加稳定 BaseSize(16/32/64)、Sensitivity(低/中/高)；集中 ResolveSizes 与 SweepActionSpeed。持久化在已有 Drawing.Eraser 下。
- IdtState 用已有 eraserPreferencesMutex 整表更新/读取，单次 SyncDraw3State；写盘复用现有异步配置队列。混合态由 entries 推导。
- ResolveInput 消费全局尺寸和增益。固定/自动不互改 B，显式 Fixed/Speed 仍保留为底层强制旁路；属性操作在当前普通擦除发布 ConfiguredEraser。
- 中灵敏度增益1，低0.85、高1.15，只对高于fineToStandard的清扫动作进行增益；不改时间门、尺寸上限、输入单位或精细阈值。
- Touch面积报告不缩放，接受floor按当前尺寸范围夹取。活动接触沿用锁存 InputSettings；现有 SessionConfigCompatible 负责有效配置变化失效。

## UI与线程
- 新增小型 Bar.EraserAttribute 模块，依托同一 BarUISet/RenderPipeline/D2D context，复用 BarButton、背景/分割线、主题和光照函数。
- 纯 EraserAttributeLayout 统一计算预览、槽位、面板和菜单，所有几何在Bar逻辑单位求值；真实预览使用 DiameterToCanvasPx / frameZoom 补偿UI缩放。
- 位置参考已呈现 MainBar 与擦除按钮，沿已有 primaryBar/mainBar 规则选择方向、工作区夹取和组镜像；仅平移/透明度开合，圆本体不缩放。
- 输入发布面板状态，渲染线程更新资源。成功呈现后发布命中快照；隐藏区域不接输入；纳入窗口范围、dirty及光源范围。
- 独立主按钮/扩展热区；菜单内选择保留，外部属性点击先关菜单再执行一次。Escape沿已有Bar键盘路由。
- 清空直接 PublishProductCommand(Clear)，检查 CommandResult::Accepted；内容禁用使用真实 ProductRuntimeSnapshot，不用白板主栏强制显示标志。

## 预期修改文件
SpeedEraser.h/.cpp、IdtState.h/.cpp、Other.Config.cppm 与异步写盘暴露入口；Bar Main/State/ToggleClickCoalescer/Button/Interaction/RenderLoop 的有限接点及新属性模块；光标共享视觉辅助和必要 shader 引用；headless/hidden测试和必需工程登记。

## 兼容与回退
新键缺失/非法使用32/中，不统一旧入口；不修改Touch辅助保存值。保留二进制基线副本，构建后校验/恢复用户原文件。无新设备、Host或常驻线程；不用GUI确认编译代替视觉验收。

## 清空验收发现与必要补齐
原Clear在active.empty()后执行，保留此合法提交边界。旧Desktop/PPT Clear的Undo通过边界快照恢复，原来没有对应Redo；Whiteboard缺少回撤fallback。本轮在每页runtime加clearRedoAvailable，仅Clear恢复后置位，新笔迹提交取消，Redo回到既有Clear事务；Whiteboard沿用同一个边界快照fallback。页/场景迁移保留标记，磁盘旧区间恢复同样建立Redo。新增隐藏验证等待Up后清空、重复Up不回流、一次Undo/Redo、新墨迹取消Redo和白板回撤。未改输入队列或活动接触终止策略。
