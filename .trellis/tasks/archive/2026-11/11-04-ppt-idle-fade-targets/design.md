# 技术设计：PPT 闲置淡化上限与主栏

## 状态与边界

本设计对应 `prd.md` R1–R9，最终方案已由用户通过后续消息明确确认，并授权进入实现。

最小行为差距是：现有分页把闲置倍率塞入整体显隐 alpha，无法分别限制背景与内容；主栏缺少独立闲置计时；设置缺少范围偏好。行为实际位于 PageControl 的配置快照/输入计时与 Bar 的材质绘制/交互，不在 COM 或全局 HWND 透明度。

继续使用现有模块、共享串行渲染线程、共享交互 worker、按钮与命中运行时。不抽出新框架、不复制一套按钮或超时策略；直接复用 `ResolvePptIdleFade`、原动画属性和 deadline 唤醒。

## 1. 配置与设置

新增字段位于原有 `config.PlugIn.PPTHelper` 组，与 `IdleFadeSeconds` 同属 main.json / NoUpload：

| 字段 | 类型 | 默认 |
| --- | --- | --- |
| `IdleFadeBottomPair` | bool | true |
| `IdleFadeMiddlePair` | bool | true |
| `IdleFadeMainBar` | bool | false |

- 不重命名原时长字段，不新增透明度数值配置，不写 PPT COM 设置文件。
- 缺字段和类型不符用上述默认；真实布尔值参与现有 ReadAll/ReadMini/Write 快照路径。检查现有 bool 解码，足够严格时直接复用，避免多余特殊分支。
- Setting 的闲置时长下拉与三项复选共用同一个 SettingRow，通过已有 InlineRowContent 加入下方分隔线及复选内容，不显示“淡化范围”标题或第二张卡片。选项为“两侧底部 / 两侧中部 / 主栏（仅底栏模式时）”，列数按可用宽度自适应。
- 时长为 0 时复用现有 ConditionalBlock 的高度/透明度动画收起内联区，开启时展开，保留勾选；退出和未完全展开时禁交互。全部取消合法，不自动重新勾选。
- 每次真实变更一次 `QueueConfigWrite()`，并 `NotifyConfigurationChanged(ConfigGroup::IdleFade)`；渲染布局重排或 disabled 绘制不得写盘。
- 移除未再使用的“淡化范围”文案键，主栏选项追加仅底栏模式说明；底部/中部名称能直接复用则复用现有键。说明改为反映三类可选区域与主栏外部翻页差异，不遗留“总是四个控件”或“乘 0.4”。全部 20 个语言文件和生成头按现有 i18n 脚本同步。

## 2. 快照与模块依赖

`Ppt::PublishSnapshot` 把三个布尔值与原时长一起放进 `PageControl::PptState`，`ArePptStatesEquivalent` 包含新字段。未改变的实际页码/COM ACK/拖动身份仍保持现有规则。

主栏需要一个窄的只读闲置快照：场次、PPT active/visible、时长、主栏是否启用。由 PageControl 从已合并且已接受的发布值按值拷贝，不直接逐帧读取 main.json。实现优先在现有 PageControl interface 增加该专用 getter，Bar 的 implementation unit 导入；不要让 `Bar:Main` interface 反向导入整个 PageControl，从而引入模块环。

- `PublishPptState` 在原锁内完成 merge、替换和比较，离开锁后才请求主栏帧/唤醒 worker。
- 只在闲置配置、场次或可见性等相关载荷改变时请求主栏，不因每次页码发布重置主栏活动时间。
- 主栏再结合同一帧的 workspace committed/pending 快照，只在已稳定提交的 Presentation 场景且本帧 bottomDockMode=BottomDocked 时生效，Free/Centered 两种居中模式都允许，Floating 禁用并清闲置进度/期限；回到底栏重新完整等待，正常输入状态不因仅停靠形态改变而清除；白板、桌面和切换中不建立闲置期限。
- 旧场次或被拒绝的发布不能开启新主栏策略，也不能把旧计时带到新场次。

现有依赖已允许 PageControl implementation 调用 Bar，Bar.Interaction implementation 导入 PageControl；设计不需要新增 module 工程项。

## 3. 不透明度解析与渲染隔离

令正常常驻材质背景为 B、内容倍率为 C，闲置进度 p∈[0,1]。共享纯策略放在现有 `Bar.ThemeMaterial.h`，常量与解析只有一份权威：

```text
background = B + (min(B, 0.30) - B) * p
content    = C + (min(C, 0.60) - C) * p
```

