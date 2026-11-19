# 实施计划：UI3 自动脏区统一（主栏与翻页控件）

角色（用户 2026-11-18 指定）：Claude 写代码并做独立审查；Codex 负责前期调查（含开工核对）与最终审查，只读。Codex：`gpt-6.1-sol` + High + `service_tier=fast`（仅本会话可用），简报/报告在 `%TEMP%\inkeys-codex\ui3\impl\`（公共段 `00-dirty-common.md`）。

## 0. 基线与规则

- 基线：HEAD `8a1ac516`（第1步已提交），检查点 `s7c`。
- 工具沿用第1步：`build.ps1`、`fixfmt.py`、`runtests.sh <tag> <tests…>`、`cmp-golden.sh`、`checkpoint.sh <name>`、`s7-verify.sh <tag>`（全量）、`dock-cmp.sh`。
- 改动的 `.cpp/.cppm/.h` 保持 UTF-8 BOM + CRLF；新文件登记进 `Inkeys.vcxproj(.filters)` / Headless 工程；构建 `InkeysRepo.sln` `Debug|ARM64`；全量验证期间不并行重活；不提交，除非用户授权；不推送。
- 每阶段：`fixfmt.py --check` → 构建 → 本阶段验证 → golden 35/0 → 检查点。阶段内失败先定位第一个有意义的错误。

## 1. 阶段

| 阶段 | 内容 | 验证 | 检查点 |
|---|---|---|---|
| V0 开工核对 | Codex 只读核对 `design.md` 与代码是否矛盾（字段清单、准备阶段可前移性、候选语义、翻页四目标、尺寸逻辑不受影响），矛盾即列出 | 主 Agent 逐条核实，修订设计后再开工 | — |
| V1 登记表核心 | 独立模块 `Inkeys.UI.UI3.VisualDirty`：身份/代次、组树、采样缓冲与规范化、比较、attempt 捕获/提交/保留、墓碑、重基准；Headless `visual_dirty_tests.cpp` | Headless 新测试全过；既有 `dirty_region_tests` 不变 | `v1` |
| V2a UI3 适配器 | 分区 `:VisualDirty`：标准图元/按钮/组的采样与局部包络；SVG/PNG 内容版本；逐类型采样字段表 `research/sample-fields.md`（独立审查） | 构建；Headless；golden 35/0 | `v2a` |
| V2b 准备阶段前移（全宿主，像素不变） | 主栏新增「帧准备」步骤（继承同步、派生视觉，早于呈现分类）；UI3 按钮角标/PNG 字段/字号解析前移并改只读绘制接口；绘制属性三档数字（按绘制实例）、popup 专用绘制参数、FineDial 几何与标签布局及宽度发布前移；翻页 `Render()` 不再推进/摆放（推进与几何准备分离）；绘制纯度检查工具；全脏原因诊断标记 | 构建；golden 35/0 逐字节；第二宿主；`--ui3-page-control-*`、橡皮、图形属性、居中、隐藏窗口 0 失败 | `v2b` |
| V3 主栏宿主接入（影子模式） | 主栏渲染状态持有登记表；扫描放在帧准备步骤内；映射状态（Body/Grip/Rigid/None）；绘制作用域表 `research/draw-scopes.md`；登记主栏渲染循环全部对象（含 V4–V6 将切换的面板与自绘对象）；影子登记表与生产隔离，只诊断。新离屏测试 `--ui3-attribute-dirty-test` 框架（每帧 `prcDirty`、每个局部帧 BGRA 对照、纯度检查、全脏原因）与影子对照输出 | 构建；golden；影子对照（新 ⊆ 旧、像素差集 ⊆ 新）覆盖全部已登记对象 | `v3` |
| V4 属性面板 | 形状/橡皮（含自动粗细菜单）/撤回/构造提示：组与成员登记、自绘对象（橡皮预览、粗细胶囊、长按环、构造提示环）；切换为新脏区，删除这些面板的组键与整组 Observe | 新测试面板用例（上限、覆盖、BGRA 一致、纯度、无未分类全脏）；`--ui3-eraser-attribute-test`、`--ui3-geometry-attribute-test` 0 失败；golden | `v4` |
| V5 绘制属性与取色器 | 自绘对象（预览/激光芯/滑块、FineDial、三档圆点数字、thumb 与数字浮窗、保持环、取色器色盘/marker/保持环）；切换并删除带键 `ChangeValue` 与组键 | 新测试绘制属性用例；golden；第二宿主 | `v5` |
| V6 主栏 / More / Logo / 状态指示 / 底栏吸附指示 | 切换剩余对象；状态指示器按稳定条目登记；吸附指示整体登记（首次峰值保留）；删除逐对象 `MarkChanged`/`Observe`；光影响应范围改由登记表提供；收窄未分类兜底 | 新测试主栏/More 用例（脏区不大于迁移前）；底栏吸附 `SAME-AS-BASELINE`；居中、隐藏窗口；golden | `v6` |
| V7 翻页控件 | 四个目标各一登记表（扫描早于旧候选复用判断与 `PendingDamage`）；主题色样/网格点阵/加载环/背景扩展回调作自绘对象；实际绘制 attempt 绑定 `sceneSerial`、回执确认时提交（隐藏/无像素候选不推进像素快照，捕获后新失效保留）；替换 `Include` 累计，保留 full replacement/fallback/回执核对 | `--ui3-page-control-*` 内置检查（含旧回执、晚 bounds、隐藏恢复、页码 damage 上限）；新测试翻页用例；golden | `v7` |
| V8 清理与全量 | 删除剩余被取代代码；规范更新（`rendering-and-ui.md` 脏区合同改写、`ui3-layers.md` 新模块）；全量验证；主 Agent 独立审查全部差异；Codex 只读最终审查（差异文件 + 验收标准逐条判定）；修复后复跑 | 全量通过（§2） | `v8` |
| V9 实机检查 | 用户开启脏区调试显示逐项检查（§4）；按反馈修正 | 用户确认 | — |
| V10 提交 | 仅在用户授权后（无署名、SSH 签名、不推送） | — | — |

## 2. 全量验证通过标准

构建 0 错误；golden `same=35 diff=0`；第二宿主 0 失败（项数可能增加）；橡皮 `failures=0`；inputworker PASS；形状编辑 `failures=0`；图形属性 `failures=0`；Headless exit 0（含新测试）；多目标每组既有 1–2 条（固定 `culled=0` + 计时预算波动）；底栏吸附 `SAME-AS-BASELINE`；居中、隐藏窗口、原生输入 0 失败；`--ui3-attribute-dirty-test` 0 失败。

## 3. 回滚

每阶段检查点为悬空 tree；回退按文件从检查点取回；不用 `git reset --hard`、`git clean`、stash。影子模式阶段可随时切回旧脏区。

## 4. 实机检查清单（V9）

- 橡皮：清空、三档尺寸、自动粗细正文与箭头、菜单内灵敏度、设置、手掌橡皮。
- 形状：类别按钮、圆心/外接、边数、辅助项、约束项、描边/填充、线型、粗细三档、关闭、长按固定进度环、类别切换与滚动（应整块）。
- 撤回：撤回/重做按钮。
- 绘制属性：色块、笔型、扩展入口与笔型菜单、粗中细三档、粗细调整入口、滑块与精调拨盘、预览弹窗、标注/溢出提示与关闭、取色器入口/Tone/Close/拖动选色。
- 主栏与 More：按钮悬停/点击、拖动、底栏吸附、主题切换、缩放变化；状态指示器小图标与胶囊。
- 翻页控件（白板与 PPT 四个位置）：翻页按钮、页码、菜单与主题面板、加载环。
- 全部检查：只闪变化元素；面板开合/移动整块；无残影、无缺边。

## 5. 进度记录

| 步骤 | 日期 | 结果 | 备注 |
|---|---|---|---|
| 规划 v1 | 2026-11-18 | 逐面板修补方案 | Codex `d1-investigate`；用户选定撤回纳入、绘制属性按钮与连续控件都改 |
| 方向变更 | 2026-11-18 | 用户要求按整体架构统一，主栏与翻页控件一起迁移 | 旧方案作废 |
| 规划 v2 | 2026-11-18 | PRD/设计/计划按统一架构重写 | Codex `d2-architecture` 可行性调查 |
| V0 开工核对 | 2026-11-18 | Codex 报 5 类必须先修订的合同缺口，主 Agent 抽查属实（失败路径 `RetainForRetry(true)` 强制全脏、三档数字绘制时写内容）并已修订：失败恢复保留现有全脏/替换路径；采样用局部坐标+映射状态、环境每帧冻结、上下文入采样；组树按绘制作用域；准备按绘制实例、字号用等价有效变换；光影算法完全不动；新增帧准备步骤早于呈现分类；翻页按实际绘制 attempt 绑定 sceneSerial；准备前移独立成 V2b（解除 V3/V4 对 V5 的依赖）；每个局部帧 BGRA 对照、全脏原因标记 | 待用户审阅 |
| V1 | 2026-11-18 | 通过 | 新模块 `Inkeys.UI.UI3.VisualDirty`（`UI3.VisualDirty.cppm`，UI3 根模块导出）与 `InkeysHeadlessTests/visual_dirty_tests.cpp`（9 组场景）；构建 0 错误，`[VisualDirty] failures=0`，Headless exit 0；检查点 `v1` |
| V2a | 2026-11-18 | 通过 | 分区 `UI3:VisualDirtySampler`（标准图元/按钮/固定按钮采样器与可见性门槛）；`UiStringClass::ValRevision()`、`UiPNGClass::contentRevision`；`research/sample-fields.md`；构建 0 错误；golden 35/0；第二宿主 38/0；检查点 `v2a` |
| V2b | 2026-11-18 | 通过（范围按设计细化调整） | 绘制纯度检查 `VerifyPurity`（含组状态）与采样易变段；按钮 PNG 只采样载荷身份；Headless `--visual-dirty-only` 入口；10 组单元测试 0 失败；检查点 `v2b`。主栏「帧准备」步骤与各宿主的派生前移随 V3 影子模式逐区接入（影子模式可在不影响生产的前提下用纯度检查与覆盖检查驱动） |
| V3 | 2026-11-18 | 通过 | 主栏影子登记表（map 图元按表+键、按钮按地址懒登记、取色器 6 图元；映射与观察段一致；默认关闭）；测试钩子 `SetBarVisualShadowTestHook`；新离屏测试 `--ui3-attribute-dirty-test`（`Bar.AttributeDirty.Test.cpp`：完整主栏场景，每帧整幅读回，核对实际变化像素 ⊆ 登记表脏区）；主栏按钮/绘制属性悬停/取色器开合/More 展开收起 0 越界、纯度 0；检查点 `v3a`、`v3b` |
| V4 | 2026-11-18 | 通过 | 形状/橡皮（含灵敏度菜单）/撤回面板 `PrepareVisuals`+`RegisterVisuals`（组=透明度层+裁剪，类别区/参数行区裁剪求交，尺寸预览与自动粗细装饰为自定义对象，粗细胶囊与长按环为按钮附加状态）；面板登记表 `visualPanels` 在面板推进后、呈现分类前准备，新键 `PanelMembers` 承担分类、脏区经 `IncludeDamage` 并入；推进但像素未变时 1 像素保活脏区（不再整窗）；构造提示拆出独立键 `ConstructionPrompt`；光影计算不变。golden 35/0、橡皮 0、形状 1587890/0（含自然帧局部提交覆盖检查）、第二宿主通过；离屏验证：面板全部按钮悬停/按下/抬起 0 越界，脏区约整窗 0.4%–16%（原为整块面板）。已知待办：取色器 Tone 色盘（自绘，V5）、绘制属性色块继承在绘制时写入（纯度检查报告，V5）；检查点 `v4` |
| R1 阶段审查 | 2026-11-18 | Codex（gpt-6.2-sol High，只读）报 2×P0 + 5×P1，主 Agent 逐条核实均成立并已修复 | P0：形状粗细胶囊在底板/图标/文字都不可见时仍绘制 → 胶囊独立贡献可见性与包络并采样输入；登记表脏区未转成呈现需求 → `AdvanceAnimationsAndDeriveLayout` 返回值并入。P1：保活不再标记分类键，只在需求完全来自面板推进时代替未分类兜底；面板根组采样 Rigid 平移与缩放（亚像素平移）；按钮部件显隐逐项入采样、按压外扩改为与中心无关的保守外扩、文字 contentScale>1 包络放大、PNG 以 icon 为准、SVG contentPct 有限性；长按环主题色入采样；登记表多层组注销收集子树、已送出候选在注销后的擦除、淘汰候选迟到确认时作废快照、作废时清候选、首段易变掩码；`UiStringClass::Initialization` 只在值改变时递增版本（原实现使每帧设置相同标签的按钮被判定变化）。测试：golden 夹具新增自然帧入口；`--ui3-attribute-dirty-test` 新增自然帧对照阶段（动画关闭，局部提交的保留画面与整幅参考逐字节比较，自然帧未呈现而画面变化计为漏呈现，失败返回非零）：66 次检查 47 次硬比较 0 不一致、0 漏呈现；18 次未稳定、1 次窗口尺寸过渡单独计数。登记表新增调试名称与变化项记录。 |
| 任务外发现 | 2026-11-18 | 待报告用户 | 动画关闭时，悬停在橡皮/形状面板按钮上，底板透明度在数帧乃至持续往复变化（悬停握手先显后淡、选中按钮上悬停与选中目标互相改写）；产品原有行为，旧机制下同样每帧整块重画，可能表现为闪烁。未修改。 |

## 6. V5 工作清单（2026-11-18 制定，接续从这里开始）

基线检查点 `r1`（V1–V4 + R1 审查修复，全部回归通过）。生产登记表沿用 `visualPanels`（在 `AdvanceAnimationsAndDeriveLayout` 尾部、面板推进后准备，键 `PanelMembers`，`IncludeDamage` 并入），把绘制属性对象加入同一登记表，然后删除绘制属性的整组标脏（`ChangeValue(..., drawAttributeDirtyKey)`、`MarkChanged(drawAttributeDirtyKey)`，`Bar.RenderLoop.cpp` 约 4840–5110、取色器 `Advance` 后额外标整组处）。

**做法**：按绘制块整体登记组合对象（包络 = 该块全部像素外接范围，状态 = 该块全部输入）；位置在采样器内用与 `UiInheritClass` 相同的公式沿继承链纯计算（主按钮 → 主栏 Center → 绘制按钮 CenterFromTopLeft → 绘制属性面板 Center → 子对象 TopLeft/Center），不读对象里可能过期的 `inhX`，因而不必前移绘制代码中的继承写入。映射 Rigid；组 = 绘制属性面板（透明度、位姿、`SetFrameDiffuseMaskGeometryScale`）。

| 块 | 绘制位置（`Bar.RenderLoop.cpp`，r1 行号） | 组合内容 |
|---|---|---|
| 面板底 | 约 7593 `drawAttributePanel.DrawFrame` | `DrawAttributeBar` 形状 |
| 色块 ×11 | 7596–7614 `DrawFeedbackButton` | 反馈底板 + 色块 + 勾，按压缩放 `drawAttributeSwatchFeedback[i].pressScale`，透明度 `swatch->pct` |
| 自定义色块 | 7615–7647 | 反馈 + 色盘 PNG + 外框 + 色芯 + 勾，按压 `drawAttributeColorPickerEntryPressScale` |
| 笔型 ×4、扩展入口/箭头/分割线 | 7649–7792 `DrawFixedButtonVisual` | 固定按钮部件 + 按压；箭头退场不缩放 |
| 粗细调节区 | 7793 起约 900 行（预览/激光芯/滑块/三档圆点与数字/拨盘刻度标签选择器/保持环与文字） | 一个组合：包络 = 调节区形状包络（核对拨盘与数字是否越出）；状态 = 全部粗细相关动画值与模式 |
| 预览弹窗与 thumb | 约 9285–9330 | 弹窗底、圆、数字（缩放锚点）、thumb |
| 标注提示/溢出提示 | 约 8989–9011、9126–9130 | 弹窗底、标题/正文、关闭按钮；溢出角标与信息反馈 |
| 笔型菜单 | 约 9143–9170 | 菜单底、自由线行、勾、标注行与信息反馈 |
| 取色器 | `owner_.colorPicker.Draw` | UI3 组件：一个组合（面板包络；进度、toneMix、显示值、标记坐标/可见、预览色、保持进度/透明度、标签版本） |

**验证**：`--ui3-attribute-dirty-test` 动画阶段 `picker.tone` 越界应归零、`eraser.open` 纯度应归零；自然帧阶段补绘制属性按钮与连续控件场景（硬比较）；golden 35/0；第二宿主；形状/橡皮回归。完成后 V6（主栏/More/状态指示/吸附指示）、V7（翻页控件）、V8（清理、全量、Codex 最终审查）。

**V5 进度（2026-11-18）**：V5a 完成（检查点 `v5a`）——`WriteVisualState` 增加 `position` 参数；`Bar.RenderLoop.cpp` 新增 `ResolveBarDrawAttributeChain`（继承链纯计算）、`BarInheritFrom`、`BarShapeRectAt`、`RegisterBarDrawAttributeVisuals`（绘制属性组、面板底、11 个色块组合、自定义色块组合），在 `PrepareBarPanelVisuals` 中登记一次（`visualPanelsRegistered[3]`）。**旧的整组标脏仍保留**（生产脏区只增不减）。离屏验证不变（自然帧 0 不一致）。下一步 V5b：笔型按钮与扩展入口、粗细调节区组合、预览弹窗与 thumb、两个提示、笔型菜单、取色器组合；全部登记后删除绘制属性整组标脏，并在自然帧阶段补绘制属性场景做硬比较。

**V5b-1（检查点 `v5b1`，已编译、未跑测试）**：`RegisterBarDrawAttributeVisuals` 增加 4 个笔型按钮组合（底板面板 TopLeft、图标底板 Left、文字底板 TopLeft，按压 `drawAttribute*PressScale`）与扩展入口底板；新增 `BarWordRectAt`、`BarSvgRectAt`。接续：扩展箭头（含退场箭头不缩放、当前箭头按笔型锚点按压）与分割线（不缩放），`Bar.RenderLoop.cpp` 约 7876–7925（搜索 `ExtensionVisual`）；然后粗细调节区（搜索 `// 粗细调节区域`）、预览弹窗与 thumb（搜索 `thumbInherit`）、标注/溢出提示（搜索 `popupTitle->Inherit`、`overflowBadge->Inherit`）、笔型菜单（搜索 `penTypeMenu->Inherit`）、保持文字（搜索 `holdLockLabel->Inherit`）、取色器组合；全部登记后删除整组标脏并补自然帧绘制属性场景。每段编译后跑 `--ui3-attribute-dirty-test`（应 exit 0）。

