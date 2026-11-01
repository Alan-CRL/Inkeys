# R7 最小增量复核

日期：2026-11-01。角色：trellis-check。仅只读检查本轮 R7 产品差异并写本记录；未修改源码、未创建子代理、未构建/重跑测试、未启动 GUI，也未改变任务状态。

## 结论

R7 所有要求均已接入，当前未发现阻塞项。粒子禁用同时覆盖 UI、Controller 构造配置和 setter，未找到写回 true 的运行路径；新增真实 Controller probe 不创建 HWND。原任务已通过的构建/五组测试不能替代本轮 R7 的增量验证，最终以根代理当前集中构建和适用自检结果为准。

## UI 与国际化

| 要求 | 当前证据与判断 |
| --- | --- |
| 性能/实验图标对调 | `Setting.Shell.cpp:154` Performance 使用 EC4A，`:157` Experimental 使用 E9D9；只换 glyph，顺序、PageId 和路由不变 |
| 界面60 FPS推荐 | `Setting.cpp:4060` 仅在60标签拼接本地化 RecommendedSuffix；`:4062`仍映射30/60，默认和存储不变 |
| 画布120 FPS推荐 | `Setting.cpp:4072` 仅在120标签追加后缀；`canvasFrameRates{30,60,120,240}`和索引映射未改 |
| 精准推荐 | `Setting.cpp:3393` 仅在精准标签追加后缀；`:3398`仍显式映射2/1/0，没有把显示文本变成数据值 |
| 绘图环境在通用前 | `Setting.cpp:3351` 的旧Effect节及原设备控件位于`:3377`的General之前；沿用原设备处理，不复制或重写业务 |
| 粒子无toggle | `Setting.cpp:3443` BeginDisabled → SettingRow，控件宽高0且回调为空 → EndDisabled；没有粒子ToggleRow或启用事件 |
| 去掉统一生效提示 | 活动源码、三语言、生成keys及快照均无NewStrokeNotice；每笔模型和修正锁存实现未删除 |

三语言中 `SettingsUI.Design.RecommendedSuffix` 分别为“（推荐）”“（推薦）”“ (Recommended)”。`SettingsUI.Draw.Effect.N` 分别为“绘图环境”“繪圖環境”“Drawing environment”。粒子说明均保留 BETA，并明确当前版本不可用。生成 `IdtI18nKeys.g.h` 和中文快照与源键一致；本轮静态搜索未见旧统一提示的活动引用。

Settings 向 Host 发布激光偏好时已明确传粒子 false（`:3406`、`:3439`），隐藏激光多指仍不覆盖原子偏好。UI没有通过禁用行继续写入粒子true。

## 粒子能力硬门与唤醒

- `Draw3.DrawingController.cpp:9447` 构造 `laserParticlesEnabled_(false)`，不再使用 `configuration_.laserParticlesEnabled` 初始化原子flag。`StrokeModelConfiguration`保留兼容字段，注释明确产品强制关闭。
- `SetLaserParticlesEnabled`（`:9576`）忽略请求值并 `exchange(false)`，仅旧值真的为true时才PublishControlWake。正常构造false后，无论true/false请求或重复请求，均不产生额外wake。
- 全部 `laserParticlesEnabled_` 写点已搜索：类默认false、构造false、setter exchange(false)；其余均为读取。没有store(true)、赋true或第二种构造入口可以绕过门禁。
- 实际Run中的初始、viewport恢复及每帧有效值均从该原子flag读取，再与Renderer能力求合取；始终false，因此旧配置/Host true请求无法使粒子进入发射/步进/绘制有效路径。
- Host仍可保留旧配置原始位以兼容，但最后应用必经上述Controller setter，且Host applied值去重不会形成重复control wake。没有改变激光多指、主体绘制、现有算法/Shader或资源结构。

## 真实 Controller probe 的无窗口边界

新增probe位于 `RunGeneralDrawingOptionsProductionTest` 的 `:6805`—`:6825`：

1. 默认构造真实ContactInputCoordinator、WindowController、InkRenderer、TransparentPresentationController。
2. 配置显式设 `laserParticlesEnabled=true` 后构造真实DrawingController，断言WindowController::Handle为空、GetLaserParticlesEnabled为false。
3. 记录真实wake generation和pending状态，连续请求true/true/false/false/true；每次均断言Get=false及wake/pending不变。

已核构造调用链：

- WindowController没有启动/创建窗口的构造逻辑，成员HWND初始null；AttachExternal/RTS/Window Service均未调用。
- DrawingController构造只保存状态、配置光标外观及Renderer的CPU参数。WindowController::ConfigureDrawingCursor只保存外观并请求控制唤醒；QueueSystemCursorRefresh在HWND为空时直接返回。
- Renderer的ConfigureLaserStyle、ConfigureShapePrimitives、ConfigureLaserParticles均配置CPU值；LaserParticleSystem::Configure（`Draw3.LaserParticleSystem.cpp:175`）只保存有效配置/比例，没有设备或窗口创建。
- TransparentPresentationController构造只分配Impl，没有Initialize/交换链创建；DrawingController未Run。WindowController析构调用DetachExternal，在未附着时清CPU状态后返回。

因此该probe是实际产品对象的状态测试，不是自写fake flag断言，且不会创建需人工关闭的窗口。它仍由既有严格早退的产品CPU probe链运行；本代理没有执行它。

## 格式、范围与规范

- 核对本轮7个UI/i18n/生成文件和3个runtime文件的BOM、CRLF：全部BOM状态相对HEAD保持，工作树全部CRLF。`git diff --check --ignore-submodules=all`通过。
- 当前diff仍是局部控件顺序/显示文案、粒子构造/setter硬门及同文件probe，没有全文件格式化、粒子算法重写、工程/依赖调整或覆盖前轮功能实现。
- 最新PRD R3/R4/R7、design和`settings-performance-and-drawing.md`已同步：移除统一提示但保留新笔锁存；粒子旧字段仅兼容，Controller和setter强制关闭；推荐仅显示值，glyph不改路由。
- 旧研究和implementation-review描述的是前轮已验收行为，本轮以本记录和更新后的PRD/spec为准。无须为暂禁粒子清理算法/资源或扩大改动范围。

## 验证状态

本代理只执行只读搜索、diff和源格式检查，未运行构建/测试。根代理当前负责完整 `InkeysRepo.sln Debug|ARM64` 及相关无窗口验证。本轮R7的最终退出码由根代理记录；未授权的GUI、粒子视觉或真实设备验证仍未执行。