- p=0 正常，p=1 闲置；B/C 来自已解析的正常主题、用户偏好与当前材质，不是被闲置修改过的上一帧结果。正常值等于/低于上限时不加深。
- 闲置目标包含原用户正常不透明度且只解析一次，禁止再次把结果乘正常用户值；例如内容 0.8 的终点为 0.6，不能为 0.48。
- 控件自身显隐、禁用、文字转换等语义 alpha 继续独立组合，闲置不把禁用图标强制提升到 60%。收起主按钮按其正常材质解析再取上限，恢复仍回到原比例。
- 背景指现有常驻背景填充，内容指原独立内容通道；不重新设计边框、选中底板或光效角色。
- 只在主栏本体、主按钮以及分页根底板/常驻按钮的绘制范围应用；退出这些 draw 段立即恢复默认。更多、所有属性/菜单及子面板不消费闲置进度，底部吸附提示等临时 UI 不加入范围。
- 不修改 `currentBarThemeStyle`、用户 config、对象的正常材质源或 HWND 整体 alpha。可给现有 renderer 增加局部闲置参数/作用域；必须在真实绘制计算最终填充及图标/文字 alpha 的地方消费，不能只在末端新增一个 surface 层统一相乘。

分页 `Scene::SetOpacity` 恢复为仅显隐使用；另持有闲置动画进度。`Bar.Scene.cpp` 当前 Advance 每帧可能重设正常 resident 填充，需把正常源与闲置投影分开，避免它覆盖闲置目标或形成上一帧乘下一帧的递减。

主栏在 RenderLoop 内维护独立闲置动画；从当前 p 重定向，淡出 0.5s、恢复 0.15s，沿用全局速率和关闭动画同帧到位。属性保护出现时立即请求恢复，不能等待下一次闲置期限。

## 4. 计时与交互所有权

沿用 `steady_clock` 作为闲置期限，动画仍用已有 FrameAnimationClock：静置数小时后首帧的 dt=0 不能让 0.5s 动画瞬间完成。

| 状态 | 主栏行为 | 翻页控件行为 |
| --- | --- | --- |
| 未选中、时长 0、非有效 PPT 可见态 | 目标正常，无到期唤醒 | 同左 |
| 新场次/重新进入/重新勾选 | 清旧状态，正常并开始完整计时 | 各自相同，消费旧外部序号 |
| 悬停/有效按下/拖动 | 恢复正常，交互结束前不计时 | 保持现有独立输入行为 |
| 主栏任一附属面板展开或仍在关闭退场 | 主栏正常且无到期唤醒 | 不改变其他 HWND 的独立计时 |
| 最后一个主栏保护消失 | 从此刻开始完整等待 | 无关 |
| 外部页码变化 | 不触发淡化、不修改活动时间 | 继续原有规则与 3s 来源宽限 |
| 期限到达 | 目标闲置；结算期限后不持续自唤醒 | 各自相同 |

### 主栏输入

- 由共享 worker 在来源和手势准入成功之后更新活动/悬停/按下信号。悬停命中使用实际已呈现的主栏背景/主按钮几何，不把整个扩大的透明 HWND 或显示器当作主栏。
- 第一笔按下既恢复又继续原命令，不新增“只用于唤醒”的消费层；Mouse/Touch/Pen 不通过另一套裸输入路径重复处理。
- Leave、Up、Cancel、CaptureChanged、邮箱溢出及 shutdown 清理活动状态。取消不得执行点击；旧代次取消不能撤销新接触。
- 输入线程只发布轻量活动信号并请求帧，不直接推进 D2D/动画。主栏 render owner 维护场次/启用/面板保护边沿的重新计时基准，并结合输入活动时间决策，避免两个线程同时写同一个生命周期时间戳。

### 面板保护

从已有 `BarStateClass` 的 drawAttribute / geometryAttribute / eraserAttribute / recallAttribute / moreExpanded 及颜色选择器、笔类型、笔宽等子面板的实际活动状态形成聚合保护。只读现有原子状态和本帧可见/退出动画状态，不复制面板自身开合状态机。

保护维持到最后一个相关面板的收起退场结束，或仍在悬停/按下的交互结束，以较晚者作为重新计时起点。只看鼠标是否在属性范围不足以实现“展开多久都不淡化”；仅看主栏 drawAttribute 也不能漏掉橡皮、撤回和更多。普通无交互 tooltip 不独立延长保护。

### deadline 唤醒

主栏渲染决策发布下一截止，复用 `Bar.PageLoadingIndicator.h` 的 `PageLoadingDeadline` 发布/单次消费设施及 `PumpSharedInteraction` 合并等待；到期只请求 Bar 一帧。交互和生命周期变化无条件请求重新决策，避免渲染线程基于旧时间淡化后永远停住。

