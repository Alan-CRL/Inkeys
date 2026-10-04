# 画笔粗细 DIP 与设备推荐预设

## 1. Scope / Trigger

修改新笔宽、默认/记忆配置、Bar粗细交互、预览/光标、Draw3名义宽度或显示上下文时适用。源任务 `.trellis/tasks/10-04-pen-width-dip-defaults`；实现与运行证据分别记录，真实设备手感不从编译推出。

## 2. Signatures

- `Inkeys::Business::PenWidth::{Selection,Context,Metrics,Resolve,CanvasWidthPx,RoundPositiveCanvasPx}` 位于 `Business/PenWidthPolicy.hpp`。
- `Selection = Preset(Thin/Medium/Thick) | ExplicitDip(double)`；`Context = revision/paintDevice/effectiveDpi/full pixelWidth/pixelHeight`。
- `GetPenWidthMetrics(snapshot)`、`SetPenWidthPresetIfRevision(index,revision,save)`、`SetPenWidthIfRevision(double,revision,save)`、`ApplyPenWidthContext(context)`。
- `InitializePenWidthPreferences(context)` 在ReadSetting后、RTS准入前只初始化目标Context及一次Medium；启动Decode/flags/GetMemory已注释停用。Host callback同步发布真实绘制目标。
- `GetBarThicknessPreviewMetrics(snapshot,candidateActive,candidateDip)`、`UpdateBarThicknessPreviewWidth(value,canvasPx,candidateActive,duration)`：实际Touch候选发布与RenderLoop共用，候选直接更新动画val；AdvanceAnimationsAndDeriveLayout必须将thicknessPreviewDragging计入属性组MarkChanged。
- `PenWidthPreferences.hpp` codec：`PenWidth.Version=1, Unit="DIP"`；deploy的`Startup`与memory的`Selections`分别保存来源身份。

## 3. Contracts

业务/配置/UI是真正Windows DIP。`systemScale=effectiveDpi/96`，有效DPI[1,1536]，非法回退96。普通硬软笔1/3/6DIP，荧光16/24/36DIP，默认Medium。大屏paintDevice0按全幅旋转对称`min(max(W,H)/1920,min(W,H)/1080)`乘参考笔2/4/6px、荧光16/24/36px，再除systemScale得到DIP。其它模式普通设备；不新增识别或EDID宽度单位。Laser3/5/7DIP默认5独立。

硬软共享Brush槽；形状当前独立StraightLine/Rectangle槽，不能凭历史“跟随画笔”注释新增跟随关系。各槽Selection是用户真值，resolved float缓存不能取代身份。Preset换场景重解析，ExplicitDip保留。Context、Selection、width必须在同一状态锁下快照；语义上下文变化推进revision，拒绝过期手势，等价刷新不重设宽度/写盘。

`idealCanvasWidthPx=resolvedWidthDip*systemScale`。Draw3原本支持连续float，`appliedCanvasWidthPx=idealCanvasWidthPx`；普通笔/荧光/形状/Laser只在`CanvasDiameterForTool -> CanvasWidthPx`转一次。Down捕获style/context，活动笔迹不被下一代状态改变。整数兼容器独立为`max(1,floor(px+0.5))`；不得先取整DIP或半径。DIP上限65536、canvas上限1048576，输入保护在转换前；不整数化每点压力、预测、模型半径或GPU几何。

Bar在96DPI D2D物理画布中使用相同目标metrics；布局zoom只能调整布局，不能作为笔宽DPI。最小可见光标、软笔变化/预览溢出、Laser光晕保留；全局激光粒子是单独瞬态视觉缓存，不修改成新多屏模拟。历史uink/撤销几何保持原物理语义，不对读取墨迹重乘DPI。橡皮策略不受本合同影响。

当前发布隐藏/不支持启动记忆与启动预设，启动只建立共享Medium；旧Preset flags/default、PenWidth.Startup以及memory值/颜色均不得应用。初始化只一次，后续显示/工具/PPT变化保留用户会话值。GetMemory兼容实现不接生产启动；临时提交安全保存身份，不构成本版恢复能力。codec仍保护Legacy/未来schema：旧Brush/Highlighter px无源DPI时估计除一次，旧Laser DIP不除，版本化DIP不再次迁移；仅为兼容数据安全，不改变本版Medium启动。

memory沿用bounded parse和现成atomic writer，保存失败保留原件，不先截断；未知futureschema原样保留，AutoClean也不能丢弃或降级覆盖。坏字段各槽回退，不吞掉无关配置。

Slider连续double，基础Brush范围1/systemScale～30DIP、荧光min(16,1/systemScale)～100DIP，向当前合法值/预设扩展。FineDial以精确origin+n*1DIP步进；只真实调整提交，进入/取消不改Selection。整数文字只显示四舍五入数字（不带DIP或≈），sub1最小显示1；展示值不反写状态。活动触摸/Slider/FineDial候选发布后，预览canvas宽度动画同帧SetDirect，非交互仍SetTar；不能让core/outer.val缓动追赶或逐帧提交业务。

## 4. Validation & Error Matrix

| 输入/边界 | 行为 |
| --- | --- |
| NaN/Infinity/非正DIP/超限/float下溢 | setter/codec拒绝；resolver安全中档回退 |
| 坏DPI/尺寸 | 96DPI/1080p确定fallback，无硬件探测等待 |
| 同px多个Preset | 仅身份选中，不比较px猜测 |
| 旧px没有源DPI | migration记录Legacy、EstimatedSourceDpi、Estimated=true |
| 未知Version/Unit/枚举、部分字段 | 不作为旧px再次除DPI；保留future对象，独立安全回退 |
| 原配置坏根/深度/体积/保存失败 | 原文件保持，不丢其它偏好 |
| 显示改变/旧slider提交 | 下一笔采纳新context，旧revision提交拒绝 |

## 5. Good / Base / Bad Cases

- Good：1440p275%大屏中档resolved≈1.939393DIP、ideal≈5.333333px，连续Draw3保留此值；整数适配5。
- Base：新配置普通设备首笔中档3DIP，荧光24DIP；点击中档复用同一Resolve。
- Bad：Bar返回round(px)写入widthDip；取DIP整数2再乘2.75变5.5/6；preview额外乘自定义zoom；仅保存当前屏幕resolved缓存。

## 6. Tests Required

真实policy矩阵普通设备100/125/150/200/275%、大屏1080/1440/2160p及荧光；横竖/非16:9/异常数据/奇数与半整数；真实codec的旧px/LaserDIP/幂等/portable/failedsave/futureAutoClean；生产启动残留旧/new Startup与memory仍Medium、后续初始化保留手动值；真实Staterevision、BarFineDial不改值/取消/身份/预览zoom分离/活动候选到core与outer.val同帧；真实Draw3浮点转换与捕获样式锁定。完整Solution Debug|ARM64、headless --no-window、早退生产probes和i18n check；有真实可见HWND的hidden测试未经GUI授权不运行。

## 7. Wrong vs Correct

```cpp
// Wrong：UI先乘Bar自己的DPI/zoom，然后用叫widthDip的状态保存px。
SetPenWidth(round(preset * barDpi));
// Correct：选择身份，状态同代解析，只有最终canvas边界转像素。
SetPenWidthPresetIfRevision(1, snapshot.revision);
auto metrics = GetPenWidthMetrics(snapshot.state);
```
