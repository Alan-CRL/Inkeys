# 实施顺序与验收计划

以下保留历史实施顺序。当前执行 research/approved-hierarchical-nav.md 的分层导航与通用拆分，用户已批准实施；禁止 GUI/Computer Use，不自动 commit/push。

## 0. 设计审查（已完成）

- [x] 核对旧任务、迁移 commit、页面入口、字体和设置行布局。
- [x] 建立 `prd.md`、`design.md`、本计划、代码证据与字体度量脚本。
- [x] 制作常规页、主页、导航收起/覆盖、语言及插件子页层级的可操作独立稿。
- [x] 用户大体认可视觉稿，允许后续小调整。
- [x] 批准稿作为首批实现基线，首批范围由后续对话收敛为导航、主页、常规页。

## 1. 全量功能映射与基础样张

- [ ] 读取 `trellis-before-dev` 与 native-desktop 相关规范，检查实际工作区改动。
- [ ] 把 `research/findings.md` 路由清单细化到每个 settingId、字段、默认/范围、条件、保存、业务命令、运行时同步和确认提示，建立迁移覆盖表。
- [ ] 为正文/说明/标题/图标、按钮/开关/下拉/滑块建立原生 ImGui 离屏样张；不创建 HWND。
- [ ] 对照 1.213 字体换算起点测量字面、基线、行高与 clip，核验简体/繁体/英语，确定各语义层级的校准参数。
- [ ] 明确库 API 是否足够；确有必要的 ImFluent 补丁小范围记录，不写第二套近似控件。

验收：DIP/物理像素只缩放一次；相同语义字面与批准稿接近；`Agpqy`、中文、括号和标点不裁切；标题字重不靠伪粗体。

## 2. 新页面容器与公共行布局

- [ ] 建立新 Shell 的内容 inset、导航状态、页面滚动与路由，继续调用原标题栏。
- [ ] 分离 automatic mode / 用户偏好 / overlayOpen，使用同一条目表和稳定 ID。
- [ ] 实现纯 RowLayout，覆盖 action 自然宽、按钮组、下拉、开关、滑块、详情箭头与多行重排。
- [ ] 添加 Token/Typography/Controls 的最小职责边界；所有可交互控件继续使用 ImFluent。

验收：展开/收起图标轴固定，窄窗展开不推挤内容；Esc/遮罩/选项均可关闭；正文、右侧 action、图标纵向对齐；短按钮不落在右侧列的左端。

## 3. 完整纵向切片：常规 + 主页

- [ ] 常规页接入原字段和 action：自启、快捷方式、缩放、光影、置顶、右键关闭、兼容性、教学安全。
- [ ] 主栏缩放实时预览与释放保存、语言/设置倍率等特殊时机保留。
- [ ] 新主页接入真实已有路由、链接和资源，不展示虚构状态。
- [ ] 对照批准稿导出原生离屏图，在 960×700 与 720×520 及 DPI 矩阵复核。

验收：视觉与功能一起通过后再迁移剩余页面；不先铺满全工程再回头修基础控件。

## 4. 分批迁移所有页面

- [ ] 语言、配置保存、软件版本/构建详情。
- [ ] 绘制、预设、组件。
- [ ] 插件总览、PPT 联动、超级置顶、快捷方式、DesktopDrawpadBlocker。
- [ ] 快捷键说明、实验选项、支持开发、调试。
- [ ] 条件可见/禁用、长文案、图片比例、失败/进行中/重启提示逐项对照。
- [ ] 每个 settingId 在覆盖表记录迁移位置与验证方式；保留既有简繁英国际化链。

## 5. 收尾与兼容验证

- [ ] 全覆盖后删除被替代的旧页面布局/wrapper；不删除尚未迁移的业务实现。
- [ ] 检查新增文件的 module/vcxproj/filters 登记；不调整无关工程项。
- [ ] 检查 DrawData/CSO、共享 epoch、Hide/Show、font rebuild 路径的接口未漂移。
- [ ] 更新与新合同直接有关的 Trellis spec 和实现记录；不 commit。

## 有意义的自动验证

