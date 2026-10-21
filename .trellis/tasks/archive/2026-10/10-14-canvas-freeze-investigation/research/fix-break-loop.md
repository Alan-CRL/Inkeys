# Bug Analysis: 四条独立的画布/工具无响应路径

## 1. Root Cause Category

| 缺陷 | 分类 | 已证的具体机制 |
| --- | --- | --- |
| F1 | B 跨层合同、E 隐含假设 | 实时绘制线程最终执行标准流 I/O；存活但停读的输出端可以一直不返回，UI 自有线程仍前进 |
| F2 | B 跨层合同、D 覆盖缺口 | 真实文件 I/O 分类在无 snapshot 的后续检查中被覆盖，controller 无法进入原有有限重试 |
| F3 | B 跨层合同、E 隐含假设 | Move mailbox 覆盖与消费者休眠通知协议不闭合；同 generation 等待无法观察仅更新数据的 Move |
| F4 | B 生命周期合同、D 覆盖缺口 | 逻辑/原生隐藏使 Up 不再到达有效 ImGui 会话，而 owner capture 与旧 backend 按下状态仍存活 |

这不是原始用户现场的统一根因结论。F1 的诊断开关前提、F2 的 PPT 冷槽前提、F3 的速率橡皮静止前提及 F4 当时仍可画新线的事实不能互相抹平。未预先记录用户现场各假设的数值先验，不事后编造概率。

## 2. 为什么原有覆盖不足

- 单独的有界生产队列不能证明完整链不阻塞；必须追到最后一个 WriteFile/WriteConsole/debugger sink。
- “读取失败”不能统一当作内容变更；真实独占句柄比复制错误枚举的 mock 更能覆盖分类和重试交接。
- 隐藏输入 helper 附带 control wake 会使原漏通知消失；测试必须通过真实 producer 和产品共用 idle helper，控制 Move 到达的四个窗口。
- 只有正常 X 关闭和 release-first 顺序不足；按住时隐藏、Hide→Show 合并、旧帧 Commit、旧 native Hide 迟到均有独立生命周期意义。
- 新普通源文件被主产品登记，并不代表直接复用该源的独立测试也已链接；本轮检查发现并补齐两个消费者。

本轮没有声称此前发布过失败修复。检查中发现的局部遗漏已明确修正；默认窗口测试的 owner 前置条件失败仍保留原始证据，未为获得绿色结果改产品或放宽断言。

## 3. Prevention Mechanisms

| 优先级 | 机制 | 本轮动作 | 状态 |
| --- | --- | --- | --- |
| P0 | 运行期所有权 | 单输出 worker、定长 lossy 队列、统一绝对 Stop 期限及隔离保活 | 已实施，生产自检及 stdout 背压完整产品鼠标 GUI 通过，含停读时正常退出 |
| P0 | 错误分类 | IoError 在严格内容检查之前保留，原预算和身份闸门不变 | 已实施，真实 worker/文件回归通过 |
| P0 | 通知握手 | arm、实际 sequence 复查、帧初 generation、作用域 disarm | 已实施，确定性协议与 headless 通过 |
| P0 | 输入会话 | owner 只释放自己的 capture；逻辑/native Hide 失效锁存，render owner 重建 | 已实施，状态、独立真实 capture 及完整产品鼠标强触发/正常退出通过；默认窗口整套仍 11 fail |
| P1 | 可执行规范 | 四项七节合同、错误矩阵、反例和测试证明边界 | 已写入 native-desktop，已独立交叉核对 |
| P1 | 集成验证 | 三消费者链接、完整主 solution、现有独立存储目标 | 构建/非 GUI 针对性回归及 F1/F4 鼠标 GUI 通过；默认窗口整套保留失败 |

## 4. Systematic Expansion

同类审查应分别追踪业务线程最终外部 I/O、跨层 error→retry 映射、数据发布与休眠代次、HWND 捕获与 backend 生命周期。原始“其他窗口相关”的报告仍可来自不同路径；未证实的 DXGI/owner 永久死锁、全局 RTS 重启、清队列或定时恢复不在本次补丁中。

默认 HWND 回归的失败不能直接推定锁屏是根因。已记录真实 owner/topmost/Z序并做原 HEAD A/B，证明预置顶匹配失败也存在于未修实现；独立捕获测试通过；解锁复测整套仍 exit1/11 fail，未逐项证明全部失败根因，不能归咎锁屏。真实笔触摸、Win7 和多屏不从逻辑输入枚举或 ARM64 编译外推。

## 5. Knowledge Capture

- 已新增 `.trellis/spec/native-desktop/canvas-freeze-boundaries.md`，同步 index、input-and-ink、draw3-integration、rendering-and-ui 的相关条款。
- 四子任务检查报告、最终构建/CLI hash/格式与覆盖限制已保存；运行生成物位于忽略的 Build 目录。
- 本仓库没有 `src/templates/markdown/spec` 模板树，没有新建或复制一套无用模板。
- 遵守用户本次禁止 commit/push 的规则，不执行会自动提交的归档或 journal 默认模式。
