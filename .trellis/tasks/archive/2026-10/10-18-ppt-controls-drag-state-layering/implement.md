# 执行计划（1–3项已批准实施）

## 0. 本轮调查与审批门

- [x] 用户明确授权创建新任务与调查；状态planning，禁止代码变更。
- [x] 记录13783fe7、候选反馈版本及“仅控件停住/主栏仍可操作”。
- [x] 对照两个候选版本，核对已有交互稳定/owner链及刚完成COM修复。
- [x] 完成三个独立research报告与主会话综合审阅。
- [x] 完成PRD/design/context及任务范围/格式检查。
- [x] 2026-10-18用户明确批准并开始修复1–3；第4项不实施新层级修改。

## 1. 批准后的实施顺序

### 1.1 抬手微移与pair最终位置

- [x] 读spec/research和最新dirty，保持最小diff与编码/CRLF。
- [x] 对拖动pair（包括released pending）统一最终位置提交；普通位置取消兜底缓动，吸附Y按第5节140ms过渡，不关闭无关视觉动画。
- [x] 核对两面真实提交后才能完成candidate与persist，旧帧不能回退坐标。
- [x] 正确采纳Up最后坐标；pending Page/background按同一系统阈值决定最终drag或原click/preview，Arrow/Whiteboard不误转；Cancel/旧gesture不采终点，也不能覆盖新tracking。
- [x] 增加有意义纯策略/生产适配测试：直移成功、try_lock fallback、第二面失败、抬手仍pending、旧frame、Up无最后Move、异常/取消；在已有离屏入口检查真实bounds多帧不变，不能仅布尔模拟锁忙。

### 1.2 侧边中轴吸附

- [x] 按用户追加要求采用20屏幕DIP中轴/侧边双门槛，选定屏幕DPI只乘一次，不随middlePairScale扩大。
- [x] include既有Bar.BottomDock.h并显式传BarBottomDockThresholdDip，不新增第二常量/配置。先Clamp，再试Y归零与碰撞；中心不可行时保留可行原候选，保持左右成对和配置格式。
- [x] 测试正负偏移、阈值内/等于/外、96/144/192 DPI、控件不同倍率、奇偶尺寸及非零monitor原点；归零不额外加5DIP或阴影偏移。
- [x] 与1.1组合验证，吸附后Up不产生二次微移，边界/碰撞不产生往返震荡。

### 1.3 局部停滞与条件性capture重入

- [x] 复用/补最少限频数值诊断区分候选不可行、inputLocked、pending mask、单client Retry和失去capture；不扩展无关日志。
- [x] 对已证实的未收敛路径修最窄状态缺口；不盲删碰撞约束或替换全局布局算法。
- [x] 将SetCapture与可能重入WM_CAPTURECHANGED的步骤移出非递归锁，保留capture成功后long-press判断、旧tracker先清退、新gesture后建立。
- [x] 尽可能用现有可注入边界验证跨控件capture取消重入与继续拖动，不增加新调度线程。真实Windows capture测试未获GUI授权时单列NOT VERIFIED。
- [x] 检查Up/Cancel/触摸主点替换/单侧Retry/布局发布中途到达时仍有有界续帧且不覆盖新候选。

### 1.4 已有页码与层级修复的回归

- [x] 回归同场busy保留可信数字、Basic控制、新session未知、pageAckEpoch及EndScreen，不重复改COM。
- [x] 静态确认静态/动态创建owner链与Show/Promote；维持唯一根TOPMOST策略。
- [x] 若未发现新缺陷，则不改Window/PptCOM，仅记录已有覆盖与尚未验证的故障现场。
- [ ] GUI批准后才能测真实PPT翻页后四控件位于两画布之上并能命中；需区分外部PPT放映窗覆盖。

## 2. 自动验证

