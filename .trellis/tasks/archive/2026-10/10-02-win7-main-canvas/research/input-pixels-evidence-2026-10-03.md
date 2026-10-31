# Research: Win7 20261003-172838-167 输入与实际像素证据

- Query: 对全量现场日志统计 RTS、controller contact、光标 visual 和实际 ULW 源像素，判别消息未收到与收到后渲染未出像素。
- Scope: internal；仅输入/controller 时间线，不检查 GPU shader 根因。
- Date: 2026-10-03

## Findings

### Files found

现场目录 `G:/Inkeys/Inkeys-Win7-Input-Pixels-x64-Release/Inkeys-Win7-Input-Pixels-x64-Release/20261003-172838-167/`：

- `console.stdout.txt`：6,288,186 bytes，33,380 行，含初始化、RTS、mouse、cursor visual、ULW 像素和历史操作。
- `console.stderr.txt`：38,599 bytes，131 行，含窗口状态和 controller 选中 runtime 的 `[EraserInput]`（标签虽名 Eraser，非橡皮 contact 也会打印）。
- `idt1791019718760.log`：44,427 bytes，常规产品日志；本主题不以其初始化成功替代实际输入证据。
- `identity.txt`：281 bytes，采集身份。二进制对应关系由主调查核验。

以下 `stdout:L...` / `stderr:L...` 均为上述原始文件的一基行号。

### 输入已进入 producer 且被 controller 消费

| 事件 | 计数 | 结论边界 |
| --- | ---: | --- |
| RTS Down arrival | 13 | 实际进入产品 StylusDown |
| RTS Down published | 13 | 每次均 `deviceKnown=1 device=2 decoded=1 published=1` |
| RTS Up arrival | 13 | 实际进入产品 StylusUp |
| RTS Up published | 13 | 每次均成功解码、发布 |
| RTS Packets published | 8 | 日志限频，不能据此认定只收到 8 个包 |
| RTS 其他 reason | 0 | 本文件未见 no-decoder、decode-failed、binding-failed、publish-failed 等 |
| `mouse event` | 5,483 | 全部 `accepted=1 reason=accepted-mouse` |
| `dropped` 记录 | 0 | 未报告诊断队列溢出；序号贯穿到 33303 |

例如 stdout:L688-L691：Down 到达 → 已知 MouseLeft/解码/发布 → ULW 脏区 → Packets 发布。stdout:L4653-L4654 对应第一次 Up 到达/发布；stderr:L15 与 L26 分别从 controller runtime 确认 generation 1、2 已成为活动 inputContact。

controller 共有 generation 1..13，全部 `device=MouseLeft inputType=2 source=Mouse inputPositionValid=1`。按工具分组是 **2 次 Pen、2 次 SolidLine、2 次 Eraser、7 次 HardPen**；不得把 HardPen 误报为荧光或把 SolidLine 误报为激光。

| generation | tool selected/effective | stderr 首次活动 contact 行 |
| --- | --- | ---: |
| 1 | Pen 0/0 | 15 |
| 2 | Pen 0/0 | 26 |
| 3 | SolidLine 5/5 | 36 |
| 4 | SolidLine 5/5 | 45 |
| 5 | Eraser 3/3 | 60 |
| 6 | Eraser 3/3 | 70 |
| 7 | HardPen 1/1 | 87 |
| 8 | HardPen 1/1 | 91 |
| 9 | HardPen 1/1 | 95 |
| 10 | HardPen 1/1 | 101 |
| 11 | HardPen 1/1 | 105 |
| 12 | HardPen 1/1 | 109 |
| 13 | HardPen 1/1 | 114 |

上述 runtime 证据比 `published=1` 更强：`DrawingController.cpp:10064` 从 `r` 判定 `!r->ended && !r->awaitingReconnect`，随后读取 runtime handle generation、模型位置、selectedTool/effectiveTool。`Host.cpp:991` 才输出 `[EraserInput]`。因此本次已排除“producer 发布但所有 contact 被 controller 丢弃”的总故障假设。

### 像素统计与操作对应

15 次像素统计全部 `api=success error=0 sourceAlphaNonzero=0 sourceRgbNonzero=0 sourceMaxBgra=(0,0,0,0)`。两次属于 Presentation 初始化/最后选择提交，13 次属于 Primary。Primary 最终 DIB 均是 alpha=1 命中底层，RGB 全零；不能把阻止穿透当作墨迹已渲染。

