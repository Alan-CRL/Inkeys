# 选中条滚动同步与普通赞助行验证（2026-10-08）

先按用户授权提交默认折叠/父行开合：f2cdf5ef，未push。随后完成本轮两个小修改，直接保留在原fluent工作区；新修改未自动commit/push，父任务和会话保持进行中，不结束或归档。

## 实现

- 选中条保存实际稳定item/window锚点。相同锚点的滚动和布局位移同步平移补间value/start/target并保留progress；不同锚点继续160ms过渡，并对旧窗口同帧滚动补偿起点。真实根窗口/DPI变换以相对DIP处理；裁剪时保持锚点，重新可见不重启动画。
- 赞助恢复真实普通NavItemEx，沿用普通透明/悬停/选中背景、键盘和页面路由。仅文字用主题AccentTextPrimary蓝色；null glyph保留原44DIP文字槽，16DIP正常前景图标另绘；选中条统一处理，不再绘制强调按钮自己的白色标记。
- 只改Setting.Design.h、Setting.Shell.cpp及回归测试；未改公开Setting接口、配置/i18n、线程/计时器、Controls/vendor，不顺手删除旧AccentNavigationItem helper。

## 实际验证

| 检查 | 结果 |
| --- | --- |
| 完整InkeysRepo.sln Debug/ARM64 | 动态定位ARM64原生MSBuild，同一PowerShell PATH workaround，1800秒超时；最终增量Solution exit0，27.51秒 |
| 最终--no-window | 主进程exit0，PASS animation correctness；真实产品控件、滚轮输入与DrawData，没有启动GUI |
| 既有分层导航 | 100%/150%/187.5%各684帧/24次导航，默认折叠、父行toggle、overlay、键盘及外部点击屏障保持通过 |
| 新增滚动/赞助 | 三倍率分别387, 387, 387帧；实际蓝条顶点与选中行同帧delta、切项过渡、中途滚动/反向、同帧切项+滚动、裁剪重现、分组高度变化、浅深与展开/紧凑赞助背景/文字/图标/Support路由/单一标记均通过 |
| 共享颜色 | 100%/187.5%各315个悬停合成样本，0 violations |
| 审查与格式 | 独立审查无未解决本轮源码问题；git diff --check、UTF-8/BOM/CRLF检查通过；原保护文件与13个PptCOM测试产物哈希一致 |

首轮构建/no-window已通过；根据独立审查补充本轮新锚点切换与同帧滚动分支的真实输入回归后，再执行上述最终构建与回归。新增用例不复制补间公式；蓝条从真实绘制顶点与ClipRect取样，文字图标与普通背景从实际导航行取样。

原始日志与机器摘要在忽略目录Inkeys/Cache/hierarchical-nav-20261008/：build-scroll-sponsor-final.*、headless-scroll-sponsor-final.log、scroll-sponsor-validation.json。原工作区Build/ARM64/Debug/Inkeys.exe已重编。真实鼠标观感、截图、HWND/DX11动画与跨屏DPI仍待实机验收，本轮没有GUI/Computer Use授权。

## 最新追加：完整圆头与图标/文字亮蓝（2026-10-08）

本节取代前文普通glyph/深蓝文字的产品状态以及不commit的交付要求；前文是上一轮已完成验证的历史记录。当前用户授权完成后commit，包含前轮未提交修复，不push，父任务继续进行中。

- 蓝条维持3×16 DIP及原锚点/滚动/动画，改为上下半宽圆弧PathFillConvex。规避vendor PathRect对窄条圆角“半宽减1物理像素”的钳制，不修改vendor或全局样式。
- 赞助图标和文字统一采用蓝条AccentFillDefault亮蓝主题色；普通无填充、悬停和选中背景、紧凑图标/名称提示与Support路由保留。
- 原真实DrawData回归直接验证蓝条顶点顶部/底部的跨度收拢，浅深展开/紧凑模式中赞助glyph/label颜色严格匹配当前蓝条token；未另写绘制公式或增加独立测试体系。
- 最终完整InkeysRepo.sln Debug|ARM64：动态ARM64原生MSBuild、同一PowerShell PATH workaround、1800秒超时，exit0，24.3秒。最终--no-window完整进程exit0，PASS animation correctness；三倍率各684帧/24次原导航和387帧滚动/赞助检查通过，悬停0 violations。
- 本轮独立审查未发现未解决代码问题；git diff --check及原UTF-8/BOM/CRLF通过。原保护文件保持。原工作区Inkeys.exe已重编。仍未启动GUI/Computer Use，真实鼠标观感、截图及HWND/DX11呈现待实机验收。

本轮原始日志在忽略目录：build-round-bright-final.*、headless-round-bright-final.log、round-bright-validation.json。本轮提交代码、回归、spec及任务记录；不push、不结束/归档父任务。

提交执行结果：用户已授权commit并执行，现有1Password签名报错 `failed to fill whole buffer`，Git以exit128报告 `failed to write commit object`，未生成新commit。停止提交，不重试/绕过签名，不改Git/SSH全局配置；代码、验证及暂存差异保留，待签名环境恢复。父任务继续in_progress，未push。

用户随后明确授权重试commit；本次重试沿用原签名配置与已验证代码范围，前次失败为历史记录，不push，不结束父任务。