1. **纯布局无窗口测试**：不是断言常量等于常量，而是验证长文/英文/小宽度时各矩形不相交、action 右边缘一致、行高包含内容、插入/隐藏行不破坏稳定 ID。
2. **字体原生离屏样张**：在共享/兼容渲染测试边界导出文本和控件，与批准图人工静态比较；浏览器图不能替代原生栅格化结果。
3. **绑定测试**：测试事件驱动不每帧写盘、slider 拖动实时通知与释放一次保存、异步动作保持顺序和 completion、取消确认不产生业务效果。模拟后端，禁止测试启动真实更新/重启/桌面写入。
4. **尺寸矩阵**：720×520、800×600、960×700、1200×840、1440×900；系统 DPI 100/125/150/200%，用户倍率 1.0/1.5/2.0。按有效 DIP 触发重排，记录无法在物理工作区满足最小逻辑尺寸的回退。
5. **国际化**：SC、TC、en-US；长文件路径、长选项文本、数字、小写 descender、多行说明与条件卡片。

## 命令与运行约束

```powershell
# 使用本机 ARM64 host MSBuild 的实际路径，构建完整 Solution。
& '<VS installation>/MSBuild/Current/Bin/arm64/MSBuild.exe' InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m
# 构建进程超时至少 300 秒，前台等待分段汇报。
# 使用实际生成路径；禁止省略 --no-window。
& '<actual output>/InkeysHeadlessTests.exe' --no-window
pwsh -File Scripts/i18n.ps1 check
git diff --check
```

本段原为首批无窗口约束；当前用户已批准实际设置窗口测试和 Computer Use。完整 Solution 与 --no-window 仍为基础检查，真实窗口验收须单独记录；锁屏时暂停输入并保留未验证项。

## 回退点

- 基础样张阶段失败：只修字体/布局边界，先不迁移页面。
- 单页绑定失败：内部路由暂回旧页；已确认的共享 renderer 生命周期保持。
- 当前 HWND/DWM 与深色已在批准范围内；动画引擎、触摸或动态脏区仍记录后续问题，不扩大实现。


## 首批实施完成记录

2026-09-06，当前批准范围已实现并完成代码检查。下一批未自动开始。

- [x] 阅读规范并记录原始绑定审计；全量逐 settingId 映射仍属后续批次。
- [x] 新导航状态、DIP几何、字体与纯行测量；Home/General 真实业务接线。
- [x] 新字体仅绑定新 Shell/Home/General；原字体供未迁移页和标题栏使用。
- [x] 基于真实 ImGui/ImFluent/Controls 的无窗口回归，无独立 D3D/窗口测试框架。
- [x] 修复字形越行框、菜单/footer右边缘差异、长展开标题、末项SetCursor与分数DPI断言。
- [x] 最终完整Solution构建、--no-window测试、i18n304/304、编码CRLF与diffcheck通过。
- [x] Trellis renderer/UI 合同已同步；不 commit、不启动窗口。

后续：用户审阅首批实际效果，再处理小调整与其他页面；阶段4全量迁移和删除旧wrapper尚未执行。


## 当前用户批准的下一轮

1. 集中修Typography/按钮文字/导航glyph/滚动条并稳定共享API。
2. 将所有剩余页面及插件详情迁移至统一Page/Section/Row/Action/Details；逐项核对基线绑定。
3. 完整ARM64 Solution与无窗口回归；脚本启动应用，逐页导航、滚动和截图，按实际效果迭代。
4. 同步规范和验证记录；不自动commit。详见research/approved-full-migration.md。


## 全页代码阶段完成（2026-09-06）

- [x] 阶段1：文字/按钮文字/导航图标/滚动条统一修正，公共控件API稳定。
- [x] 阶段2：全部实际页面迁入统一布局；旧34%列和Widgets旧页包装删除。
- [x] 完整ARM64 Solution、集成无窗口与CPU控件回归、i18n、编码和diff检查通过；独立逐页审查无未解决代码问题。
- [ ] 实际窗口截图及按图精修：Windows锁屏当前阻挡，已请求用户解锁；解锁后按research/full-migration-validation.md继续。

