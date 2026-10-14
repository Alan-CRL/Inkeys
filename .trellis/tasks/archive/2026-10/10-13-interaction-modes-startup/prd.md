# 产品模式与启动预设：交互模式切换和命名

## Goal

第一阶段完成交互模式切换修复与三语命名；当前实施用户已审批的启动位置预设和环形启动进度。

## Requirements

- R1：修复设置切换交互模式时卡死并拖住共享 UI 渲染线程的问题，定位真实同步链和锁循环；选择后运行时状态与持久化配置一致。
- R2：标题使用「选择交互模式／選擇互動模式／Choose an interaction mode」。
- R3：保留用户提供的三语名称和说明。恢复原有下拉框样式；两条说明放进模式选择块上方的独立信息卡片，使用现有 PPT 提示卡片类似的蓝色信息图标、圆角边框和内边距（参考用户图二），文字按实际高度自适应换行。
  - zh-CN：大屏触控模式；适用于教室、会议室等场景，直接在触控大屏上书写和操作。
  - zh-CN：桌面交互模式；适用于个人电脑、平板及鼠标操控的大屏，兼顾鼠标、触控笔、数位板和触摸操作。
  - zh-TW：大螢幕觸控模式；適用於教室、會議室等情境，直接在觸控大螢幕上書寫與操作。
  - zh-TW：桌面互動模式；適用於個人電腦、平板及以滑鼠操控的大螢幕，兼顧滑鼠、觸控筆、繪圖板與觸控操作。
  - en-US：Large-Screen Touch Mode；For writing and interacting directly on large touchscreens in classrooms, meeting rooms, and similar settings.
  - en-US：Desktop Mode；For PCs, tablets, and mouse-operated large displays, with mouse, pen, graphics tablet, or touch input.
- R4：产品分类以交互场景为依据；Surface 即使用手指触摸也属于桌面交互，普通投影或投屏不能仅因画面大而归入大屏触控。
- R5：保留全部启动自动判定规则，包括尺寸未知时回退大屏；不覆盖已有手动选择。用户已确认本次仅修复切换和文案。

## Acceptance Criteria

- [x] 下拉切换的 Sync 在设置绘制期间转交已有 FIFO；生产队列/guard/原子模式更新的线程回归通过，保留同步与配置保存顺序。真实 HWND 事务与 GUI 点击未执行（R1）。
- [x] 恢复原有 Widgets::combo；上方说明独立信息卡片，三语×四种缩放几何回放通过。默认字体回放不代表嵌入字体像素验收（R2/R3）。
- [x] 文案分类符合 Surface/投影边界；启动自动判定代码保持不变（R4/R5）。
- [x] 最终代码通过 i18n check、增强线程回归、完整 Headless --no-window、Debug|ARM64 solution build和独立审查；构建/Headless沿用受限环境外验证。

## Confirmed Evidence

- 设置入口：Inkeys/Inkeys/UI/Setting/Setting.cpp:3762、3789、3801，已有组合框和 SyncDraw3State/WriteSetting 调用；原下拉框在无窗口 ImGui 回放中可切换，现场故障未复现；本次改为两个直接互斥选项，避免弹出选择路径与会话级缓存。
- 默认判定：Inkeys/IdtMain.cpp:2408；配置覆盖：2519；PaintDevice 读取/保存：Inkeys/IdtConfiguration.cpp:289、555。
- 物理尺寸不可用原因：Inkeys/Inkeys/Display/Display.cpp:650，包括复制/混合拓扑、EDID 缺失和映射不唯一。

## Out of Scope

- 三种启动位置之外的新预设、旧画笔记忆恢复、交互模式默认判定调整、配置系统迁移、无关重构、自动提交或推送、GUI 测试。

## Key Decisions

- 用户已审阅规划并确认保留全部启动自动判定，第一阶段只修复切换和文案。
- 自动识别与产品分类仍有差异，作为后续启动预设工作处理；本次不扩大范围。

## 本轮纠正

- 用户反馈单选项实现仍在切换时卡死，整个共享 UI 受影响；原默认字体+同步桩回放没有覆盖生产跨线程调用，不能证明故障已修复。
- 本轮明确要求恢复下拉框、上方说明卡片化；此前直接单选方案被替换。

## 用户验收

2026-10-13 用户确认已经修复，并授权提交本轮 commit；明确要求任务继续进行，后续还将修改启动预设，不执行归档或任务结束。


