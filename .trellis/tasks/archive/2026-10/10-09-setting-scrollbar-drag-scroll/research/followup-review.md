# 后续独立审查：PASS（自动验证范围）

- 角色：trellis-check，直接复审，不递归派发代理。
- 基线：上一轮提交 b8beea0b；当前四项后续需求由用户明确授权实现和再次提交。
- 已读取完整 hook 保存文件、check.jsonl 六个引用及 prd/design/implement；本轮授权覆盖旧 helper 正文不变、下一帧滚动及额外 RowGap 的历史条款。
- 产品与测试由实施代理统一写入；检查代理只读核对并将发现交实施代理修复，避免同时编辑同一文件。本文件由检查代理负责。
- 最终源码冻结后独立读取 build-final / headless-final 的退出结果和实际日志；没有重复启动完整构建、GUI、Computer Use，也没有执行 commit/push。

## Findings (fixed)

1. File: Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp、Setting.Pages.cpp、Setting.Shell.cpp。
   Issue: 内容候选 ID 初稿在 PushID 内存储，帧首读取时 ID 栈已恢复，真实主页卡片不能转交拖动；旧 helper 帧末写 target，持续位移要等下一绘制帧。
   Fix: 候选 storage key 使用稳定 window seed；仅内容点击项登记候选，使用原生拖动阈值转移 capture、取消本次点击。NewFrame 后采样唯一宿主意图，真实宿主首次 Begin 前消费一次 pending target，原生 Begin 同帧更新 Scroll、cursor、clip、items 和 DrawData；帧末只捕获/释放。真实 RenderHome 的 hero/feature/common/author、跨 child 和实际字形验证已通过，原 slider/input/native 轨道所有权保留。

2. File: Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp、Setting.Controls.h、Setting.Design.h。
   Issue: 初稿全 DrawList 顶点扫描既增加热路径开销，又可能漏掉 native 合并到 parent 的装饰；新 ImGui 类型声明放纯 Design.h 导致首轮构建失败。
   Fix: 退出装饰按本版 native 几何作常数计算并缓存 thumb 三态，不扫描全窗口/全顶点；child 名使用 stack buffer。带 ImGui 类型的 wrapper 声明放已有依赖的 Controls.h；无公共设置 API、配置格式、vendor、Win32 input 或工程依赖变更。

3. File: Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp。
   Issue: 滚动样式与 vendor Card/Expander 的跨 Begin/End Push/Pop 栈交错，可能提前弹出 padding/background；Details 内容可能继承父条的渐隐倍率。
   Fix: 只备份/恢复 ScrollbarSize、ScrollbarPadding、三个颜色字段，不夹用 vendor 栈。Details 独立作用域在内容回调前恢复。测试在真实 Card/Details 内容内及 nested child 返回后检查实际 padding、background、栈深与全部五字段，不仅检查总栈长度；最终全量通过。

4. File: Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp、Setting.Shell.cpp、Setting.cpp。
   Issue: 中段重复扣除额外 4 DIP 形成过大间隙；初稿 Combo 在 items 提交之后修正退出宽度，真实 item 可用宽与裁剪不一致。
   Fix: 中段保留已有 native 4 DIP 右 padding；所有实际竖直宿主通过本地 Begin/Combo/Card/Details 在提交 items 前得到共同宽度/裁剪。8 DIP 轨道与三态 alpha 用原动画开关、160ms 和速率连续过渡；退出装饰无输入，终值零留白。Combo 使用当前 vendor 等价的必要本地提交代码与私有辅助，不修改 vendor；实际 Selectable 登记候选，轻点/拖动语义保留。

5. File: Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp。
   Issue: 初稿可能覆盖外部 scroll target、重复采样同帧位移，或把未提交/关闭 Popup 的旧状态作为有效滚动；位置重基准使用 ImTrunc 与库 ImRound64 不一致。
   Fix: frame guard 与 pending 消费标记分工明确，仅实际提交宿主投递；采样入口及 Begin 前均让外部目标优先并记录 rebase；匹配 native ImRound64/clamp，保留浮点累计。精确 PopupId、导航释放门禁、无范围浮层屏障、父子单消费、上下界/反向、迟到 target 与真实 0.25px 输入队列回归均通过。

6. File: InkeysHeadlessTests/setting_design_tests.cpp。
   Issue: 旧每帧满宽/满 alpha 断言和固定两帧 Home 采样不能覆盖新增动画；1.875 倍两条短 Combo 的严格零溢出预期与 native 不符；测试空 aggregate 临时触发 MSVC C2512。
   Fix: 连续帧观察实际 native target、thumb DrawData 与真实 item 宽度，保留中间态/终值严格断言；测试初始化改命名对象并修复 shadow。诊断证明同倍率未修改 vendor 与本地实现同为 bar=true、reserve=15px、ScrollMax=0.5px，因此保留两条列表的严格 native 等价，并另以真实空列表测淡出终值。最终三倍率空列表全部 bar=false/reserve=0/max=0，无背景输入传播；没有改变 native 溢出规则或放宽误差掩盖失败。

