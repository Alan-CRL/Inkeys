# 顶部边框视觉修正（2026-09-07）

基线 9ef709df，用户认可现有窗口/主题总体效果，仅指出恢复态顶部边框过粗。用户明确禁止 Computer Use，允许普通脚本截图和窗口操作；本轮没有 commit 授权，不自动提交。

## 最小变更边界

- 只修顶部多余可见 non-client 条带；先用窗口 outer/client/DWM visible bounds 与截图确认来源。
- 预计只涉及 Setting 的 frame 度量/创建/NCCALCSIZE/顶部命中及对应回归；若保持几何仅需绘制修正，优先最少代码且需证明无副作用的方案。
- 保留标准 WS_OVERLAPPEDWINDOW、USER32 原生最大化协议、右侧完整 scrollbar client 命中、左/右/下 resize 行为、标题按钮/主题切换、DPI、共享渲染/session 与配置。
- 不以恢复旧的全边 1px NC + 向内容扩 resize 方案来去掉顶部条带，不改全页布局/字号/主题。
- 定向测试覆盖恢复/最大化、顶边与顶角、caption 按钮、right scrollbar、多 DPI；全Solution ARM64与集成无窗口回归。GUI仅操作单独测试副本并校验PID，锁屏不发送输入。

用户于2026-09-08再次确认：目标是“智绘教Inkeys选项”窗口最上沿、caption上方红箭头粗条；悬浮主栏及主按钮没有问题。所有生产变更限Setting及WindowRole::Setting专用分支。主栏截图仅为自动化打开选项入口，不能当作本修复的视觉对象。