| stdout 行 | tick | 场景 | 关联证据 |
| ---: | ---: | --- | --- |
| 30 | 56310932 | Presentation 全帧初始化 | 空白合理；不单独用作故障证据 |
| 44 | 56317421 | Primary 全帧初始化 | 空白合理，final alpha1 |
| 690 | 56318576 | 首次 Pen Down | L689 成功发布；L704 tool0 mouse1/1；L706 present成功，dirty同为554,904..564,914 |
| 4679 | 56319590 | Pen 首次抬起/提交 | L4654 Up发布；L4693成功提交相同 dirty554,779..1082,914，覆盖笔画而仍无像素 |
| 8790 | 56320604 | 第二次 Pen 运动中 | L8753 Packets发布，处于 Down L6187 与 Up L9227 之间 |
| 11318 | 56322070 | SolidLine Down | L11310发布；L11319 tool5；L11321 present成功同dirty |
| 16650 | 56323614 | Eraser hover，白色32px自绘光标 | L16651 frame tool3 primary1；L16652 visual；L16731成功提交相同dirty |
| 19956 | 56324628 | 第二次 Eraser contact | Down L19395，Up L20210；controller stderr:L70 contactGen6 eraserContact1；附近L19899白色visual |
| 22639 | 56327764 | 第一次 HardPen Down | L22631发布；L22640 tool1；L22642 present成功同dirty |
| 27286 | 56329870 | 第四次 HardPen Down | L27278发布；L27287 tool1；L27301 present成功同dirty |
| 31882 | 56330884 | 第七次 HardPen 运动中 | L31857 Packets发布，位于Down L31202与Up L32004之间 |
| 33358 | 56332429 | clear/历史恢复全帧 | L33355 AutoSave strokes13、17783bytes；L33359起undo rebuild |
| 33364 | 56333505 | clear/历史恢复全帧 | L33361 AutoSave strokes11；其后undo rebuild |
| 33371 | 56334613 | 历史 composition rebuild dirty | dirty768,768..1280,1024；不保证此区域本应有可见像素 |
| 33375 | 56335034 | Presentation 选择态全帧 | 最后L33379 selection1，L33380 present成功 |

最强光标判别并非“没有笔画也许输入失败”，而是 **有确定白色自绘光标且包含它的源脏区仍全零**：

- stdout:L16652：`visual index=0 source=primary shape=2 x=966 y=956 width=32 height=32 opacity=.5 fill=1 outline=1.3 rgb=1/1/1`。
- stdout:L16650：对应 ULW dirty `(948,938,984,974)` 完整包含该圆环；源 BGRA 全零，最终 DIB 仅 alpha=1。
- stdout:L16731：相同 dirty，`present success=1 visuals=1 laserTips=0`。
- stdout 的队列事件按生产时间输出，像素日志直接打印，允许像素行在 visual 队列行之前出现；不要据纯文本顺序认定像素检查发生于尚未生成 visual 的旧帧。这里以 tick（相差15ms）、同dirty、同present配对。

本次全部16条 `visual index=` 都是 primary shape2（橡皮）；全部 `laserTips=0`。frame工具统计：tool0 37、tool1 36、tool3 18、tool5 19；**没有 tool4 或 laser-tip**。本附件可证明普通笔/硬笔/实线/橡皮出像素异常，不能单独验证用户之前提及的激光复现。

### 文档与历史证据的边界

stdout:L33355 `AutoSave action=capture trigger=clear strokes=13 bytes=17783`，L33361 strokes11、L33372 strokes5；L33359-L33370 有 item12..5 的 composition_rebuild undo。结合13个controller contact，这证明本次已产生文档/历史/序列化工作。它不证明GPU preimage具有非透明墨迹，仍以像素读回为准。

43条 `present success=0 visuals=0 laserTips=0 dirty=(0,0,0,0)` 在未提交脏区的空闲帧出现；不能从该计数直接判断43次ULW API失败。实际15条ulw-pixels返回均success。其余日志中 present成功：53条 visuals0，16条visuals1；所有laserTips0。

## Code patterns

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cppm:29` 工具枚举：Pen0、HardPen1、Highlighter2、Eraser3、Laser4、SolidLine5。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.RealtimeStylus.cpp:1141` Down arrival；`:1236` 发布结果；`:1244` Up arrival；`:1304` Up结果；`:1399` 限频Packets结果；`:1535` 统一日志格式。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:9948` frame诊断；`:9962` visual属性；`:10064` 从活动runtime构造inputContact；`:10076` selected/effective工具。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:991` 打印所选runtime的输入快照，而不是只打印producer状态。

## Related specs

- `.trellis/spec/native-desktop/input-and-ink.md`：WM_MOUSE仅光标反馈、RTS producer/controller区分、有界诊断、ULW source/final区分。
- `.trellis/tasks/10-02-win7-main-canvas/prd.md`、`design.md`、`implement.md`：需用现场实际像素排除API成功但无内容；避免依据OS版本更换后端。

## External references

本主题为原始日志与当前源码相关性分析，未新增外部API结论。Win7 Pointer/RTS支持文档见前轮 `win7-input-presentation-2026-10-03.md`。

## Caveats / Not Found

- 最终收窄：本次原始输入、RTS解码、producer发布、controller消费、文档历史均有正证据，渲染到ULW源读回之间出现全透明；缺少Win8 Pointer API不能解释本次这些已准入contact与白色visual仍无像素。
- 全零是15个采样提交区域的结果，不是对每帧/整个GPU纹理所有时刻的断言。橡皮hover采样明确覆盖应有白色光标，故不是只采了合法空白区。
- 未定位具体GPU shader/resource/viewport状态；该主题刻意不研究shader，交由主调查的独立渲染研究。
- 没有Laser工具操作正证据；不凭之前症状补造本次Laser日志。
- 未运行GUI、测试、构建；未修改生产代码、spec或Git状态。
