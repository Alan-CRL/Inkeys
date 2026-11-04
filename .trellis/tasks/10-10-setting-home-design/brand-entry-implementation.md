# 第五轮实施记录：并行书写与窗口开启归位

2026-10-11，trellis-implement。源码已向主会话发出冻结通知；任务保持 in_progress。本记录只说明已实现的路径与待执行验证，不把静态审查当作编译或 WARP 运行证据。

## 已实现

- Setting.HomeBrand.h 在原六组笔画首次 Build 时追加每组截面和索引段的局部时间；完整顶点、索引、彩虹与已修复 k 端帽计算保持原样。书写时六笔近同时开始，按各笔进度提交完整索引段，每笔最多一个活动半圆端帽；闭合 k 首端补一个临时起笔半圆。活动端帽共用原截面，AA 仅在外缘，半圆单位模板一次缓存；不逐帧采样曲线或扩容 vector。写完直接走原 Draw。
- HomeMotionState 的 Pending/Playing/Finished/Skipped 仅由新的 ShowSerial 发起；切页离开立即结束、返回不重播。时序为起笔等待 120ms、书写 530ms、完整静止 Hold 300ms、三次 Bezier 空间归位 270ms、单次轻落定 110ms。Hold 姿态固定且覆盖层 alpha=1，.95s 后 fade 与 docking 才同时开始；时间沿用有限 delta 与现有速率。
- RenderHome 保留真实目标占位，播放期间不提交横幅内第二份字标。RenderHomeEntryOverlay 在导航后用前景 draw list 提交纯色覆盖层和同一透明字标，clip 从 32 DIP 标题栏下方到客户内容区底部。文字、链接和按钮不扭曲；完成后由原静态字标与视差接管。
- ConfigureHomeEntryViewport 使用外层实际 content ClipRect；RenderHome 的真实字体、标语与按钮换行度量给出 hero/brand。首帧 AutoResizeY 内层 clip 为空不作为尺寸判断依据，因此不会先出现正常页面再盖回。完整横幅和中心/曲线路径包络不够时，本 Show 立即永久 Skipped。17-float 实际布局键缓存路径门禁，仅 Show 首次或尺寸/字体/布局改变重算；中途缩小不安全时立即静态归位，放大不补播。
- SessionState 增加真实 visible→hidden 的 HideSerial。协程在 NewFrame 后、BeginDragScrolling 之前用 ConsumeSettingHomeReset 消费，复位目的地 Home、导航/弹层/选择条、内容 alpha 与活动控件；包括 Hide→Show 发生在两个渲染回调之间。BeginScrollChild(resetToTop) 在 Begin 与旧拖动 storage 消费前清理 Scroll/ScrollTarget/惯性缓存，重开首布局直接使用顶部坐标。
- CancelHomeEntryOnInput 是产品与测试共用入口，内容/导航提交之前检查鼠标按下、键盘按下/激活、滚轮及字符输入；实际操作收束入场，且该取消帧仍阻断底层控件。无输入时保留正常 Fluent 配色，避免全程 Disabled 使渐显界面禁用配色后在落定跳回。HomePointerAllowed 在 Pending/Playing 门禁视差与图标反馈；不另造点击或路由。
- 原开场背景/正文柔光的调用已经删除；原 Ripple 辅助声明保留为未使用代码，不再建立缓存或提交背景波网格。本轮未新增 Shader、纹理、device、线程、timer、配置或文案。

## 影响文件

- 生产：Setting.HomeBrand.h、Setting.Pages.h/.cpp、Setting.SessionState.h、Setting.cpp，以及 Begin 前滚动复位所需 Setting.Controls.h/.cpp。
- 回归：setting_session_state_tests.cpp、setting_design_tests.cpp、setting_home_warp_tests.cpp。
- 主会话另外拥有 PRD/design/context/spec 与最终验证记录；未回滚任何其他人的更改。

## 已更新的验证源

- SessionState Show/Hide 精确序号、重复 Show/Hide、遮挡/resize/presentation 不重播。
- 三语言、浅深、导航展开/收起、320/480/720/960/1440 窗口与原倍率矩阵，在真实 RenderHome/outer ClipRect 检查实际播放或跳过；保留原文本、许可/仓库按钮、Tab/Enter/Space、点击/拖动和滚动回归。
- 真实主页绘制验证 .65/.8/.949s 的完成 Hold，1.08s 曲线路径偏离直线且 alpha 同步推进，速率/零 delta、禁用再开启、内部导航不重播、尺寸跳过后扩宽、缩小中止、Hide reset 首次顶部布局、实际鼠标/Space 取消无业务穿透。原四角视差、透明 alpha/y 下伸、来源/焦点/弹层/离屏与一次图标反馈继续检查。
- WARP Frame 复用生产 shell、导航、Home、前景覆盖及取消 helper；cold AutoResize 未预热入口。读取 .13/.38/.65/.8/.949/1.08/1.27s 像素，检查标题栏外不受覆盖、导航确被覆盖、完整 Hold 读回一致、实际文字 RGBA/UV/布局不变、按钮命中固定，结束等于原静态整页。保留原透明网格与 k 接合 1/1.875/4 倍实像素检查。
- 同机真实 static/idle/moving/returning/writing/hold/dock/settle，每模式 20 帧预热与 100 帧 CPU median/p95、完成提交/copy/Map median/p95、顶点/索引/批次、ImGui 分配及原 mesh/截面/索引段 capacity 不增长。计数不覆盖既有 std::string/vector 或驱动分配，不设置脆弱绝对耗时门槛。

## 本代理已执行与待执行

- 已执行 Scripts/i18n.ps1 check：简/繁/英资源均 384 条，en-US/zh-TW PASS；未改文案，不需要 sync。
- 已执行 git diff --check，无空白错误；所改 C++ 文件均严格 UTF-8、无 BOM，全部 LF 为 CRLF，最小必要 diff。
- 按主会话协调要求，本代理没有运行 MSBuild 或测试 EXE，避免共享输出并行写入。ARM64 原生完整 solution/PATH workaround、--no-window 与新增真实 WARP/CPU 结果由主会话统一执行，可能据失败修正源码。
- 未开启 GUI/Computer Use、未执行外链、未 commit/push。Win7 实机、原生窗口最终观感和实际 WARP 性能结论仍需明确独立证据；目前只沿用原 FL11.0 兼容渲染接口。
