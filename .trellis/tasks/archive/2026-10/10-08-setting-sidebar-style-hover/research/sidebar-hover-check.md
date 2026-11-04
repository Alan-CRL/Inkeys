# 三段式侧栏与悬停颜色独立复核

日期：2026-10-08。角色：trellis-check。隔离工作区：codex/fluent-setting-sidebar。前置发布基线同步已完成；读取本轮check.jsonl、PRD/design/implement、批准范围、桌面规范及前置验证/内容实施记录。

当前状态：独立复核完成。本轮生产源码冻结；完整Solution及最终测试补点后的增量Solution均退出0，正确最终产物无窗口回归退出0，i18n与本轮diff/编码检查通过。未执行GUI、Computer Use或HWND测试。

## Findings (fixed)

- Setting.cpp（Checker局部自修）：已打开窗口只更新SettingUIScale和字体请求、不扩大旧client时，用户倍率2可使逻辑pane小于固定分区总高。增加私有SettingEnsureMinimumMessage（WM_APP+0x532），render松手回调仅PostMessage；UpdateSettingScale发布私有atomic最新用户倍率，消息不携带旧倍率。HWND线程只在普通窗口client不足时，以每轴max(current,720/520*EffectiveScale)增长，复用ResolveWindowFrameCreationCorrection及实测outer/client差值，夹紧当前workArea；SWP_NOZORDER|SWP_NOACTIVATE，充足或倍率减小不缩水。
- Setting.cpp（Checker局部自修）：最大化/最小化时不改其几何，仅记HWND-owned pending；WM_SIZE恢复普通窗时投递已有ensure消息一次，成功入队才清pending，普通resize不重复。仍读取最新倍率，保留原WM_DPICHANGED建议矩形及USER32最大化合同。
- setting_design_tests.cpp（Checker发现、runtime_merge修复）：初版检查导航/按钮press却未显式verify实际Combo主体的按下/打开释放帧；末次popup选择测试可能在没有打开时静默跳过。现两个owner打开/重开和末次选择前均验证Combo主体press/release颜色，末次popup必须Expect open才走选择。补点后重新完整Solution增量及最终回归通过，不沿用328帧/307样本的旧结果。
- Setting.Shell.cpp / Setting.Controls.cpp（实现方编译自修）：新增GetColorU32使用所需imfluent_internal.h显式include已补；未更改工程、依赖或公开Setting接口。
- rendering-and-ui.md：主代理同步实际固定区域、Caption例外、alpha加权普通RGBA和私有ensure/pending恢复合同，旧紧凑分组占位描述已删除。

## Production sidebar review

- Setting.Shell.cpp实际调用ResolveNavigationPaneGeometry并使用三个独立child；固定顶部含汉堡、Home/Language，中段按General/Draw/Plugins/Components/HotKeys/Experimental/Configuration/Version八项排序，固定底部含Community/Support/Restart/Exit/Debug。退休Preset无入口，删除Preferences/SupportGroup标题。
- 中段保留##settings-nav-list稳定child及各page数字PushID，只有此child接受鼠标滚动；固定child及pane均NoScrollWithMouse。中段NoScrollbar保留完整宽度，NavItemEx继续36DIP行/16DIP图标，图标中心x=24；顶部、底部及赞助同4DIP inset/24DIP槽。顶/中、中/底、支持/操作三条1DIP分割线分别位于12DIP分隔带。
- 固定预算真实累计：展开top144/footer220、minimum pane364DIP；收起top144/footer184、minimum328DIP。中段占剩余高度，无128DIP旧底栏常量。default/wide/min逻辑客户区除32DIP标题栏后仍足够固定分区；8行中段316DIP，剩余不足时滚动。
- 赞助用实际AccentButton、对应TextOnAccentPrimary前景和同轴DrawIcon，compact保留填充与名称提示；选中保持独立前景标记。退出只临时覆盖TextPrimary为主题criticalText（浅196/43/28、深255/153/164），NavItemEx背景和交互继续普通侧栏层级。
- 调试使用CaptionNavigationButton实际ImFluent Button、Caption 12DIP字体和28DIP显式高度；以实际Caption测量+24DIP左右padding取自然宽并居中，无glyph。expanded及narrow overlay展开显示，compact隐藏；不可见ID由###固定，与用户语言文字分离。
- NavigationState::desktopCompact/narrow/overlayOpen逻辑未改变；narrow reserve仍48DIP，overlay开关不改变正文矩形。选中变量/page路由、Build→Version选中、Esc/遮罩/选项关闭覆盖继续原路径。Community=-2、Restart=-3、Exit=-4仍在Setting.cpp原业务域/FIFO执行，Support/Debug仍用原PageId。
- 新用户倍率原子变量仅供私有窗口最小client消息读取，pending仅HWND线程访问。消息在WndProc IO处理前处理，无渲染线程同步SetWindowPos，不改Setting公开接口、窗口样式、字体字号、行高或DPI路径。

