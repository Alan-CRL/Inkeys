# Fluent 设置内容实施映射

日期：2026-10-08。发布来源固定为 `chore/publish@e2c212d4d9cd68367c08363c450d392e9d63bcef`；Fluent 框架来源为 `834307a4`。研究来源与逐行事实见 `content-source-audit.md`，本文件记录实际整合结果，不用引用数量代替行为审查。

## 实施边界

`Setting.cpp` 逐块保留 Fluent 冲突中的标题栏、DWM、常驻字体/session、串行帧、异步业务 FIFO 和 Design 行布局，再将发布业务接入；没有整文件覆盖，没有恢复旧设置页固定坐标。`Setting.Pages.cpp/.h` 承载主页和常规页纯渲染，副作用继续由原 `Setting.cpp` 宏/队列执行。`Setting.SessionState.h` 增加被生产页直接调用的 `ResolveLinkedPptScaleDraft`，使用本帧 Slider/Reset 编辑事件与新加入同步状态，避免用未保存 Record 回滚持续拖动。`Setting.Shell.cpp` 本阶段仅移除已退休的预设条目和校准现有分组索引；三段式侧栏属于依赖本任务验收的下一子任务。

删去 `PageId::Preset` / `HomeAction::Preset` / 原预设 switch 及相关配置 UI；`Plugins` 仍为8，后续页面的旧编号8..14不移动，6/7保留空洞，避免导航数字和内部 switch 不一致。新的 GeneralDraft/Events 仅为内部页面接口，不修改 Setting module 的对外接口。HomeContent 增加头像与反馈二维码纹理及长宽比，教程改回发布源真实资源[4]，不复用版本Logo[1]。

## 17 个实际发布视图

| 视图 | 已接入的内容与显示条件 | 配置/保存/实时行为 |
| --- | --- | --- |
| 主页 | 移除预设卡片，保留Fluent自适应布局；恢复开发者、QQ、邮件、Home.Group/GroupE、BilibiliChannel、反馈QR、教程/赞助入口 | 群入口简中打开发布QQ邀请，其他语言GitHub讨论；社区侧栏仍独立打开community页面。所有外链继续FIFO，反馈hover显示资源[7] |
| 语言 | 原三语言顺序与Select/说明/Warn | 改变才保存；立即load对应语言并排队重启确认 |
| 配置保存 | 配置清理、桌面画布自动保存；不暴露发布#if0的保留天数 | AutoClean保存main+deploy；Save.Enable先SyncDraw3State再WriteSetting |
| 软件版本 | 版本/架构/构建/UserId、UpdateNew时独立ManualUpdate原提示/按钮、修复/修复改架构、自动更新、渠道和架构；完整12更新状态移入此页 | 原Clipboard、mandatoryUpdate、启动/复用更新逻辑及清理待安装器FIFO保留 |
| CI/CD | 返回及真实运行/仓库URL、分支、提交者、系统/镜像/工具版本 | 只读，外链FIFO |
| 常规 | 发布四个分组原标题（StartUp.N/Appearance.N/Behavior.N/Tentative.N）；启动、创建/更多、启动加载动画；动画/仅开启时速率、光影/仅开启时动态光影、Bar/Setting倍率、置顶、右键退出、安全模式；不再显示AvoidFullscreen | StartupPreview通过Preference SetConfigured/ConsumeWritePending；动画/光影即时应用后队列保存；速率0.1..5按0.1取整，Bar0.5..2及Setting1..2按0.01取整，速率/Bar松手写盘 |
| 绘制 | 设备、自动橡皮总开关、五入口Fixed/Speed；Pen+Speed显示响应；触摸接触面积辅助。移除旧AI/平滑/吸附/旧mode/性能/光标配置 | 设备SyncDraw3State；整页同一preferences快照；总开关helper保留入口偏好并排队写盘；入口helper+QueueConfigWrite；隐藏Auto不改稳定响应枚举；TouchArea更新Host+main配置 |
| 插件概览 | PPT、SuperTop、LnkHelper、DDB四个详情入口及版本/错误 | 仅页面路由，不写配置 |
| PPT详情 | 加载页、底部/中间两对、ResetPositions、记忆位置、两组0.5..3倍率/Reset/Sync、接管/一次/展开/长按；无FixedInk/ExitShow/第三scale/WPS清理入口 | 两组倍率按0.01取整，拖动/Reset/Sync即时更新+对应组Notify；所有scale控件释放才写盘；新加入Sync取另一组，Reset即使已是1仍传播；ResetPositions后显式PptComWriteSetting；显示开关对应组Notify；新接管项走main配置 |
| SuperTop详情 | 条件说明、总开关与指示 | 原deploy字段变化才保存，不新增进程操作 |
| 快捷方式详情 | FixLnk/CreateLnk | 保存后，开FixLnk或CorrectLnk已开时改CreateLnk才SetShortcut |
| DDB详情 | 完整发布用途/GPL/仓库/无质量保证/自担后果原文、启用/管理员/间隔/17拦截目标，名称与每条说明按源精确恢复 | ConfigureDdb与RestartDdb使用FIFO；间隔/拦截保存后QueueDdbWriteInteraction；未引入render线程进程I/O |
| 组件 | 应用/系统/键盘/点名/ClassIsland共16项；IslandCaller/ClassIsland原说明精确恢复 | 每项变化WriteSetting+SyncUi3BuiltInComponents即时更新主栏 |
| 快捷键 | 只读说明 | 无新编辑器 |
| 实验室 | 四控制台输出、DComp、DirtyDebug；仅DirtyDebug开时FrameRate；动画/光影移到常规且不重复 | 四Console写main并QueueConfigWrite，DComp重启应用；Debug即时SetDebugOptions并排队保存，隐藏子项不改值 |
| 赞助 | 图片[9]与发布完整感谢原句 | 保留自适应图片，无新增支付操作 |
| 调试 | 拓扑、名称/Windows标识、像素/DPI、物理可用性/原因、EDID原始尺寸；DRAW3_RTS_DIAGNOSTICS门控临时策略、标尺、手动尺寸、beta、TouchArea与诊断 | 同帧GetSnapshot；策略/校准/beta只写Host临时options；TouchArea共享正式配置并队列保存。删除旧TouchTest/Recall/PPT诊断入口 |