**V5b-2（检查点 `v5b2`，已编译）**：扩展箭头与分割线按 3 个槽位（软笔/硬笔/荧光笔）登记为输入采样组合（共享载体在绘制循环中按槽位重写，不能按标准对象登记）。接续：粗细调节区（搜索 `// 粗细调节区域`）。

**V5b-3（检查点 `v5b3`，已编译）**：粗细调节区组合 `draw.thickness`（包络 = 调节区/分割线/调节入口/粗细文字/保持文字外接范围 + 暂定 8 DIP 外扩，待自然帧验证；状态 = 区域内全部动画值、拨盘量程与相位、视图模式与交互标志、笔型、材质）。接续：预览弹窗与 thumb（搜索 `popupSurface->Inherit`、`thumbInherit`）、标注/溢出提示、笔型菜单、取色器组合，然后删除整组标脏并补自然帧绘制属性场景。

**V5b-4（检查点 `v5b4`，已编译）**：预览弹窗组合 `draw.preview-popup`（弹窗底/圆 TopLeft 自面板，数字按锚点缩放 `ResolveScaledDirtyBounds`）与 thumb `draw.thumb`（外扩 8 DIP + 按压）。接续：标注/溢出提示（搜索 `popupTitle->Inherit`、`overflowBadge->Inherit`）、笔型菜单（搜索 `penTypeMenu->Inherit`）、取色器组合，然后删除整组标脏与补测试。

