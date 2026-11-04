# SettingUI 样式与转场实施计划

状态：最终完整规划已获用户批准；`task.json.status=in_progress`，已运行task.py start。

## Before Implementation

- [x] 重新核对工作区、`feature/i18n` 和任务PRD/design；保护用户新增改动，保留所有文件原始编码/BOM/CRLF。
- [x] 使用trellis-before-dev加载native-desktop及research上下文，确认无新增产品决策；真实源文件行号随实施可能变化，按符号定位。
- [x] 如采用工作流指定的implement/check子代理，先加载trellis-channel并按该skill执行；不为研究额外派发不必要代理。所有worker共享同一任务与边界，避免并发编辑共享Setting文件。
- [x] 用户批准最终评审后运行 `python .trellis/scripts/task.py start 11-02-settingui-style-motion`，不得提前开始。

## Ordered Work

### 1. 基线与完整条件块盘点

- [x] 复用现有Headless的Setting/session/home WARP入口定位回放；记录新页首帧宽度、导航残留颜色、开屏标题栏item及点击次数，先证明相应问题能被回归捕获。
- [x] 遍历Setting.cpp实际switch和Pages.cpp，列出所有顶级块与条件显示项/分区标题；已有稳定ID复用，不用文案做key，不遗漏自绘首页/插件/组件/版本/调测页。
- [x] 对resize和maximize记录资源/serial/材质调用顺序及分阶段成本，区分直接事实和候选根因。采样有开关、聚合、有界，不永久逐帧写盘。

### 2. 低耦合修正

- [x] R3：真实控件验证后局部修复颜色终点；保留透明加权插值，验证共享消费者和准确端点。
- [x] R1：将权威开屏状态提前接入四项控件绘制/命中/键盘导航；保持原chrome与取消帧防点穿规则。
- [x] R5：以证据消除等价材质请求的无必要拆装及跨帧底色失配；保留真实composition/能力改变及失败恢复，不改DWM动画。
- [x] R4：只改已定位的资源/布局/同步浪费；验证Size持续呈现、旧serial完成不覆盖新目标、松手最终尺寸准确及其他共享客户端可前进。

### 3. 正文稳定布局（R2，后续动效的前置）

- [x] 分离正文轨道布局预算和视觉alpha，核对既有gutter/最窄窗口/native BeginChild真实几何。
- [x] 有界准备真实首次/缓存失效布局；确保action一次、无Shell/保存/焦点副作用，没有先显示错误宽度再修正。
- [x] 校验短→长/长→长、20语字体/语言变更、各DPI/用户倍率和阈值附近；导航/flyout/Combo原有样式不无关改动。

### 4. 页面入场（R6）

- [x] 按真实route/subpage generation接入，当前页重复点不重播，保持每页既有scroll。
- [x] 完整覆盖所有产品页面的行/卡片/首页自绘块，标题随所属组、同排一起、嵌套不重复。
- [x] 按2026-11-03细化使用旧页125ms淡出、新块12DIP/位置310ms/透明度450ms、27ms错峰且最多108ms延迟；最后位置418ms到位、透明度558ms结束再110ms渐显thumb，复用全局速率。
- [x] 在items提交前处理用户滚动/点击取消，正常消费同次输入，不吞掉/重放/双触发；快速导航只以最新目标为准。
- [x] 保持字标开屏的快照与现有时序，开屏完成/取消不补播；开屏跳过和导航返回首页正常逐块入场。
- [x] 统一旧contentReveal与新动效，处理裁剪/子窗口、焦点、页外元素，不事后平移整页DrawData掩盖布局问题。

### 5. 条件条目（R7）

- [x] 给盘点的真实条件块接入稳定ID、测量高度、180ms显隐、分组标题与当前值反向续接；首次条件不触发新增动画。
- [x] 保留退出视觉但撤销不合法输入，副作用回调不因动画多执行；关闭全局动画即时归位。
- [x] 以真实owner维护动态总高、稳定可见锚点和有条件贴底；用户滚轮/拖动优先，范围随当前占位连续变化。
- [x] 覆盖上方/下方变化、同帧多项显隐、跨溢出阈值、消失焦点、滚动条拖动和新页动画交错。

### 6. 集成验证与交付

- [x] 按A1-A8完整复核，不仅检查最后一批改动；必要规范同步到native-desktop，区分旧文档与新合同。
- [x] 保留baseline/after计数和耗时、离屏图像与结果，明确人工未验项。仅相关静态/无窗口验证，无授权不打开应用或调测GUI。
- [x] 检查diff范围/BOM/EOL；不提交commit或push，除非用户对本任务另行授权。

