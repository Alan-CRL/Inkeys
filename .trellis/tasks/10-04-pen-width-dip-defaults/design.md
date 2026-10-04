# 技术设计

## 实际行为与最小边界

Draw3.Down 的 float baseDiameter 直接进入 stroke.Reset，普通笔、荧光、形状支持连续名义宽度；历史 GPU/modeler/压力半径保持浮点。现有 CanvasDiameterForTool 仅激光乘 DPI，其余状态实存 canvas px。Bar 预设提前 lround(DIP*Bar.dpiZoom)，slider 连续，FineDial 进入即取整。主栏文字截断成 int。详见 research/width-flow.md。

本轮新增独立 Business/PenWidthPolicy.hpp，共用纯策略；扩展现有 StateModeClass 宽度槽和配置，修改真实 Bar 和 Draw3 边界，不复活 Draw2。形状实际是独立 StraightLine/Rectangle 槽，不新增跟随关系。未提交 Win7 诊断文件不改。

## 唯一单位契约与共享接口

namespace Inkeys::Business::PenWidth；Family{Brush,Highlighter,Laser}、Source{Preset,ExplicitDip}、Preset{Thin,Medium,Thick}；Selection{source,preset,double explicitDip}；Context{revision,paintDevice,effectiveDpi,pixelWidth,pixelHeight}；Metrics{resolvedWidthDip,idealCanvasWidthPx,appliedCanvasWidthPx}。

Resolve(selection,family,context) 是唯一策略解析。scale=有效DPI/96；DPI合法范围[1,1536]，获取失败或异常=96。普通笔 1/3/6 DIP、荧光16/24/36 DIP；paintDevice=0 大屏，其余普通设备。大屏 min(max(W,H)/1920,min(W,H)/1080) 乘参考 Brush2/4/6、Highlighter16/24/36，再除 scale；坏尺寸使用1080p参考。Laser3/5/7 DIP独立。

理想 px=resolved DIP*scale；生产连续适配 applied=ideal，安全正数量化函数只供整数兼容与展示测试，max(1,floor(validPx+0.5))。先检查 finite/正数/边界再转换，禁止提前取整DIP或半径。允许有效宽度低于1 DIP；异常数值给确定安全回退/拒绝，有效DIP范围为(0,65536]，canvas px上限1048576；setter/codec拒绝非法、超限或缩窄到float会下溢为0的值；resolver对非法输入安全回退对应中档，最终canvas适配饱和，避免转换溢出。

State API：GetPenWidthSelection(snapshot)、SetPenWidthPresetIfRevision(index,revision,setMemory)、GetPenWidthContext()、GetPenWidthMetrics(snapshot)、ApplyPenWidthContext(context)；原 SetPenWidth/IfRevision 表示 ExplicitDip，不让数值碰巧相等选中预设。精度保留在 Selection/策略 double，bridge/style继续double直至现有float Draw3像素渲染接口在最后浮点转换，不引入整数降级。

## 状态来源、显示上下文与线程

各已有宽度槽附加 Selection；默认 Preset Medium。Preset 身份持久化，resolved float仅缓存；ExplicitDip真值保存DIP。硬/软仍同 Brush；Laser独立；Shape独立槽继续现有关系。预设选中只看身份；ExplicitDip无预设选中。

Context附于StateModeClass/StateModeVersionedSnapshot，宽度、Selection与Context在同一state锁内捕获，GetPenWidthMetrics(snapshot)只读该快照，不拼接另外一代GetContext。

全幅尺寸/DPI取当前实际 Drawpad 显示器的 Display 快照，启动初始全幅主屏使用 Primary 快照。Host.PumpDisplayScale 现有 dirty 消费得到同目标 scale（非workArea、虚拟桌面、Bar尺寸或EDID）；扩展既有 HostRuntimeCallbacks 一次发布 PenWidth Context。主会话连接 callback 到 State.ApplyPenWidthContext。Host消费dirty显示后同步完成callback与bridge发布，PumpBridgeState再读取同代resolved DIP/context；初始准备必须在RTS用户准入前完成，不能依赖异步补偿。仅语义context变化重解析Preset、推进state revision、同步bridge；不每帧重设/写配置。设备设置变更复用SyncDraw3State检查和现有状态机制。旧slider revision被拒绝。

