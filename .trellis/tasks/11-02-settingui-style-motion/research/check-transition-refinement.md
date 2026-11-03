# 2026-11-03 旧页淡出与柔和入场检查

范围：阶段提交925d8b2b后的本轮R6细化。已读取PRD/design追加要求、当前完整diff、PageTransition纯状态、Setting实际路由门、Controls输入/时序和相关真实测试；没有GUI或commit。

## 产品路径检查

- PageTransitionState保存实际displayed与最新target，Leaving阶段仅更新目标而不重启旧页计时；完全隐去才提交新目标。Returning从当前opacity回升，新的目标请求可再次平滑转为Leaving。初次、关动画、字标开屏和显式Hide/Show重置直达，未淡出不存在的旧页。
- Setting.cpp先保存完整requested route/General section/plugin/drawing子页，Holding时只替换实际正文路由，正文绘制后恢复完整requested状态给导航；正文switch仍只执行一次。代码检查未发现General Storage、插件/绘制详情或连续导航目标丢失。
- 旧页只对实际inner page及其child内容调用ApplyContentAlpha；外层底色不参与，native正文轨道在Begin中只乘一次transition opacity。PageMotion退出时冻结已有elapsed，独立保持几何/opacity的单调状态，避免首个离页帧把半入场页面直接变全实。
- 输入屏障只清旧owner子树的ActiveID、Hover和排队ScrollTarget，以及归属旧正文的Nav激活；NoInputs仅加旧正文外层，不添加额外BeginDisabled使旧页变灰。同级标题栏/侧栏保持操作。真实CPU回放已覆盖旧页鼠标无业务动作和同级输入所有权不被清除。
- 时间源集中：Exit140ms；Move340ms；Fade500ms，错峰30ms且最多120ms，最后位置约460ms落定而opacity到620ms，thumb随后120ms。开屏/20语/字体/资源管线及Win7支持边界未扩大。

## 键盘失败的实证诊断与测试修正

新测试原先在Render之后调用NavInitWindow，并假设Tab8次会直接到两层非NavFlattened child里的Button。vendor BeginChild/EndChild把这些宿主作为独立导航锚点，Enter进入；NavInitWindow注明必须在实际items提交前调用。此失败不能直接归因于新退出屏障。

只修改测试：在实际root Begin内发起FocusWindow/NavInit；真实Tab遍历并仅在实际注册的child anchor上按Enter进入，始终不手工指定业务按钮NavID。先记录无退出屏障的基线，再在旧页输入被清理、新页显示后执行相同操作。

真实完整二进制结果（三倍率100%/120%/187.5%相同）：baseline-tab-only=0、baseline-native-child-activation=1、after-departure=1。证明当前屏障未破坏原生键盘进入路径，无需为测试假设更改产品NavFlattened或焦点框架。测试仍检查到达真实firstAction，而未删除或放宽键盘断言。

## 验证

- 原生ARM64完整InkeysRepo.sln Debug|ARM64增量构建退出0，日志Build/settingui-motion-review/build-transition-keyboard.log。
- 实际Headless `INKEYS_SETTING_MOTION_ONLY=1 --no-window --setting-home-only` 退出0；100/120/187.5%每倍率613帧，共1839帧，动作各一次；stderr为空。日志transition-keyboard.log/.stderr.log。
- 本次明确移除了INKEYS_SETTING_MOTION_SNAPSHOTS，仅为CPU真实回放；28张WARP场景由主代理独立运行记录，不能把本次CPU通过冒充新截图验证。
- 主代理同时修正慢速位置测试，比较实际终态DrawData基线而非带AA边缘差异的逻辑origin；该断言在本次真实运行中通过。
- 未修改产品输入屏障或路由代码，没有发现需要额外架构/产品决策的新缺陷。完整最终Headless及实机体验仍由主代理整体验收；原Win7/DWM未验证边界保持。
