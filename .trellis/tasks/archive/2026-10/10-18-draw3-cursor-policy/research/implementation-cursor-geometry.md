# 荧光笔边缘与笔形圆角实施（2026-10-19）

当前权威为 cursor-geometry-revision.md，补充两类型/固定来源策略。本次仅修改正式 Assets/inkPixelShader.hlsl 两处光标数学，并由当前 Generate-Win7-GuardedShader.ps1 同步完整 Win7 guarded 数组。开始前已核对 Build/verification/cursor-geometry-20261019/baseline-hashes.json 六份源/数组基线全部相符。

最小边界：不改配置/UI、Controller/Window、输入或活动工具投影、实际墨迹/历史、VS/CB/公共API/资源ID。ScreenPen 非高亮和 legacy 矩形继续原内轮廓公式；只对显式高亮 type5 将左右内缩减半。PenIcon 保留原尖端和五边形外框，只对四个非尖端角作内切圆限制，不外扩 dirty 范围。不运行 solution/产品/测试 EXE、GUI/窗口/GPU harness，不做 Git 写入。

拟定几何：高亮矩形通过既有 uv.x=outlineOpacity+1=2 识别，内矩形半尺寸减 (outlineWidth/2,outlineWidth)，非正内尺寸为空、coverage 不超过 outer。PenIcon 内切圆目标半径为 min(halfSize)/8，即 24 DIP 图标下 1.5 DIP；每角切线距离限制为相邻边较短边的 1/4，退化边/角保留旧轮廓。只在两个切点法线间的角扇区用圆的距离限制旧 SDF；尖端索引0不参与，不用全局膨胀/收缩圆化。

验证只进行静态几何推理与 ARM64 FXC /O3 正式参考PS/guarded编译、DXBC签名/DefaultInk尾部/正向guard/字节身份核验；实际像素和 Win7 视觉仍待目标环境验证。

## 最终实现与静态几何说明

### 荧光笔高亮边缘

仅 `type == 5 && input.uv.x >= 2.0` 使用新内轮廓。现有协议中 `uv.x=outlineOpacity+1`，产品高亮的显式 opacity=1 对应2；屏幕笔荧光笔的非高亮边缘 opacity=0.35 对应1.35，旧调用的继承 opacity 对应0，因此后两者完全保留原 `CursorCoverage(distanceToShape + outlineWidth)` 公式。Circle/Eraser/PenIcon 的 type 也不会进入这段矩形处理。

高亮内矩形半尺寸为 `halfSize - outlineWidth * (0.5,1)`，即左右0.625 DIP、上下1.25 DIP；外矩形、RGB、实际宽度和填充0.35不变。只有两个内半尺寸都正时才形成内部，非正时直接0；内coverage还取 `min(outerCoverage,...)`，因此不会出现反向内矩形，且 `0 ≤ inner ≤ outer ≤ 1`。outline继续为outer-inner，独立alpha/预乘Add-Retain公式未改。

### 四个内切圆角与尖端

原五边形顶点和原边距离/内外判定均保留，索引0（左上热点）完全跳过圆角处理。其余四角的目标半径为 `min(halfSize)*0.125`，对24 DIP正方图标恰为1.5 DIP；随DPI同步缩放。

设当前顶点为v，指向前/后顶点的单位向量为u/w，内角为θ。圆心在内角平分线上：`C=v+normalize(u+w)*r/sin(θ/2)`，与原边的切点距离为 `t=r*cos(θ/2)/sin(θ/2)`。选择 `r=min(目标r,0.25*min(相邻边长)*sinHalf/cosHalf)`，因此每个切点最多占相邻边1/4，相邻两圆角合计至多占半条边，不会越过或相交。普通24 DIP图标四角均保持1.5 DIP半径，不触发该保护性夹限。

