# Implement: UI3 主栏状态指示器

前置：`theme`、`feature/i18n` 已合并（33495b3c、6ee09764），合并后 Debug|ARM64 完整构建与 `InkeysHeadlessTests.exe --no-window` 通过。

每阶段结束都要：增量构建 `InkeysRepo.sln Debug|ARM64`（arm64 host MSBuild，`-m:1 -nr:false`）、跑相关无窗口测试、`git diff --check`；阶段可独立提交、独立回退。

## 阶段 1 — 纯模型与布局（无 UI 依赖）
- [x] 新建 `Bar.StatusIndicator.h`：Spec/Kind/Lifetime/Action、注册表（容量 25、去重、过滤、驱逐规则）、条目状态机（绝对截止、队首计时）、进度单调化、Fan 多层同心弧布局、正常态夹取（`baseBounds`）、胶囊宽度/滚动速度计算。
- [x] 无窗口测试 `status_indicator_tests.cpp`：去重不重放、过滤开关以收起态恢复、25 上限与驱逐、队首计时与下移顺序、圆形新右旧左与补位、正常态不越 `baseBounds`、Fan 层容量与向下镜像、滚动 3s 内完成、进度回零被吸收、动画关闭时立即完成。
- 回退点：仅新增文件。

## 阶段 2 — 跨线程发布与渲染接线（调试源先行）
- [x] `Bar.Main.cppm` 导出发布 API；`Bar.StatusIndicator.cpp` 的 mutex 队列 + `UpdateRendering`，渲染线程在 `AdvanceAnimationsAndDeriveLayout` drain/推进并 `MarkChanged`。
- [x] SVG 资源（放映、大头针、新版本、4 个通用图标）入 `Inkeys/src/UI/`、`Inkeys.rc`；由状态面板私有数组持有，复用 Svg 加载/缓存，无需扩充全局 `BarUISetSvgEnum`。
- [x] 绘制：胶囊、圆形轨道、Fan、选中胶囊；主按钮之后 Grip 变换下绘制；材质插值、背景/内容透明度手动相乘。
- [x] 指示环：独立超椭圆几何缓存、加粗着色、`DrawPointLightFrame` 彩色漫反射、呼吸、绘制态门禁。
- [x] 脏区 key、光影影响求交；Fan/选中胶囊从首帧加入完整 Back 包络和内容边界，交由现有 viewport/capacity 事务保留及收缩。
- [x] 设置调试页"添加随机/删除一个"（含 i18n），用于后续阶段全部人工验证。
- 验证：调试页加到 25 个，开启脏区可视化观察残影/视口；深浅主题、透明度、光影开关、动画关闭/速率。

## 阶段 3 — 交互
- [x] 呈现后命中快照；主按钮命中块之前测试并消费。
- [x] 胶囊点击收起/消失；单圆形悬停/按压（沿用按钮曲线与 `SuppressHoverUntilPointerMove`）；多圆形 Fan 展开/点选/再点收起/点主按钮或主栏其他处收起/3s 无交互收起。
- [x] 笔/触摸指针会话与触觉合同。
- 验证：鼠标、笔、触摸三种输入；Fan 越界与收缩后视口正确。

## 阶段 4 — 设置、配置、跳转
- [x] `main.json` schema `StatusIndicator` 组；旧 `PlugIn.SuperTop.Indicator` 一次迁移（`ReadAllHasValue` 写回前记录）；启动同步 `SetStatusIndicatorOptions`。
- [x] 常规 → “状态指示器”独立子页（v3 决策）：总开关 + `CheckboxGroup`；移除插件页超级置顶指示器展示行及其 i18n 键。
- [x] `ShowSoftwareVersion` 全链路（目标枚举、Action、dispatch、fadeOnly 目标判定、导出），扩展 `CheckSettingOpenNavigation`。
- [x] i18n：zh-CN 新键 → `i18n.ps1 sync` → 补全其余 19 种语言（含此前缺失的调试页文案）→ `check` 通过。
- 验证：设置无窗口测试（布局/行记录）、配置旧文件迁移（有旧键 false / 无旧键 / 已有新键三种）。

## 阶段 5 — 状态源
- [x] PPT/WPS：`IdtPlug-in.cpp` 新场次开始边沿 → `PublishPptStatusIndicator`，`EndSession` → `RetirePptStatusIndicator`；同会话重锚不触发；白板期间保留。
- [x] 超级置顶：I18n 就绪后 `hasSuperTop` → `PublishSuperTopStatusIndicator` 一次。
- [x] 新版本：交互 worker `PollDeadline` 轮询（下载中 10Hz、其余 1Hz）经 `StatusIndicatorUpdateTracker` 归一；未静默=检测即弹出，静默=下载 >0% 弹出并带进度，`UpdateRestart` 完成，失败结束进度保留状态，Latest/Newer 注销；点击跳转软件版本页。
- [x] 人工验证反馈修复：停靠方块伸进主按钮内部的命中让给主按钮（`StatusIndicatorInsideMainButton`）。
- 验证：真实 PowerPoint/WPS 放映开始/结束/白板往返；Release 构建或设置页触发更新检查观察下载进度环（Debug 构建默认不启动自动更新）。

## 阶段 6 — 收尾
- [ ] `trellis-check` 全范围复核；完整 Solution 构建；`InkeysHeadlessTests.exe --no-window`；`Inkeys.exe --bar-eraser-offscreen-test`（确认主栏离屏回归）；i18n check。
- [ ] 规范更新（`trellis-update-spec`）：rendering-and-ui（状态指示器绘制/脏区/视口/命中合同）、configuration-i18n-and-assets（新配置与迁移）、ppt-interop（会话边沿消费者）、eraser-attributes 或 rendering-and-ui 中设置直达入口扩展到软件版本。
- [ ] 人工验收清单（PRD Acceptance Criteria 逐条）。

## 审阅关口
- 阶段 1 测试通过后再进入渲染；阶段 2、3 完成后（含点击/轨道展开）请用户用调试页人工确认动画观感与参数（尺寸、错开步长、颜色）再继续设置与状态源。
- 调试随机状态中约 1/3 的持久条目附带模拟下载进度（约 6s 走完并完成），用于在阶段 5 之前验证进度环绘制。

## 阶段 2'/3' — v2 重设计（2026-11-04，取代阶段 2/3 的视觉与部分交互）
- [x] 合并 `temp/state`（cd244350），Debug|ARM64 完整构建、无窗口测试、`--bar-eraser-offscreen-test` 通过。
- [x] 状态图标按实心语言重绘（`research/status_icon_build.py`，lint 7/7）；HTML 预览稿 `research/indicator-v2-preview.html`。
- [x] 合并 `temp/state`（4732db1b）后按用户确认的预览稿实施：按 prd “Decisions v2” 重写 `Bar.StatusIndicator.h` 布局与收起动画、`.cpp` 绘制（主栏 `BarUiShapeClass` 材质、遮挡裁剪 + `PushPanelOpacity` 单次合成、圆角文字遮罩与渐隐、光晕）、交互（单方块重新展开、主栏按钮式悬停）、RenderLoop 倒转输入与胶囊扩窗包络；更新无窗口与离屏测试。
- 验证（2026-11-04）：Debug|ARM64 完整构建 0 错误；`InkeysHeadlessTests.exe --no-window` 0 失败（含倒转、着陆让位、帧率无关、单方块重新展开新用例）；`Inkeys.exe --bar-eraser-offscreen-test` 0 失败；状态图标 lint 7/7；`git diff --check` 通过。真实窗口观感、鼠标/笔/触摸与透明度/光影组合仍需用户人工验收。