未自动commit。任务保持in_progress，尚未完成全页真实视觉验收。


## 窗口与深色实施顺序

1. 补前阶段窗口测试，读取实际窗口与官方frame合同。
2. 并行实现Setting frame/最大化/hit-test与深色token/配置；在Setting.cpp串行接线。
3. 添加几何、scrollbar与frame不相交、实时主题/保存回退回归。
4. 完整ARM64构建并通过Computer Use检查浅深/窗口状态/滚动和余下页面，修复实际问题。
5. 同步规范/记录，git add暂存，不自动commit。


## 窗口与深色代码阶段完成（2026-09-07）

- [x] 真实系统 sizing frame / client 分离、标准窗口样式、USER32 最大化协议保留、标题栏不透明主题色。
- [x] 标题栏独立即时浅深切换、统一 palette、原 FIFO 保存 SettingDarkMode，默认浅色；字体/设备/渲染管线保持原生命周期。
- [x] 多显示器协议复查修复、最终 ARM64 host 完整 Solution、集成 --no-window、六倍率 CPU 控件回归及国际化检查通过。
- [x] 本次源码/规范/任务记录检查完成；按用户要求暂存，不自动 commit。
- [ ] 实机验收：桌面锁定，实际窗口切换/拖动/最大化/滚动条及余下页面检查待解锁后继续。详见 research/window-dark-validation.md。


## 选项窗口顶部边框小修（2026-09-08）

- [x] 明确只修选项窗口最上沿；主栏和主按钮未改，本轮禁止Computer Use。
- [x] 恢复态可见top按DWM从12px收至2px，保留顶部操作高度、侧边scrollbar和标题控件命中。
- [x] 仅Setting创建后校正估计/真实frame的一像素差，保留工作区约束。
- [x] 独立目录完整ARM64Solution、无窗口与真实HWND集成测试、独立审查通过。
- [x] 读取用户已打开的选项窗口，确认2px顶部及命中结果。实际拖动/整页新截图受会话锁定影响，记录边界。
- 验收详情：research/top-border-validation.md；不自动commit。

## 旧任务接管后的剩余验收（2026-10-08）

用户已批准归档两个 08-13 任务，由本任务统一跟进。接管记录见 research/task-handover.md；前文复选框与授权属于历史阶段，未验收项继续保留。

- [x] 保留两个旧任务的需求、实现与验证资料，登记归档来源及接管关系。
- [ ] 完成原 A6/A7/A10 的剩余页面、导航、窗口和多 DPI 实机验收。
- [ ] 补重复 Hide/Show、打开延迟、资源增长及设备 epoch 恢复证据，对应 A11。
- [ ] 记录空闲帧时间和打开/缩放/切页表现；沿用旧任务约 10% 持续回退比较目标，缺少可比基线时不宣称通过。
- [ ] 补既有 DWM 材质级联、composition 变化和透明 clear 验证，对应 A12；不能覆盖的环境明确记录。

本次不执行产品实现或 GUI，不新增系统主题、高对比、动画或媒体功能；后续验证按当前授权范围执行，不沿用历史 GUI/提交授权。


## 当前实施：分层导航

按 [research/approved-hierarchical-nav.md](research/approved-hierarchical-nav.md) 执行导航、内容、连续帧无窗口回归、独立检查与完整 ARM64 构建。父任务仍保留实机验收。

## 分层导航增量交付（2026-10-08）

- [x] 通用四页、旧入口映射、独立子页状态与原保存/实时行为接入。
- [x] 展开/紧凑/覆盖导航、独立箭头、浮层、键盘与点击屏障、反向/速率/淡入动画。
- [x] 完整 ARM64 Solution、真实产品控件连续帧、三倍率布局、悬停色、i18n、编码与差异检查、独立审查。
- [x] 原 fluent 工作区程序重编，保护用户原有文件，交付未提交差异。
- [ ] 真实鼠标、截图与 GUI 动画/DPI/业务副作用验收；父任务既有剩余验收继续保留。

最终证据：[research/hierarchical-navigation-validation.md](research/hierarchical-navigation-validation.md)、[research/hierarchical-navigation-review.md](research/hierarchical-navigation-review.md)。本轮不自动 commit/push，不把自动检查视为实机验收。

