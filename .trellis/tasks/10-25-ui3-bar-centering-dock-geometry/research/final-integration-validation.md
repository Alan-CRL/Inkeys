# 2026-10-26 接力、最终集成与质量验证

## 当前结论与边界

本轮已恢复并复核原居中任务，完成追加的 Freeze 隐藏退场回归，以及吸附任务的三个窄修复。父任务及两个子任务均保持 `in_progress`，当前任务指针为父任务；代码和自动化已完成，真实视觉验收待授权。未 commit、push 或 archive。未修改不相关的 `09-06-setting-winui3-design-rebuild`。

用户已明确授权继续全部实现，原 PRD/设计/实施计划里的“只规划、待审批”是历史阶段。GUI/Computer Use 仍受用户提供的 AGENTS.md 限制；本轮未启动产品/设置/浏览器窗口、Office/WPS，也未操作桌面。

## 进入会话的审计

- HEAD `fdda877d1d65dae877ee46cd081f36940eea23f6`，进入时仅 6 个 tracked 文件有修改，与 handoff 完全一致。保存原始差异与字节快照到忽略目录 `TestResults/ui3-task-resume/incoming.patch`、`incoming-tracked.zip`。
- 原居中门禁在实际根反推和 viewport 预测两处共享；只有现有 workspace placement 活跃时窄化 display 门禁，未改变场景 Y/宽度曲线、Leave 或普通显示切换所有权。
- **没有产品 DPI/zoom 策略修改。** 之前 diff 中的 dpiZoom/configZoom/合成 monitor DPI 设置属于离屏 fixture。新吸附 fixture 的 dpi 读取仅用于通过生产 helper 生成场景输入，不进入产品路径。
- 原始产物完整 `--no-window` 在仓库内 TEMP/TMP 下退出 0，329.844 秒；默认系统临时目录受写权限限制时有 572 个持久化失败，不能当作代码回归，也未改代码绕过。该基线补齐了上一 handoff 的未完成项。

## 最终改动与最小边界

1. **居中**：保留原共享 workspace 所有权窄修复，重跑全部原场景。
2. **定格退场**：Freeze 原本就在 List 中，仅 hide；问题是隐藏位置未接入当前布局批次。现在复用通用前驱锚点，首选不可见时优先前驱、前区全空才用后继；不按 preset 猜测可配置 A2 的前驱。隐藏成员先画、可见成员后画，用同帧显隐指纹决定层级。保留对象身份、List 顺序、原曲线和 UI3 自动动画。
3. **脱离首帧**：成功 Docked→候选 Floating 时，按旧成功屏幕端点与当前 base 计算普通/底端 seed，首个成功前均不积分；失败候选重新播种，不截断真实成功初值。普通输入阈值、捕获和水平远端 seed、serial/barrier、频率/阻尼不变。
4. **释放吸收**：layoutTranslation 与 screenDelta 分开；同步两轴绝对 base/visual/grip、布局像素 indicator hit 和 DIP button/shape hit。相对 offset/scale 不平移；真实直移补齐 visualBodyCenterScreenX。保持既有几何锁与奇偶发布。
5. **容量/裁剪保护**：完整 predicted/committed viewport 先求解，再作为 requiredViewport 决定 capacity 和实际资源；按最终 origin 算 source。绘制前和 GetDC 前验证 source+size 包含于真实 bitmap，活跃提示峰值包络同时在 viewport 内。无效则保留全脏和成功快照、沿既有退避重算，不钳 source 提交。

未改工程、SDK、依赖、配置格式、DPI 策略、动画参数、主/设置窗口、旧 UI2 或第三方源码。新增 Interaction 接缝仅在 presentation harness 编译开启时存在，运行真实 mailbox/Pump/Seek；Release 的两个 CLI stub 均在产品窗口初始化前返回 2。

## 可信红/绿证据

- 居中旧基线：前一 agent 的真实 RenderLoop 中间成功帧最大偏心 105.3 px；本轮复核同一门禁及样本范围，未把基线与后来增加失败注入的 fixture 声称逐字节相同。
- Freeze 新回归先红：旧逻辑右展目标 640 DIP、前驱 340 DIP；左展目标 40 DIP、前驱 115 DIP。最终左右展开、100%/150%、合并/分离与两种 A2 顺序共 **1248 帧、24 个 case/order/mode 组合**，List 身份/顺序、前驱目标、渐隐及居中断言通过。
- 吸附先红：旧真实 Seek/Pump/RenderLoop 的脱离高度额外误差最大 **40.848717281 px**，释放吸收屏幕端点误差 **65/98 px**。旧 raw 文件为 `dock-absorb-before-fix.csv/.log`。曾用零 translation 的初版吸收夹具不构成反例，已加强后才采信。
- 容量纯回归直接让 predicted viewport 超出旧 capacity；旧五参数顺序确认越界，新 requiredViewport 路径包含完整 source。覆盖重复帧不增长、idle 不缩 target、负 source、空 size、越界和整数加法溢出；不把该静态反例冒充本次 GUI 提示裁剪的唯一根因。