仅当 `dot(p-C,u)<0 && dot(p-C,w)<0`（两个切点外法线之间的角扇区）时，使用 `max(原SDF,length(p-C)-r)` 切去角尖，其余区域继续原直边SDF。在两条扇区边界上，圆距离与原切线距离相等，切点处相切，故过渡连续。最终距离不小于原SDF，所以外部覆盖和AA范围不会扩张；原24 DIP描述、左上热点、quad、CPU bounds和dirty全部不变。尖端邻接边的切点距其远端受1/4上限约束，圆角不会圆化左上笔头。

对极小/非等宽高的合法描述，过小半径、边长和退化半角保持旧轮廓；所有圆心计算前均验证长度及sin/cos大于1e-4，避免除零或非有限圆角。没有新增CPU镜像数学、公共参数或API。

### 只让笔形执行圆角数学

FXC最初把原有三元距离选择平铺，导致Circle/Rectangle/Eraser也执行新增圆角运算。按root确认，将该选择改成窄的 `[branch] if(type==21)`，else保持原Rectangle/Circle公式，避免本次增加其它光标的像素计算开销。最终guarded ASM第273行比较type21、第277行进入其分支，圆角数学全部位于该分支内，至第508行else才处理其它形状；这是静态执行范围证据，不是性能或GPU视觉测量。

## 生成结果与身份

动态发现的工具：`C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/arm64/fxc.exe`。正式参考PS与完整guarded PS均 `ps_5_0 /E main /O3` 编译退出0，反汇编退出0。输出在 `Inkeys/Cache/CursorGeometryShader/generated/`；没有手改产品CSO或资源链，主会话仍通过已有solution构建正式嵌入资源。

- 正式PS源 SHA256：`CDB41DD4DED5B230154BE2E04566365058604386551E15F3D2BB73A330AC0E0C`。
- 正式参考PS CSO SHA256：`D6C9BE02F6F238B5BD6D9FC2D21A0E72C1444492C838C49E1B98DC78C656E31E`。
- 完整guarded PS：**36380 bytes**，SHA256 **`F61BFB9DE4A86157037588D3DEEB62185318BA8AEDD83C20A625EC40EFC09327`**。头文件数组重新提取的长度和SHA与CSO/证据一致。
- 生成器未改，SHA256仍为 `19F04617EAB0BD452EBB487FE3219B78BDB2A2C633752BDFEAB030B0A505ED6A`，DefaultInk门仍为 `type==0 || type==3 || type<0 || type>21`，新笔形不会重入尾部。
- 正式参考/guarded ISGN均 `4493E3D3258BE71AEC723F26979FE3A407C4ECCFAE277B41BFD046DAC55B19E6`，OSGN均 `F9355CCAD20A6284685169A043421A10B2BDC8395B02145240D94A39524F3128`；RDEF亦相同。VS/CB/输入签名和资源布局未扩展。
- 原/还原DefaultInk尾部SHA均 `84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5`，与前轮相同。完整生成证据 `math_preserved=true`、`unknown_type_preserved=true`；25条derivative/discard指令最小guard深度1。

## 范围与验证结果

- 与本轮baseline比较，屏蔽PenIcon距离函数及本轮矩形内轮廓/必要type21选择分支后，正式PS其余字节文本完全一致；未触及其它工具数学或DefaultInk尾部。
- baseline-hashes中的PenCursor、WindowControl、RendererPrimitives、VS逐项仍匹配；旧baseline和前轮Cache证据保留。
- 正式PS和guarded头均严格UTF-8可解码，保留BOM/CRLF，无混合换行；`git diff --ignore-submodules=all --check`通过。
- 本代理仅执行静态检查、FXC编译/反汇编/生成，没有solution build、产品/测试EXE、GUI/窗口/GPU harness或Git写入。未新增复制生产数学的CPU测试。
- 源码和同步数组已冻结，后续独立check、完整Debug|ARM64和headless由root执行。本轮FXC/签名/数学证明不等于实际GPU像素或Win7视觉通过；新圆角和左右边缘的真实可见效果仍待目标设备/明确GUI授权。