**V5b-5（检查点 `v5b5`，已编译）**：标注/溢出提示弹窗组合 `draw.annotation-popup`、`draw.overflow-popup`（包络 = 弹窗底 + 关闭按压回弹；状态 = 弹窗/标题/正文/关闭部件与锚点）。接续：溢出角标与信息反馈（搜索 `overflowBadge->Inherit(TopLeft`）、笔型菜单（搜索 `penTypeMenu->Inherit`）、取色器组合，然后删除整组标脏与补测试。

**V5b-6（检查点 `v5b6`，已编译）**：组合累加器 `BarPanelCompositeAccumulator`；溢出信息（反馈+图标+角标）、笔型菜单底、自由线行（行+勾+文字）、标注文字、标注信息（反馈+图标）。接续：取色器组合（UI3 `ColorPicker`：需公开采样输入或在 UI3 内提供 `RegisterVisuals`），然后删除绘制属性整组标脏（`ChangeValue(..., drawAttributeDirtyKey)` 等）与取色器额外标整组，补自然帧绘制属性场景（色块/笔型/三档/滑块/提示/菜单/取色器）。

**V5b-7 / V5c（检查点 `v5b7`、`v5c`、`v5d`）**：取色器由 UI3 自供 `ColorPicker::MakeVisualSampler`（不挂绘制属性组）；绘制属性全部绘制块已登记后，`AdvanceAnimationsAndDeriveLayout` 中 `drawAttributeDirtyKey` 改指 `PanelMembers`（不再整组观察；整栏拖动的整组标记保留）。保活放宽：本帧无未分类请求且解析出的业务脏区为空时（被标记键前后包络均空、登记表无变化），一律 1 像素保活，不再退回整窗。离屏验证：动画阶段 0 越界（取色器 Tone 已覆盖）；自然帧 85 次检查，53 次局部提交硬比较 0 不一致、0 漏呈现，平均脏区约整窗 8.4%；28 次未稳定（动画关闭时悬停握手往复，产品原有）、4 次窗口尺寸过渡。全量验证 `v5d` 通过（构建 0 错误；golden 35/0；host 38/0；eraser/inputworker/shape/geometry 0；headless 退出码 0；multitarget 每组 1–2 与基线一致；dock 与基线相同；centering/hidden/native 0）。纯度 575 仍是影子登记表中绘制属性色块的旧 inhX（影子表仍按对象登记，V6 迁移主栏时一并替换为纯继承计算）。

