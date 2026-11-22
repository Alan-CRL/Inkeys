# C5 交接（2026-11-20）

## 最新状态（2026-11-21，P1–P5 完成，待用户实机验收与提交授权）
- **P5 完成**：spec 已由 Sol 更新（`settings-component-editor.md` 七节重写为现行合同、`rendering-and-ui.md` 共享层边界一段、`index.md` 一句），主 Agent 抽查放下表与 `ResolveDrop` 一致。代码自 fix2 快照未变（最终树 `c5-p5.tree`，仅文档与本文件差异）。
- **最终全量门禁（fix2 代码，干净重建）**：构建 EXIT=0、C5244=0；host 543/0；属性脏区 0 违规；i18n check PASS；轨迹 16/16 trace-diff=0；golden 35/35 逐字节一致；几何 0 失败；橡皮 0 失败；headless 全过。
- **待用户**：实机验收（含 Win7 SP1 + KB2670838：危险幽灵、页面淡出）；验收后授权提交，再归档本子任务并推进父任务 `11-20-settings-bar-component-editor`。

## 上一状态（2026-11-21，P1–P4 已验收，P5 spec 交给 Sol 进行中）
- **P4 验收**：Sol P4 + fix1 + fix2（`impl/c5-p4*.md`）；主 Agent 修正：去 `IdtI18nKeys.g.h` 手工 BOM（生成器无 BOM）、`ImFluent::GetColorU32`（不存在）→ `ColorConvertFloat4ToU32(ImFluent::GetStyle().Colors[...])`、清理 Codex 沙箱遗留 `mspdbsrv`（占用 headless PDB 致 C2471）。fix2 树 `d8afc144…`（`c5-p4-fix2.tree`）。实测：构建 EXIT=0、C5244=0；host 543/0；属性脏区 0 违规；i18n PASS；轨迹 16/16；fix1 版本 golden/橡皮/headless 全过，几何并发下 3 项 `pin-badge-hold` 偶发、单独复跑 0 失败。变异全部被抓：分割线根因、预乘退路、More 边缘例外、悬停阈值、空 More 悬停、生产入口悬停调用。审阅 review3 通过。
- **P4 裁定**：危险幽灵改 `LayerParameters1`（Win7 Platform Update 明确支持空图层）；More 边缘例外仅限用户分割线落 B1；悬停按 More 命中计时、每次进入只消费一次；胶囊松手淡出、危险色 `SystemFillCritical`/`TextOnAccentPrimary`；卡片→`Draw` 端到端离屏用例不做，记入 spec 已知后续，由实机验收覆盖。
- **P5**：spec 简报 `impl/c5-p5-spec.md`（`sol-write c5-p5-spec`）。之后：核对 spec diff → 最终全量门禁（`p1gate.sh` + `tests.sh` + host + `--ui3-attribute-dirty-test` + i18n check）→ 交用户实机验收（含 Win7）→ 提交需用户授权。

## 上一状态（2026-11-21，P1–P3 已验收，P4 交给 Sol 进行中）
- **P2/P3 验收**：Sol 完成初稿 + fix1 + fix2（简报/报告均在 `%TEMP%\inkeys-comp\impl\c5-p2*.md`）；主 Agent 一处编译修正（`Bar.ContentInstance.cpp` `static_cast<double>(s.cameraTop.val)`）。fix2 树快照 `75347365…`（`c5-p2-fix2.tree`）。实测：构建 EXIT=0；host checks=505 failures=0；`p1gate.sh p2-3` 轨迹 16/16 trace-diff=0、golden same=35；fix1 版本 golden/几何/橡皮/headless 全过（fix2 版本 `tests.sh p2-3` 在跑）。变异验证：去外按门禁 → `press-before-confirm` 失败；去 X 抬起命中条件 → `host-close-*` 失败（已还原，树哈希一致）。三轮 `sol-read` 审阅：review1/review2 不通过→已修；review3 通过，3 条 P2 并入 P4 第 0 条。
- **裁定记录**：外按收起范围 = 组件页可见期间整个设置客户区（prd 第 27 行未限定预览区）；原生非客户区（标题栏）不处理。`CheckMatchingBarButton` 夹具 1200×361 → 1600×720 + `AlignMainButtonPixelPhase`（仅整体平移对齐像素相位，审阅确认未掩盖样式差异）。
- **结构**：设置页按压/点击/X 纯输入逻辑 `PreviewPressInput`（`Setting.ComponentEditor.h`，设置页与 host 测试共用）；外按门禁 `BarContentInstance::ShouldDismissMoreOnPress`（几何未确认时不收起）。
- **P4**：简报 `impl/c5-p4.md`，`codex-run.sh sol-write c5-p4`（日志 `impl/c5-p4.log`，报告 `impl/c5-p4.report.md`）。完成后：核对 diff → 构建 → host → headless/i18n check → `p1gate.sh` + `tests.sh` → 变异抽查 → `sol-read` 审阅 → 修复 → P5（spec 更新、全量门禁、交用户实机验收）。提交需用户授权。