## Findings (not fixed)

最终冻结源码没有剩余已确认的任务范围内代码问题。以下是验收范围和证据限制，未通过修改产品或测试绕过：

- 完整真实 Home 提交的 CPU median/p95 跨倍率、跨运行没有一致下降。最终 held 新/旧 median/p95（µs）：1 倍 497.6/710.5 vs 487.5/943；1.5 倍 479.4/903.2 vs 616.7/944.7；1.875 倍 635.4/1364.6 vs 661/4498.9。每组独立 100 个真实预热后 frame；计时包含当前相同 UI/动画包装的提交，不是纯 helper 微基准或旧完整版本应用比较。可靠改善是实际输入至 Scroll/字形绘制少一个绘制帧，不能宣称一致 CPU 提速或端到端百分比。
- 真实 DX11/DWM、鼠标手感、实机触屏采样及最终观感未测。用户规定本轮无 GUI/Computer Use；headless MouseSource/DrawData 不能替代这些人工验收。
- 当前 ImGui 在计算 MouseDelta 前 floor MousePos；小数位置队列自然得到 0/1px delta。测试没有伪造 MouseDelta，不能宣称提高 backend 的采样精度。
- 1.875 倍两条短 Combo 的 0.5px 范围是同版 native 行为，严格对照通过；改变该规则超出本轮最小修改范围，因此保持。

## Verification

- Lint / 静态：PASS。独立检查全部七个产品/测试文件、实际 Begin 调用路径及正式 rendering-and-ui.md / spec-boundaries 合同；剩余直接 BeginChild 均为固定 NoScrollbar/NoScrollWithMouse 或 scrim 容器。git diff --check 通过；本轮未新增独立 C++ lint 工具。
- TypeCheck / 编译：PASS。主会话动态定位 ARM64 原生 MSBuild，在同一次 PowerShell invocation 使用 PATH workaround，完整 InkeysRepo.sln Debug|ARM64、超时 1800 秒。独立读取 build-final-result.json：ExitCode 0，65.7257406 秒；实际日志 0 errors、9 条既有 vendor warnings，没有本轮新增 warning。
- Tests：PASS。独立读取 headless-final-result.json：完整 InkeysHeadlessTests.exe --no-window ExitCode 0，125.8065025 秒；stderr 字节数 0，实际 stdout 为 PASS animation correctness，无 FAIL。三倍率 Hierarchy 684 帧、DragScroll 387 帧、真实 Home 739 帧；包含浅深主题、三导航模式、实际宽度/alpha、点击与拖动、native 轨道、Popup/释放屏障、同帧字形、外部目标、Card/Details 内部字段及现有全量回归。
- 国际化：独立读取 i18n-check.log，en-US 与 zh-TW 各 370/370、PASS。
- 格式：独立对七个 source/tests 与 HEAD checkout（git cat-file --filters）核对 UTF-8、BOM、CRLF、无 bare LF、末尾换行均保持；check-format-audit.json 全部 true。最终任务/研究记录由主会话统一再审计。
- 基线真实性：独立提取实际 b8beea0b helper 与 deployment，和测试 Legacy 函数去空白对照，两项完全一致（check-legacy-baseline-audit.json）。不是公式模拟或新算法替身。

## 真实验证过程记录

- build-first：Exit1，22.87247 秒，首个源码错误是纯 Design.h 引入 ImGui 类型声明缺依赖，已修。
- build-second：Exit0，87.8447836 秒；headless-first：Exit1，114.8640786 秒，60 个失败是旧 nav thumb 及 Home 显隐取样断言，后续用实际连续帧中间态/终值修正。
- build-third：Exit1，43.5759277 秒，产品通过，新增测试 C2512 与 shadow 已修；build-test-fix 初次 auto-review timeout 未执行，重试构建 Exit0，33.3575245 秒。
- headless-second：Exit1，119.3953078 秒，仅 Combo 1.875 倍零 range 预期失败；diagnostic build Exit0，28.6563174 秒，diagnostic headless Exit1，117.6860417 秒，仅该已定位预期失败。真实 local/vendor 同值及 Card/Details 内容字段证据保留，不把这些过程运行标记为通过。
- 最终 build-final / headless-final 运行完整冻结源码，均 Exit0；以上过程失败全部有准确原因和最终覆盖。