- [x] 复用项目已有测试入口和构建配置，不新建构建系统、不改SDK/依赖。
- [x] 用当前VS/vswhere定位ARM64原生MSBuild；每次在同一PowerShell invocation先Remove-Item Env:PATH -ErrorAction SilentlyContinue，再设置MSBUILDDISABLENODEREUSE=1。
- [x] 必须编译产品时构建InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64 /nr:false，监督超时拟900秒，至少允许5分钟。
- [x] 执行现有InkeysHeadlessTests.exe --no-window；新增边界测试随既有target登记，不单独构建错误依赖的Inkeys.vcxproj。
- [x] 另运行现有Inkeys.exe --bar-eraser-offscreen-test以实际执行PageControl.cpp的RunOffscreenTests。已静态确认IdtMain在配置/单实例/任何产品HWND前return，此harness只创建共享WARP/D2D资源、不创建或显示HWND；与headless纯策略结果分列。不得误运行无参数产品或--ppt-hidden-window-test。
- [x] 检查真实退出码和首个有意义错误；环境/PATH/PDB/权限错误与源码错误分开，不改项目绕过。
- [x] 独立trellis-check完整审阅当前改动与测试，修复发现后才重复必要验证。
- [x] git diff --check、编码/BOM/CRLF、仅任务范围变更检查；更新必要spec，保持minimal diff。

## 3. GUI/设备验证（须另有授权）

- [ ] 真实鼠标/触摸拖侧边及底部控件，抬手后连续观察无额外位置漂移，主栏可操作。
- [ ] 高DPI/不同PPT控件倍率下中轴高度绝对值与扣除间隙后的侧距均不超过20屏幕DIP双门槛的命中及离开，140ms吸附Y过渡，左右pair同步。
- [ ] 两控件交替/多触点/捕获转移/松手取消、Render Busy与单面失败恢复，记录每窗状态而非笼统“卡死”。
- [ ] Office/WPP放映连续翻页、短暂busy、结束重开，区分可信旧显示与新未知；COM损坏提供方仍单列实机验收。
- [ ] Primary/Presentation两画布切换与实际Z序/命中，含外部topmost竞争、白板切换；不把静态owner正确当作所有GUI已通过。

## 4. 收尾

- [x] 汇总确切修复、覆盖测试、未验证设备与未复现现场；详见research/verification.md。
- [x] 已确认本任务未授权commit/push；未执行提交或推送，前任务的授权不延续。
- [ ] 获得完成指示及必要结果后才归档，不因规划文档完成关闭任务。

## 本轮执行状态

- 2026-10-18：1–3项实现、独立全diff审查、R1补正复核和三项自动验证完成；无遗留阻断项。
- 完整solution 29.02秒、headless 99.48秒、生产离屏24.11秒，均exit 0。
- 第3节真实设备/GUI条目仍未执行；本任务保留in_progress，不归档。

## 5. 用户追加吸附体验修订（覆盖1.2初版40DIP）

- [x] 记录新授权：20屏幕DIP中轴+扣间隙后侧边额外距离<=20屏幕DIP双条件，含等号；只给吸附Y修正添加短促UI3过渡。
- [x] 原生产代理修改PageControl.cpp/.cppm及真实离屏用例；原测试代理仅改page_control_tests.cpp和自身研究报告，不覆盖彼此文件。
- [x] 用既有BarBottomDockThresholdDip=20，无新配置/线程/工程；保持普通拖动直接落定、捕获锁外与raw/fit身份修复。
- [x] 复核动画两面同步/中间帧未commit/有界完成，连续Move和Up不重启，脱离/取消/新手势/模式切换不会留下pending或锁死输入。
- [x] 独立trellis-check审查本修订及整体diff，必要修正后冻结编译输入。
- [x] 同原方式重跑完整Debug|ARM64 solution、--no-window及生产--bar-eraser-offscreen-test；记录新结果，保留首轮日志。
- [x] 同步canonical spec、context摘录和最终验证报告，检查格式/范围与context大小。

GUI/Office、commit/push仍未授权，不启动交互窗口，不归档。

本次吸附修订最终验证：完整solution 29.95秒、headless 77.67秒、生产离屏23.66秒，均exit0；Scene failures=0。单调进度R2补正与独立复审已完成，详见research/snap-revision-verification.md。第3节真实设备/GUI条目继续未验证。

## 2026-10-19 PR #238 审阅与结案

用户本轮明确授权采纳审阅、解决合并冲突、结束任务并提交 commit；本节覆盖前文历史性的“未授权提交/不归档”状态。第3节 GUI/设备项目继续保持未验证，不影响用户已授权范围的自动验证结案。修正双20屏幕DIP及140ms验收描述，合入dev时保留双方PPT会话与描述符缓存修复；本轮证据见research/pr238-completion.md。不push。

- [x] PR #238 行内审阅修正、dev冲突解决、独立复核与三项自动验证通过；用户已授权结案，提交后归档。
