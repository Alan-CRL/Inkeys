# 实现与验证记录（2026-10-19）

## 当前交付状态

用户于2026-10-19确认关闭设置后普通快捷键正常，随后确认没有问题并明确授权提交commit、结束任务。代码/设置/正式spec及自动验证已完成，按实现→归档→会话记录收尾，不push。代理未启动GUI/Computer Use或实际安装系统Hook；用户未逐项确认的平台/设备边界继续保留。

## 实现范围

- 独立键盘服务：专用jthread/WH_KEYBOARD_LL、Ctrl+Alt+X系统注册，固定数组/有限原子快照/255有效槽SPSC；全局X唯一WM_HOTKEY入口，普通功能组合按批注条件归属。
- 物理Down/repeat/Up所有权、独立注入修饰阻断、配置/服务/前台/页面身份及启动校准；回调不执行业务、配置、HiMsg或日志格式化。
- 合成式退出保留墨迹；分离式真实清空成功/真实空页后进入选择；返回最近批注工具保留Laser/子型/宽色，定格不自动恢复。
- Ctrl+Z、Ctrl+Y、Ctrl+Shift+Z、Ctrl+Delete及Ctrl+Q。双Delete要求持续Ctrl与中间DeleteUp，无额外时间窗；首击不长期阻止新笔迹，第二步不重复Clear，使用具名结果和只读ControllerFence。
- 稳定页scope和独立内容revision、实际Controller条件验证、64具名回执槽、独立可组合contact租约/Selection交接门；复合期间普通Clear/Undo/Redo短暂拒绝新入队，避免空态回执后的竞争。
- 六个NoUpload Inkeys3 bool默认全部开启，两个Redo各自开关，Clear一项同时管理两步；设置实际注册/安装/注销/同步状态、三语和正式生成资源均更新。
- 原鼠标Hook/PPT滚轮/Raw/WM_TOUCH来源逻辑保持。ContactInput必要变更仅独立组合shortcut门位，不恢复旧Drawpad或PPT键盘翻页转译。

## 构建证据

由vswhere定位当前安装的ARM64原生MSBuild，完整InkeysRepo.sln Debug|ARM64，同一PowerShell先规范化Path/PATH并禁用node reuse。构建runner设15分钟上限，ProcessStartInfo UseShellExecute=false/CreateNoWindow=true/Hidden，保存实际ExitCode/stdout/stderr和详细日志。

1. initial：源码错误ResolveFence在module实现单元不可达；补显式Draw3.Shortcut.h include后解决。没有删除缓存、改工具链或升级依赖。
2. reviewed：完整Solution exit0、0errors、130warnings、120.38秒；三项第一轮无窗口验证均exit0。
3. final：后续事务互斥及诊断修正的源码编译完成，ARM64增量链接器发生LNK1000 Internal error during IncrBuildImage。判定工具链内部失败，不修改源码/工程绕过。
4. complete：仅本次命令附/p:LinkIncremental=false，完整Solution **exit0、0errors、1warning、10.75秒**。唯一最终warning为Headless旧EDITANDCONTINUE与命令行INCREMENTAL:NO的LNK4075。此前重编译输出既有第三方/类型转换warning，未为清零改无关代码。

## 最终无窗口验证

| 命令 | 实际结果 | 耗时 |
| --- | --- | --- |
| InkeysHeadlessTests.exe --no-window | exit0，0失败 | 94.96秒 |
| Inkeys.exe --state-mode-production-test | exit0，ShortcutBusinessProduction/StateMode/FineDial PASS | 0.41秒 |
| Inkeys.exe --pen-width-production-test | exit0，生产笔宽探针PASS | 0.19秒 |
| Scripts/i18n.ps1 sync/check | en-US/zh-TW均391/391，PASS | — |
| task.py validate | implement/check各12条，PASS | — |
| git diff --ignore-submodules=all --check | PASS | — |
| tracked编码/BOM/CRLF | 28文件保持原格式，详research/format-check.json | — |
| 独立fresh全范围审查 | 确认问题全部修正，无剩余静态阻塞项 | — |

Headless覆盖生产Matcher/按压归属/首次可信启动/启动持键/采样边沿/注入修饰、饱和压力/快照/FIFO、严格六bool读写及冻结副本、生产Scope/具名回执/只读Fence、租约和真实ContactInput门禁组合。生产状态探针复用实际Bar owner Dispatch/Drain及真实IdtState，控制延迟后端覆盖连续X、Clear结果、CtrlUp前后、新笔迹同轮Delete、Clear→Undo、页/工具变化、暂时不可用及租约释放。

这些探针不创建Host/窗口/RTS，不执行真实系统Hook或启动校准的Win32桌面/完整性采样；不能替代真实设备/窗口行为。桥压力测量只记录本次纯生产桥步骤，含计时/调度影响，不作系统Hook零延迟承诺。

## 人工验收建议

1. 两种样式、有墨迹/空页、画笔/形状/橡皮/激光：Ctrl+Alt+X往返与最近工具/宽色恢复；分离式清空，合成式保留。
2. 六组合逐项开关/重启，普通文本输入Ctrl+Z优先；连续按X，持续Ctrl两次Delete（包括第二下后立即松Ctrl），第一次清空后继续落笔再按Delete。
3. 定格可用场景与PPT/白板不可用；PPT方向/PageUp/Down/空格/Enter/Esc原生行为，鼠标滚轮和原拖动无回归。
4. Windows11普通/管理员焦点、Win7、国际布局/AltGr、真实长按/手感、设置三语/缩放，以及系统注册冲突/失败状态。

启动若在更高完整性或无法证明桌面/访问权的前台，不能信任API零值：设置会显示等待同步，可在普通权限窗口松开按键后重试。日常事件匹配不轮询GetAsyncKeyState，也不宣称本次已证明任意UIPI前台首次启动均可完成校准。

## 文件与工具环境

完整原始构建/测试日志在本机Build/ARM64/Debug/keyboard-shortcut-logs/（ignored，不纳入源码提交）。最终二进制Build/ARM64/Debug/Inkeys.exe。普通git递归子模块读取HiMsg worktree外元数据会遇sandbox权限限制，检查父仓库时使用ignore-submodules=all；未修改子模块或权限。本次用户已授权commit与任务归档；不push。

## 人工确认与收尾

2026-10-19 用户反馈只有全局往返有效；截图显示注册和钩子均就绪。进一步确认测试时设置窗口在前台，按既定输入优先级普通功能键交给设置处理，全局X继续生效。用户关闭设置后确认普通快捷键可用，随后明确表示没有问题并要求提交commit、结束任务。

该轮只读调查未修改产品代码；沿用上述最终源码编译、三项无窗口验证和独立全范围检查结果，不重复启动GUI或构建，也不据此扩大Win7/UIPI/Office设备覆盖声明。