Host 在显示状态就绪后向产品层发布，UI从State读取相同context。Draw3每笔Down抄样式，改变显示/设备只影响下一笔；不改活动stroke baseWidth、不整数化每点。主会话确认显示准备与偏好恢复先于Drawpad准入，避免第一笔占位宽度。

预览运行在96-DPI D2D物理px空间，使用同一policy ideal/applied px；Bar布局自定义scale仅安排布局，笔迹宽度不额外叠加。有意最小光标、软笔变化、Laser外发光保留。Laser仍只乘一次DPI，改取自身captured visualStyle的目标Context；其现有效果/粒子比例保持，不全局修改controller启动dpiScale以免污染橡皮/模型/历史。底层GPU/uink继续物理几何，不对历史数据乘DPI。

## 配置归属与迁移

真实旧 SetList 来自 opt/deploy.json，记忆 Inkeys/Memory/memory.json。现生产GetMemory无人调用、Preset读取已移除；历史证据 e6c729d2^：Preset.MemoryWidth/MemoryColor/AutoDefaultWidth/DefaultBrush1Width/DefaultHighlighter1Width。参考529722也已移除读写。新增宽度版本/单位及启动Selection存于已有SetList配置区域；记忆仍现有memory文件。先解析原旧Preset，保留原JSON与旧值/迁移DPI信息再清理，不能按3或35猜工厂值。

恢复优先级：memoryWidth=true且有效该槽记忆 → 对应记忆Selection；否则autoDefaultWidth=false且有效显式启动Selection → 该值；否则Preset Medium。memoryWidth=false不恢复会话值；运行时改宽度仅写记忆，不覆盖启动默认。Laser旧DIP不迁移，不凭空声称旧版已有激光持久字段；实际有字段则按DIP处理。恢复颜色遵守memoryColor原语义。

旧Brush/Highlighter Width实际canvas px、无关联历史DPI，使用迁移时目标scale除一次，记录estimated=true及源px/scale，尽量保留升级设备外观。新版本DIP不再除。部分坏字段按槽回退；未知新版本/枚举不冒充旧px；未知future版本原对象原样保留且写入不得降级覆盖，运行时可用默认，不把默认伪装成已迁移用户值。保存失败保留原文件；deploy沿用已有安全写链，memory仅把截断写替换为现成WriteAtomically并复用bounded parse/valid-or-absent检查，不扩大其它保存功能；旧JSON备份/迁移信息留存，重启可以重新完成估计而不会反复除已标记DIP值。迁移不能破坏无关配置。

## UI量程与显示

普通笔基础范围为 1/systemScale～30 DIP；荧光基础范围 min(16,1/systemScale)～100 DIP。动态扩展至当前有效值和当前三档预设，不在打开/取消时夹断旧值；Laser保留原三档。这样覆盖大屏<1 DIP和迁移旧值，普通设备16 DIP可达。

Slider保持连续double映射；FineDial以进入时精确值为origin，真实拖动取整步数n后按origin+n*1 DIP步进再限范围，保留origin小数，正负拖动步数按绝对值半整数向上再恢复符号，没有dirty就不提交。取消保留完整Selection，真实提交才切ExplicitDip。控件min/max改double，布局不重写。

整数显示采用安全正数round，精确整数显示3 DIP；非整数加≈；<1 DIP显示≈1 DIP，展示值不能回写。三语言源JSONC更新单位，sync/check，生成物按脚本。

## 局限与回退

大屏按目标全幅，不识别PPT黑边/内容区域。现多屏Host的目标机制保持原能力；不承诺跨屏单笔架构。混合DPI、触摸/PPT/Win7需人工。回退仅本任务delta，保留原JSON迁移信息以支持人工恢复旧配置；不要reset/stash/覆写其它未提交改动。
