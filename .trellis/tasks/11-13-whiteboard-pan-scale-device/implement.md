# 白板惯性尺寸与输入设备适配：执行计划 v1

> 最新授权（2026-11-13）：用户要求先提交当前已验证改动，但不结束任务、不push。之后屏幕笔惯性与默认光标问题先只读调查，不修改产品代码。

状态：in_progress，代码和自动验证已完成，待真人设备体验验收；完整规划v1摘要已获用户后续明确批准（“批准并开始实现”，2026-11-13）。允许源码实现与非交互验证，不GUI、不commit/push、不归档。实施单一端到端手势合同，以下为顺序阶段，不另建父子任务。

## 1. 进入实现前

- [x] 确认最新 prd.md、design.md 及用户 R6–R10；不得恢复“精确保留Surface旧距离/时长”的过时约束。
- [x] 确认主会话已展示最终规划摘要，且后续用户明确批准；不是依据单项体验确认直接开始。
- [x] task.py validate 本任务、确认 implement.jsonl/check.jsonl 为真实 spec/research。读取被索引或截断的全文，不只依赖父会话。
- [x] 按 trellis-before-dev 读 native-desktop / native 相关索引；记录 Git 初始脏文件、BOM/CRLF/编码及基线源码哈希，排除他人修改。
- [x] 用原版本先保存无窗口 pan-feel/关键生产 probe 的原始输出与命令；用途是比较中性DIP兼容和语义回归，不要求新物理profile逐字相同。

## 2. 实施顺序

### A. 事实解析与默认DIP兼容

- [x] 提供 display+mode 同锁只读快照；复用已有实际输出/输入来源事实，无 Move 热路径硬件查询。
- [x] 纯 MotionContext resolver：IndirectDip、TouchPhysical/TouchFallback、ScreenPenPhysical/ScreenPenFallback，含轴变换、阈值、版本、失效原因和有效签名。
- [x] 默认 context 完整保留当前DIP数值；物理资格排除 Unknown Pen/外置板、复制/映射不符；橡皮开发覆盖不生效。
- [x] 如提取共享映射helper，先保存并逐字段对比现有橡皮各分支 Config；不得改变橡皮、手掌橡皮和笔宽体验。
- [x] resolver/单位/源识别/回退/旋转纯测试；Physical轴保持独立，不能用平均PPI。

### B. 有序context绑定与单位闭环

- [x] Register为Pending；HandleSelectionDown保留静态路由/native历史，动态类别与metric最终权限集中到Dynamics已有有序Down。移除Pending引起的提前跨类Discard/永久Pen抑制/SuppressLaterPendingContacts，不新造另一套排序FSM。
- [x] caller复用足额decision scratch（覆盖本批所有Down，不只16）；Step顺序回传准入/拒收，Controller据此应用Accepted/Suppressed。OwnerClass/TouchPanHeld/Active只认有效accepted，ignored仍可靠收终态。
- [x] Controller Register关联不可变候选context，内核在稳定有序第一Down应用；同帧旧Up→新Down、publication逆序仍正确。
- [x] Dynamics内部使用本批mm/DIP；每Dispatch增量逆变换至系统DIP再累加，快照逐指还原focus/span；每帧只推进一次动画。
- [x] MotionContext仅用纯数值，不让内核依赖HMONITOR/Win32；新生产调用的帧数组index越界/无效context安全拒收并回收终态，不能默默换域或读悬空引用；只有明确未启用适配的旧纯内核调用省略context时使用默认DIP。
- [x] Window反馈抑制标为provisional并以真实身份/Down QPC/epoch的核心裁决纠正；RTS完整发布保持。真正Touch期间拒收Pen仍锁到终态；旧ack不能清新笔。必要时只传现有身份，不按相近时间猜RTS/Pointer同指。
- [x] 更新所有空间门槛消费者：slop/epsilon、反向与末段资格、释放/boost门槛、时间异常限速、最终模长。旧时间窗口、导航历史/水位/稳定排序保持。
- [x] 保持VelocityDip、frame.contentVelocity、raw/selected/boosted输出系统DIP；px速度给预取/tile，显示DIP速度给blur，不把mm误传给渲染。
- [x] 内部velocity_不从canonical Snapshot回填，避免二次逆变换；span逐指恢复后求值，release tail标量沿其释放方向转换，releaseSource/context独立于帧末来源。
- [x] 真输出/DPI/映射/使用中物理资格变化走Stop旧批栅栏；纯响应变化下次手势生效；无害revision不误停。

### C. 响应参数与诊断