## Color and test review

- 旧算法先保持生产LerpColorU32不变新增真实测试，已有Headless工程编译退出0；旧--no-window退出1/FAILED count2。两倍率各323帧/306样本、117通道越界，worst excess0.108497。第一次Nav hover可见0.813149低于透明层合成起点0.952941和浅灰目标0.905882；原始证据hover-old-no-window.log及hover-final-validation.json。
- 独立读取LerpColorU32：两端alpha权重得到RGB，插值alpha再返回straight RGBA；t两端直接保留原值、双透明中段归零。AnimateColorU32原0.083秒指数动画、storage连续帧、ID/target/控件值行为保留。UPSTREAM本地patch7与关键中文注释可追踪，没有关闭动画或调浅目标。
- CheckAnimatedColors始终同一context/window storage，idle预热后真实NavItemEx、Design Button/ComboBox和实际popup Selectable生成DrawData。SampleFlatDrawData遍历clip内实际白纹理/同色平面三角形，内填充样点避开文字、glyph、圆角和描边；Composite只做普通source-over，verify比较前一实际fill及当前目标合成两端，未复制生产插值公式。
- 最终两倍率1.0/1.875各328帧/315样本，Nav84、Button84、ComboA88、ComboB4、Popup55，均violations0，最大可见通道偏差0.000492036<1.01/255量化容差。首次/连续移入、快速进出、Nav/Button/Combo主体press/release、同owner重开、不同owner同label、popup选择一次及关闭、同session浅深浅均有实际路径。没有发现需额外修改生产Combo的状态复用污染。
- 生产geometry纯helper的三尺寸/六倍率/展开收起36项矩阵，以及真实ImGui child+实际Nav/AccentNavigationItem/CaptionNavigationButton的72种尺寸/倍率/模式/主题组合（每项三帧）均通过。真实设置中段scroll到最大后第八项有绘制顶点，root/top/footer ScrollY/ScrollMaxY为0，固定原点不动；赞助compact仍填充、浅深退出红前景、调试仅四Caption文字glyph的16顶点/自然宽28DIP、14个实际font glyph居中均通过。
- 窗口session测试直接调用既有生产ResolveWindowFrameCreationCorrection，覆盖倍率2不足增长、充足不缩水、受限workArea夹紧；没有执行私有WM_APP/真实HWND。Shell未链接Headless，完整Shell通过完整Solution编译和本报告逐路径静态审查；fixture通过不等于完整Shell真实点击或业务FIFO/DX11呈现验收。

## Findings (not fixed)

- 物理workArea不足以提供720×520逻辑client（含用户倍率）时，窗口尺寸按既有合同夹工作区。Shell把footerTop限制为至少topEnd，保持固定区互不覆盖，但超出pane部分会被父child裁剪，中段可用高度可能为0；这种物理边界不承诺所有底部入口可达，也不通过缩水36DIP行或隐藏展开Debug掩盖。
- 未执行真实窗口倍率/maximize→restore/DPI跨屏、鼠标悬停/滚动、DX11呈现或截图验收。窗口私有消息以生产静态路径+Solution编译验证，CPU测试只覆盖复用几何helper；完整Shell未链接至Headless。
- 前置任务已移交父任务的LnkHelper同步I/O/COM边界及发布硬编码翻译债项保持原范围，不重复扩大本轮修改。

## Verification

- Lint：PASS。本轮Setting.cpp/Shell/Design/Controls、ImFluent/UPSTREAM、design/session tests及规范准确路径git diff --check HEAD退出0；Checker逐文件确认UTF-8无BOM/完整CRLF，独立报告亦保持UTF-8/CRLF。
- TypeCheck/Build：PASS。ARM64原生MSBuild由vswhere动态定位；同invocation保存Path→Remove Env:PATH→恢复单一Path并断言键数1，MSBUILDDISABLENODEREUSE=1，完整InkeysRepo.sln Debug|ARM64 /m:1 /nr:false，允许1800秒。首完整退出0/21.0秒；最后测试补点后的完整Solution增量退出0/17.2秒。日志sidebar-solution-{stdout,stderr}.log及sidebar-checked-solution-{stdout,stderr}.log。Checker读取产物输出日志和最终退出码记录，既有依赖/转换警告不作无关清理。
- Tests：PASS。最后完整Solution生成的根Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window退出0，sidebar-checked-no-window.log末尾PASS animation correctness。Checker读取最终原始DrawData数值、新增assert源码和hover-final-validation.json；最终为315样本而非补点前307。不执行GUI/HWND。
- i18n：PASS。最终check退出0，en-US/zh-TW均366/366；本轮未改语言源/生成物。
- 全量merge原publish29文件历史空白诊断已在前置任务记录，不进行无关清理；本轮自有文件要求独立通过。