**V6a（检查点 `v6a`，2026-11-18）**：主栏底（Body）、主按钮（Grip）、Logo（Grip，按 Center 继承纯计算）、背景提示、More 面板底/分割线/关闭键（Rigid，More 组随面板可见性）、主栏与 More 全部按钮（按「地址 + 所在区域」懒登记，准备时以绘制同算法写入本帧继承位置）登记进生产登记表 `visualPanels`；主栏组采样 Body/Grip 映射全部参数、PPT 闲置淡化、常驻内容透明度。`AdvanceAnimationsAndDeriveLayout` 中逐对象标脏（`FixedVisualContext`、按钮、超椭圆、PNG、材质、Logo）与 MainGroup/MoreGroup 整组标记改为分类键 `PanelMembers`（底栏形变、主栏时间轴同样）；登记表准备移到 PPT 闲置淡化之后；Advance 开头清零 `visualPanelsPrepared`（提前返回不沿用旧候选）。保留：状态指示器与吸附指示的组件整体键（布局在呈现阶段才确定，分类前无法采样，且各为单一自绘对象）、整栏直接拖动的 sustain 组键、旧几何条（不绘制）键。修正两处绘制期写入：绘制属性反馈底板几何在登记表准备前按同一公式同步（`SyncBarDrawAttributeFeedbackGeometry`）；取色器 Tone/Close 按钮只采样局部状态（继承位置绘制时才解析、由已采样的面板布局决定）。纯度检查改由生产登记表执行。离屏测试新增「只用生产脏区」覆盖核对（硬失败 7）与纯度硬失败（8），自然帧补主栏按钮、More 浮层按钮与关闭键：动画阶段 0 越界、生产覆盖 0 越界、纯度 0；自然帧 105 次检查 0 不一致、0 漏呈现，平均脏区约整窗 4.4%（统计排除 1 像素保活）；44 次未稳定（动画关闭时悬停淡入淡出仍按自身时长逐帧推进，主栏/More 按钮同属原有行为）。回归：golden 35/0、host 38/0、橡皮 0、图形属性 0。接续：V7 翻页控件。

