# 实施与验证计划

当前状态：R11已修复并以真实末项场景红/绿验证，完整ARM64构建及注意事项/复制专项通过，见 `validation-cursor-boundary.md`。R10与前轮结果/限制保留，未提交、未推送、未自动归档。

## R11 游标边界

- [x] 无窗口复现CopyButton为最后项直接EndChild/End的真实错误，证明旧测试后续item掩盖。
- [x] 共用helper最小修复，不关检查、不改imgui第三方、不改变复制/ID/标准间距。
- [x] 末项/裁剪/滚动/分数DPI/缩进回归及旧复制专项，完整Debug ARM64构建与静态复核。
- [x] 按break-loop记录根因/测试缺口并更新规范，不启动GUI或提交。

## R10 诊断复制与全称

- [x] 两块下方各一个右对齐标准复制按钮，复制同帧完整字符串；ImGui fake clipboard真实点击/尺寸/游标回归。
- [x] DXGI/D3D完整常量、flags组合/未知位、Count/Quality、HRESULT和驱动/输入全称；只改owner文本格式，不改设备/输入流程。
- [x] DPI X/Y与乘号、EDID全称，保留原数据及全部屏幕。
- [x] 纯formatter生产probe接既有设备策略自检，完整Debug ARM64构建、复制与文案无窗口专项、最终只读复核。
- [x] 更新code-spec、验证/编码与最小diff记录；不启动GUI或提交。

## R9 最终作用范围

- [x] 条目附注用F167填充字形，只在已有普通解释外显示i；单段note退回普通说明。
- [x] 快捷键/ClassIsland/通道等原块恢复，PPT重复支持说明去除且按钮/条件保留，架构恢复普通说明。
- [x] 新增顶部版本状态专用标题，不更改实际更新偏好栏目。
- [x] 完整Debug ARM64构建、attention/copy无窗口专项、三语言571检查和最终diff/编码复核通过。

## R8 注意事项提示行

- [x] 删除激光两项指定句子，不移为note，不改变BETA/粒子禁用。
- [x] 盘点活动设置的重启/启动、权限依赖、数据风险等注意事项，逐项显式拆分，保留正常描述/模式名/独立错误对话框。
- [x] 共用设置行增加可选note，同字号蓝色i提示；无note保持旧几何，宽窄/多语言/分数高DPI不重叠。
- [x] 新注意事项专项纯布局/实际ImGui测试，跳过无关HomeWARP基准；i18n sync/check与完整solution Debug ARM64构建。
- [x] 全范围静态复核、编码/最小diff检查、更新spec与本轮验证记录；不启动GUI或提交。

## 2026-11-01 继续修改

- [x] 对调性能/实验图标；默认两帧率和精准选项增加推荐标记。
- [x] 绘图效果优化改为绘图环境，通用整组移到其下方；移除统一生效提示。
- [x] 粒子保留无toggle说明行；Controller构造和setter强制关闭，旧配置不可开启，保留兼容字段和算法资源。
- [x] 执行i18n、完整Debug ARM64构建、粒子probe与文案专项（通过）；执行完整设置专项（失败/超时，如实记录，未改阈值）。
- [x] 最终diff/编码检查及更新spec、验证记录，不提交或启动GUI。

## 启动前

- [x] 用户在最终规划摘要之后明确同意实施；运行 `task.py start`。
- [x] 读取 PRD、设计、三份 research、implement/check.jsonl 的实际引用和相关规范。
- [x] 上下文清单校验已发现部分规范超过 32 KiB 注入上限；不得把自动截断片段当作全文，必须按规范章节与研究锚点从磁盘补读相关合同，不修改全局注入配置。
- [x] 核对工作树只包含本任务允许的变化；记录将改源码的 BOM/编码与 CRLF/LF。
- [x] 重新确认构建入口为现有 InkeysRepo.sln、Debug | ARM64，保持用户 AGENTS 优先于历史 spec 的依赖描述。

## 实施顺序

