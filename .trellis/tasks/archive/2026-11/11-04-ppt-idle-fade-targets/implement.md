# 实施清单：PPT 闲置淡化上限与主栏

## 阶段门

- [x] 用户授权新建/延续 Trellis 并进入规划。
- [x] 新建后续任务，阅读已归档原淡化任务与生产代码。
- [x] 用户确认固定背景 30%、内容 60%，无调节入口。
- [x] 用户确认主栏悬停恢复。
- [x] 用户确认主栏仅按闲置到期淡化，外部翻页不影响其计时。
- [x] 用户确认所有主栏附属面板均保护主栏，包含更多。
- [x] 最终 PRD/design/implement 已完成完整审阅与归并，最终规划在本轮对话呈现。
- [x] 用户在后续消息明确回复“确认，进入实现”，批准最新方案。
- [x] 批准后执行 `task.py start 11-04-ppt-idle-fade-targets`，状态已为 in_progress。

用户已批准，激活任务后开始实施。遵守项目配置的 Trellis dispatch 方式；若使用 implement/check 子代理，先读并遵守相应 skill/agent 指令及上下文清单，不能让代理越过本阶段门。

## 变更边界

修改原生 main.json 的 PPT 闲置范围、PPT 发布快照、现有 PageControl/Bar 输入与渲染、联动页、i18n 和对应验证/spec。正常用户透明度、显隐/禁用、COM 协议与翻页业务保持原合同；不新增线程/构建系统、不改第三方、不顺手整理无关源码。

## 顺序

### 1. 配置、固定策略与快照

- [x] 按 R2 新增三个 NoUpload bool，检查通用 bool 解码与旧配置缺值的默认；只在通用路径不满足时加最小严格类型处理。
- [x] 在 `Bar.ThemeMaterial.h` 记录唯一背景/内容上限与只减不增投影，覆盖低值、等值、高值、主题端点与动画中间值。
- [x] Ppt 发布新字段，PptState 等价比较包含全部字段；添加供主栏读取的窄闲置快照，保持接口依赖无环。
- [x] 配置/场次/可见性变化在原锁外请求必要客户端；页码更新不重置主栏。

### 2. 分页渲染与范围

- [x] 四个 HWND 分别依据底部/中部选项启用闲置，复用现有 `ResolvePptIdleFade`、活动信号、外部序号和输入 owner。
- [x] 新增独立闲置进度；Scene::SetOpacity 只处理显隐，背景和内容按正常值分别投影，不与用户值二次相乘。
- [x] 接入正常材质变更/重绘；Page Scene 原有正常值更新不能覆盖闲置目标。菜单/主题面板保持原透明度。
- [x] 取消/重新勾选、关闭/重新开启、新场次/返回可见处理期限与外部序号，防止重开立即误淡化。

### 3. 主栏输入、计时与面板保护

- [x] 在已准入的共享 worker 输入路径发布主栏悬停/有效按下/释放/拖动/取消信号，使用已呈现主栏和主按钮几何，保留第一下点击原业务。
- [x] 主栏 render owner 消费窄配置快照和稳定 workspace 状态，维护独立生命周期基准；复用原超时策略但始终传入 externalTurn=false。
- [x] 聚合所有属性/更多/可交互子面板开合与实际退场状态，保护维持到最后一个面板及交互结束；从该时刻完整计时。
- [x] 复用 `PageLoadingDeadline` 的发布/消费语义及共享 worker 的等待合并，到期请求 Bar，不新增轮询；消除过去 deadline 自旋。
- [x] 在 RenderLoop 主栏/主按钮绘制段消费独立闲置进度、局部恢复渲染参数；不对整个 HWND 或全局材质设 alpha。
- [x] 验证 Cancel/CaptureChanged/溢出/场次切换/暂不可见/shutdown 都清理旧信号，设备/显示布局变化不伪造新活动。

### 4. 联动页与 i18n

- [x] 时长与范围复选共用一张卡片；三项顺序、默认、主栏底栏注释、关闭时动画折叠/开启展开及允许全不选与 PRD 一致。
- [x] 真实变更才保存、通知一次；未变字段和 disabled 绘制不得反写；保留原时长下拉和现有 FIFO 边界。
- [x] 更新说明及全部 20 语言，复用已有底部/中部键；执行 i18n sync 生成并 check。
- [x] 无窗口使用实际控件验证默认/单项改变/关闭禁用/全不选/再次开启；覆盖窄宽、多倍缩放与长文案。测试通过生产事件处理路径证明保存与通知，不复制旁路 UI。

### 5. 回归与构建

- [x] 配置：缺字段、非法类型、每个 bool 的 true/false 和组合读写；既有等待值与背景/内容不被重置。
- [x] 策略：30%/60% 取小值、四 HWND 与主栏独立、到期边界、悬停/按下优先、主栏不响应外部翻页、保护重叠/最后关闭重新计时、关闭与重新启用、旧场次退休。
- [x] 生产渲染：真实 Scene/Bar 绘制的背景与内容 alpha 到正确终点和中间值；低于上限不反向加深，禁用/显隐仍独立，正常主题/透明度变化不二次相乘。属性/更多像素和状态不变。
- [x] 生产输入/调度：悬停命中范围、Leave、有效 Down/Up、取消、CaptureChanged、拖动、第一下点击单次执行及到期一次唤醒，保护关闭后新期限真正进入共享等待。
- [x] 动画：静置唤醒首帧 dt=0、中途反向、主栏展开/收起/入口、动画关闭同帧到位、场景切换和失败重试无残留。
- [x] 基于现有 `page_control_tests.cpp`、`bar_theme_tests.cpp`、`setting_design_tests.cpp` 与生产离屏/输入测试扩展，不为了测试复制实际公式。若原入口无法隔离主栏生产链，才在现有测试文件/初始化前入口增加最小无窗口专项，并记录真实命令。
- [x] 执行以下验证，检查实际退出码与首个有意义错误，保存源码/产物身份、日志与未测范围；编译完成后才运行该次新产物，不能与链接并行。

