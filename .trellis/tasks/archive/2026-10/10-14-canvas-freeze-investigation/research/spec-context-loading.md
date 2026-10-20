# 大型规范的上下文加载合同

当前 Trellis 原生上下文注入单文件上限为 32768 bytes；以下规范超过该上限。为避免把前缀注入冒充已读完整合同，manifest 只注册本说明和其他可完整注入的 spec/research，不更改项目注入配置，不复制或截断原规范。

实施和检查子代理必须在行动前自行分块读取本任务适用的原文件，直到文件末尾；原生已注入的内容可用于定位，但不能替代未注入部分。每块输出应足以保留完整段落，单次读取截断时继续读取后续行。完成后在内部记录已加载文件及相关合同；未读完不得开始修改或给出合规通过结论。

| 子任务 | 必须补全加载的规范 |
| --- | --- |
| F1 | `.trellis/spec/native-desktop/draw3-integration.md`、`.trellis/spec/native-desktop/input-and-ink.md` |
| F2 | `.trellis/spec/native-desktop/draw3-integration.md`、`.trellis/spec/native-desktop/input-and-ink.md` |
| F3 | `.trellis/spec/native-desktop/input-and-ink.md`、`.trellis/spec/native-desktop/draw3-integration.md` |
| F4 | `.trellis/spec/native-desktop/rendering-and-ui.md`、`.trellis/spec/native-desktop/draw3-integration.md` |
| 父任务集成检查 | 上述三个规范 |

优先使用 Codex 原生 manifest 上下文注入；本说明只是工具已知大小限制下的 child-side 完整加载回退。各子任务的其余 manifest 条目、PRD、design 和 implement 仍须按原 Trellis 顺序读取。生产源码在实施时依准确变更点读取，不注册到 manifest；不得仅凭旧调查报告代替当前源码或规范。