不得为主栏新增线程、逐帧计秒、Sleep 轮询或常驻 50ms timer。动画活动时由共享 render scheduler 续帧，完成后回 Idle；deadline 无效为 max，不能让已过期期限造成 worker 自旋。

## 5. 即时变更、故障和退出

- 取消勾选/时长设为关闭：对应区域恢复，清到期期限，保留其他区域状态；重新开启从正常态完整等待，不消费启用前外部页码序号。
- 非零时长之间切换：按已有最后有效活动时间重新计算期限，延续原分页行为；非零变化不伪造用户活动。
- 正常主题或透明度调整：以最新正常 B/C 重算投影；不从已淡化值继续累乘。DPI/布局变化不得重新开始闲置计时，显示器或设备重建不丢失已确认的正常偏好。
- 退出 PPT/进入白板桌面/主栏暂不可见/新场次：退休旧闲置期限与状态。恢复可见后从正常态计时，退出中的 HWND 仍完成原显隐动画。
- 失败帧不发布提前成功的命中快照，重试仍按现有 dirty/present 协议；闲置动画变化必须使该对象和必要的旧/新绘制范围重新绘制。设备重建清资源、不伪造交互，不让全局 default 被上个 draw 的局部参数污染。

## 6. 预期改动文件

| 文件/范围 | 必要性 |
| --- | --- |
| `Other.Config.cppm`，按需 `Other.Config.cpp` | 三个范围字段，验证 bool 读取/旧值回退。 |
| `UI/Ppt/Ppt.cpp` | 发布三个范围；不改 COM。 |
| `UI/PageControl/PageControl.cppm/.cpp` | 快照等价性与窄 getter、独立范围门禁、清旧外部信号、恢复纯显隐 alpha。 |
| `UI/Bar/Bar.ThemeMaterial.h`、`Bar.Rendering.cppm/.cpp` | 两个固定上限及唯一解析、局部绘制通道消费。 |
| `UI/Bar/Bar.Scene.cppm/.cpp` | 分页独立闲置动画及常驻投影，菜单隔离。 |
| `UI/Bar/Bar.Main.cppm`、`Bar.Interaction.cpp`、`Bar.RenderLoop.cpp` | 主栏输入信号、deadline、属性聚合保护、渲染计时/动画与局部绘制。仅在声明/生命周期确有需要时改 `Bar.Main.cpp` / `Bar.State.cppm`。 |
| `UI/Setting/Setting.cpp`、i18n 20 语言/生成头 | 内联范围选择与一次事件写回、准确说明。 |
| 现有 headless/离屏测试及 `IdtMain.cpp` 既有初始化前测试区（若必要） | 验证生产配置/输入/绘制链，按既有测试接入方式增加最小无窗口 probe。 |
| native-desktop 专题 spec | 记录取上限、两个子系统的外部翻页差异、面板保护与配置合同。 |

不修改 solution、toolchain、依赖版本、资源图标或托管工程；无新编译单元时不改 vcxproj。若实际实施发现必须扩大边界，先报告并回规划修订。

## 7. 风险与回滚

关键风险是双重相乘、Scene 正常填充覆盖闲置、局部参数泄漏至附属面板、漏掉 More/子面板保护、输入取消留下永远悬停、旧场次期限泄漏或闲置后首帧动画跳变。必须通过生产路径的状态/像素证据验证，不能只写一套测试专用公式。

配置新增字段均可由旧版本忽略；回滚本任务代码时不覆盖正常透明度与原等待时长，删除新字段即可回到旧范围默认。代码回滚按任务文件集合进行，不对工作区执行整体 reset/clean。

无窗口验证不证明真实 Office、DWM/ULW 桌面合成或输入设备手感；这些维持单独 GUI 验收边界，不据此扩大平台支持承诺。

## 本轮内联动画补充

InlineRowContent 增加可选 visibilityId/visible，只有本行启用；原有内联行默认路径不变。SettingRow 预览与 ConditionalBlock 共用 PrepareConditionalMotion 初始化/目标逻辑，父卡片按同一 reveal 值计量含 padding 的内联高度；header/action 先画，只有 inline body 进入原条件裁剪/透明度/禁交互范围。父行末尾接续条件高度补偿产生的 CursorStartPos delta，避免视口上方变化后覆盖新布局锚点。

滚动测试把条件区域完整放在视口上方200DIP、后续锚点前留300DIP；逐帧校验补偿前范围仍在视口上方。初版24DIP余量小于30ms动画中段增长，实际诊断 anchor=125、expected=64、occupied=61，已进入视口；该摆位不满足原CompensateConditionalHeight的max(old,new)前提，故只调整测试，不改变通用策略。
