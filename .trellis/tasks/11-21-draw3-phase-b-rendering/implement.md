# 实施计划

配套文档：[需求](prd.md)、[设计](design.md)。用户于 2026-11-22 批准本计划。

## 节奏与约束

每个阶段：实现 → 构建 → 无窗口自检 → Codex 只读审查 → 修复 → 向用户报告。提交只在用户授权后进行，不推送。源文件保持 UTF-8 BOM 与 CRLF，最小改动，不顺手重构。

## 分工

| 角色 | 负责 |
| --- | --- |
| Claude | 规格与任务文档；S1–S4 的实现；构建与门禁；逐条裁定审查意见；独立审查 |
| `gpt-6.2-sol`（High，fast） | 按规格写 S1、S3、S4 的自检；S5 的实现；每阶段只读审查 |
| `gpt-6.2-astra`（High，无 fast） | S0 开工核对；S6 的实现；S7 终审 |

一次只让一个 Codex 写文件；它写的时候不并行跑产品自检。Codex 的结论一律由 Claude 读码或实测核实。

## 阶段

| 阶段 | 内容 | 主要文件 | 自检 |
| --- | --- | --- | --- |
| S0 | 任务文档、清除与合成计数、基线（单笔抬笔 12 次整屏清、纯激光抬起 8 次）、Astra 开工核对 | 任务目录、`Draw3.Renderer.*` | 计数基线 |
| S1 | 按层通用的多矩形登记、1/16 规则、ClearView 能力日志、类型 25 单层合成、保护版重新生成 | `Draw3.Renderer.cpp/.cppm`、`Assets/inkPixelShader.hlsl`、`Assets/inkVertexShader.hlsl`、`Draw3.Win7GuardedShader.h` | 多矩形、L1 登记、阈值两侧、单层与双层等价、保护版比对加类型 25 |
| S2 | 清除入口统一；上屏合成按层内容选择 | `Draw3.DrawingController.cpp`、`Draw3.ShapeRuntimeActions.inl` | 三种合成选择与双层合成逐像素相同 |
| S3 | 路径 B 及全部入口 | `Draw3.DrawingController.cpp`、`Draw3.StrokeGeometry.cpp` | 与整笔重放容差 0；清除次数断言 |
| S4 | 路径 A | 同上 | 与整笔重放容差 1 级；条件不满足时落到路径 B；命中率输出 |
| S5 | 图形临时层按矩形清 | `Draw3.RendererShapes.cpp` | 哨兵像素 |
| S6 | 窗口交接两段回执 | `IdtState.cpp`、`Window.cpp`、`Draw3.TransparentPresentation.cpp`、`Draw3.Host.cpp` | 无窗口状态用例；真实画面由用户实机验收 |
| S7 | Claude 独立审查、Astra 终审、全量门禁、规范文档更新 | `.trellis/spec/` | 全量 |

## 验证入口

- 构建：ARM64 原生 MSBuild 构建 `InkeysRepo.sln`，`Debug | ARM64`，同一 PowerShell 调用内先清理重复的 PATH 并设 `MSBUILDDISABLENODEREUSE=1`，超时至少 5 分钟，核对退出码。
- 无窗口自检：`Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window`；产品自带 `--draw3-renderer-upload-test`、`--draw3-pen-taper-test`、`--draw3-shape-renderer-test`、`--draw3-laser-resources-test`。自检参数全等匹配，启动前先核实参数拼写，避免误启动产品主窗。
- 着色器改动后核对产品 FxCompile 产物、资源嵌入和保护版实际字节码与哈希。
- 不启动产品窗口，不做界面操作测试。

## 需要用户实机确认的项目

- Win7 WARP：保护版重新生成后主画布、橡皮、激光正常。
- 兆芯：新字节码下书写与合成正常。
- DComp 与 Win7Dwm：有墨迹切换时无整幅墨迹消失。
- 旧 Intel 核显、4K：抬笔与持续书写的手感。

## 进度记录