**V7（检查点 `v7`，2026-11-18）**：翻页场景 `BarPageSurface::Impl` 每个实例（每个目标窗口）持有一个登记表：顶层组（整体透明度、缩放、逻辑与 presentation 原点差、字号目标、常驻内容透明度）→ 根栏组（PPT 闲置淡化：根栏底、拖柄、分页按钮 2–5、未保存提示、加载环）/ 菜单组（姿态、浮层透明度、字号目标姿态：菜单底、按钮 6–8）/ 主题组（主题底、分割线、两个标题、预览按钮 9–17 含底色块与底纹）。准备 `PrepareVisuals()` 放在 `Advance` 末尾与同时间戳短路路径、`ConfigurePagination`、`SetBounds` 之后（宿主读取 `PendingDamage` 与判断旧候选之前）；绘制时才写入的派生几何（按钮继承、拖柄矩形）准备时以同一公式先写入。登记表脏区裁到客户区后并入场景 `damage`；`changedSincePrevious`（新增于登记表：与上一次准备或已提交快照相比是否有任何采样变化）为真且有脏区时递增 `damageRevision`，使等待回执的旧候选失效。`CapturePresentationCandidate` 记录本次准备序号，`CommitPresentationCandidate` 只提交被捕获的那次，`CommitPresented` 提交最近一次。删除：逐按钮 `Include`（推进结果、悬停反馈、内容更新 `IncludeButton`、标题/未保存/预览/启用/切换/放映态）、加载环 `Include`、分页模式下「视觉变化即整幅」。保留：布局/缩放/原点/字号策略/常驻透明度/背景模式/设备/生命周期整幅、菜单主题重定向整幅、presentation 变化整幅、光影（主光整幅、鼠标光边带）、隐藏指示器时立即擦除环、回执核对。登记表核心：与最近一个未确认候选完全相同的准备复用其序号、不另占候选槽（等待回执时的重复准备不挤掉被绘制的候选）。测试：Headless 新增 `TestChangedSincePrevious`（含去重与淘汰用例调整），`[VisualDirty] failures=0`；翻页内置检查新增逐按钮悬停/按下/取消/离开逐帧局部 = 整幅对照，`[PageControlUI3] failures=0`（含原有加载环四面×三倍率×两方向局部对照）；隐藏窗口 0；属性测试 0；golden 35/0。全量验证 `v7full` 通过（构建 0 错误；golden 35/0；host 38/0；橡皮/翻页、inputworker、形状、图形属性 0；Headless 退出码 0；多目标每组 2 条，同为 `culled=0` + 计时预算，平均 p95 26.1→28.6 ms、p99 42.9→35.5 ms，噪声大，另行复测；底栏吸附与基线相同；居中、隐藏窗口、原生 0）。

