# 画笔粗细 DIP 与设备推荐预设

## 1. Scope / Trigger

修改新笔宽、默认/记忆配置、Bar粗细交互、预览/光标、Draw3名义宽度或显示上下文时适用。源任务 `.trellis/tasks/10-04-pen-width-dip-defaults`；实现与运行证据分别记录，真实设备手感不从编译推出。

## 2. Signatures

- `Inkeys::Business::PenWidth::{Selection,Context,Metrics,Resolve,CanvasWidthPx,RoundPositiveCanvasPx}` 位于 `Business/PenWidthPolicy.hpp`。
- `Selection = Preset(Thin/Medium/Thick) | ExplicitDip(double)`；`Context = revision/paintDevice/effectiveDpi/full pixelWidth/pixelHeight`。
- `GetPenWidthMetrics(snapshot)`、`SetPenWidthPresetIfRevision(index,revision,save)`、`SetPenWidthIfRevision(double,revision,save)`、`ApplyPenWidthContext(context)`。
- `InitializePenWidthPreferences(context)` 在ReadSetting后、RTS准入前只初始化目标Context及一次Medium；启动Decode/flags/GetMemory已注释停用。Host callback同步发布真实绘制目标。
- `IsBarThicknessPresetSelected(mode,index,dpiZoom,snapshot)`：Preset有效身份优先；ExplicitDip对同snapshot的Family/Context预设DIP做严格数值匹配，容差`8*double epsilon*max(abs(value),abs(target))`，无绝对下限；不写Selection/配置，不比较显示整数或canvas量化值。若异常尺寸安全回退使多个档位DIP相同，ExplicitDip只选Thin→Medium→Thick的首个匹配；Preset仍保留其身份。画笔/形状布局与hover共用此函数。
- `GetBarThicknessPreviewMetrics(snapshot,candidateActive,candidateDip)`、`UpdateBarThicknessPreviewWidth(value,canvasPx,candidateActive,duration)`：实际Touch候选发布与RenderLoop共用，候选直接更新动画val；AdvanceAnimationsAndDeriveLayout必须将thicknessPreviewDragging计入属性组MarkChanged。
- `PenWidthPreferences.hpp` codec：`PenWidth.Version=1, Unit="DIP"`；deploy的`Startup`与memory的`Selections`分别保存来源身份。

## 3. Contracts

业务/配置/UI是真正Windows DIP。`systemScale=effectiveDpi/96`，有效DPI[1,1536]，非法回退96。普通硬软笔1/3/6DIP，荧光16/24/36DIP，默认Medium。大屏paintDevice0按全幅旋转对称`min(max(W,H)/1920,min(W,H)/1080)`乘参考笔2/4/6px、荧光16/24/36px，再除systemScale得到DIP。其它模式普通设备；不新增识别或EDID宽度单位。Laser3/5/7DIP默认5独立。

2026-11-02 用户批准的共享样式合同（任务 shape-style-tablet-bar）替代旧独立形状槽：硬笔/软笔/实线/虚线/矩形完全共用Brush宽色。Brush颜色分Primary（桌面+PPT）、浅白板（首次现有红色）、深白板（首次白色）三份，按当前实际页主题选色并保留各类用户改色；Highlighter/Laser各自全局一份颜色。三组各自全局一份Selection宽度，工作区只恢复工具身份，不得从旧快照反写宽色。形状细/中/粗的点击、读数、预览继续共用Brush同代Context与1/3/6DIP（大屏参考2/4/6px）策略。各槽Selection是用户真值，resolved float缓存不能取代身份。Preset换场景重解析，ExplicitDip保留。Context、Selection、width必须在同一状态锁下快照；语义上下文变化推进revision，拒绝过期手势，等价刷新不重设宽度/写盘。

`idealCanvasWidthPx=resolvedWidthDip*systemScale`。Draw3原本支持连续float，`appliedCanvasWidthPx=idealCanvasWidthPx`；普通笔/荧光/形状/Laser只在`CanvasDiameterForTool -> CanvasWidthPx`转一次。Down捕获style/context，活动笔迹不被下一代状态改变。整数兼容器独立为`max(1,floor(px+0.5))`；不得先取整DIP或半径。DIP上限65536、canvas上限1048576，输入保护在转换前；不整数化每点压力、预测、模型半径或GPU几何。

Bar在96DPI D2D物理画布中使用相同目标metrics；布局zoom只能调整布局，不能作为笔宽DPI。最小可见光标、软笔变化/预览溢出、Laser光晕保留；全局激光粒子是单独瞬态视觉缓存，不修改成新多屏模拟。历史uink/撤销几何保持原物理语义，不对读取墨迹重乘DPI。橡皮策略不受本合同影响。

