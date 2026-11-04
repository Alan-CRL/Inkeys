# 现有 PPT 闲置淡化与常驻透明度（规划研究）

## 任务选择

旧任务 `.trellis/tasks/archive/2026-11/11-02-ppt-linkage-idle-fade/` 已归档，保存原始乘 0.4 方案。本次创建独立后续任务，避免把已经完成的旧验收改写为本次需求。

## 代码事实

| 位置 | 现状与本次影响 |
| --- | --- |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm:645` | `PptState.idleFadeSeconds` 来自 PPT 发布快照。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm:656` | 闲置整体倍率 0.4；淡化 0.5s、恢复 0.15s。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cppm:679` | `ResolvePptIdleFade` 已提供到期、交互阻止、外部翻页优先和下次唤醒策略，可用于复用。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:1307` | 每个 HWND 独立计时；PptCompact 可见时启用，白板不启用；悬停或按下视为交互。 |
| `Inkeys/Inkeys/UI/PageControl/PageControl.cpp:1318` | 当前把闲置倍率放进 `Scene::SetOpacity`，因此同时乘背景与内容，需与显隐通道分开。 |
| `Inkeys/Inkeys/UI/Ppt/Ppt.cpp:148` | `PublishSnapshot` 从 `config.PlugIn.PPTHelper` 发布闲置时长。新增范围应遵守配置→快照→消费者路径。 |
| `Inkeys/Inkeys/Other/Other.Config.cppm:524`、`Other.Config.cpp:661` | 时长存于原生 main.json 的 PPTHelper 组、NoUpload，默认 10s；允许 0/5/10/15/30，其余回退。COM 配置不持有此设置。 |
| `Inkeys/Inkeys/UI/Setting/Setting.cpp:4507` | PPT 联动“控件显示”下已有闲置淡化下拉框，选项关闭/5/10/15/30，修改 QueueConfigWrite 并通知 IdleFade 快照更新。 |
| `Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp:862`、`:988` | 手掌橡皮/笔速橡皮已有 `CheckboxGroup`，可复用其内联布局，而非新造控件。 |
| `Inkeys/Inkeys/UI/Bar/Bar.ThemeMaterial.h:88`、`:100` | 默认背景不透明度深色 0.80、浅色 0.88。原乘法对应闲置背景 0.32/0.352，内容默认 1.0 对应闲置 0.4。 |
| `Inkeys/Inkeys/UI/Bar/Bar.Scene.cpp:686`、`:1103` | 分页已分别消费 resident 背景及内容；绘制后内容恢复 1.0，避免污染弹出面板。 |
| `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:11451`、`:11550` | 主栏现有独立 resident 内容入口；不得在整个 HWND 上叠加 alpha 而连带淡化属性面板。主栏绘制与橡皮/撤回/画笔/形状面板分段，主栏内容消费后恢复为 1.0；保护聚合复用原子开关及本帧实际退场状态；不能仅检查 drawAttribute。 |
| `InkeysHeadlessTests/page_control_tests.cpp:1944` | 已有闲置到期、关闭、不可见、交互与外部翻页策略回归，需调整并扩展真实独立透明度验证。 |

## 已确认产品决定

- 用户已确认固定背景不透明度上限 30%、内容 40%，不提供调节入口；这接近旧默认视觉，背景略淡，内容维持原默认淡化终点。
- 目标按 `min(用户正常不透明度, 闲置上限)` 分别解析，避免用户本来更淡时被反向加深。用户选择的方案为不透明度“上限”；最终 PRD 显式记录 min 公式及内容原值 30% 保持 30% 的验收样例，最终规划仍需用户整体验收。
- 用户已确认主栏悬停也恢复；与翻页控件一致，交互期间阻止淡化。用户已确认主栏仅等待闲置计时到期，外部翻页不使主栏立即淡化，也不重新计时；PageControl 既有外部翻页规则不变。
- 两类翻页控件与主栏共用已确认的 30%/40% 固定预设；没有独立调节入口。

## 变更边界草案