## Validation Commands and Evidence

当前项目使用Visual Studio/MSBuild，复用已有Solution和Headless，不创建新构建系统。

1. 构建前动态定位当前Visual Studio中的ARM64原生MSBuild；同一PowerShell invocation先 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`，设置 `$env:MSBUILDDISABLENODEREUSE='1'`，然后运行原生MSBuild `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m`。完整solution单次至少允许5分钟，建议10分钟；检查退出码与首个有意义错误，环境问题不能修改源码绕过。
2. 运行 `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，完整适用集成验证。迭代可用既有 `--setting-home-only` 等定向入口，不能用定向代替最后全范围测试。`--setting-home-only` 实际运行设计测试，名称不代表只覆盖单一helper。
3. 运行 `Build/ARM64/Debug/Inkeys.exe --i18n-locales-test`、`--setting-font-lifecycle-test`：均为既有非交互测试入口；不启动默认主程序。确保20语加载/格式化、同字族复用与字体重建不受新缓存/布局影响。
4. 复用已有home WARP/offscreen回放和真实RenderFrame边界，测试没有HWND/可见窗的资源与绘制。适用时用en-US、zh-CN、vi-VN、uk-UA等长文/变音符样本及20语轻量页面遍历；多倍率覆盖100/125/150/200%和窄/宽/最小尺寸，不无理由重跑上次完整48分钟字体矩阵。
5. `git diff --check`，逐文件核对BOM/EOL与无关修改。未跟踪新文件另行检查尾空格及路径；任务context执行 `python .trellis/scripts/task.py validate 11-02-settingui-style-motion`。

沙箱TEMP若禁止配置测试原子替换，可仅对测试子进程设置TEMP/TMP为仓库内Build scratch，记录环境差异；不修改产品写盘器。不得启动需要用户关闭的窗口。

## Required Behavioral Matrix

| 范围 | 最低证据 |
| --- | --- |
| R1 | 开屏准备/播放/淡退/取消/结束各帧真实item和Win32命中策略；四项不可见时无法鼠标/键盘激活。 |
| R2/R6 | 实际页面BeginChild宽度与卡片右边缘序列、滚动条alpha与几何；首次、返回保存scroll、快速切换、短长页、真实输入立即到位及业务次数。 |
| R3 | NavItemEx实绘顶点/状态端点，30/60/120/240Hz；同时验证普通按钮等共享调用未回归。 |
| R4 | baseline/after相同输入的resource计数、layout/backend/resize/Present分段耗时；重复尺寸、风暴合并、失败重试、epoch、隐藏、最后尺寸及共享客户端进展。 |
| R5 | 实际材质调用记录与frame快照、等价请求无拆装、真composition/高对比/能力失效重验、首尾RTV/clear/palette一致。 |
| R7 | 条件显隐current-height/总高/scroll/真实命中/事件次数序列，中途反向/多块/滚动/贴底/阈值、关闭动画。 |

## Manual Acceptance and Scope Stop

真Win7连续八方向resize、系统框架/Snap/最小尺寸，以及真Win10/11动态背景下最大化/还原的模糊/透明度，最终需实机人工观察。此任务目前没有GUI授权，自动化结果须明确报告该边界；不伪称这些场景已验证。

若证据显示局部修正不足、必须更改共享调度/设备/交换链策略、冻结内容或降低材质质量，应保存数据并回到规划确认，而不是扩围后才通知。不要仅因本机环境失败升级SDK/依赖或绕过签名。

## 实施完成状态

上述代码及自动化步骤已完成，详细命令、失败定位与最终通过证据见research/validation.md。勾选代表已完成代码及可执行的无窗口验证，不代表下节真实Win7/DWM人工验收已完成；性能结论仅限去除重复事务，未给出实机FPS或延迟改善承诺。未提交或push，等待用户审阅。

## 2026-11-03 柔和度细化完成

用户直接要求继续当前任务，已实现目标/显示路由分离、旧页先退、独立位置/透明度曲线，以及旧页输入隔离而保留正常配色。新增纯路由状态、真实UI输入/时序/原生键盘对照及28场景离屏验证。最终构建、1839帧定向和完整Headless均通过，详见research/transition-refinement-validation.md。本轮未commit，任务不结束。