1. **配置和合同（R1–R4）**
   - [x] 增加 `UI.FrameRate=60`、`Drawing.FrameRate=120`、`Drawing.MultiTouchEnabled=true`、`Drawing.PenTailCorrectionEnabled=true`、`Drawing.InkPredictionMode=2`、`Drawing.Laser.MultiTouchEnabled=false`、`ParticlesEnabled=false`。
   - [x] 两帧率与预测模式严格整数白名单解码，运行时边界再校验；不改三项迁移的旧键。
   - [x] 扩展已有 `--config-boundary-test`，覆盖七字段缺项、非法类型/值/超宽整数、写读往返、三个预测值和子偏好保留。
2. **共享界面节拍（R1）**
   - [x] Scheduler/模块增加唯一帧率入口，启动读取和设置更新均走此入口。
   - [x] 连续批次按 30/60 调度，切档重算后续 deadline；保留 idle、请求、注销和恢复合同。
   - [x] 在真实 Scheduler 测试中验证两个档、运行时切换、Settings/Bar/PageControl 共用、idle 和 Stop，不仅测数值换算。
3. **Draw3 帧率与绘制偏好（R1/R3/R4）**
   - [x] 模型创建接受完整档位，产品启动传入且默认仍 120；检查 DPI/恢复的全部创建点。
   - [x] Host 以既有发布/control wake 模式传递全局多指和激光设置，确保启动前值与运行中值可达 controller。
   - [x] Host/Controller 接入一份通用偏好快照，ActiveStroke或等价每笔状态在新独立落笔时锁存预测模式与抬笔修正；成功断触接续沿用旧值。
   - [x] `ApplyPredictionMode` 显式取模式，各笔 modeler.Reset 和四处编译期预测判断全部改读该笔保存值；保留橡皮本身禁用预测的原行为，Disabled 不调用 Predict。
   - [x] `FinalizeStoredStroke` 中仅按该笔修正开关控制 `FitCompletedPenGeometry`，仍执行正常 taper、公切线、真实 Up 与提交；不重写求解器或阈值。
   - [x] 全局门禁放在真实 Touch Down 准入且先于手势登记，正确清理 ignored/terminal，区分物理接触和 awaitingReconnect。
   - [x] 激光复用现有 setter；全局关闭仅将多指有效值关掉，不重写子偏好；粒子保持现有 Inactive 锁存时机。
   - [x] 扩展既有 Draw3 生产自检/无窗口回放，覆盖默认、四种组合、Pen/Mouse、普通笔/形状/橡皮/Laser、Up/Cancel、工具切换和重连。
   - [x] 新增真实路径回归：一笔中途切三种模式/修正开关、并发笔画不同快照、接续/延后完成、下一独立笔生效；缺项默认输出与旧基线一致。
4. **画布初始化事实（R5）**
   - [x] 在现有设备/呈现/输入初始化 owner 捕获实际结果，新增 Host 只读值快照，不解析日志。
   - [x] 发布失败/停止/初始化中和代次；成功、回退、恢复、resize 后更新；不暴露 COM 指针。
   - [x] 验证 Hardware/WARP、DComp/ULW、RTS Extended/Basic/Mouse 和查询失败的格式/字段选择。只构造末端字符串不算贯通发布验证。
5. **设置与国际化（R1–R5）**
   - [x] 新增 PageId=16（实施时再次确认未占用），导航置于绘制后，保持旧编号。
   - [x] 性能页两组帧率和三项迁移；绘制靠上“通用”三项按多指绘图/抬笔笔锋修正/墨迹预测排列，另设激光分类；软件调测中新增只读信息块。
   - [x] 用现有 ComboRow/ToggleRow/文本卡片，写清 BETA 和生效时机，恢复警告保持靠前。
   - [x] 墨迹预测显示精准/快速/关闭，显式映射 Kalman/StrokeEnd/Disabled；说明按设计稿描述实际效果，不显示算法类名。通用三项统一提示新笔生效。
   - [x] 扩展设置导航/显隐测试，运行 i18n sync/check，审查生成文件无无关变化。
6. **集成核查与 spec**
   - [x] 用 trellis-check 核对所有改动和跨层可达性，按需要更新配置、界面节拍、Draw3 准入与诊断快照合同。
   - [x] 若遇范围扩大或产品语义变化，先更新规划再审阅；不随机修改工具链/依赖解决本机错误。
   - [x] 不自动提交、推送或归档；仅汇报改动、实际验证与未执行项。

