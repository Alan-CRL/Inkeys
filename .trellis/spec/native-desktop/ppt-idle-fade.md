# PPT 闲置淡化：范围、独立上限与主栏保护

## 1. Scope / Trigger

修改 PPT 闲置淡化范围、背景/内容透明度、主栏悬停/交互恢复、附属面板保护或闲置到期唤醒时适用。范围仅 Presentation；桌面、白板和普通弹出面板不继承本策略。正常用户透明度仍遵守 [常驻透明度配置合同](configuration-i18n-and-assets.md#ui3-常驻透明度配置合同)。

## 2. Signatures

- `config.PlugIn.PPTHelper.IdleFadeSeconds`：0 / 5 / 10 / 15 / 30，默认 10；0 为关闭。
- 同组 NoUpload bool：`IdleFadeBottomPair=true`、`IdleFadeMiddlePair=true`、`IdleFadeMainBar=false`，缺值或非法类型回到各自默认。
- `PptState` 发布 `idleFadeSeconds` 与上述三范围；`ReadPptIdleFadeSnapshot()` 返回主栏需要的场次/可见性/时长/是否启用。
- `ResolvePptIdleFade(input)`：已有超时/交互/外部翻页纯策略；`PptMainIdleFadeState::Update()` 对主栏固定不传外部翻页。
- `BarThemeMaterial::ResolveIdleOpacity(normal, ceiling, progress)`；固定 `IdleBackgroundOpacity=0.30`、`IdleContentOpacity=0.60`。
- `BarPageSurface::SetIdleFade(dimmed,duration)` 与 `SetOpacity(value,duration)` 分离；后者仅承载显隐。
- `BarUIRendering::SetResidentIdleFade(progress)` 为局部绘制通道，正常 progress=0；离开常驻对象绘制段必须恢复默认。

## 3. Contracts

### 不透明度

背景 B 和内容 C 分别以正常主题/用户值为源，投影到 `min(B,0.30)` / `min(C,0.60)`；进度从 0 到 1 在正常值和该终点之间插值。不得写回正常偏好，不得把已淡化的上一帧当新 normal，不得在最终结果上再乘用户值。例如 C=0.8 的终点为 0.6，C=0.5 的终点仍为 0.5。

显隐、禁用与内容转换 alpha 继续独立组合，不能把禁用文字强行提升至 60%。背景指正常常驻填充、内容指图标/文字；不借本任务重做边框、选中底板和光效角色。

主栏仅在 BottomDocked 生效（Free普通底栏与Centered居中底栏都允许），Floating保持正常、清旧期限，返回底栏完整重新计时；仅停靠形态切换不清除live输入。主栏本体含主按钮及收起入口；分页仅根底板与常驻按钮消费本策略。属性、更多、子面板及临时提示不参与淡化，不对整个 HWND 加 alpha，也不更改全局主题源使多个 HWND 共用同一个闲置进度。

### 计时与交互

- 四个分页 HWND 和主栏各自计时，不按左右 pair 共用一个计时器。
- 首次有效可见、新场次、白板返回或重新勾选区域从正常态重新等待。取消勾选、关闭时长、退出有效 PPT 可见态清理期限并恢复；不能重用旧外部翻页序号。
- 悬停、有效 Down/拖动时恢复且保持，最后一个交互保护结束后完整计时。第一下点击继续原业务；透明边角/扩大后的 HWND 不等于有效悬停。
- 外部翻页保留原分页规则及 3 秒控件来源宽限；主栏不因页码变化淡化，也不以该变化重置计时。
- 主栏任一可交互附属面板展开即保持正常，含画笔/形状/橡皮/撤回、颜色选择/笔宽等子面板及更多；聚合现有状态，不只看 drawAttribute。最后一个面板的退场与交互保护都消失后重新计时。
- 淡出 0.5s、恢复 0.15s，反向从 current 接续，尊重现有速率/动画关闭和 idle 唤醒首帧 dt=0。

### 所有权、设置与唤醒

Ppt 发布 main.json 配置到合并后的 PageControl 快照；主栏只读窄值快照并结合稳定 workspace 状态。新字段参与等价比较；主栏唤醒在原发布锁外，页码发布不得伪装成主栏活动。

输入信号由共享 worker 来源准入后发布，生命周期/面板保护基准由 render owner 维护；Cancel/Leave/Up/CaptureChanged/溢出/退出不能留下永远悬停或旧接触。主栏期限复用既有 `PageLoadingDeadline` 的发布/单次消费及 worker 等待，动画由共享 scheduler 续帧，无轮询线程或持续到期自旋。

PPT 联动的 `RenderPptIdleFadeRow` 让时长与复选项共用一张 SettingRow 卡片，无独立“淡化范围”标题。主栏选项标注仅底栏模式。InlineRowContent 的可选 visibilityId/visible 复用现有 ConditionalBlock：关闭时内联区180ms高度与透明度折叠，开启时展开，中途反向接续current；父行/下拉常驻，退出/未完全展开时禁交互并保留偏好。允许全不选；每项仅真实事件回写，合并为一次 FIFO 保存与 IdleFade 通知。全部 20 语言及 i18n 生成链同步。

## 4. Validation & Error Matrix

| 条件 | 结果 |
| --- | --- |
| B=0.80/0.88、C=1 | 闲置至 0.30/0.60，恢复精确回正常 |
| 正常值等于/低于上限 | 不反向加深，不二次相乘 |
| 外部翻页 | 分页按原规则淡化；主栏期限/目标不变 |
| 任一主栏附属面板或其收起退场仍在 | 主栏保持正常，最后结束后开始全新期限 |
| 全部范围未选或时长 0 | 无闲置作用，偏好保留，不持续唤醒 |
| 取消/重新勾选、场次/工作区切换 | 旧期限退休，重新进入正常计时 |
| 静置后首次唤醒、动画中反向、关闭动画 | 复用原时钟/动画语义，无首帧跳完或终点残留 |
| D2D/present 失败或设备重建 | 保持原重试和已呈现命中合同，局部参数不泄漏至后续 draw |

## 5. Good / Base / Bad Cases

- Good：主栏与左下分页分别处于 10 秒倒计时和闲置态；悬停左下只恢复它，主栏展开更多后持续清晰，关闭更多后重新等完整 10 秒。
- Base：旧配置缺新字段，仍仅底部/中部淡化；主栏默认不开启。
- Bad：Scene 仍 SetOpacity(0.4)，所有内容整体相乘；或用整个 Bar HWND 的 alpha 让属性窗口也透明；或外部页码改变不断延后主栏到期。

## 6. Tests Required

需要有意义的配置/快照/策略、实际共享控件输入与生产离屏绘制验证：30%/60% 终点/低值/中间值、独立计时、主栏外部翻页隔离、所有面板保护与最后关闭重计时、旧场次/关闭/重启范围、第一下点击单次执行、取消/离开与期限一次消费、正常透明度/主题/收展/动画关闭、菜单/属性像素隔离。

Setting 实际合并卡片覆盖默认 true/true/false、独立编辑、全不选、收起不写回、再启用展开、双向中间帧/反向/关闭动画及二十语言窄宽布局。配置覆盖严格 bool 与旧文件默认，i18n sync/check。

编译使用完整 InkeysRepo.sln Debug|ARM64、动态发现 ARM64 host MSBuild 与 PATH 规范化。验证用 Headless --no-window 及已确认初始化前离屏入口；不得把 hidden-window 或真实 Office/DWM/设备 GUI 验收记为无窗口测试已经覆盖。

## 7. Wrong vs Correct

```cpp
// Wrong：改变用户偏好并再次整体乘，附属面板也受影响。
config.Experimental.Inkeys3.UI3.ContentOpacity = 40;
scene.SetOpacity(0.4);

// Correct：保留正常源、独立投影；显隐和附属面板继续使用各自通道。
const double idleContent = ResolveIdleOpacity(normalContent, IdleContentOpacity, progress);
// 在常驻内容 draw 范围消费 idleContent，离开范围恢复默认参数。
```