## 父行默认折叠追加（2026-10-08）

本轮按用户小修改调整默认折叠、父行toggle与最近子页保留，并补实际控件输入/反向动画回归；复用完整ARM64 Solution和无窗口验证，不GUI/commit/push，不结束或归档父任务。

本次追加代码、真实控件回归和独立审查已完成；最终证据见 [research/parent-toggle-validation.md](research/parent-toggle-validation.md)。任务与会话继续进行中。

## 选中条滚动同步与赞助追加（2026-10-08）

上一轮已提交f2cdf5ef，本轮仅修滚动位移同步和普通蓝字赞助，并补实际控件回归/独立审查/完整ARM64无窗口验证；不GUI/commit/push，不结束父任务。

本轮追加实现、独立审查及最终自动检查已通过，证据见 [research/scroll-sponsor-validation.md](research/scroll-sponsor-validation.md)。前轮已提交f2cdf5ef；新修改保留未提交，父任务与会话继续进行中。

## 圆头与亮蓝追加（2026-10-08）

仅调整Shell实际绘制和既有DrawData回归，复核圆头几何、浅深主题下赞助图标/文字颜色及普通背景；独立审查、ARM64完整Solution和--no-window通过后提交本轮与前轮未提交差异，不push，不结束父任务。

圆头/亮蓝追加最终完整ARM64构建与无窗口真实DrawData检查通过，详见research/scroll-sponsor-validation.md最新追加章节；按当前用户授权提交前轮滚动修复与本轮最小调整，父任务继续in_progress，不push或结束任务。

## 侧栏子任务收尾（2026-10-08）

用户确认侧栏任务结束：实现已提交2f10f402，本次归档10-08-setting-sidebar-style-hover至archive/2026-10并提交收尾记录。通用分类/分层导航、默认折叠、滚动同步与圆头/亮蓝增量交付完成；剩余真实鼠标/截图、全页/DPI、窗口/常驻会话验收仍在父任务，父任务保持in_progress。本次不改源码、不push。

## 当前增量：设置 DPI 与开屏策略（2026-10-24）

- [x] 退休设置手动倍率控件、草稿、配置和用户倍率重建路径；保留系统 WM_DPICHANGED 与其他界面缩放。
- [x] 复用传统设置配置及 SessionState.ShowSerial，接入首开策略而不影响其他主页动画。
- [x] 三语言生成链、通用页交互、首开/重复激活/重建及真实尺寸边界回归已更新。
- [x] 独立审查、完整 ARM64 Solution、无窗口测试及编码/diff最终检查通过；结果登记 research/settings-scale-entry-policy.md。

## 当前增量：Win32 外框与 Win7 Aero（2026-10-24）

- [x] 核对用户截图、固定灰色边框、原生 caption 样式与 DWM/USER32 API 文档。
- [x] 现代外框恢复系统默认色；旧系统隐藏创建后初始化菜单，再去原生 caption，保留四边 frame 与原有尺寸校正。
- [x] 显式接回旧系统 Alt+Space、图标/标题栏菜单和系统命令，并增加旧/新 frame 几何与菜单状态回归。
- [x] 独立审查、完整 ARM64 Solution、--no-window 及编码/diff检查通过；结果登记 research/settings-win32-frame-followup.md，Win7真实外观/菜单交互仍待实机验收。

## 当前增量：设置内容与排版（2026-10-24）

- [x] 确认十项需求、图标真实映射/字库、PPT加载门禁与配置所有写入口，以及CI元数据注入合同。
- [x] 完成UI/配置/托管源码、独立审查及真实renderer和持久化边界回归源码。
- [x] 构建PptCOM并手动同步同次DLL/TLB至native资源，SHA-256一致。
- [x] 完整ARM64 Solution、managed/native无窗口、嵌入DLL版本与资源、i18n、编码/diff检查，登记research/settings-content-layout-followup.md。

## 当前增量：置顶间隔与 CI/CD 人工预览（2026-10-24）

