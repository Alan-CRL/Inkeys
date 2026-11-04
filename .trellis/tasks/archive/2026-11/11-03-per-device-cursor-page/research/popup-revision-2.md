# 下拉框底部避让续修（2026-11-04）

## 原因与改动边界

普通 `Design::ComboBox` 强制给出按钮下方位置，但其 AutoResize 路径没有客户区垂直预算；位于页面底部时弹层向客户端外延伸。显式 `BelowComboMotion` 路径虽约束 below 高度，却固定只向下，甚至不足一行也不会上移。两条路径共用同一现有 ImGui popup，无需新增原生窗口。

本轮只改 Setting.Controls.cpp/.h 与 setting_design_tests.cpp，保留已有下拉状态/API名称、动作、滚动条、退出装饰、文案及其它控件逻辑。

## 实现

- 两条路径统一在 BeginPopup 前计算目标完整高度、客户区上下预算；默认下方，若下方不足且上方更大则上方。两侧均不够时在较大一侧缩短并沿用既有滚动。
- 位置保持与按钮同X，显式给出本帧尺寸和最小1像素约束；取消普通路径的 AutoResize 依赖，避免首帧外溢、次帧才收敛。
- 动画沿选定一侧作原有12DIP位移并受该侧剩余预算约束，保留180ms透明度、反向、滚动位置和无输入退出装饰。没有增加方向共享状态或改名。

## 回归覆盖（尚待主会话运行）

- 原 CheckBelowComboPopup 调整为正常下方/底部上方；保留开关、反向、动画倍率、禁用动画、滚动、选择单事件、遗漏/语言版本失效；补向上退场边界。
- 同fixture切换普通 no-motion 路径，逐项真实鼠标点击4个选项（包括最后一项），检查首帧完整向上；充足空间向下、打开时resize、上下均不足时滚动及 End/Enter 访问最后项。
- 实际 RenderCursorPage 的780 DIP宽fixture缩到480DIP高，以真实 ScrollMax 滚到底，打开实际 ##cursor-dot-hide/##select 并鼠标选择最后一项；与既有20语言3DPI cursor-only矩阵合并。
- 新 `INKEYS_SETTING_POPUP_ONLY=1` 配合 `--no-window --setting-home-only` 覆盖以上以及既有 CheckComboBoxInteraction（Solid/Acrylic）、CheckDragScrolling 的长列表/滚动条。正常完整回归也包含新的弹层边界检查。
- popup-only使用1/1.5/1.875三档；不涉及GUI、主窗口或新构建系统。

## 当前结果

逐文件与本轮增量前备份比较，BOM、CRLF/LF保持，git diff --check通过。源码身份见 Build/cursor-revision2/popup-source-state.json；构建及测试由主会话统一完成，尚未宣称运行通过。未创建commit或操作GUI。

## 静态复核修正

新增首帧打开lambda与旧动画用例的局部 `firstOpen` 同名，已仅将新增lambda及3处调用改为 `openFirstFrame`，保留旧变量与测试逻辑；未修改产品源码或运行构建。

新增选项点击局部变量与外层旧 `point` 同名的C4456警告已通过仅重命名为 `optionPoint` 及3次使用消除；旧代码保留。两处测试命名修正后再次冻结。

## 其它公共控件消费者回归

用户补充教学安全等页面也应统一修复；没有增加产品页面专用分支。新增用例直接复用 CheckGeneralSections 中真实 RenderGeneral(Interaction)、已注册的teaching-safety/##select矩形、真实ScrollY和通用noEdits检查，将教学安全入口放到客户区底部，首帧确认向上且完全在客户区内，再真实鼠标点击Mode4，断言安全模式仅提交一次且其它行为偏好/事件不变。既有general-only和完整回归均会执行；general-only自动覆盖20语言与1/1.5/1.875DPI。增量前备份见Build/cursor-revision2/pre-teaching-backups。仅修改测试和研究记录，尚待主会话统一构建/运行。

## 共享消费者最终闭环

全量产品入口审计确认教学安全、光标、PPT闲置时长、快捷方式预设等均通过同一Design::ComboBox；没有页面位置特判或绕过的直接ImGui/ImFluent下拉。新增真实RenderGeneral(Interaction)教学安全位于客户区底部的首帧上开/鼠标末项单事件回归，已在general-only的20语言×3DPI全部通过。新增用例后完整solution再次退出0，五项受影响无窗口检查再次全部退出0。产品Inkeys.exe SHA与补用例前完全相同，证明本次仅补验证，共享产品实现未变。40项源码身份保持。没有GUI、commit、push或归档。
