# 第五轮规范同步审计

2026-10-11，主会话按 `.trellis/workflow.md` Phase 3.3 审查本轮新合同与易错边界。

## 路径与范围

- 工作流要求加载 `trellis-update-spec`，但当前可用技能目录、项目 `.agents/skills`/`.codex/skills` 和全局技能/插件中未找到该技能；没有声称已调用不存在的技能。采用已读取的 workflow Phase 3.3 手工回写新模式/陷阱/技术决定，不影响用户已批准的实施。
- 仅修改 `.trellis/spec/native-desktop/rendering-and-ui.md` 末尾“Setting 首页品牌、版权与开源动效合同”，先前其他 Setting/渲染章节不重写。中文文案、布局、链接与现有输入合同保留，删除冲突的900ms柔光/内部返回重播描述。
- 任务保持 in_progress，无 commit/push/finish/archive；用户任务范围优先于工作流的默认结束/提交步骤。

## 新知识与决定

- Show 和 Hide 分别使用明确递增序号，不靠缺席帧推断。渲染线程在布局前消费 Hide，覆盖无中间渲染的快速 Hide→Show；不能为复位执行无RTV隐藏绘制。
- 主页最顶端必须在 Begin 前清 Scroll、ScrollTarget 及旧拖动storage，再设置下一child滚动值；仅在后面 SetScrollY 会让首帧旧坐标泄漏。
- 六组透明原矢量按缓存截面/索引块时间书写，活动定向半圆共用截面；完整阶段复用原网格。禁止全局任意索引前缀导致未到达端帽/平切/接缝。
- 完整书写后的300ms是独立Hold，覆盖alpha1且中心姿态不动；随后覆盖渐退与真实空间Bezier路径同时推进，精确归位后才恢复视差。标题栏在覆盖范围之外。
- 用实际hero、outer content clip、起点/目标/完整运动包络判断能否播放；cold AutoResize内层暂空不可制造旧页面闪现。门禁按真实布局键缓存，尺寸不安全永久消费本Show，放大不补播。
- 无弹层时不能无条件ClosePopupToLevel(0)：该API有非空栈断言。丢旧子页/弹层焦点同时必须重新Focus/NavInit设置root，快速无缺帧重开时根窗不Appearing，仅FocusWindow(nullptr)会让Tab不可达；回归用真实Tab/Enter而不是赋值NavActivateId。
- 输入在正文/导航之前取消并阻断取消帧；检查已按住的键，不能只看按下边沿。无操作帧保留原正常配色，避免全程Disabled在渐退时露出禁用色。字标视差/图标反馈另在入场状态下门禁。
- 原管线/FL11.0复用，不新增shader/纹理/device/thread/timer。容量与ImGui计数不能冒充整页/驱动零分配，copy/Map完成耗时不能冒充纯GPU或窗口帧率。

## 证据边界

本文件记录规范决定，实际编译、测试、WARP阶段像素和性能以 `research/brand-entry-validation.md` 以及独立 `research/brand-entry-check-review.md` 为准。Windows11 ARM64无窗口WARP证据、静态Win7兼容审计、原生窗口/Win7实机人工审阅分别记录，不相互替代。