- 2026-11-22：S0 开始。任务文档重写并激活。
- 2026-11-22：Astra 开工核对（报告在仓库外 `%TEMP%\inkeys-codex\phase-b\s0-astra-report.md`）。采纳并写入设计的 5 项：重做经历史直接回放后 L1 有残留；路径 A 需按“实际画进 L1 的点”建指纹（橡皮单点印记、模型 Up 回退、手掌晋升、公切线反向改写）；改尺寸拷回 L1 须标记写域未知；交接第一段仍接受遮挡状态以免循环等待；同帧多笔保持现有遍历顺序。另记：焦点层落定改单层合成。
- 2026-11-22：S1 生产代码完成（登记、清除规则、单层合成、类型 25）。保护版重新生成：41856 字节，SHA256 `2DEC1FF46DF01019B11950982568D273C5C59F6A34299DB7546644FB1C656234`；生产像素着色器 41500 → 41624 字节。
- 2026-11-22：S1 自检由 Sol 编写（渲染器上传自检 R7 块）。Sol 只读审查报 P1 一条（画布左侧外的大半径单点：包围盒裁剪后为空，右伸短胶囊却进入画布，层被登记为空）与 P2 两条（极端坐标溢出；容量用例防不住回归），均属实并已修复：新增 `RectFromStrokeOrDotPoints`，登记前把坐标收进 ±2^24，自检补容量、合并吞并、极端坐标、全在画布外、生产写域回读。变异验证：临时撤掉单点修复后 `--draw3-renderer-commit-failure-test` 报 4 处失败。
- 2026-11-22：S2 完成。控制器 19 处、图形运行时动作 15 处清除改走登记入口，与整清 L2 相邻的 4 处保持整屏清；`ApplyActiveOperatorLayers` 按层内容选择合成；L1 三处书写热路径写入补写域声明。自检在 `--draw3-laser-raster-failure-test`。
- 2026-11-22：S1+S2 实测（Debug|ARM64，本机 Adreno X1-85 与 WARP）：无窗口测试全集通过；渲染器上传、提交失败、激光失败、笔锋、图形渲染、激光资源、图形焦点隐藏、上传兼容自检 0 失败；像素比对矩阵 WARP 69 项 0 失败、保护版 18 项 0 失败（哈希为新生成值），硬件 0 失败。`--draw3-hidden-test` 与 `--draw3-eraser-hidden-test` 以 0xE143001D 提前退出，与基线相同（既有问题）。
- 2026-11-22：S3 生产代码完成并构建通过（路径 B）。相对设计的一处调整：只有激光结束的帧不能跳过整个抬笔块，因为其余触点的 L0 刷新只在“无触点结束”的分支里做；改为该帧走普通书写帧的 L0 刷新（抽成 `refreshSharedLiveLayer`），不清共享层、不重建。`DrawStoredStroke` 改为绘制后按脏区补登记（`BindOperatorTarget` + `NoteOperatorWrite`）。抬笔主流程在本机没有端到端像素自检：隐藏窗口集成自检提前退出前的撤销、重做记录条数与基线相同且无失败。Sol 只读审查进行中。
- 2026-11-22：S3 审查（Sol 只读）：P1 一条（大半径单点入库重放的写域小于实际绘制，残留会混入同帧下一笔）与 P2 两条（焦点图形在首次栅格成功前就被发布；断触续接手工诊断标记不随本笔进 L2）属实并修复。焦点图形恢复“先栅格一次验证成功”，仍画两次，设计里“只画一次”的说法作废。多步构造预览的重画条件保持原来的“有触点结束即重画”。
- 2026-11-22：S4 完成（路径 A）。每个触点记录画进 L1 的点的指纹（哈希、点数、末点、是否精确、L1 内容世代）；荧光笔用已提交图元是否为整笔图元前缀判断。等价性自检由 Sol 编写，入口 `--draw3-renderer-commit-failure-test`：路径 B 各工具与“整笔画进 L1 双层合成”逐字节相同；路径 A 笔、橡皮、荧光笔与整笔绘制逐字节相同；各拒绝条件生效。
- 2026-11-22：S4 审查（Sol 只读）四条属实并修复：
  - 历史直接回放等留在 L1 的残留会被路径 A 写进 L2 → 渲染器新增 L1 内容世代 `StableLayerEpoch()`，起笔时 L1 必须为空且之后世代不变才可沿用前缀。
  - 画布偏移较大或带小数时，入库坐标往返丢低位，路径 A 与整笔重放可差十几级 → 定稿点列必须逐点往返还原才走路径 A（偏移为 0 时恒成立），容差由 1 级收紧为 0。
  - 荧光笔前缀缓存重画失败后仍可沿用 → 失败置为不可沿用。
  - 单点右伸：合成脏区与可见性裁剪保持基线行为（与历史瓦片足迹一致），只把工作区清除用的写域按实际绘制端点放大。