**V8（检查点 `v8`，2026-11-18）**：删除影子登记表（`visualShadow`、`ClassifyBarVisual`、`RegisterBarShadowMapVisuals`、`PrepareBarVisualShadow` 等；测试钩子改由生产登记表填充，`BarVisualShadowFrame` 字段保留兼容）；自审发现并修正翻页页码按钮准备时未传次文字 `pageTotal`（采样会读到旧继承位置）。规范：`rendering-and-ui.md` 新增「UI3 自动脏区登记表合同」（7 节），改写「UI3 基于变化的脏区事务合同」中逐对象标脏/Observe、每帧顺序、推进标脏、继承同步四条；`ui3-layers.md` 模块表补 `Inkeys.UI.UI3.VisualDirty` 与 `:VisualDirtySampler`。复测：属性测试 0/0/0（自然帧 105 次 0 不一致 0 漏呈现）、`--visual-dirty-only` 0、host 38/0、`[PageControlUI3]` 0、golden 35/0。接续：多目标耗时复测、Codex 最终只读审查（简报 `impl/v8-final-review.md`）。

**V8 Codex 终审与修复（检查点 `v8b`、`v8c`，2026-11-18）**：Codex（gpt-6.2-sol High 只读）终审报 3×P0 + 5×P1，主 Agent 逐条读码核实均成立并修复：① 笔型登记少了激光按钮且 Brush2 错配激光按压 → 与绘制列表一致的 5 项；② 粗细保持环进度由交互线程写入、未入采样 → 准备时锁存本帧值，采样与绘制共用；③ 翻页已确认 A、候选 B 待回执时回到 A，旧候选不失效且 B 的回执清掉补画 → 登记表 damage 每次并入，状态变化或 damage 扩大即递增修订号；④ 迟到的淘汰回执擦不掉被淘汰候选的旧位置 → 登记表核心累计被淘汰候选包络，迟到确认时连同已提交/未确认包络转墓碑；⑤ `Render()` 内推进会在裁剪后重新准备、无像素回执推进像素快照 → `Render` 不准备，`CommitPresentationCandidate` 增加 `pixelsSubmitted`；⑥ 精调拨盘采样读取绘制回写值、粗细读数绘制期写入、More 绘制与登记读取不同快照 → 拨盘候选标志与有效值准备时锁存（发布前再确认实时标志）、读数准备时派生、More 绘制用帧快照；⑦ PNG 按钮可见性误用 SVG 内容缩放、主题预览底色块与底纹不贡献包络 → 修正；⑧ 测试护栏 → 翻页反馈用例要求有像素变化、悬停/离开严格局部，新增 A→B→A 与淘汰移走用例，属性测试补激光步骤。复审（第二轮）确认 ①–④ 解决，指出把翻页「字号修正即整幅」交给登记表不安全（系数在绘制时推进、拒绘时不推进）→ 已恢复原兜底（按下/取消期间整幅属原有行为，记为后续：字号解析前移）。全量 `v8full`（检查点 `v8b`）通过，多目标平均 p95 21.2 ms（基线 26.1 ms）；`v8c` 复测：`--visual-dirty-only` 0、属性测试 0/0/0（自然帧 107 次 0 不一致 0 漏呈现）、`[PageControlUI3]` 0、host 38/0、golden 35/0。

