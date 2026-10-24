# 技术方案

## 边界与依据
- 只覆盖UI3主栏、属性面板及共用Bar renderer的PPT/白板分页控件；设置ImGui字体和启动预览不改。
- 调查见research/font-rendering-chain.md、font-setting-and-config.md、platform-text-rendering.md。
- Word已合并word.size、frameZoom和contentScale，但按钮pressScale、底栏bodyTransform和popupGeometryScale仍由D2D矩阵施加；因此不能只floor设计DIP或GetFormat参数。

## 字号策略
- 在最终文字绘制入口解析有效字体高度像素比，合并D2D当前变换Y轴长度及有效单位/DPI；已手乘的frameZoom不重复乘显示器DPI。
- 关闭开关：最终px字号=floor(原字号×有效像素比)，然后反算为DWrite字号。保留字体大小外的所有几何和变换；不使用round，不提高到1px。结果为0时跳过文字绘制。
- 开启开关：保留原字号及既有行为。非法/非正字号或不可绘制的变换不传给DWrite。
- 用小型纯数值helper供生产和Headless测试共用，D2D包装留在BarUIRendering。Word、独立细刻度标签、调试帧率文字等实际文字路径统一接入。
- MeasureText保留现有未截断上界，避免动态字号使提示框/面板重排；细刻度本身以最终格式测量并布局。现有文字格式缓存按解析后的实际字号区分；细刻度缓存包含实际格式字号或由owner正确失效。

## 配置与刷新
- 在既有UI3实验容器新增Experimental.Inkeys3.UI3.AllowFractionalFontSize，IdtAtomic<bool>，默认false，NoUpload；复用严格bool codec、默认/复制/重置机制，无旧字段迁移。
- 设置只新增实验卡与三语标题/说明，补足父容器高度，复用QueueConfigWrite冻结FIFO写入main.json。
- 变化后轻请求主栏和四分页；各render owner捕获/比较策略，变化产生实际damage并更新字体缓存，不能只唤醒后仍走idle早退。隐藏再显示也复核当前策略。
- 所有DWrite资源失效在render owner发生，不在设置回调持锁操作跨客户端资源。

## 取舍、兼容与回滚
- 只截断字号，不切换抗锯齿模式或强制字形坐标取整；透明目标保持原合成方式。
- 极小/内容过渡动画可短暂无字（量化为0px）；不为避免此现象反向放大字号。
- 字号变化不能保证消除全部设备模糊；最终观感需用户在实际大屏复核。
- 实验开关开启即可恢复原字号策略。源码回滚仅涉及本任务字体策略、配置、设置及对应测试/资源；不修改工程依赖、SDK或第三方库。