- [x] vector friction改为接受数值tau/c；保留唯一闭式与默认DIP入口，不恢复曲线kind/旧tracker。
- [x] 用可靠boost前手速解析design v1的a/w/G，释放时锁存本条曲线；不乘实际初速，不在coast中降档。boost后最终模长cap接到本profile，不能仅改tracker上限。
- [x] Pen/PenTail沿实际笔关系取短尾，间接设备保持DIP基准；热开关与默认值不变。
- [x] Diagnostics保留已有gate/sink和关闭成本，输出context/回退/两种真实单位/参数/终点，释放标签不被同帧后来来源覆盖。
- [x] 运行参数重放，必要校准数值时保存版本、理由及同一矩阵结果；不可把公式PASS当硬件舒适性证明。

### D. 回归、独立检查与评审

- [x] 完成下表生产与纯测试、ARM64 solution及完整无窗口回归；冻结源/产物/日志哈希和退出码。
- [x] 调用 trellis-check 按完整任务范围审查/修复；先给真实manifest上下文，不让子代理重复派implement/check。
- [x] 更新 canvas-pan.md 及实际受影响规范为最终合同，清除只对当前实现失效的表述；不把本轮候选冒充已实现事实。
- [x] 向用户报告已实现/未验硬件范围与参数初值；本任务没有commit/push/归档授权，收尾时单独遵守当时授权。

## 3. 文件范围与所有权

预计产品文件：

- Draw3.CanvasNavigation.cpp/.cppm：纯数值context、参数化空间门槛/曲线、有序切域与canonical DIP输出。
- Draw3.DrawingController.cpp：事实resolver、唯一准入、context绑定、Stop屏障、全部输出和生产probe。
- Draw3.WindowControl.cpp/.cppm：同锁display+mode只读快照、按身份/代际应用权威Pen反馈；仅在必要时Host补接线。
- Draw3.PenCursor.cppm：开工核实通用DrawingCursorSample没有每笔稳定token，旧source继承不能证明RTS/Pointer身份；允许在既有DrawingCursorSink增加小型默认空元数据回调，供真实Down绑定及权威反馈确认，latest语义不变。所有权归integration实现代理。
- Draw3.SpeedEraser.h/.cpp 或现有私有共享header：仅当小型事实helper确需提取，现有响应不变；不新建通用设备服务。
- InkeysHeadlessTests/draw3_canvas_pan_tests.cpp；必要的display/speed_eraser/palm_eraser测试验证共享边界不变。
- 本任务研究/验证记录及实际受影响的spec。

不预计修改设置/i18n、Config持久化、RTS后端选择、DM Owner、Display EDID读取、solution/vcxproj、依赖版本或第三方。RealtimeStylus仅在权威反馈确实缺身份时允许小型元数据接线，完整Down/Move/Up发布不变。若实际必须新增编译单元，先评估是否可在现有模块内完成；不得为验证便利改工程。实现/检查由Trellis子代理承担，主会话协调、规划和规范；工作代理明确文件所有权，不回滚他人编辑。

## 4. 必须覆盖的验证矩阵

| ID | 验证 | 观察结果 |
| --- | --- | --- |
| V1 | 纯resolver：可信/缺EDID/复制/错误monitor或rect/Unknown/外置板/旋转/两轴密度/无snapshot/橡皮override | 设备模型、unit、fallback reason正确，finite且不伪造mm |
| V2 | 同物理轨迹，100/150/200/300%DPI、不同分辨率、横竖屏、各向异性 | 同profile下物理起步/反向/释放/距离/时长等价；内容按住1:1，所有出口单位正确 |
| V3 | 同帧TouchPhysical Up→Mouse Down/Move、相反publication、同QPC ties、Pen/尾、不同context | 实际消费次序选profile，每段正确逆变换，无跨域residual或错类热停 |
| V4 | 真实多指加减/异步更新、第二指映射不符/未知、随后Pen Down及晚到终态；A Touch Down10/Up30、B不兼容Touch Down20、C Pen Down40并交换publication | 不跳变、不伪造样本；B拒收不挡C；真正Touch期间拒收Pen仍锁到终态，旧ack不清新笔；单帧>16个顺序Down/Up不漏决定 |
| V5 | 快速连续同向、反向尾段、弱末段/轻放、stale Up、hitch、overflow、Stop与逆时钟 | 原错误抵消不回归；真反向清旧速，Down立即停，终态闭合，旧批不复活 |
| V6 | 参数端点/600与1500mm/s及750与2250意图DIP/s、w边界、G=.25..2、boost前后、轴cap；独立轻放/低速接力/主动减速分别测试 | 解析选参连续单调，Pen较短；等v0的单条自由曲线40%/20%界，vector cap正确；不将其用于不同residual的相同手势序列，不删除既有接力 |
| V7 | 输出/DPI/物理失效/模式热切、身份补全、无害revision、别屏/橡皮调试更新 | 真切域Stop，响应延后生效，无害更新不误停，旧residual不混域 |
| V8 | 生产ContactInput→CanvasPanInput、开关/默认值、hover、不兼容Down、日志启停；前帧Touch已Up但窗口bool未清的Pen contact+feedback双结果 | 测试调用真实routine；Pending不误抑制，合法Pen可纠正临时反馈；不依赖末端手造helper证明准入/接线 |
| V9 | 960Hz/10MHz、float像素坐标、短dt、低报点率、屏幕大坐标 | 量化界和压缩拒绝不因单位换算失效，保留真实尾段保护 |
| V10 | 中性DIP与间接输入、共享橡皮helper（若有） | 旧间接曲线/帧输出与基线一致；橡皮Config逐字段及现有测试不变 |
| V11 | 完整solution、pan/control/config/diagnostic/headless、格式与最小diff | exit0且证据可追溯，环境问题与源码问题分开 |
| V12 | 用户Surface Touch、真大屏、屏幕Pen/尾、无EDID两模式 | 舒适性和旧连甩问题由真人验证；未测设备明确标未验证 |