```powershell
# i18n：沿用项目脚本
& .\Scripts\i18n.ps1 sync
& .\Scripts\i18n.ps1 check

# Solution：从当前 Visual Studio 安装动态发现 ARM64 原生 MSBuild。
# 在同一 PowerShell invocation 规范化 Path/PATH，保留原路径值，设置：
# $env:MSBUILDDISABLENODEREUSE = '1'
# MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false
# 允许至少 5 分钟；建议 10 分钟。不要单独编译 Inkeys.vcxproj。

# 已有无窗口入口：先确认本轮产物实际生成并检查各自退出码。
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --page-control-only
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window --setting-home-only
& .\Build\ARM64\Debug\Inkeys.exe --config-boundary-test
& .\Build\ARM64\Debug\Inkeys.exe --ui3-input-worker-test
& .\Build\ARM64\Debug\Inkeys.exe --bar-eraser-offscreen-test
# 收敛时完整 --no-window 验证渲染/动画公共改动；如前面已包含同一全部范围，无必要重复。
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
git diff --check --ignore-submodules
```

不运行 `--page-control-hidden-test` 或启动产品主窗口/设置窗口；hidden-window 与无 HWND 验证不同。不得通过修改项目配置、依赖或关闭签名来绕过本机环境问题。

### 6. 最终检查与交付

- [x] 按 `trellis-check` 完整审阅所有受影响层，逐项核对 AC1–AC8；重要异常回到设计/实施修正，再运行受影响验证。
- [x] 按 `trellis-update-spec` 更新背景/内容上限、三范围配置、外部翻页差异与主栏面板保护知识；只记录本次有证据的合同，不重写无关大文档。
- [x] 检查所有改动文件 BOM/编码/CRLF 与 diff，任何无关改动单列且不擅自包含。
- [x] 汇报修改、验证、退出码与真实 GUI/Office 未测范围。用户未明确授权本任务 commit 前不提交，也不 push；任务不提前归档为完成。

## 风险与回滚点

- 第 1 步可独立核对配置/纯策略；第 2 步先确保分页不再整体乘；第 3 步验证主栏局部绘制/期限；第 4 步才暴露完整范围入口。最终必须联合验收，不能仅凭某一层 helper 通过宣告完成。
- 高风险位置：Scene Advance 正常材质更新、renderer 局部参数恢复、主栏输入准入/取消、面板退场结束边沿、PPT merge 后的唤醒和旧外部序号。
- 回滚只撤销本任务相关源码/配置新增项，保持原正常透明度、PPT 时长与其他任务修改。发生超范围需求先报告并回规划，不能在当前实施顺手扩张。

## 完成与验证边界

实现、静态审阅、i18n、完整 Debug|ARM64 Solution 构建、五项专项和完整无窗口回归已完成，实际退出码均为 0。最终证据见 research/validation.md。上方验证项的 UI/保护覆盖包含生产控件输入、生产像素、纯状态和源码聚合审查；未执行真实逐面板/Office/设备 GUI 验收，单独 presentationVisible 退出未新增完整 RenderLoop 专项，不能把这些边界说成已经现场通过。没有 commit/push；按用户后续实际使用反馈再决定收尾归档。

## 用户验收后的本轮调整（已直接授权）

- [x] 移除单独“淡化范围”标题和卡片，三个复选直接内联到“闲置时自动淡化”同一张卡片，更新实际 UI 回归。
- [x] 主栏选项标注“仅底栏模式时”，运行时只允许 BottomDocked（包含普通底栏和居中底栏），Floating 恢复且回到底栏完整重计时。
- [x] 内容不透明度上限改为60%，背景仍30%；更新20语言、生成项、相关像素/策略测试及专题合同。
- [x] 运行最小充分验证：i18n、完整Debug|ARM64构建、UI专项、分页/主栏策略和真实离屏套件。既有未变的完整无窗口矩阵已在前轮通过，本轮不无条件重复。

- [x] 补充要求：父行常驻，关闭时内联区复用条件动画折叠、开启展开；验证中间高度/渐隐、快速反向、关闭动画与收起期间无交互。

第二轮实际结果见 research/validation-revision-2.md；完整ARM64构建、i18n、UI/既有motion/page-policy/真实offscreen四组无窗口回归全部退出0。没有GUI、commit或push。

功能代码已按用户授权完成SSH签名提交：`f485146eab917aec5706d74482dfbdf8740d7636`（feat(ppt): add scoped idle fading and docked toolbar support）。验证记录中“未commit”为验证时点快照；实际收尾状态以本节及task.json为准。任务context清单中的自身research引用已指向归档路径。
