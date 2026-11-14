基线已核对：`bugfix/hand-erase @ babdecf6`。已读相关规范及一期归档的撤回修订。全程只读，无构建、GUI或 Git 写操作；命令进程被环境拒绝，`get_context.py --mode packages` 改由直接读取规范索引替代。

**① 代码事实**

- **几何**：实时点为 `x/y/r/time`，持久点为 `x/y/width`，保存时 `width=2r`。橡皮重放固定选 `RoundCapsule + Erase`。PS 用变半径胶囊距离，包含圆或零长段退化为较大圆；AA 为 `fwidth×1.5 + smoothstep`，擦除输出 `Add=0、Retain=1−coverage`。[点模型:74](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cppm:74)、[保存:898](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:898)、[重放:1125](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:1125)、[PS:5](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:5)、[输出:589](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:589)。
- **已有矩形基础**：荧光笔支持轴对齐矩形沿线段扫掠，每段六顶点，VS 用端点范围加半宽高和2px外扩；PS 用轴向及线段法线半平面。它每段只有一组宽高，CPU取两端尺寸平均，不能直接保持变尺寸橡皮的端点范围。[生成:585](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp:585)、[VS:98](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkVertexShader.hlsl:98)、[SDF:72](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:72)。
- **Win7**：正式VS/PS经中间目录无BOM副本编译SM5.0 CSO；Win7 WARP另选 `Draw3.Win7GuardedShader.h` 字节码。生成器把尾部导数/discard置于正向类型门内；新增类型须同步门限、正式CSO和兼容字节。[工程:1393](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys.vcxproj:1393)、[选择:755](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:755)、[生成器:42](D:/Project/Inkeys/Repo/Inkeys/.trellis/tasks/archive/2026-10/10-18-draw3-cursor-policy/research/Generate-Win7-GuardedShader.ps1:42)。
- **存储**：样式无橡皮形状；Undo/Redo切换笔划可见性，仍需准确的形状覆盖瓦片。自动保存、白板共用UInk；白板另存版本1历史JSON，点严格为三项。[样式:80](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkDocument.cppm:80)、[历史:949](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.InkHistory.cpp:949)、[自动保存:756](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp:756)、[白板:97](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.WhiteboardStore.cpp:97)、[JSON:582](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:582)。
- **光标**：鼠标/笔主光标与逐Touch光标分别生成，最终共用瞬态绘制；手掌进入Touch橡皮路径，笔速仅替换尺寸。抓手为两条竖胶囊：相对直径D，半径`.05D`、半高`.24D`、横偏移`±.12D`；内描边`.04D`、灰207，Hover透明度`.5`、接触为1。设置中的四种光标透明度不作用于橡皮。[共享参数:3](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/EraserGripVisual.h:3)、[抓手:390](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:390)、[入口:14080](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14080)、[Touch:14170](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:14170)。
- **预览/尺寸**：Bar属性预览由D2D画圆及抓手，布局先转画布px再除UI倍率；命中、选中环、焦点也按圆。基础D为24/32/40 DIP，笔速范围`.5D～5D`；手掌独立取 `D=√(接触W×H)` px。[预览:379](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp:379)、[布局:100](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeLayout.cppm:100)、[尺寸:55](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp:55)、[手掌:118](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.cpp:118)。

**② 规划推断（均高置信）**

- 固定比例可保留逐点标量尺寸，笔划锁存形状/比例；变尺寸段需计算两端矩形的扫掠，单击直接画矩形。同步修改顶点包围盒、SDF、实时dirty、历史瓦片和重放；手掌圆形邻域也须审查。Erase/AA合成可沿用，计算留在绘制owner/GPU，RTS同步回调不加工作。
- 光标需扩展外观及GPU形状，替换抓手；Bar同步改宽高布局、命中、环与焦点。小圆角统一使用最终px并限制不超过短边一半，不能复用随DPI缩放的Shape圆角。
- UInk当前只接受版本10；未知字段可忽略，但新增inkType会降级普通笔，`ink.extra`又被当前Draw3导入拒绝。[版本:837](D:/Project/Inkeys/Repo/Inkeys/inkStrokeModelerTest/draw3/uink_codec.cpp:837)、[降级:1405](D:/Project/Inkeys/Repo/Inkeys/inkStrokeModelerTest/draw3/uink_codec.cpp:1405)、[导入:35](D:/Project/Inkeys/Repo/Inkeys/inkStrokeModelerTest/draw3/uink_draw3_import.cpp:35)。因此可设计缺省圆形扩展读取旧文件；旧版正确读取新矩形不能保证。升版本可明确拒读；同版本扩展则必须定义降级，并同步白板历史。

**③ 产品决定**

- 尺寸D映射：短边＝D（面积增大）、长边＝D（窄向更精细）、等圆面积 `WH=πD²/4`（擦除面积连续）；另定竖矩形比例。
- 形状全入口共享，或手掌独立：前者统一，后者可保留掌擦语义。
- 旧版新文件策略：拒读、近似圆擦、另存兼容副本；分别影响可打开性、重放准确性、实现范围。
- 新光标中心纹样、小圆角具体px值，以及是否保持现有Hover/接触透明度，需视觉定稿。