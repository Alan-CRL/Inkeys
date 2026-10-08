# 三段式侧栏与悬停修复验证结论

日期：2026-10-08。前置 `10-08-setting-publish-baseline-sync` 的内容映射、完整 ARM64 Solution、无窗口回归和独立复核完成后，本子任务才开始实施。

## 已实现

- 顶部固定汉堡、主页和语言；中段依次为常规、绘制、插件、组件、快捷键、实验选项、配置保存、软件版本，只有中段独立滚动；底部固定社区名片、强调色填充赞助、分割线、重启、红色文字及图标关闭、调试。三条细分割线取代旧分组标题，无预设入口。
- 保持 248/48 DIP 导航宽度、900 DIP 覆盖断点、36 DIP 普通行、16 DIP 图标和 x=24 图标轴。调试是居中自然宽、28 DIP 高、12 DIP 文字、无图标按钮；收起隐藏，桌面及覆盖展开显示。现有 PageId、业务返回码/FIFO、选中、键盘路径、覆盖正文和用户展开偏好保留。
- 固定区域真实测量后分配中段剩余高度，替代旧 128 DIP 预算。用户倍率提高后仅通过私有消息让 HWND 线程必要时扩大已有普通窗口；最大化/最小化保留 pending 并于恢复后重试，不改公开接口及最大化/DPI几何合同。
- 共享 ImFluent 颜色动画按 alpha 加权 RGB，插值 alpha 后返回普通 RGBA；透明端点正确处理，原时长、目标色与交互保留，中文注释和 UPSTREAM 第7项补丁记录已同步。

## 构建与回归

| 检查 | 结果与证据 |
| --- | --- |
| 完整 Solution | ARM64 原生 MSBuild 由 vswhere 发现；`InkeysRepo.sln Debug|ARM64` 首最终轮退出0/21.0秒，测试断言补严后完整Solution增量退出0/17.2秒；同一 PowerShell invocation 保留单一 Path、关闭 node reuse，允许1800秒。 |
| 最终无窗口产物 | 根 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` 退出0，`PASS animation correctness`；日志 `research/sidebar-checked-no-window.log`。 |
| 旧悬停复现 | 真实控件 DrawData；100%/187.5%各323帧/306样本，每倍率117通道越界，旧回归退出1。Nav可见0.813149低于起始0.952941和目标0.905882。 |
| 修后颜色 | 100%/187.5%各328帧/315样本、0越界；最大误差0.000492036，小于1LSB。实际Nav、Button、两个Combo主体、Popup Selectable覆盖首次移入、快速进出、按下/释放、弹层重开/选择及原context主题切换。 |
| 侧栏无窗口几何 | 720×520、960×700、1440×900逻辑客户区；100/125/150/187.5/200/400%倍率，36项生产几何矩阵，72项实际child/模式/主题fixture。固定root/top/footer的ScrollY与ScrollMaxY为0，中段滚动可达第8项，真实赞助填充、红色退出、Caption调试及14个图标均检查。 |
| 国际化 | `Scripts/i18n.ps1 check` 退出0，简中基准及繁中/英文366项，366/366。 |
| 原工作区 | 15个受保护文件SHA256全部不变，原status及fluent/chore-publish refs不变；隔离HEAD=834307a4，MERGE_HEAD=e2c212d4，冲突清零，无commit/push。见 `research/delivery-preservation.json`。 |

独立复核见 `research/sidebar-hover-check.md`，侧栏实现见 `research/sidebar-implementation.md`，旧失败、最终数值、源码哈希和退出码见 `research/hover-validation.md`、`research/hover-final-validation.json`。本轮新日志仅规整行尾空白，保留正文/换行/诊断和退出记录。

## 验证边界与父任务后续

- Headless复用生产Design/Controls/Typography和真实ImGui/ImFluent，未链接完整Shell/Pages；完整Shell接线通过静态独立复核与Solution编译。无窗口fixture不能代替完整页面实际点击、业务FIFO的实机副作用或窗口消息测试。
- 未启动GUI、Computer Use、HWND fixture或Office。真实鼠标悬停观感、截图、键盘/覆盖实机操作、跨屏DPI、用户倍率/最大化恢复、DX11/DWM/DComp、窗口/常驻会话验收由父任务继续承载。
- 工作区物理尺寸不足以提供720×520逻辑客户区（含用户倍率）时沿既有合同夹紧：固定区不重叠，但超出pane的底部内容可能裁剪，不能宣称此边界下所有固定项可达。
- 前置已记录的LnkHelper同步I/O/COM初始化边界及发布硬编码中文由父任务后续处理；本轮不扩大为公开接口或线程重构。
- 全量merge继承29个发布原文件的空白诊断，原blob与固定来源相同；见前置 `research/inherited-whitespace.json`，不进行无关清理。本轮自有源码和任务记录单独检查，最终结果另记 `research/delivery-diff-validation.json`。

本次交付保留隔离工作区的待审查merge，不commit/push；父任务继续in_progress承载实机验收。两个子任务的completed只表示本轮内容实现、编译及无窗口/静态复核完成。

后续用户授权：将同一已验证tree转入 D:/Project/Inkeys/Repo/Inkeys 的 fluent 工作区，不保留临时分支。此前“原工作区不变/隔离保留”描述的是上一轮交付状态；当前转入及原目录重编证据统一见父任务 research/fluent-integration.md。旧PptCOM.dll及原用户测试产物先备份，IdtMain.h用户内容保持。仍未授权commit/push/GUI。
