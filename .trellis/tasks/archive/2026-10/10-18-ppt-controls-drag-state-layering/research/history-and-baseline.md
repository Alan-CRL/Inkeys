# Research: 版本基线、已有修复与验证边界

- Query: 区分反馈版本与当前源码，避免重复修复已覆盖的 PageControl/COM/owner 问题。
- Scope: internal；仅主会话执行只读 Git 命令，研究代理未执行 Git。
- Date: 2026-10-18

## 用户最新补充

- 反馈版本没有确切证据，用户认为很可能20261010b，也可能20261014a。
- “拖动卡死”仅被拖动控件停止，主栏仍可操作；不可直接归为全进程/共享窗口线程死锁。
- 用户认为翻页 `-/-` 很可能已由刚完成的COM识别修复覆盖，调查需要分别检查旧版路径与当前剩余条件。
- 本轮只批准创建任务、调查和规划，用户批准实施前保持planning；不改代码、测试、配置或spec，不开GUI/Office，不commit/push。

## Git基线与历史证据（主会话直接确认）

当前HEAD：13783fe7960135caaa0040672fca03eb2b9ebdae，branch=bugfix/pptcom。前次修复WORK=0eb1c7c587b8b284d103ae865ea29ea59e444677；归档184cf86e，会话13783fe7。起始主仓库无dirty，唯一新改动为本任务目录。19个关键文件的SHA256见planning-baseline.json。

| 对照 | 源码证据 | 结论边界 |
| --- | --- | --- |
| 20261010b | a50ad143 Prepare to Publish 20261010b，2026-10-10 | 发布准备提交；并非反馈设备安装包/二进制身份证明 |
| 20261014a | 5d319bbf Revent 20261014a，2026-10-14 | 本分支前次调查使用的源码基线；仍不证明具体用户安装内容 |
| 交互稳定修复 | 579d1c5eb358d368a0e42a2fc298b13d2dbc3f38，2026-08-26，fix: stabilize PPT page control interactions | 只读merge-base确认是a50ad143、5d319bbf和HEAD共同祖先 |
| 两画布owner嵌套 | 8a25cfcd22b01fac3c8792d06e9f002ac16750c7，2026-09-21，fix(window): nest drawpad owner chain | 同样是两个反馈候选版本及HEAD共同祖先，不应当作本轮尚未实施的修复 |

Git rev-parse 对路径blob核对：PageControl.cpp、PageControl.cppm在a50ad143与5d319bbf完全相同。当前相对5d319bbf，这两文件仅增加pageAckEpoch与callback参数/比较，未改变拖动、吸附、锁或窗口几何算法。不能把旧版拖动症状归因于刚才的COM提交。

8a25cfcd的实际diff：静态CreateGroup和动态CreateDynamic均将Drawpad的owner从Freeze改为DrawpadPresentation；Presentation仍归Freeze；四个PPT与Bar仍归Drawpad。相应window_tests加入静态/动态链和双画布互斥显隐时Bar仍高于两画布的断言。典型的“Presentation与Drawpad互为兄弟、UI只保证高于其中一个”机制已覆盖。Win32运行时窗口关系仍需故障快照确认，且不能将其等同外部PPT放映窗穿插问题。

两个候选版本的Window.cpp并非相同：a50ad143到5d319bbf有114行相关变更，涉及CommandCompletion/超时/Setting刷新等；未撤销嵌套owner链。不能仅凭两个版本PageControl相同推断所有窗口管理路径相同。

## 已有测试与本轮验证边界

- 生产测试入口在InkeysHeadlessTests/animation_tests.cpp:1644；--no-window跳过RunWindowTests，1673是该条件入口。PageControl/PptUi/PptSession纯逻辑测试在1685-1687仍运行。
- page_control_tests.cpp已有drag tracker的latest-wins、两窗mask、抬手保持pending ownership、try_lock fallback请求pair、旧版本拒绝等纯逻辑测试；这些并未证明真实RenderSurface的缓动已经到最终几何。
- window_tests.cpp已有owner链、双画布切换、隐藏竞争topmost窗及根刷新验证；会创建HWND，本轮不执行，也不把前次无窗口结果当成该组测试已运行。
- 0eb1c7c5已经完成的完整solution/无窗口/双CLR COM验证属于前任务记录，只用于说明当前基线经过相关自动化；不是本次微移、吸附、局部停住或GUI层级的实机复现证据。
- 本轮未编译、未运行产品/测试二进制。只在结束时检查task context、文本格式、dirty范围和基线文件SHA256。

## 只读命令类型

执行git log/show/diff、rev-parse路径blob比较、merge-base --is-ancestor；无git add/commit/push、checkout或reset。阅读现有源码/spec/归档文档，所有本轮写入限定本任务目录。