当前发布隐藏/不支持启动记忆与启动预设，启动只建立共享Medium；旧Preset flags/default、PenWidth.Startup以及memory值/颜色均不得应用。初始化只一次，后续显示/工具/PPT变化保留用户会话值。GetMemory兼容实现不接生产启动；临时提交安全保存身份，不构成本版恢复能力。codec仍保护Legacy/未来schema：旧Brush/Highlighter px无源DPI时估计除一次，旧Laser DIP不除，版本化DIP不再次迁移；仅为兼容数据安全，不改变本版Medium启动。

全局 memory 没有工作区字段；共享样式保存快照必须同状态锁投影Primary Brush色和三组最新全局Selection及已有编码的全局颜色，不能把白板Brush色写入Primary，也不能让旧Desktop宽度覆盖白板/PPT最新全局值。不得分开读取来源、工作区与样式后拼接跨代状态。沿用现有编码字段与启动恢复停用；不为会话共享添加白板/Laser颜色schema。具体生产保存入口与运行证据见本任务最终验证记录。

memory沿用bounded parse和现成atomic writer，保存失败保留原件，不先截断；未知futureschema原样保留，AutoClean也不能丢弃或降级覆盖。坏字段各槽回退，不吞掉无关配置。

Slider连续double，基础Brush范围1/systemScale～30DIP、荧光min(16,1/systemScale)～100DIP，向当前合法值/预设扩展。FineDial以精确origin+n*1DIP步进；只真实调整提交，进入/取消不改Selection。整数文字只显示四舍五入数字（不带DIP或≈），sub1最小显示1；展示值不反写状态。活动触摸/Slider/FineDial候选发布后，预览canvas宽度动画同帧SetDirect，非交互仍SetTar；不能让core/outer.val缓动追赶或逐帧提交业务。

## 4. Validation & Error Matrix

| 输入/边界 | 行为 |
| --- | --- |
| NaN/Infinity/非正DIP/超限/float下溢 | setter/codec拒绝；resolver安全中档回退 |
| 坏DPI/尺寸 | 96DPI/1080p确定fallback，无硬件探测等待 |
| 同px多个Preset | Preset身份优先；ExplicitDip匹配精确DIP，不比较px/整数文字猜测 |
| 旧px没有源DPI | migration记录Legacy、EstimatedSourceDpi、Estimated=true |
| 未知Version/Unit/枚举、部分字段 | 不作为旧px再次除DPI；保留future对象，独立安全回退 |
| 原配置坏根/深度/体积/保存失败 | 原文件保持，不丢其它偏好 |
| 显示改变/旧slider提交 | 下一笔采纳新context，旧revision提交拒绝 |

## 5. Good / Base / Bad Cases

- Good：1440p275%大屏中档resolved≈1.939393DIP、ideal≈5.333333px，连续Draw3保留此值；整数适配5。
- Base：新配置普通设备首笔中档3DIP，荧光24DIP；点击中档复用同一Resolve。
- Bad：Bar返回round(px)写入widthDip；取DIP整数2再乘2.75变5.5/6；preview额外乘自定义zoom；仅保存当前屏幕resolved缓存。

## 6. Tests Required

生产Bar新增手动非预设→提交匹配高亮且保持ExplicitDip、严格double容差/同文字不相等/非法输入、形状直线矩形各档与Brush共享Selection/颜色，三组宽度跨场景不回滚且组间独立、Brush三范围颜色默认/往返记忆、荧光/Laser颜色跨场景共享、主题/页切换后首笔正确、Explicit换屏不改值；真实policy矩阵普通设备100/125/150/200/275%、大屏1080/1440/2160p及荧光；横竖/非16:9/异常数据/奇数与半整数；真实codec的旧px/LaserDIP/幂等/portable/failedsave/futureAutoClean；生产启动残留旧/new Startup与memory仍Medium、后续初始化保留手动值；真实Staterevision、BarFineDial不改值/取消/身份/预览zoom分离/活动候选到core与outer.val同帧；真实Draw3浮点转换与捕获样式锁定。完整Solution Debug|ARM64、headless --no-window、早退生产probes和i18n check；有真实可见HWND的hidden测试未经GUI授权不运行。

## 7. Wrong vs Correct

```cpp
// Wrong：UI先乘Bar自己的DPI/zoom，然后用叫widthDip的状态保存px。
SetPenWidth(round(preset * barDpi));
// Correct：选择身份，状态同代解析，只有最终canvas边界转像素。
SetPenWidthPresetIfRevision(1, snapshot.revision);
auto metrics = GetPenWidthMetrics(snapshot.state);
```

## P2 图形家族扩展
开放Line/Arrow复用StraightLine槽，其他闭合预设复用Rectangle槽；不新增每模板宽度策略或跟随普通笔的关系。GetShapeStyle发布DIP；图形面板只提供无数字斜向三档预设，不提供独立Slider/FineDial；精细调节沿用普通画笔既有入口。预设、精调、预览和最终canvas转换仍遵循前述同代Context与单次DPI合同。