## 最终当前产物实际验证

| 验证 | 结果 |
| --- | --- |
| 完整 `InkeysRepo.sln Debug|ARM64`，VS 当前安装的 ARM64 原生 MSBuild | 退出 0；最终日志 `final-a2-solution-build.log`，仅有既有第三方警告 |
| `InkeysHeadlessTests.exe --no-window`，1200 秒期限、私有 TEMP/TMP | **退出 0，337.813 秒，末尾 PASS animation correctness**；SHA-256 与最终 Solution 的测试产物仍一致 |
| `Inkeys.exe --ui3-workspace-centering-test` | **退出 0，24.687 秒**；6 组、72 阶段、2370 个成功样本，最大已记录中心误差 **1.13687e-13 px**；Freeze 24 组合通过 |
| `Inkeys.exe --ui3-bottom-dock-transition-test` | **退出 0，14.75 秒**；19 组（Desktop/PPT/Whiteboard × 6 环境 + 动画关闭），722 候选记录、674 成功帧、72 个动画脱离转换、19 次吸收、38 次失败注入；动画脱离最大已记录高度误差 **1.13687e-13 px** |
| 既有 `--bar-eraser-offscreen-test` | **退出 0，27.5 秒，EraserVisual failures=0**；在私有 cwd 配置目录执行，覆盖共用隐藏锚点及现有真实 UI3 像素/脏区回归 |
| 独立 CSV/格式/范围复核 | 712 次实际 ULW 候选 source/dirty 全部有效；吸收前后记录的竖向屏幕端点误差 0；预期 10 个 tracked 文件、原 BOM/CRLF、`git diff --check` 均通过 |

原始日志、CSV、二进制 SHA、独立复核 JSON 均在 `TestResults/ui3-task-resume/`。CSV 浮点使用原有流精度；1e-6 的直接几何断言在 C++ 中执行，CSV 只按已记录误差做独立复核，不声称用截短端点重建全部浮点精度。

## Full-scope trellis-check

- 检查当前 UI3 唯一路线、主栏表现锁/渲染所有权、输入 serial→布局/映射→indicator/dirty→viewport/capacity→GetDC/ULW→完整成功快照；无新的并行几何真值或补偿状态。
- Freeze 前驱选择支持可配置 A2，隐藏/可见绘制两遍不读取实时 hide 拼代；现有 AnchorHidden 接口与所有指定首选锚点保留。
- 释放重基准的锁序为 geometry→hit，与成功发布一致；命中函数不在持 hit 锁时取得 geometry/读取 seqlock。失败 EndDraw 不得被成功 sink 或吸收 serial 增量误判成完整成功。
- 失败重试断言区分：映射稳定时完整 client dirty 也构成整窗恢复；映射变化才强制 prcDirty=nullptr。没有为过严测试扩大产品行为。
- 组合夹具按生产场景 dock 线生成后续输入，按下仍取在途成功位置；动画关闭按既有立即落定合同检查，不误用动画连续性要求。负原点在真实 Display 服务已启动时明确 SKIP，不能伪称负原点覆盖；本轮六个负原点/场景组合均实际产生样本。
- 代码之外没有 lint/typecheck 构建系统；完整 C++ Solution 编译、无窗口算法、真实离屏事务及 diff/编码检查构成本轮自动化质量门。未另跑 Release，关闭 harness 的 stub/编译边界只做静态审查。

## 环境问题与未验证项

- 沙箱内构建曾报 C1902；沙箱外配合私有临时目录构建成功，归类 Codex/本机环境问题，未唯一定位 PDB 根因。一次完整重编译 C1060 内存不足，在命令层限制 `-m:1 -p:CL_MPCount=1` 后成功。保留 Path 再移除 PATH/重建单一 Path，所有规范化在同一 PowerShell invocation。没有修改工程或工具链规避。
- 本次不能声称真实 ULW/DWM、Office/WPS、真实多屏/任务栏、白板最小化恢复和回弹主观手感已验证。GUI 授权问题已向用户单独提出；未收到授权前不执行。
- 三任务不归档、不标 completed，待真实视觉验收或用户明确调整验收范围；当前父任务指针保留以便继续。