**V9 实机检查第一批（检查点 `v9`，2026-11-18）**：用户实机报三项，Codex（gpt-6.2-sol High 只读）独立调查结论与主 Agent 一致。① 翻页控件看不到调试框：设置页运行中切换只通知了主栏（改动前已存在）→ 同时调用 `Ppt::SetDebugEnabled`。② 红/绿框不是每帧都有：主栏红框在 1px 保活、空 damage 整窗兜底、alpha-only 整窗之前就算好，这些帧提交像素却不画框；小于 2×内缩的矩形退化；翻页无 damage 早退在最终绿帧之前 → 新增 `ResolveDebugFrameRect`（裁到窗口、最小可见 8px、实际画出范围并入提交区并记为上一帧框），主栏先定本帧最终刷新范围再标框（最终空闲帧只重涂旧框），翻页调试开启且绿框未提交前不早退。调试关闭时提交区与业务 damage 行为不变。③ 居中吸附松手窗口巨大：离屏未复现——吸附测试新增「远处浮动 → 斜向拖回居中吸附区松手」手势（每次提交的 ULW 不得超过本次按下的手势表面），100%/150% 均不增长；主栏新增调试模式专用诊断日志 `[ViewportOversize]`（窗口任一边越出可见内容 ∪ 吸附提示包络 240 DIP 以上时边沿触发一行，含分支、各包络、历史窗口与平移、锚点、弹簧位移），待用户实机复现后按日志定位首个异常分量再修。

**V9 复审修复（检查点 `v9e`、`v9f`）**：Codex 复审 v9 报 2×P1（翻页在途旧回执吞掉开关刷新；静止首次开启只有蓝框）+ 4×P2，复核 v9e 又报 2×P1（主栏静止时开启不刷新；快速关开停在红框——主栏同样存在）+ 1×P2（直移确认快路不检查覆盖层），均读码属实并修复：翻页每面覆盖层修订号、回执按修订号消费刷新与锁存；两宿主先定兜底范围再算框（主栏重涂条件要求旧框与窗口相交）；开关切换时登记整窗刷新并重新武装锁存，刷新/整窗替换帧框标记整窗；诊断边沿在开关与关闭时复位；远距手势测试逐段校验 Down 提交、松手目标模式，计数恰为 4。不修（已知限制）：≤5px 窄窗框不可见、固定表面越屏时保活框在屏外。验证 `v9f`：构建 0 错误；吸附测试除 `direct=` 计数外与基线一致；`[PageControlUI3]` 0；host 38/0；golden 35/0；属性测试 0/0/0。

**V9 最终复核（检查点 `v9g`）**：Codex 复核 v9f 确认开关、快速关开、直移确认均已解决，无持续出帧或隐藏面空转；新报主栏需求阶段与绘制阶段各读一次调试开关全局（改动前已有），两阶段间切换会画框却不登记擦除 → 绘制阶段改用需求阶段锁存的 `observedDebugModeEnabled`/`observedDebugFrameRateEnabled`。验证 `v9g`：构建 0 错误；吸附测试除 `direct=` 计数外与基线一致；`[PageControlUI3]` 0；host 38/0；golden 35/0；属性测试 0/0/0；Headless（v9f）退出码 0、0 FAIL。待办：用户实机复核三项；居中吸附巨大窗口需用户开启调试后复现，把日志中的 `[ViewportOversize]` 行发回再定位修复。

**V10 实机第二批（检查点 `v10c`→`v10e`，2026-11-18）**：① 绘制属性粗细三档预设悬停无悬停态、无红框——属本任务回归：`draw.thickness` 组合登记只采样调节区底板、分隔线、三角按钮与按压缩放，漏了三档预设底板的 `pct/fill/frame` 与数字透明度、三角 SVG 颜色，悬停只改这些字段时登记表零 damage、不呈现（Codex 独立调查结论一致）。补采三档底板（含按压外扩包络）、数字 pct/几何、三角 SVG；Codex 复审又指出激光外壳色、拨盘激活交接标志与预览锚点、大圆高对比数字的实时预设值未采样，一并补上，拨盘预览锚点改为准备阶段锁存（采样与绘制读同一值）。属性测试新增粗细中/粗/三角悬停（影子逐帧覆盖 + 自然帧推进到稳定后逐字节对照，`natural()` 增加帧数上限参数）。② 绘制/橡皮/形状/More 按钮按下瞬间整窗刷新：离屏按真实 Down 的视觉写入（面板关/开）只刷新按钮自身（约 3%–10% 窗口，原因位 0），未复现；新增调试模式专用诊断 `[FullWindowPresent]`（成功提交的帧从局部转整窗时边沿记一行：原因位、未分类来源位、提交前 NeedsFullDirty、映射/视口变化、收缩判定、内容与预测包络、各阶段范围、已标记键旧新包络、登记表变化项），调试模式下登记表记录变化项名称；`DirtyRegionTracker::VisitChangedKeys` 诊断访问器。待用户实机复现回传日志。验证 `v10e`：构建 0 错误；属性测试 0/0/0（自然帧 137，0 不一致 0 漏呈现）；host 38/0；golden 35 与基线一致；吸附测试与 `v9g` 一致。Codex：调查 1、审查 1、复核 1（gpt-6.2-sol High，只读）。

