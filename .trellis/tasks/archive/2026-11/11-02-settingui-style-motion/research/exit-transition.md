# 旧页淡出与实际路由提交（2026-11-03）

## 修改边界

此轮按用户明确的同任务细化要求实现：旧页先淡出，才提交新页；位置/透明度分离及减速由主代理修改Controls时序。未改变渲染管线、字体、配置、资源或窗口平台能力；未运行GUI/commit。

## 生产路径

- 新内部值状态 `Setting.PageTransition.h`：`PageRoute{page,subpage}`，只比较当前实际页面的活动子页；不以未活动的插件/绘制字段当作导航。`PageTransitionState::Duration=.140F` 为旧页时长权威值。
- `Resolve` 初次/关闭动画/显式Show或开屏reset直接提交。正常离页保持displayed，smoothstep降低旧页opacity；目标改变只更新target而不重启离页，完全隐去才提交最新target一次。返回当前displayed从当前opacity反向恢复，不闪回1或制造新路由。
- `Setting.cpp` 在标题栏处理及synchronizeDestination后捕获目标，确保版本/反馈/调测标题栏路由也进入同一门禁。只在Holding期间临时应用displayed给正文switch；完整desired destination/plugin/drawing在导航提交前恢复，不丢未活动子页值或快速点击请求。
- 正文switch每帧只执行一次，不重复渲染或使用新页冒充旧页。原滑块离页保存及业务FIFO入口不改。
- Holding时调用主代理共享 `SuppressDepartingPageInput(pageMotion)`，正文窗口标NoInputs，保持原BeginDisabled条件，避免可用控件突然切灰色造成额外闪烁。输入屏障需覆盖嵌套page/Card和原生轨道；队列中的旧body Active/Nav/hover/scroll目标只能按owner作用域清理，不影响侧栏/标题栏。
- 主代理ConfigurePageMotion最后exiting参数使用Holding，冻结旧页当前浮入姿态，返回source时同样避免先到位再淡出。轨道alpha在Begin组合一次；内容alpha仅对实际inner page调用既有ApplyContentAlpha，正文底色/导航/标题栏不参与。

## 验证

`setting_session_state_tests.cpp`新增生产helper直接回归：初次和同页、实际旧页identity/半透明边界、快速换目标、General/plugin/drawing子页、返回反向、关闭动画、Show/reset、现有rate。没有复制过渡算法。

按主代理要求，工作者未在并发编辑期间运行完整solution build。主代理补充真实ImGui旧/新页内容、输入阻断、颜色和时序回放，并执行完整Debug|ARM64 solution及适用Headless。
