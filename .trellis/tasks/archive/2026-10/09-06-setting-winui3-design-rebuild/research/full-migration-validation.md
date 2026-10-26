# 全页迁移验证记录

基线：`0ffd0341`。当前代码阶段一、二均已完成；真实窗口视觉验收尚待桌面解锁，任务不归档、不自动提交。

## 完成的实现

- 文字保持14/20、12/16、28/36语义层级，HarmonyOS字面统一乘0.97光学比例；所有输入控件集中使用13 DIP文字。
- 导航glyph16 DIP，24 DIP槽、原轴线保持；滚动条8 DIP命中轨道、2 DIPpadding、透明轨道、轻灰滑块/交互状态。
- 修复SetAccentColor重建preset、覆盖项目先前token的调用顺序；字体/图标/控件/滚动参数集中定义。
- 公共Controls包含typed Row、ButtonGroup、Details、Card、Notice、NavigationRow及控件wrapper；默认按钮与天然宽度测量使用同一公式。
- 全部18个实际视图接入统一字体与页面组件；固定34%操作列与旧BeginSettingsCard布局已从产品退出。
- 特殊PPT缩放组合、版本/构建、赞助图片、调试内容保留专用布局与原行为；插件各子页独立保存滚动/展开状态。

## 实际执行的检查

| 检查 | 结果 |
| --- | --- |
| 阶段一完整Solution Debug/ARM64 | PASS |
| 全页完整`InkeysRepo.sln` Debug/ARM64 | PASS；0 errors，31.64秒 |
| 集成`InkeysHeadlessTests.exe --no-window` | PASS，exit0；汇总输出`PASS animation correctness`包含新增设置测试 |
| 独立ARM64 CPU probe | PASS，exit0，约3.80秒；见full-headless-results.md |
| i18n | en-US、zh-TW均304/304 PASS |
| 编码和换行 | 与full-encoding-baseline.json比较PASS |
| diff检查 | PASS；任务中原有LF文件产生Git自动CRLF提示，不是行末空白失败 |
| 全差异审查 | 未发现需追加生产修复的问题 |

使用 ARM64 host `MSBuild.exe`，完整Solution带PptCOM，构建超时900秒；仍采用仅子进程规范化Path环境键、`/m:1 /nr:false`的已确认本机调用方式。日志：`Build/setting-winui3-full-migration-build.log`；47条告警来自已有转换/依赖代码，本轮未扩为告警清理。

测试覆盖真实字体96组、既有Row72组、按钮独立字号及测量/绘制宽一致性、窄按钮组重排、Details/Notice/Card/Navigation末项与分数DPI、真实细滚动条及重复Apply不重复缩放。公共默认Button宽度差10 DIP的问题已由测试抓出并修复。

业务审计见full-page-binding-audit.md、full-pages-static-audit.json；原130配置引用、96关键业务调用、6FIFO命令类型及原路由集合保留。独立check逐页核对了条件、保存、异步和即时同步，并非仅凭数量宣称等价。

## 真实窗口检查的进度和阻碍

用户已允许脚本启动窗口、截图与操作，禁止Computer Use。主会话编写`research/window-review.py`，仅操作明确PID/句柄，输入前验证命中窗口归属。使用`Build/SettingVisualReview/app/`的隔离程序和配置副本，保护原工作配置和默认构建产物。

- 受限执行进程起初位于不可交互桌面，GetCursorPos/SetCursorPos失败；检查脚本现明确验证返回值，失败不发送输入。
- 经用户已授予的脚本权限在交互桌面启动独立进程后，读取到Windows锁屏窗口`LockScreenBackstopFrame`覆盖测试窗口；命中校验阻止了鼠标操作。
- 已请求用户手动解锁。未向锁屏发送点击或按键、未尝试登录/绕过；不能把当前结果当成设置页已完成实机截图验收。
- 已关闭本任务启动的旧测试进程并更新隔离副本为全页新构建，等待解锁后重启。未终止原用户进程。
- 当前仅捕获启动主栏的诊断图，没有最终设置页面截图。旧HTML和首批效果图不作为本轮已实现截图。

## 解锁后的待办

1. 在交互桌面启动最新隔离副本，记录新PID与窗口句柄；通过主栏设置入口打开设置窗口。
2. 按full-page-binding-audit末尾覆盖表进行只导航/滚动/尺寸调整，拍摄主页、常规及所有其余可达视图。构建详情若因本地CI数据为空不可达，记录条件，不能虚构数据。
3. 检查默认、宽窗、窄内容预算和长说明/复合控件，按真实图继续修正视觉问题并补必要回归。
4. 将最终图交由check复核，补全验收；按用户要求不使用Computer Use、不自动commit。

最后通过只读WTS会话状态查询确认SessionFlags=0（锁定）；检查脚本已增加会话解锁门和输入前遮挡检查。


## 2026-09-07 提交前检查点

用户解锁后已通过脚本打开隔离测试窗口，获得preview/native/01至11的真实截图；独立视觉审查确认这11张图的可见内容没有实质排版、对齐或内部裁切问题。页底、支持/调试、插件详情和窄窗口矩阵尚未完成；随后桌面再次锁定。部分PrintWindow采样出现旧帧或非客户区色边，后续需以实时屏幕矩形捕获复核滚动，不把未变化的采样判成产品缺陷。

用户现要求先提交当前修改。任务保持in_progress，后续从剩余窗口验收继续，不把代码提交当作视觉全验收完成。临时full-baseline源码副本已清理，原始内容可从0ffd0341恢复。