## 12 个更新状态与动作

- NotStarted：原未启动说明；Repair和Check均通过Dialogs.UpdateModuleUnavailable.Body排队信息提示；ManualDownload原官网链接。
- ObtainInformation、InformationFail、InformationDamage、InformationUnStandardized、DownloadFail、DownloadDamage：对应翻译状态、原ManualDownload链接及Check。
- Downloading：用原downloadLine模板和有效文件大小百分比；保留Check。
- Restart：只说明已下载待重启，不增加自动重启；保留Check。
- Latest/Newer：对应状态加LTS/Insider/Dev/Canary/Other渠道；保留Check。
- New：UpdateNow设置mandatoryUpdate并进入UpdateObtainInformation；Check只进入查询。

以上内容保留在版本页的Fluent Notice/按钮组，不恢复全局绝对坐标底栏；状态文案与每个按钮动作逐项对应研究源。

## 明确保留的框架例外与验证

Setting倍率保留Fluent松手即应用及字体请求合同，因此对应多语言说明保留即时应用版本，避免显示发布旧“需要重启”提示。全局更新状态的位置、诊断的行组件、主页插画等按Fluent布局；控件目标和配置业务仍以发布源为准。硬编码实验/调试和DDB/组件文字的国际化缺口原发布源已存在，本任务不声称完成全局国际化。

已运行针对本分工五个设置文件的退休符号/冲突标记静态搜索（无命中）、UTF-8无BOM/全CRLF检查、`git diff --check HEAD -- <本分工设置文件>`（退出0）。新增Draw/General和旧插件行遵循中文关键注释、一次动作事件和FIFO。完整Solution和无窗口测试由runtime整合分工执行，第二轮完整Solution及最后增量完整Solution均成功，最后--no-window退出0；包含直接调用生产linked-scale helper的连续拖动、空闲帧、默认值Reset、双向加入同步与未同步案例。Headless没有链接完整Setting.Pages/Shell，逐页内容/副作用主要由固定source映射及独立只读复核覆盖，不能把无窗口回归当完整页面点击验收。真实鼠标、DComp/DWM或GUI验收尚未执行。

## 复核修正与最终验证

独立研究/Checker发现并修正了PPT ResetPositions后的显式写盘（helper未启动仍必须保存）、内部dynamicEdgeLighting默认false、Version独立ManualUpdate字面、DDB/组件原说明与错误摘要。所有修正均进入最后成功的完整Solution，不沿用修前构建结果。

PPT时机的早期研究结论已纠正：固定source在slider/Reset/Sync块即时赋pptComSetlist及Notify，4892的!allItemActive只负责写盘和再次Notify；恢复真实source实时应用+松手写盘。linked-scale生产helper采用真实编辑事件，配套headless场景由runtime测试负责人登记，源码不再使用未保存Record作为拖动候选来源。

首轮完整Solution在vcpkg前置环境失败，未进入生产源码编译，属于隔离构建环境问题；首轮Headless完整编译及--no-window退出0。隔离依赖环境修复后的第二轮完整Solution退出0，用时486.7秒，主Inkeys/PptCOM/Headless均成功；随后补回常规页四个原分组标题键，最终增量完整Solution退出0，用时13.0秒；包含linked-scale新测试的最终--no-window退出0。三语言DrawDescription已去除退休AI文案，最终i18n check三语言366/366通过。实机颜色、滚动、HWND/DComp/DWM与窗口操作尚未执行，不作通过声明。

第一子任务设置内容分工已完成，可将此记录与`content-source-audit.md`、`runtime-merge-validation.md`及独立Checker报告作为依赖验收证据。后续三段式侧栏和共享悬停插值由第二子任务继续，本记录不声称其已实现。未创建commit、未push、未启动GUI或Computer Use。
