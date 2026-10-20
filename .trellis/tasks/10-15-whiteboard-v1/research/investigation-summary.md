# 白板 UI3 实现与卡顿调查（只读）

日期：2026-10-15。代码基线 d4d42869，工作区产品代码无改动。调查不启动GUI、不操作用户窗口、不注入消息、不构建、不提交或结束任务。

## 结论

当前白板仅真正复用了底层UI3 metrics、按钮绘制/反馈、SVG和属性动画数学；没有复用主栏绘制属性完整的节点编排、继承布局、分组透明度合成及脏区绘制管线。此前将底层复用描述为完整统一不准确。反复通过局部字段/分支补齐规范，形成独立Scene适配层，是重复出现布局、文字槽和生命周期偏差的结构性原因。

日志确认实际昂贵回调，能解释主栏被连带拖慢；具体D2D操作的耗时归属尚未完成采样，不把圆角缓存直接定为主因。

## 运行证据

来自 Build/ARM64/Debug/log/idt1792070038504.log，时间21:13:58—21:15:00。本次只读进程查询未取得匹配的运行实例，因此没有即时CPU/FPS轨迹。现有日志是50ms阈值的异常汇总，不是连续帧率数据。

- 第34行（21:14:34.484）：PptBottomLeft目标1244×856，26次回调累计916.541ms，单次最大104.363ms；GetDC累计794.385ms/最大95.722ms，记录Draw累计30.308ms/最大5.220ms。
- 第35行：43次回调累计541.860ms，其中GetDC435.010ms。说明不是只有进入白板的一次冷启动尖峰。
- 以上慢样本圆角遮罩缓存分别26/43/48次命中、零miss/零创建。不能用“每帧创建圆角缓存”解释这些慢样本。普通预览clip与renderer柔光mask是两套不同缓存。
- 调度器串行执行客户端，慢分页回调会推迟下一轮Bar。GetDC会暴露排队绘制完成/同步开销，不能仅凭API计时归为纯GDI问题。
- 第45/47行另有Settings590.499ms回调及随后Bar599.795ms间隔，是独立阻塞事件，不能全部归给白板。

数值提取见 runtime-ui3-observations.json；原日志只读，未修改。

## 已证实的管线差异

1. 主栏RenderLoop在BeginDraw后PushFrameDirtyClip，再Clear/绘制。PageControl先Clear整target、Render全部可见widget，之后才读取PendingDamage用于ULW；呈现窗口crop没有减少这部分D2D全量绘制。
2. 高水位backing保留是合理资源策略，但全量Clear/绘制让展开后的大缓冲继续承担hover/光源更新成本。
3. Scene将父opacity传播给子项后，再按widget各自PushLayer；主栏橡皮属性按整组面板合成。主题预览又有额外圆角mask层。额外合成成本明确存在，具体耗时需测量。
4. Scene设置pose缩放却未接主栏的inverse diffuse geometry scale和frame dirty clip/exact-mask生命周期，是另一处复用不完整，尚未证明是本次首要耗时。
5. 各renderer独立持有device context/target，未发现多Scene交替使用同一个context导致每帧重建。

## 历史故障和缓存的解释

- 原文字消失：把标准2×2标签填到secondaryText，标准metrics把secondary字号设为0；属于调用方违反共享接口。
- 面板消失：把隐藏普通按钮实例转换为panel/divider，初始化与hover清理顺序互相覆盖；暴露了用按钮flags模拟其他节点的生命周期问题。
- 白色预览上白字不可见：自绘背景与共享前景色未作为同一主题语义处理。
- ThemeDrawingResources缓存brush和roundedgeometry，是对自绘回调的资源复用，不是UI3动画必要的新概念，也不能替代整组透明度合成或脏区裁剪。BuildWidgets重新产生callback/spec时缓存也可能随之替换，不能声称跨菜单开关永久稳定。

## 之前的验证为什么没挡住

单Scene离屏像素/虚拟时间比较能验证终态与数学pose，不能验证多客户端真实调度吞吐、GDI同步/ULW成本和主栏是否被饿住。小renderer benchmark没有性能预算；scheduler测试的>=15ms是限制最高帧率，不能证明最低帧率。此前零失败记录是真实的，但“完整复用/性能已统一”的结论超出了证据。

## 后续修复应遵循的顺序（本轮不实施）

1. 先用现有实际运行诊断建立Bar+左右分页共同运行基线，并补足分页resources/图层/完整提交阶段的证据，区分录制Draw和WARP执行/同步。
2. 对齐主栏完整绘制事务：先算本帧dirty/viewport再裁剪Clear及draw、按面板子树合成opacity、稳定尺度缓存和现有提交/休眠合同；不是继续单改曲线数值。
3. 把标准操作行、弹出层级及布局定义提升为真正共享的UI入口，白板仅提供业务内容与锚点，避免PageControl/Scene各自硬编码编排。
4. 性能验收必须覆盖主栏和分页共同活动、菜单开合/悬停/静置后的真实提交间隔与帧时间；保留现有像素/命中测试作为功能验证。

详证见 control-architecture-audit.md、render-hotpath-audit.md、verification-gap-audit.md。