## 上一状态（P1 已验收，P2/P3 交给 Sol）
- **分工已变更（用户“策略明确”）**：Claude 只做规划与过程监督（简报、构建、跑门禁、核对 diff/审阅意见、裁定）；实现交 Codex `gpt-6.2-sol` High（`codex-run.sh sol-write`），独立审阅另起 Sol 只读会话（`sol-read`）；Astra 只在大型复杂修改时用；**所有模型 fast 不可用**（脚本已改为 `default`）。
- **P1 验收结论**：正常环境构建 EXIT=0；`p1gate.sh p1-final` trace-diff=0、golden same=35；host 305 项仅 16 项预期 replica 失败；golden/橡皮/headless 全过（SecRandom 管道夹具在正常环境通过，确为 Astra 沙箱问题）；几何仅 2 项已知时序偶发 `pin-badge-hold`（并发 Sol 时耗时 506s，待单独复跑）。P1 树快照 `fb07ae13…`（`%TEMP%\inkeys-comp\c5-p1.tree`），diff `impl/c5-p1.diff`。
- **Sol P1 审查**（`impl/c5-p1-review.report.md`）裁定：pressScale 双写（UI3 先按同时长同曲线提交，与原代码同序）→ 无行为差异，不改；fold 在共享函数入口锁存（原代码逐按钮读原子量）→ 消除帧内不一致而非引入，保留；headless 断言可被 `{}` 绕过 → 已改为校验批次调用首个实参；`Bar.MainContent.cppm` 在 Bar 命名空间内 `using namespace UI3` 违反 `ui3-layers.md:73` → 已移到全局作用域（.cpp 同补）。
- **P2/P3 初稿**（Claude 起草，未提交，快照 `f52005e5…` = `%TEMP%\inkeys-comp\c5-p2-draft.tree`）：预览改用共享层、相机/预留、沙盒、按压/激活接口与宿主接线；host 315 项剩 8 项失败（`batch-edge-sync` 因 `Rebuild` 不复用实例、`preview.match.*`、`more-badge-new-epoch`）。已交 Sol 完成：简报 `impl/c5-p2.md`，日志 `impl/c5-p2.log`，报告 `impl/c5-p2.report.md`。
- **下一步**：读 Sol 报告 → 核对 diff（白名单、BOM+CRLF、未削弱断言）→ 构建 → host 全过 → `p1gate.sh`（真实主栏零变化）+ `tests.sh`（几何单独复跑）→ `sol-read` 独立审阅 P2/P3 → 裁定修复（交 Sol）→ P4 简报。

## 历史状态（P1 期间）
- 任务 `11-20-bar-content-shared-choreography` 已 `task.py start`（in_progress），父任务 `11-20-settings-bar-component-editor`。prd/design/implement 已获用户批准。
- 分工（用户指定）：Claude 主导实现；Astra（gpt-6.2-astra，High，**仅 default，fast 不可用**）用于超大规模任务；Sol（gpt-6.2-sol，High，fast 可用，只读）每阶段审查。Codex 直连失败时走 `HTTPS_PROXY/HTTP_PROXY=http://127.0.0.1:7890` + Bash `dangerouslyDisableSandbox`。
- 工作区未提交（提交需用户授权）。P0 回滚快照：tree `e1c64a4cc4f9e83bb0bc8261c849f7791630a3a5`（`%TEMP%\inkeys-comp\c5-p0.tree`）。

## P0（已完成）
- 新增 `--ui3-trace-dump <dir>`（`Bar.Golden.Test.cpp` `RunUi3TraceDump`，`IdtMain.cpp` 分派，`Bar.Main.cppm` 导出）：16 场景（大/小 × 收起展开、前半程加入、后半程重开、More 正反向、折叠、按压、绘制属性、换边中重排），固定 60fps 帧时钟逐帧记录主栏/More/按钮动画值，两次运行需逐字节一致。基线在 `Build/ui3-trace/base`（HEAD 行为）。
- `Bar.Host.Test.cpp` 新增 `CheckPreviewReplica`（`preview.replica.<large|compact>.*`）。旧实现上按预期失败 16 项：`more-narrow/roomy-region-stable`（宿主裁框随 More 跳动）、`close-glyph-pixels`（X 无像素：预览未 `AdvanceSvgVisual` closeSvg）、`batch-edge-sync`（偏差约 50px）。`divider-insert-a1/b1/a2` 在模型+实例层通过 → 若界面拖不进，问题在设置页命中/卡片拖动路径（P4 查）。
- 旧断言 `preview.more-mid-frame`（要求 More 中途缩放下移）是旧合同，P2 要按新合同替换。