- [x] 定位现有秒表码点 U+E916、构建元数据定义与 url 非空自动切换逻辑；当前字段位于 Setting.cpp，不为此移动到新头文件。
- [x] 最小修改图标与临时字段初始化，明确标记为本地预览，并独立审查。
- [x] 完整现有 Debug|ARM64 Solution 编译、字节格式/差异检查，交付可供用户查看的程序；仅数据/图标追加不新写镜像测试。
- [x] 用户完成实际查看后，已清除 CI/CD 临时值并恢复空字段；见下方清理增量。

## 当前增量：CI/CD 返回链接与测试清理（2026-10-24）

- [x] 定位 CI/CD 标题前的重复文字链接、人工预览字段与 build-windows.yml signal1 替换表达式。
- [x] 删除文字链接与 SameLine，仅保留返回箭头；signal1 内字段/标记恢复原空定义，独立核对最小差异。
- [x] 原样元数据和工作流替换内存验证、完整 Debug|ARM64 编译、产物无测试值、编码/CRLF/diff检查；完成上一轮清理待办。

## 当前增量：主页与问题反馈（2026-10-24）

- [x] 核对 Home renderer/动作、General Basic/Appearance/Shortcuts、原问卷/二维码、标题栏几何与命中、语言社区路由。
- [x] 按 research/settings-home-feedback.md 的接口约定并行完成主页/反馈 renderer+i18n 与 native 路由/标题栏接线，最小修改并保持原文件格式。
- [x] 补真实控件点击、二维码常驻、多语言/DPI/窄宽布局、标题栏几何及浮层交互无窗口回归，独立代码检查。
- [x] ARM64 原生 MSBuild 完整 Debug|ARM64 Solution、适用 --no-window、i18n/编码/diff/产物检查；更新相关 spec/验证记录，父任务保持进行中。

## 当前增量：名片复制与反馈卡片精修（2026-10-24）

- [x] 定位作者卡、原黑色透明二维码、通用Card/SettingRow描边路径、Clipboard API和现有反馈/赞助路由。
- [x] 公共CopyHyperlink/卡片边框、Pages布局/i18n、原生资源加载/实际GitHub图标并行实施，保持格式及最小差异。
- [x] 真实鼠标/剪贴板mock/tooltip状态、字号/间距、二维码右对齐/三边间隙/主题tint和各类卡片描边回归，独立审查。
- [x] 完整ARM64 Solution低并发构建、适用无窗口、i18n/字节/diff与产物检查，更新验证和规范；不结束父任务。

## 当前增量：笔速橡皮（2026-10-24）

- [x] 页面迁移、内部导航、设备复选组、三语言与现有业务接线。
- [x] 只读尺寸退化判断及无窗口交互/边界/布局回归。
- [x] 独立审查、完整 ARM64 Solution、相关 no-window 与 i18n/编码/diff 检查，登记 research/settings-speed-eraser.md；父任务不结束，图片暂停。

## 当前增量：笔速橡皮循环演示

- [x] 手写场景/轨迹/直径/擦除时序离线预计算，atlas缓存、轻量播放shader与页面轮播接线。
- [x] 真实离屏像素/导出PNG、生命周期/性能和页面无窗口回归。
- [x] 独立审查、完整ARM64构建、i18n/格式/保护文件检查及证据记录；见 research/settings-eraser-demo-verification.json 和 settings-eraser-demo-review.md，不结束父任务。

## 当前增量：演示场景与动作精修

- [x] 几何笔迹与独立往返轨迹、延迟增粗和正确擦除时序，重新生成atlas与帧表。
- [x] 播放shader最小接线、场景文案、离屏像素/时序/性能回归与视觉检查。
- [x] 独立审查、完整ARM64 solution、无窗口/i18n/格式/保护资源检查，记录结果且不结束任务。

## 当前增量：笔速橡皮与联系方式精修

- [x] 最小化修改精细控制门控/标题、绘制分类顺序及作者QQ/Email标签。
- [x] 更新受影响的真实控件无窗口回归并独立审查。
- [x] 完整ARM64 solution、相关无窗口/i18n/格式/保护资源检查及结果记录；父任务保持进行中。