**V10 实机第三批（检查点 `v10j`→`v10k`，2026-11-18）**：用户回传调试日志。按下主栏与绘制/橡皮/形状/More 面板按钮时，`[FullWindowPresent]` 均为 `reason=19`、`unclassifiedSource=0`，登记表只报被按按钮。根因是基线既有的兜底：帧开始 `FontAnimationsActive()` 为真即未分类整幅；按钮按压以按压目标作字号修正目标，标签修正随按压播放，所以整个按压期间都整窗。离屏未复现，是因为自然帧阶段关闭了动画。修复：修正值本已入登记表采样（易变段）→ 采样时按宿主设置的字号帧序号登记，解析时只有「本帧已登记且本帧推进过」的修正才交给登记表（宿主只唤醒），其余照旧整幅；翻页不变。详见规范 `rendering-and-ui.md` 主栏字号修正条。测试：属性测试新增开动画自然帧 `n.anim.*`（主栏 Draw/Select 按下抬起、几何面板指针 Down/Up/离开），零未分类帧且收敛逐字节对照为硬断言（返回 9）。A/B：恢复旧条件时同组 92/104 帧 `reason=19` 整窗，与实机日志一致；修复后 0 帧，主栏按钮约占窗口 10%，几何面板按钮约 1%–2%。Codex 审查报 P1（覆盖修正未推进时可能停在中途）、P2（记号可消费、会过期、跨渲染器；测试只记日志），已改为按帧登记、要求本帧推进，并改为硬断言。验证 `v10k`：构建 0 错误；属性测试 0/0/0（自然帧 149，动画帧 206，未分类 0，未收敛 0）；host 38/0；golden 35 与基线一致；Headless 首跑 `[HomeWARP]` 限时等待偶发失败一次，单独重跑退出码 0。日志中另有 `reason=4`（面板关闭后窗口收缩，整窗替换属设计），以及居中吸底 `[ViewportOversize]`：Codex 调查认为预测包络把联动的根位置、主栏偏移、主栏宽度当独立区间取极值，属基线既有问题，用户决定现在修（见下）。

**V10 第四批（检查点 `v10l`→`v10m`，2026-11-19）**：① 居中吸底预测包络：Codex 调查确认主栏偏移与宽同批次同曲线推进，而预测把它们当独立区间相加。新增 `ResolveBarBottomDockCenteredPoseRange`（`Bar.BottomDock.cppm`），在共同进度上逐姿态求根、主栏中心、左缘的精确极值；同相、描边非负且本帧实际按居中摆放时，覆盖原独立区间，否则回退。More 父范围补半个宽度变化量。Headless 新增两组单元测试。Codex 审查无 P0/P1；P2（续接标志、摆放门禁、More 锚点、测试位 8）均已处理。② 字号修正：Codex 复核指出同序号重入的残留反例，改为「本次登记之后是否推进」（`changedSinceMark_`）；开动画按压步骤逐项把关（须真实局部呈现、非整窗替换含 alpha 位 8）。验证 `v10m`：构建 0 错误；吸附测试与 `v10e` 一致；居中测试 0 失败；属性测试 0/0/0（动画帧 206，未分类 0）；host 38/0；golden 35 与基线一致；Headless（v10l）退出码 0。居中展开时的实机窗口尺寸没有离屏测试覆盖，待用户实机复核 `[ViewportOversize]`。

**V10 收尾（检查点 `v10n`，2026-11-19）**：Codex 复核 v10m：摆放门禁已解决；按其余 P2 再收紧——描边在推进（可能 Back 过冲穿过 0）或带中点时回退独立区间；开动画按压步骤出现任一整窗原因位（含 16 tracker 整窗）即失败。已知限制（基线既有，未改）：More 同宽切换图标顺序时 More 按钮移动无补偿、菜单与提示框的逐帧派生局部几何只取当前值；后果只是中途扩窗，因为当前内容总并入视口，不会裁剪。golden 宿主的自然帧把 Retry、无效截图与 Idle 合并为结束（残留仍会被收敛后的逐字节对照拦下）。验证 `v10n`：构建 0 错误；属性测试 0/0/0（动画帧 206，`NATURAL-ANIM-FAIL` 0）；吸附测试与 `v10e` 一致；居中测试 0 失败；host 38/0；golden（v10m）35 与基线一致；Headless（v10m）退出码 0。Codex 本批：调查 0、审查 2、复核 3（gpt-6.2-sol High，只读）。

**V9 确认与 V10 提交（2026-11-19）**：用户确认实机检查通过（脏区与居中吸底窗口跳变均已解决），授权提交并归档本任务与父任务。工作区 = `v10n` + HEAD `109c66e2`（空 grip 边界保持为空），提交前在该组合状态复跑：构建、Headless、host、golden、属性测试、居中、吸附。调试模式诊断日志 `[ViewportOversize]`、`[FullWindowPresent]` 保留。后续另开任务：脏区调试中的帧率显示。