## P1 Astra 已跑完（2026-11-20，exit=0，报告 `%TEMP%\inkeys-comp\impl\c5-p1.report.md`）——待主 Agent 独立验收
- 产物：新分区 `Bar.MainContent.cppm/.cpp`（`:MainContent`，`Bar.cppm` 导出，vcxproj/filters 已登记）；改动 `Bar.RenderLoop.cpp`（-541 行净减）、`Bar.Button.cpp/.cppm`、`Bar.Main.cppm`、`Bar.cppm`。未动 ContentInstance/设置页/测试。共享接口签名与 P2 调用顺序见报告末尾“P2 使用说明”（`ResolveMainBarPresentation`、`Resolve*Style`、`ResolveMainBarBatch`、`ApplyMainBarButtonTargets`、`AnchorHiddenBarButton`(列表版)、`ApplyMainBarBodyTargets`（预览传 `mainButtonWidth=nullopt` 居中）、`ApplyMainBarMoreTargets`、`UpdateMainBarHoverHint`/`PositionMainBarHoverHint`）。
- Astra 自测（直接调用入口，未经主 Agent 复核）：轨迹 `Build/ui3-trace/p1-3` trace-diff=0；golden `p1-3` same=35；几何/橡皮/attribute-dirty 0 失败；host 305 项只有预期 16 项 replica 失败。
- Astra 沙箱环境问题（需主 Agent 在正常环境复跑）：`build.ps1` 末行 EXIT=1（它改 `-m:1` 才成功）；`p1gate.sh` 启动报 Win32 错误 5。
- headless 3 项失败待处理：① `InkeysHeadlessTests/recall_attribute_tests.cpp:445` 源码断言要求 RenderLoop 中出现 `layoutSnapshot != state.mainBarLayoutSnapshot.value()`（比较已移入 `MainContent:142` `ResolveMainBarBatch`）；② 同文件 `:462` 源码断言要求旧签名的 Pierce→Freeze 锚定调用（已移入 `MainContent:440`）。两者需按新位置改写断言并保留原意（同宽快照变化仍触发批次、先结算 idle 再提交目标；Pierce 锚定到 Freeze），在报告中逐条记录旧→新。③ `ui3_input_tests.cpp:183` SecRandom 命名管道夹具在 Astra 沙箱 Win32 错误 5，疑为沙箱环境，正常环境复跑确认。
- **下一步（压缩后从这里继续）**：
  1. `git diff` 审查：白名单、编码 BOM+CRLF（`file` 检查）、有无 TODO/桩/改常量；抽查 `Bar.MainContent.cpp` 与 RenderLoop 调用点的求值顺序。
  2. 正常环境 `powershell -File %TEMP%\inkeys-comp\build.ps1`（EXIT=0）→ `bash %TEMP%/inkeys-comp/p1gate.sh p1-final`（trace-diff=0、same=35）→ `--ui3-host-offscreen-test`（仅 16 项 replica 失败）。
  3. 改写上述 2 条 headless 源码断言后跑 headless（后台，约 8 分钟）。
  4. 生成 `impl/c5-p1.diff`（临时索引对 P0 tree `e1c64a4c` 做 `git diff`，含新增文件），`codex-run.sh sol-read c5-p1-review`（简报已写好，跑前在简报末尾补“实测结果”与“已接受偏差”）。
  5. 核实 Sol 意见、修复、必要时复审；存 P1 树快照；然后进入 P2。

## P1（原始安排）
- 简报 `%TEMP%\inkeys-comp\impl\c5-p1.md`；运行命令 `bash %TEMP%/inkeys-comp/codex-run.sh astra-write c5-p1`（日志 `impl/c5-p1.log`，末行 `EXIT=`；报告 `impl/c5-p1.report.md`）。
- 门禁脚本 `%TEMP%\inkeys-comp\p1gate.sh <tag>`：轨迹逐帧对比 base + golden 对 `comp-base`；P0 构建上验证 trace-diff=0、golden same=35，约 3.5 分钟。
- **Astra 运行期间不要跑任何产品测试、不要改源码。**
- Astra 结束后：① 看 `c5-p1.log` 末行与报告；② `git diff` 审查白名单/越界/编码（BOM+CRLF）/TODO；③ 自己构建 + `p1gate.sh p1-final`（要求 trace-diff=0、same=35）+ `--ui3-host-offscreen-test`（只允许那 16 项 replica 失败）+ 几何/橡皮/attribute-dirty/headless（`%TEMP%\inkeys-comp\tests.sh`）；④ 生成 `impl/c5-p1.diff`（含新增文件，用临时索引 `git diff <P0 tree>`），跑 Sol 审查 `codex-run.sh sol-read c5-p1-review`（简报已写好）；⑤ 逐条读码核实后修复，必要时再复审一轮；⑥ 存 P1 树快照。

## 后续阶段（见 implement.md）
- P2 预览改用共享层（删自写批次/缩放；保留隐藏实例；先定位后推进 More 并推进 X 的 SVG；相机按目标包络、上方常驻一行 More 峰值预留、≥2 行向下扩、下方约一行间隙、裁框稳定；沙盒默认绘制/软笔、设置可见）。
- P3 设置侧输入适配（单一 DragSession；按下即按压视觉；越阈值先清按压再 Lift；抬起同实例同 revision 才派发；工具模式切换、More/X/面板项/外部按下收起；提示只小尺寸）。
- P4 组件页：垃圾桶胶囊（主栏下方居中，新文案 `TrashLabel`）、放下语义表（拖出预览区=删除可删项/固定项回位/库项放弃）、分割线界面拖入排查、幽灵按钮主栏材质底板、预览纹理 tint 参与页面动画。
- P5 spec 更新 + 全量门禁 + 用户实机验收。
