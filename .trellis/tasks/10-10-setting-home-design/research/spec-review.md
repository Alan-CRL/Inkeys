# 规范更新审计

按照 workflow.md Phase 3.3 审核本任务产生的新合同和非直观知识。当前仓库未安装可读取的 trellis-update-spec 技能源，使用已读取的工作流要求和 native-desktop 规范执行审核；不因此暂停用户已授权的实施。

- 将 RenderHome 的缓存网站矢量、完整下伸、局部主题色、80%/520 DIP 预算和自然换行记录到 rendering-and-ui.md 末尾的新首页合同。
- 记录完整 GPL 名称分段链接、GitHub/GitCode 独立名称链接的稳定 ID、现有拖动取消及业务层异步外链边界；Star/Sponsor 复用原动作。
- 记录开源卡文本/按钮的 280/24 DIP 预算及高度，三语言源 → sync → check，生产 RenderHome/真实字体/输入/DrawData 的验收范围。
- 审查中发现 InvisibleButton 已内建导航焦点绘制，不应重复 RenderNavCursor；真实鼠标轻点隐藏键盘焦点框后，测试需要真实 Tab 进入键盘导航再验证 Enter/Space。窄窗链接分行由字体宽度决定，避免把 viewport 宽当作固定分行事实。
- 已明确 CPU 回归无法替代 HWND/DX11 原生观感与触摸验收。未修改其他模块规范、现有流程、工具链或依赖。

本次只追加当前主页合同，保持规范原编码和 CRLF；不改写无关历史条目。用户已明确禁止 commit/push，不执行工作流默认提交步骤；任务保留 in_progress 供人工审阅。


## 第二轮规范复核

前述为第一轮记录，第二轮 follow-up-plan.md 已覆盖独立开源卡和静态品牌等旧合同。主会话已替换 rendering-and-ui.md 中本任务新增的首页合同，记录单横幅、统一标语、居中开源按钮、紧凑版权、HomeMotionState/ShowSerial、真实鼠标门禁、900ms 反光、缓存、离屏暂停及稳定命中边界。configuration-i18n-and-assets.md 增加基准为空时允许显式空翻译的窄规则。

新增验证经验：真实冷 AutoResizeY 首帧不能先预热；深浅切换后以连续真实像素稳定取基线；离屏需先断言实际 scroll/clip 包络；切换 ImGui 输入来源需实际位置/按钮/滚轮事件，重复坐标会被去重。CPU/完成WARP提交/全页现有分配与动效缓存计数的边界均明确。trellis-check 只读复核规范一致，无额外兼容或整页零分配承诺。当前仍未安装可读取的 trellis-update-spec 技能，依 workflow Phase3.3 执行以上审计，不暂停已授权实施。

## 第三轮规范复核

ripple-revision.md 覆盖上一轮仅笔画弱反光/背景固定约定。主会话已最小修改 rendering-and-ui.md 首页合同：真端头共享截面半圆、重合回折端点的统一宽/共同方向，整圆角 hero 可见性与明显背景/笔画波带、有限66×43预计算网格、仅开场几何增量但批次不增、稳定缓存与颜色通道范围。没有改无关模块或全局控件样式。

实际 WARP 回归发现高倍率侧接合透明洞；不能通过保守中心取样或放宽alpha阈值隐藏。字标与波纹证据必须分开：隔离生产字标真正RGBA用于笔画RGB/alpha；背景扩散按真实圆角hero之外严格不变；纯视差仍按字标范围之外不变。高亮波峰宽度应覆盖至少数个网格步长，避免有限网格显示碎边；实际图像复核不可仅以像素数量替代。

继续依 workflow Phase3.3 作手动规范审计：当前没有可读取的 trellis-update-spec 技能源，不假称已调用技能，不为缺失技能暂停授权实施。完整ARM64、全套--no-window、真实Windows11 WARP与格式检查的最终证据记录在 ripple-implementation-validation.md；Win7实机与原生窗口仍明确待人工。

## 第四轮规范复核

用户确认k圆头修复，并将强蓝波纹改为克制背景/下方文案反光、艺术字完全不参与。主会话只替换本任务主页合同：原字标颜色/AA、无开场时间缓存键，背景8%/10%中性柔光、真正文案的轻微颜色调制、交互链接语义色优先、结束/稳定时提前门禁、真实WARP隔离字标RGBA精确一致与字体UV三角形验证。

trellis-update-spec 在本机仓库 .codex/skills（空）及全局技能目录均未找到可读取源，继续依 workflow Phase3.3 手动审计，不假称已使用缺失技能。规范沿用原编码和CRLF；未修改无关模块、全局按钮样式或工具链。

用户本轮授权提交此前代码。实际Git沿用原1Password SSH签名，报failed to fill whole buffer并未写入commit object，已停止该操作、不重试、不绕过签名。原暂存快照保留且以SHA256核验；第四轮工作区改动不加入暂存、不commit/push，当前任务继续供审阅，不执行finish/archive。
