# 验证记录：擦除形状与缓存精灵光标（阶段 A + B）

日期：2026-11-07；分支 bugfix/hand-erase，基于 a2785d6c，未提交。阶段 A、B、C 均已实施。

## 实施分工

- Codex `gpt-6.1-sol`，effort high，`service_tier=default`，`--sandbox workspace-write`：
  - 阶段 A：存储私有键 `inkeysEraserShape`、形状枚举与等面积换算、擦除类型 23 几何/着色器、白板 JSON、测试。
  - 阶段 A 修订（主 Agent 审查要求）：矩形扫掠凸包从“逐像素礼品包装”改为 VS 每段计算、PS 仅 4 个轴向半平面 + 2 次 dot + max。
  - 阶段 B：`Draw3.EraserCursor.h` CPU 栅格器（形状/描边/抓手/星光 D1/方块橡皮 Inkeys 图标，解析距离 + 1px AA）、预乘 BGRA8 + 盒式 mip、Draw3 自有 IMMUTABLE 纹理缓存（外观键 × 2 的幂档）、精灵类型 24（t15/s1）、测试与 draw3-cursors.md 规范。
- 主 Agent（Claude）：存储门禁独立核验（旧 `ValidateMap` 忽略未知键、旧导入不读该字段）；着色器性能问题发现与返工；缓存逻辑审查（每帧仅查找、失败配方不逐帧重试、外观键 ≤ 18）；参数逐项对照 design §4；构建、Win7 字节码生成、测试。
- 光标视觉经 v1–v4 网页预览与用户确认定稿（research/eraser-cursor-preview-final.html、eraser-cursor-preview-sparkle-d1.html）。

## 结果

| 项目 | 结果 |
| --- | --- |
| InkeysRepo.sln Debug\|ARM64（ARM64 原生 MSBuild） | exit 0（期间 2 次 LNK1000 增量链接内部错误，删除 .ilk 后通过，属环境） |
| Win7 guarded 字节码（Generate-Win7-GuardedShader.ps1 + fxc 10.0.26100） | 阶段 A：41360 B；阶段 B：41700 B，均“original math and signatures preserved”，重建后再次构建 exit 0 |
| `Inkeys.exe --draw3-palm-eraser-test` | PASS |
| `Inkeys.exe --draw3-renderer-upload-test`（含精灵纹理缓存，无 HWND/交换链） | PASS failures=0 |
| `InkeysHeadlessTests.exe --no-window --palm-eraser-only` | 1790 项 0 失败 |
| `InkeysHeadlessTests.exe --no-window` 全量（含栅格器用例） | exit 0，stderr 空 |
| `inkStrokeModelerTest.sln` UInk 测试 | 持久化/页文件全部通过；`highlighter_geometry_tests.cpp:663` 1 项失败为既有问题（所用 demo `stroke_geometry`/`ink_prediction` 自 71858b0b 未改动），未处理 |

## 未验证

- 矩形擦除与精灵光标在真实 GPU 上的画面（仅 CPU 数学、纹理上传与回归）；Win7 运行；GUI 未启动。

## 阶段 C（配置 / 光标页 / 主栏预览）

- Codex（同上设置）：三配置键、偏好→Draw3 传递、`InlinePreviewSelector` 控件、光标页“橡皮光标”卡、主栏预览/命中/选中环/焦点、zh-CN 文案与测试。首轮因 prd AC5 与 R3/R8 圆角描述矛盾按约束停下，主 Agent 修正文档后重跑。
- 主 Agent：UI 规格（来自 v3/v4 模拟）、审查（控件尺寸与选中环、预览缓存、主栏位图缓存）；19 语译文（10 键）；修复 2 处编译问题：`Draw3.EraserCursor.h` 被含 `<windows.h>` 的 UI 代码引用时 `std::min/max` 宏冲突（改为 `(std::min)(...)`，22 处），`Setting.cpp` 中 `Design::LayoutRect` → `Inkeys::UI::Setting::LayoutRect`（2 处）。

| 项目 | 结果 |
| --- | --- |
| i18n sync/check（pwsh 7） | 20 语 780/780，PASS |
| InkeysRepo.sln Debug\|ARM64 | 上述修复后 exit 0 |
| `--draw3-palm-eraser-test` / `--draw3-renderer-upload-test` | PASS / PASS failures=0 |
| `--config-boundary-test` | PASS |
| `InkeysHeadlessTests --no-window --setting-eraser-only`（含橡皮光标卡） | settings=0 speed=0 attributes=0 palm=0 |
| `InkeysHeadlessTests --no-window` 全量 | exit 0，stderr 空 |

未验证：设置页与主栏的实际 GUI 观感、真实 GPU 光标画面、Win7 运行。

## 用户复核修订（2026-11-07，主 Agent 实施）

- 配色：所有纹样与 Inkeys 图标（屏幕 + 橡皮）统一为描边灰（浅 207 / 深 110），取消独立纹样色与半透明屏幕。
- 光标页：橡皮光标改为独立分类，位于“按设备设置”与“数位板识别”之间；卡片标题改为“外观”（新键 `Cursor/Eraser/Appearance`）。
- 手掌页：“适用工具”移入“精细控制”、“边擦边写”上方；页尾新增“橡皮光标”分类（i 信息块 + 手掌橡皮形状卡：跟随默认 / 三形状）。
- 运行：`Drawing.PalmEraser.Shape`（-1..2，默认 -1，严格校验）→ `PalmEraser::Settings::shape` → `PromotePalmRuntime` 经 `ResolveShape` 覆盖笔划形状；光标随笔划形状。排除范围当时仍为圆形；其后已在 custom 子任务 R6 改为随形状。
- i18n：6 个新键，19 语补译，路径名取各语言既有“绘制 › 光标 › 橡皮光标”。

