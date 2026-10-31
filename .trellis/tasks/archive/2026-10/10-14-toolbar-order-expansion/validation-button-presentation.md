# 主栏按钮统一表现延续验证

## 范围与基线

feature/mode，2dd3b3448c6317e6924f667db5aee5629f4b479f。本轮修复组件添加覆盖分离式Freeze有效尺寸，并把主栏运行时尺寸/显隐/业务表现汇入统一状态派生入口。任务保持in_progress，不自动commit/push/finish/archive，不GUI/CU。

## 调查结论

真实设置入口SyncUi3BuiltInComponents调用SyncLegacyExtensionButtons/Load，再UpdateRendering。Load把A2注册配置基值写回有效size，Freeze变成twoTwo；A2样式缓存仅看业务styleKey，未变时跳过再次几何投影，Pierce仍twoOne。Recall事件/idle另有独立hide/size应用，idle结算晚于目标布局扫描；其既有3秒保护与灰色点击延期语义应保留。

统一入口需区分基础元数据和有效表现，Load不覆盖动态结果，A2几何不能被内容缓存短路。业务事件只更改状态/期限并锁外请求；同帧统一派生与到期结算在宽度前，状态/颜色不重开布局。成员/有效size/IsVisible变化即使等宽也须沿既有批次重定向，保留动画当前值、隐藏锚点、命中和dirty。

研究详见research/toolbar-presentation-writers.md及toolbar-history-state.md。

## 自动验证

| 项目 | 结果 | 证据 |
|---|---|---|
| 真实组件Load修复前回归 | 已复现：编译0、Bar退出1，仅目标尺寸断言失败（failures=1） | Build/ToolbarOrderExpansionValidation/button-presentation/red-offscreen-results.log |
| Trellis检查 | 通过：修正锁内capture等待、历史popup跨帧读view，未发现剩余确认缺陷 | research/toolbar-presentation-check.md |
| Debug ARM64完整solution | 最终退出0，0 errors / 131既有warnings，3:35 | Build/ToolbarOrderExpansionValidation/button-presentation/solution.log |
| 真实Bar离屏 | 退出0，480组件矩阵；EraserVisual failures=0 | Build/ToolbarOrderExpansionValidation/button-presentation/offscreen-results.log |
| Headless --no-window | 退出0；Config metadata/RecallAttribute/EraserAttribute均0 failures | Build/ToolbarOrderExpansionValidation/button-presentation/headless.stdout.log |
| 生产工具状态CLI | 退出0，两组PASS（穿透/定格/清空事务、跨代过期物理等） | Build/ToolbarOrderExpansionValidation/button-presentation/state-mode.stdout.log |
| 格式/写入库存审查 | 通过，22文件UTF-8/BOM/换行检查、diff --check与有效字段库存 | Build/ToolbarOrderExpansionValidation/button-presentation/encoding.log及检查记录 |

修复前先只加入真实组件回归。首次编译因测试缺少IdtConfiguration.h报setlist未声明，补齐已有配置声明后完整solution退出0；这是测试编译修正，不是产品修复。随后Bar真实入口退出1，Freeze/Pierce有效twoOne断言失败，稳定身份/邻接/成员增加断言通过，证据保存在red-offscreen-results.log。

独立检查发现两个不可用属性控件关闭调用可能同步SendMessage取消capture，原新增grid表现锁覆盖这些调用；已移到锁外预处理。另把主栏统一投影发布的历史view与锚点可见性存到frame，显式传给历史属性Advance，避免并发Load/StateUpdate夹帧发布令popup拼接不同视图。首次完整实现solution退出0，检查修正后的最终完整solution退出0；Bar、Headless与生产状态CLI全部退出0。

## 验证边界

构造/配置基础值、独立hover/按压和渲染SetTar不是主栏业务派生写入；分页/属性按钮属于独立实例，不纳入本轮。生产无窗口Bar回归复用真实全局owner、注册/Load/Update与timer期限入口；无Draw3 Host，注入历史事实不证明真实命令Accepted/系统WM_TIMER投递。源码合同仅作静态补充，不能代替实际调用。

不执行完整私有RenderLoop/真实HWND设置与点击，统一帧接线、锁序及布局消费者由检查和完整solution编译覆盖。人工待验：组件设置增删时分离式列布局、Undo/Redo期限与灰色连点、窄栏选择态不重新出现、左右排序、动画中途增删/换边及真实PPT/白板交互。原第三光源离屏回归继续保留。

构建沿用动态ARM64原生MSBuild、同PowerShell invocation PATH规范化和已验证命令范围参数，允许至少5分钟；不修改工程/工具链/依赖。测试均明确无窗口CLI、Hidden启动并检查真实退出码。

## 最终自动验收范围

- 修复前只加测试时真实组件Load断言失败；最终同一回归通过，保持Freeze/Pierce稳定身份及邻接。
- 真实注册/Sync/Load/Update：组件0/1/2/3/4/2/1/0数量、两模式、桌面/PPT/白板、空/有内容和五工具，480组；两主栏组件容量、更多overflow、反复Load/刷新、持久化A1/A2尺寸顺序保持。
- 生产MainLayoutSnapshot：等宽两枚2×1与单枚2×2/成员置换会失效；hover/按压/颜色/当前动画值不入指纹；生产timeline/SetTar helper的零dt、早晚批次、从current续接及120帧收敛。
- 生产Recall状态/墙钟：早到/大dt/旧唤醒与重装、灰色拒绝点击延期、Dismiss/绘制/工作区、Load交错、保护中与到期后精简Selection不显示历史按钮；到期只更state，统一投影应用有效字段。
- 真实RecallAttribute::Advance双向popup帧回归：捕获打开view后再次发布全局空页Selection，所传帧仍保持正确popup；传入隐藏锚点门禁则退场。
- 原第三光源回归继续通过：24坐标组与静止帧、4组D2D隔离光像素maxDelta=0/offLit=0。

## 构建与环境说明

最终日志solution.log，首次实现编译保留solution-first-implementation.log；ARM64原生MSBuild由vswhere动态定位，完整InkeysRepo.sln Debug|ARM64，同PowerShell先Remove-Item Env:PATH和MSBUILDDISABLENODEREUSE=1，持久化会话允许至少5分钟。沿用VcpkgManifestInstall=false、CL_MPCount=1、MultiProcMaxCount=1、LinkIncremental=false、m:1、nr:false命令参数，不改工程/工具链/依赖。

131 warnings为既有hashlib及数值转换、串行非增量链接相关警告，本轮新逻辑无新增源码错误。PATH规范化后既有applocal提示pwsh.exe不可定位（9009），非阻断且MSBuild真实退出0，最终测试可正常执行；这是本机/Codex构建环境提示，不改源码绕过。

Bar和state-mode由初始化前精确CLI分派，Hidden Start-Process/WaitForExit核对真实退出码；Headless明确--no-window。没有GUI、Computer Use或Presentation HWND fixture；没有新增本地化/配置字段，未无必要重跑i18n sync/设置语言回放。本轮不commit/push/finish/archive，任务保持in_progress。

## 人工验收与任务收尾

2026-10-14，用户明确确认“修改到位，人工验证通过”，并授权结束任务及提交commit。人工验收状态已记为passed_user_confirmed；以上“人工待验/任务继续/暂不提交”的段落保留为当时阶段记录，本节为最终状态。沿用已通过的自动验证证据，本次收尾不修改产品代码、不启动GUI、不push。代码修复提交后进行Trellis任务归档与会话记录，具体提交哈希以归档task.json和Git历史为准。
