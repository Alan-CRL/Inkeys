# 第六轮基线检查

- HEAD b2b6c2c4，feature/shape；检查时工作树干净（创建任务前）。
- origin/dev c754e20020b9e0ece44580f3851ddf9467575302，已 fetch，未 merge。
- 上游最新白板漫游修复含 dc319ef9、b861d464、c2f21bd0。读取了上游 .trellis/spec/native-desktop/canvas-pan.md：当前产品平移，异步多指只更新真实上报指头、增减指重置 focus 防跳变。PTP DM 因 RTS 冲突被隔离，本轮不重新启用。
- Bar.ShapeEditor.cpp:2141 的 Pointer 入口只有 message/POINT/capture/presentation；2185 起编辑 Move/Up 直接发送 CanvasPoint 与真实 EditSession 的 angleDelta；当前没有该入口的多指信息及独立预测输入。
- Bar.GeometryAttribute.cpp:337 起 circle/polygon/triangle/rightSolid 等只取当前 tool；requestedCategory 到349才解析，595起 wanted 仍未用分类过滤参数，所以跨分类留下圆形参数有直接代码依据。
- 同文件359/636起已按字体/语言/设备代次测字；650附近 TransitionContent 复用于正棱柱与梯形槽位，首次字号变化须追到实际字体布局/转换链，暂不把此推断写作已确定根因。
- 原文档与本会话已包含前五轮决策，没有额外调用历史全文搜索。