| 项目 | 结果 |
| --- | --- |
| i18n check（pwsh 7） | 20 语 786/786 PASS |
| InkeysRepo.sln Debug\|ARM64 | exit 0 |
| `--draw3-palm-eraser-test`（含手掌形状覆盖/越界规范化） | PASS |
| `--draw3-renderer-upload-test` | PASS failures=0 |
| `--config-boundary-test`（含 `Drawing.PalmEraser.Shape` 边界） | PASS |
| `--setting-eraser-only`（外观卡新标题/后续标题、手掌形状 4 预览与点击） | settings=0 speed=0 attributes=0 palm=0 |
| `--no-window` 全量 | exit 0，stderr 空 |

未验证：真实 GUI 观感（设置页两处、主栏）、真实 GPU 光标、Win7、手掌形状实机擦除。

## PR 前审阅与修复（2026-11-07）

以下为本次文档收尾依据的主 Agent 核实结论与独立运行记录；文档收尾未重新执行构建或测试。

- 审阅方式：Codex `gpt-6.1-sol`（High，Fast，只读）三路独立审阅：手掌逻辑 / 渲染存储 / 设置文档；主 Agent 核实，修复交 Codex `gpt-6.1-sol`（High，Fast，workspace-write）执行两轮。
- 已修复（提交 `3a055c34`）：
  1. `Draw3.DrawingController.cpp` 的 `PromotePalmRuntime`：普通活动橡皮（原 tool 已是 Eraser 且有擦除点）转手掌时，保留该笔 Down 锁存的形状，避免手掌所选形状覆盖整笔 eraserShape、改变旧擦除点固化/重放形状；手掌形状与定尺、光标、屏蔽范围同步为该形状。其他原工具（笔 / 荧光笔 / 激光 / 图形）仍用手掌所选形状。`--draw3-palm-eraser-test` 相关断言已改为 Eraser 保留原形状、Pen/Laser 覆盖。
  2. 两处新增的邻近 Touch 落笔拒收分支（`RejectPalmNeighborTouch`）在调用前保存 MetricKey，并补 `Invalidate(InitRejected)` 退休已登记的 metrics 记录；无低成本夹具，未新增专门测试。
  3. spec `palm-eraser.md` 中“不使用速度尺寸控制器”限定为动态模式，并补充上述形状保留规则。
- 已知未修项按用户决定仅记录、不修复：blocking 的两项低概率竞态（中）见 [blocking validation](../11-07-palm-eraser-phase2-blocking/validation.md)“PR 前审阅：已知未修项”；光标纹样显隐使用烘焙尺寸（低）见 [shapes validation](../11-07-palm-eraser-phase2-shapes/validation.md) 同名节。任务完成不代表这些问题已修复，也不代表未测场景已验证。

最终验证结果（主 Agent 独立运行）：

| 项目 | 结果 |
| --- | --- |
| `InkeysRepo.sln` Debug\|ARM64 | exit 0 |
| `--draw3-palm-eraser-test` | PASS |
| `--config-boundary-test` | PASS |
| `--draw3-renderer-upload-test` | PASS failures=0 |
| `InkeysHeadlessTests --no-window` 全量 | exit 0，无失败 |
| `--palm-eraser-only`（首轮修复后） | cases=2053 failures=0 |
| `Scripts/i18n.ps1 check` | 通过 |
| `git diff --check` | 通过 |

仍未验证：真实大屏多触点、GUI 观感、真实 GPU 光标、Win7 运行；此前记录的待母语复核项仍保留。

## PR 前审阅：已知未修项

按用户决定仅记录、不修复：光标纹样显隐误用烘焙尺寸（低）。

- 触发条件：纹理最小烘焙短边为 32px，Draw3.EraserCursor.h 的 PatternDistance/CompositeLogo 用烘焙短边判定“星光 <20px 隐藏、≥40px 才有小星、Inkeys <24px 隐藏”。实际绘制短边更小（如笔速橡皮缩小到 16px）或升档后缩回时，纹样仍显示，违反本子任务 prd AC2 的显隐门限。
- 影响：光标纹样显示不符门限，不影响擦除范围；默认 24/32/40 DIP 档在 100% 缩放下不受影响。
- 未修原因：需把显隐档加入缓存配方/缓存键，并改栅格器、渲染器、设置页与主栏预览调用方及测试，估计超过 40 行，触及渲染缓存且无 GUI 验证。
- 建议处理时机：作为后续小任务处理。

## PR 前审阅：测试环境备注

- inkStrokeModelerTestTests 在仓库根目录运行时，仅 highlighter_geometry_tests.cpp:663 一项失败，为既有测试索引错误：把 stored[1] 与 scratch[0] 比较；demo 几何实现与用例在本分支均未改动。
- uink_tests.cpp:2255/2724 的目录 lease 失败仅在从 ARM64\Debug 目录运行时出现，属于工作目录相关的环境因素。