行为差距位于 PPT 页面快照、PageControl/Bar 各自的渲染与交互状态，以及联动页和原生配置；不在托管 COM 协议。预期最小触及配置声明/读盘校验、Ppt 快照、PageControl、Bar 主栏材质消费及其交互唤醒、Setting、i18n、现有 headless 测试与专题 spec。保留显隐动画、命中、点击业务和白板行为；不做无关架构整理。

## 规划阶段验证

仅阅读仓库代码/旧任务、创建规划文档。未执行编译、GUI、产品代码修改、task start 或 commit。旧任务 artifacts 已足够解释现有规则，本轮无需另行搜索跨会话原始对话。

## 主栏输入与属性保护证据

- `Bar.State.cppm:18-25`：画笔、形状、橡皮、撤回属性分别有原子开关；更多浮层为独立 `moreExpanded`，不是上述四类属性之一。用户已确认全部主栏可交互附属面板保护主栏，包含更多；白板独立菜单不属 PPT 主栏范围。
- `Bar.Interaction.cpp:1328`、`:1357`：真实 Mouse/Touch/Pen 输入经共享队列和手势准入后消费，不能在裸 WndProc 收到 Down 就认为是主栏有效点击；旧取消和拒绝接触不应误唤醒。
- `Bar.Interaction.cpp:1583`、`:1601`：Leave/Cancel 由唯一 worker 消费，已有 TickSurfaceInteractions 和 deadline 合并；应复用截止唤醒，不新增定时轮询线程。
- `Bar.Interaction.cpp:3405`：CollapseAuxiliaryPanels 统一关闭画笔/形状/橡皮/撤回/更多和子级颜色/笔宽等浮层。
- `Bar.RenderLoop.cpp:11451`、`:11465`、`:11550`：主栏按钮和主按钮 Logo 有独立常驻内容通道，More 与属性面板分段绘制；整个 Bar HWND 降透明会错误影响属性，不能采用。
- `PageControl.cpp:6026`、`:6057`：同场次页码外部变化递增 externalTurnSerial，现有四个控件各自消费。控件发起翻页的 3s grace 保持现有判定，不在本任务改写来源识别。

## 通道隔离补充

- `Bar.Rendering.cpp:2871`、`:3035`：Shape/Superellipse 依据 `residentSurface` 解析背景用户覆盖值，主按钮原有比例需保留；不得修改全局用户配置或全局主题 source 来影响主栏局部闲置状态。
- `Bar.Rendering.cpp:3118`、`:3302`：SVG/文字已有 resident 内容乘数，闲置目标需先按正常值与 40% 取小值，再组合原控件自身禁用/显隐等语义 alpha，不能再次乘用户内容值。
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md:51`：用户正常背景/内容范围 30~100；收起主按钮等表面仍可能因原有几何/材质比例低于预设上限，必须保持只减不增。

## 最终规划补充

- 用户已确认“所有附属面板都保护，包含更多”：画笔/形状/橡皮/撤回、颜色选择器和笔宽等可交互子面板、更多均保持主栏清晰；最后一个完全收起后重新计时。
- `Bar.PageLoadingIndicator.h` 末尾的 `PageLoadingDeadline` 已被 `PageControl.cpp:435`、`:1316`、`:2729` 用于闲置期限。它 publish 后用 CAS 单次消费到期，适合主栏直接复用，不必新建通用 deadline 框架。
- `PageControl.cppm` interface 仅导入 RenderPipeline/Bar.Metrics，完整 Bar import 位于 implementation；Bar.Interaction 已导入 PageControl。主栏窄 getter 可由 implementation 依赖，避免把双方完整 interface 相互导入。
- `PageControl.cpp:6026` 的 PublishPptState 在锁内 merge，锁外 Acknowledge/RequestAll；新主栏唤醒也应在锁外，且只比较闲置相关载荷，不能以每次页码发布当作活动。
- 已有 `--bar-eraser-offscreen-test` 在产品初始化前进入 `RunEraserAttributeOffscreenTest`，其内部调用 `PageControl::RunOffscreenTests`；可扩展生产离屏验证。`--page-control-hidden-test` 明确创建窗口，不在默认验证范围。