- 既有问题（未处理，记录）：圆头单点的右伸短胶囊不在入库重放脏区、可见性裁剪和历史瓦片足迹之内，大半径单点抬笔后右伸部分不进 L2；重做合成或登记失败时先清层后回滚，回滚走历史直接回放会把最后一笔留在 L1。
- 2026-11-22：`--draw3-hidden-test` 在本机的行为：进程以 0xE143001D（输入生产者停止未能证实，`FailedCleanupDeadline.cpp`）提前终止，多数运行停在“WM_MOUSE pan: Win32 mouse producer is inactive”之后；少数运行会继续到断触续接与白板翻页段并报失败。是否为基线既有，待用基线工作树对照。
- 2026-11-22：`--draw3-hidden-test` 基线对照。用基线提交 `cd942ed0` 在临时工作树单独构建并运行 4 次：3 次跑到后半段并报 6–8 处失败（断触续接、白板翻页段），1 次在前半段终止且 0 失败，全部以 0xE143001D 结束。结论：本机上该自检后半段的失败是基线既有，不是本任务引入；它在本机不能作为通过/失败的依据，只能比较终止前的输出。
- 2026-11-22：S5 完成。旧式图元由 Sol 实现（材质层按上次写域清除，合成范围收进 `LegacyShapeWriteRect`，极端坐标退回整视口）。通用图形 Sol 按简报停下（图元未剪裁，会略超出图形矩形），由 Claude 补上：图元绘制在带剪裁的光栅状态可用时剪裁到图形矩形（后续合成只读该矩形，结果不变），材质层与合成层都按上次写域清除；该光栅状态是可选资源，缺失时不剪裁并退回整屏清。自检由 Sol 编写，`--draw3-shape-renderer-test` 515 项 0 失败。Sol 报告的疑似既有问题：通用图形合成矩形为包围盒外扩 1 像素，扁长填充椭圆在矩形外一列可能仍有约 22% 覆盖被裁掉，未回读确认，未处理。
- 2026-11-22：S5 审查（Sol 只读）。属实并修复一条：通用图形外框恰好贴在画布边外时入库重放脏区为空，但合成矩形多出的 1 像素列仍会写入 L0 且未登记 → 写域按合成矩形补登记。其余九项无问题；左侧单点自检用例偏弱（相邻的“8px extension”用例仍覆盖），未改。
- 2026-11-22：S6 完成（Astra 实现，Claude 验收并修改）。两段交接：`IdtState.cpp` 的 `ApplyReadyDrawpadSurface` 协调，`PlanInkHandoff` 为可无窗口验证的纯逻辑，窗口服务新增只改一个 HWND 的分步命令，宿主把“显示后确认”请求交给绘制线程，呈现层核对 `Present1` 原始返回值并等一次 `DwmFlush`。S6 审查（Sol 只读）后改动：Astra 原实现在三次确认失败后撤回新窗并停止重试，可能停在“模式已切、窗口未切”；改为耗尽后退回直接成对切换，删除撤回分支。保留的取舍：同一工作区内翻页时不走两段交接（两页墨迹叠加比一帧空白更糟），沿用直接成对切换。
- 2026-11-22：S7 终审（Astra 只读）五条：
  - 历史直接回放把最后一笔留在 L1，之后若只有激光抬起，新流程不重建共享层 → 纯激光抬起的快速分支增加“上次抬笔重建后 L1 世代未变”的条件，否则按原流程重建。
  - 活动通用图形与步骤间构造预览同样漏登记合成矩形多出的 1 像素 → 两处写域按合成矩形声明。
  - 实时颜色 RGB 用除以 255 解码，入库重放用乘 1/255，路径 A 的前缀与整笔重放可差 1 级 → 实时解码改为与入库一致。
  - 耗尽回退的直接切换只带较弱的守卫 → 改用与分步命令相同的场景与窗口代际复核。
  - 同帧两笔覆盖同一瓦片且前一笔提交失败时，后一笔的撤销前像缺少前一笔：沿用改动前的提交顺序，属既有问题，未处理。
- 2026-11-22：最终门禁（Debug|ARM64）。产品自检 14 项全部退出码 0 且无失败：`--state-mode-production-test`、`--draw3-renderer-commit-failure-test`、`--draw3-shape-renderer-test`（515 项）、`--draw3-renderer-upload-test`、`--draw3-laser-raster-failure-test`、`--draw3-shape-focus-hidden-test`、`--draw3-shape-core-test`、`--draw3-pen-taper-test`、`--draw3-pen-width-test`、`--draw3-laser-resources-test`、`--draw3-palm-eraser-test`、`--draw3-renderer-map-test`、`--draw3-renderer-pixel-test`、`--draw3-renderer-pixel-test-hardware`。`--draw3-eraser-hidden-test`、`--draw3-auxiliary-resource-lifecycle-test` 与 `--draw3-hidden-test` 一样以 0xE143001D 提前结束，后者经基线对照输出一致。未构建 Win32/x64。
- 未做：设计里的“合成跳过”计数与路径 A/B 命中率的调试输出（计数字段 `tailPathStrokes`/`isolatedPathStrokes` 已有，未接输出）；强制 ClearView 缺失的整屏清回退自检。
- 待实机验证：Win7 WARP 受保护着色器下的类型 25；兆芯；DComp 与 Win7Dwm 下选择态与绘制态切换是否还有空闪；旧 Intel 核显 4K 下的抬笔延迟；画布边缘的图形预览。