## 验证矩阵

| 验证 | 范围与判据 |
| --- | --- |
| 配置边界 | 缺项/正确值/类型错误/非法档位/超宽整数分别恢复正确默认；读写往返；已有三项键保持 |
| Scheduler | 30 与 60 连续帧节拍有可容忍抖动的下限，单/多客户端同批；30↔60、idle/restart/Stop、不新增空转 |
| 画布模型 | 每档经真实模型配置创建得出整套既有 profile；UI 帧率不污染 Draw3，重启档位正确 |
| 通用预测/修正 | 新落笔锁存真实模型参数，三预测模式贯通；修正开/关都保留正常收笔。默认输出回归旧基线，预测关闭与修正关闭相互独立 |
| 每笔时机 | 同一笔中途切设置不影响其 Move/Up/待接续完成；成功续接沿原值，新独立笔使用新值，并发触点可保有不同模式 |
| Touch/激光 | 全局默认开、两激光默认关；四种组合；被忽略触点完整终态；首指结束后不接管；既有contact不截断；手势与重连无残留 |
| 粒子 | 接线可达现有 setter，旧 Laser 过程不被设置切换意外改变；Inactive 后应用，保留现有资源失败降级 |
| 初始化快照 | 发布到 UI 的实际设备/呈现/交换链/输入同代，回退和不可用真实；console off 仍有值，无跨线程 COM 访问 |
| 设置/i18n | 新页面路由、通用三项顺序/默认/模式映射、迁移后无重复入口、条件显隐/保存值、各语言键完整；说明面向用户效果，含必要的 BETA 与生效时机 |
| 构建 | 完整 InkeysRepo.sln Debug ARM64，退出码 0；首个有意义错误分类，不改配置绕过环境问题 |

## 命令和环境

- 本会话可用 Python：`C:/Program Files/KiCad/10.0/bin/python.exe`（ARM64 3.11.5）。PATH 上 WindowsApps python 启动别名不可执行；不修改全局 PATH。
- 规划检查：上述 Python 执行 `.trellis/scripts/task.py validate .trellis/tasks/archive/2026-11/10-31-settings-performance-drawing-diagnostics`。
- 国际化：`./Scripts/i18n.ps1 sync`，随后 `./Scripts/i18n.ps1 check`。
- 用 vswhere 查当前 Visual Studio 安装，再从安装目录查 `MSBuild/Current/Bin/arm64/MSBuild.exe`；不硬编码 VS 版本。
- MSBuild 在同一个 PowerShell invocation 先执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`、设置 `$env:MSBUILDDISABLENODEREUSE='1'`，再执行完整 Solution `/p:Configuration=Debug /p:Platform=ARM64 /nr:false`。允许至少 5 分钟，通过轮询保持进展更新，不因几十秒静默重启构建。
- 无窗口套件：`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`。实际路径以既有输出为准。
- 适用的产品自检：`Inkeys.exe --config-boundary-test` 和已确认在任何产品 HWND 初始化前严格早退的 Draw3 自检，stdout/stderr 重定向，不开交互控制台。新测试必须沿既有早退边界，harness 关闭也不能落入普通启动。
- 不运行默认 Inkeys 启动、hidden HWND fixture、窗口合同专用测试、GUI 或 Computer Use；真实触控/粒子观感/驱动回退留作未授权的人工验证。
- 收尾：`git diff --check`、`git diff --stat`、`git status --short --ignore-submodules=all`，复核编码/换行和最小 diff。

## 回退点

- 配置新增是增量字段，旧三项保持原路径；出现问题只撤回本任务改动，不清理用户配置或历史笔迹。
- 节拍、模型档位、Touch gate、快照发布按上述步骤分别验证；失败先查所属 owner，不用 UI 端临时拦截掩盖。
- 记录第一条有效构建/测试错误及退出码；SDK、PATH、依赖或权限问题单独报告，不升级工具链、不绕过签名或关闭验证。