V2纯double公式相对1e-9；生产float链相对1e-4或有计算依据的量化界。门槛边界单列；不可用宽松容差掩盖拒收、符号、生命周期或跨域错误。

## 5. 构建及命令合同（实现后才执行）

- 动态用 vswhere 定位当前VS的 `MSBuild/Current/Bin/arm64/MSBuild.exe`；同一PowerShell invocation先 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`，再 `$env:MSBUILDDISABLENODEREUSE='1'`。
- 构建 `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false`，超时至少5分钟，正常预算30分钟；不用单独vcxproj替代solution。
- Headless `--no-window --canvas-pan-only`、完整 `--no-window`；若共享helper触及手掌橡皮，再加 `--no-window --palm-eraser-only`。已核实当前入口在animation_tests.cpp，没有speed-eraser-only选项，笔速橡皮由完整无窗口套件覆盖，不能传未知选项冒充专项。
- 产品 `--draw3-control-fence-test`、`--draw3-canvas-pan-feel-test`、`--draw3-diagnostic-output-test`、`--config-boundary-test`。未来如增加生产尺度case，优先接到现有control-fence/pan-feel入口，不为它启动应用主窗。
- 所有进程无GUI启动，保存cwd/argv/配置/工具链/源码与产物哈希/退出码/完整日志。完整Headless预算20分钟，不因短时无输出反复重启。
- 上次C1902及默认输出LNK1104是已记录的本机环境边界。若再次出现，定位首个有意义错误；不改SDK/代码/工程绕过。必要时仅用命令行OutDir保存同配置验证产物，并明确默认输出未完成的限制。
- `git diff --ignore-submodules=all --check` 与逐文件编码/BOM/CRLF/无关diff检查；子模块不可读时不将错误误判产品源码问题。较大Debug测试Rig用堆持有，不修改工程栈大小或删用例绕过。

## 6. 回滚与风险

先完成默认DIP兼容与生产语义门禁，再接物理/设备profile。发现单位/准入错误时回滚本任务的最小实现块，不恢复已删除旧曲线、不撤销已修的输入水位或Touchpad隔离。只动本任务来源已知的修改。

主要风险：Step前后准入状态不一致、同帧多域的累计/快照单位混用、界限被误读为连甩总距限制、真实大屏噪声和EDID语义错误、条件编译probe与实际入口不一致。硬件未验证不能当完成舒适性验收；实现完成后也需如实记录。

## 开工证据

2026-11-13：基线见 `Build/Logs/pan-scale-device-2026-11-13/baseline.result.json`。当前默认产品EXE与旧验证EXE哈希不同，旧验证EXE已不在独立目录；本轮保存现有EXE哈希并运行无窗口pan-feel/control-fence，均exit0且完整输出与上次已验证基线字节一致。Headless pan exit0。源码哈希/原BOM与CRLF已保存，后续构建不以这份现有EXE冒充本轮新产物。

## 开工接口核实

2026-11-13：确认权威Pen反馈需要真实每笔身份；source(context/cursor/generation)和被Move覆盖的sample.qpc不能单独防ABA，WM_POINTER的lastKnownPenSource继承也不是桥接证据。必要的小型PenCursor sink接口纳入已批准的元数据接线边界，不新增后端或产品策略。最终方案需以真实token/epoch及可证明桥接保守处理，禁止按时间邻近猜同指。

## 实施与验证完成状态

A–D源码/纯策略/生产回归、独立检查及规范同步已完成。完整ARM64构建、七项R3无窗口验证、最后诊断/注释后的五项专项及默认输出恢复构建/三项专项均通过，见validation.md和research/implementation-validation-results.json。

- [ ] V12真人Surface/大屏/笔/无EDID两模式体验验收及数值校准；未获GUI授权，不由自动化模拟冒充。
- [ ] 用户后续明确授权后的commit/push/归档，本轮不执行。