## 当前阶段：启动位置预设与环形启动进度（用户已批准实施）
- R6：启动位置三选一：0居中底栏（展开、底部居中吸附），1左下角悬浮窗，2右下角悬浮窗（后两者仅主按钮，Floating+Free+fold=true）。
- R7：角落按钮可见轮廓距工作区侧边和底边均24 DIP，经同一DPI和主栏zoom换算；不计透明window padding。改设置仅下次启动生效；默认居中，缺失/非法值回退0，目标仍主显示器。
- R8：main.json新增UI.Bar.StartupPlacement，已有原子int包装。IdtMain ReadMini读取后冻结本次选择，同一值交给Preview和正式Bar，不能被ReadAll或设置本次改动重选。
- R9：共享启动几何；正式Bar首帧前应用位置和折叠终态，禁止闪出展开栏。应用一次，后续拖动/展开/吸附/PPT/白板沿旧规则。左向右展开，右向左展开。
- R10：CachedStartupBarWidthDip始终表示展开主栏完整包络；发布展开layout宽度，角落首帧不能写80污染缓存。
- R11：角落Preview80×80 DIP、n=3超椭圆，复用折叠主按钮#181818/0.6填充、白0.18内描边；居中原外形与0.8透明度保持。共享无状态超椭圆路径构造，不导入完整Bar renderer。
- R12：居中底栏使用原192 DIP居中条形进度（1 DIP轨道/3 DIP填充，保留像素下限与圆角）；左右角落使用32 DIP外径、3 DIP圆头描边、顶部顺时针环形进度、正常原蓝色、最终失败首可见帧立即红。反光方向固定45°，保留mask、3秒门、平滑ratio、重试/fatal和严格Preview先隐再正式UI显；既有300/1000ms档位。动画禁用/失败bypass也按所选位置显示正式UI。
- R13：恢复预设导航，只提供启动后位置说明和既有combo，三语i18n。旧预设条目不恢复，动画开关仍常规。配置写盘走冻结payload FIFO，不在ImGui锁内等窗口。
- [x] A6：三种位置的生产初始化赋值回放及现有handoff/禁用/恢复状态测试通过；角落渲染注册前即Floating+Free+fold与全子控件透明终态。没有执行真实HWND/ULW首帧或GUI闪帧观察。
- [x] A7：旧配置、非法值、Mini/ReadAll及本次冻结、下次生效经过真实main.json读写与冻结payload测试。
- [x] A8：4档DPI×4档zoom、负坐标/任务栏工作区及24 DIP可见轮廓几何通过；展开/折叠首帧发布测试确认缓存保持展开目标宽度。
- [x] A9：无HWND真实D2D/WARP像素验证n3透明角、反光mask、圆头ring0/50/100%、fatal含0%立即红；8种外形/缩放生成40份BMP。现有进度、3秒门、平滑、重试/恢复、顺序交接测试通过。
- [x] A10：i18n三语340/340、独立源码/规格审查、完整Debug|ARM64 solution(exit0)、Headless --no-window(exit0)通过；12组实际ImGui页面/侧栏回放及git diff/编码CRLF检查通过。无GUI/commit/push，任务保持进行中。

## 2026-10-14 现场测试反馈修正
- 居中底栏恢复最初的居中水平条形进度；左右角落继续环形进度。此要求覆盖R12中三位置统一环形的旧选择，启动状态/真实进度/失败红色/交接不变。
- 所有Preview反光方向固定约45°，独立于DPI/zoom及窗口宽高比，保留原高光宽度、4秒周期、opacity mask与两端离屏行程。
- [x] A11：生产与离屏共用的进度形态选择，居中条形/角落环形0/50/100%及失败含0%红色像素通过。
- [x] A12：45°方向在中心/角落与多档缩放下保持一致；mask/反光行程/进度门/顺序交接回归、完整ARM64与Headless --no-window、独立审查通过。无GUI/自动commit，任务继续进行。
- 验证记录：完整Debug|ARM64构建与最终Headless --no-window均exit0，独立审查通过；三个位置×五档缩放生成75份当前离屏BMP。首轮3个圆角误报已通过真实像素对照修正测试采样，生产圆角未改。日志/审查在忽略目录Build/StartupPreviewStyleValidation；未执行真实HWND/ULW或GUI动画验收。

## 最终人工验收与结案
2026-10-14用户确认测试完成、人工验证通过，授权提交剩余修改并结束任务。先前未执行GUI的验证边界记录保留；本次人工验收来自用户反馈，非AI代为执行。原始构建/测试日志保留在Git忽略目录Build/InteractionModesTaskLogs；不push。
