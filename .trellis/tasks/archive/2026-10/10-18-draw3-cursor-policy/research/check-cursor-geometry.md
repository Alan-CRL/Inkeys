# 光标边缘与圆角：Trellis 独立审查

日期：2026-10-19。当前增量以 cursor-geometry-revision.md、PRD AC13/AC14 和 draw3-cursors.md 为准；两类型/固定来源策略继续有效。本文件只记录本轮纯shader审查，前轮 check/verification 与中间FXC产物均不作为本轮通过证据。

已预载最新check manifest、PRD/design/implement和Win7 shader合同。按root要求等待owner最终冻结后才审查实际源/最终数组；本代理只写本文，没有修改owner源码、构建solution、执行产品/测试EXE、启动GUI/窗口/GPU harness或进行Git写入。

## 正式PS最小范围

以 Build/verification/cursor-geometry-20261019/baseline/inkPixelShader.hlsl 与 baseline-hashes.json 为本轮基线，独立文本还原检查确认：替换回GetPenIconDist、type21的必要branch选择和显式高亮矩形内轮廓后，其余正式PS文本与基线完全一致。没有触及实际Highlighter墨迹type3、DefaultInk/薄线/AA/光影/历史/其它工具数学。

baseline中PenCursor.cpp、WindowControl.cpp、RendererPrimitives.cpp、inkVertexShader.hlsl四份SHA逐项仍匹配。24 DIP外观尺寸、输入热点、中心偏移、quad/CPU bounds/dirty、UV/CB/资源布局及来源/配置链没有本轮改动。

## 荧光笔左右边缘

- 门控是type==5且uv.x>=2。生产合法协议中uv.x=outlineOpacity+1，高亮值为2；ScreenPen荧光笔为1.35，legacy继承值为0，均保留原inner公式。Circle、EraserGripCircle、PenIcon和Laser不进入此矩形分支。
- 内半尺寸为halfSize-outlineWidth*(0.5,1)，因此左右从1.25 DIP变为0.625 DIP，上下保持1.25 DIP。外矩形、输入宽度、RGB和实际填充opacity*fillAlpha=0.35不变；改变只影响光标type5。
- 任一内半尺寸非正时innerCoverage直接为0，不构造可见的反向内矩形；否则inner取min(outer,CursorCoverage(...))。因CursorCoverage本身saturate，代数上始终0<=inner<=outer<=1，outline=outer-inner非负。
- 独立边缘alpha、填充alpha和预乘Add/Retain公式未变。在合法alpha范围内最终alpha=(outer-inner)*edgeAlpha+inner*fillAlpha<=outer，因此小矩形也不会外扩或产生负Retain。上下厚度的轴向内缩保持原值；拐角处按左右新内缩求相交轮廓。

## 四圆角与尖端

这是源码代数审查，不是CPU镜像渲染测试或GPU像素验收。

原五点和原polygon SDF保持。索引0（左上笔尖）跳过圆角，索引1..4分别从实际像素坐标取得朝前/后顶点的单位向量u/w；非等宽高输入也会先缩放顶点再计算边长/角度，不把圆误算成椭圆。

设内角为theta，|u+w|=2*cos(theta/2)。代码用cosHalf=|u+w|/2、sinHalf=sqrt(1-cosHalf^2)，圆心C=v+normalize(u+w)*r/sinHalf，切点沿每边的距离t=r*cosHalf/sinHalf。由此圆心在内角平分线上、到两边的垂距均为r，切点处与两条原边相切，是实际内切圆弧。

- 目标r=min(halfSize)/8，24 DIP正方图标为1.5 DIP，DPI等比缩放。
- r上界为0.25*min(两邻边长)*sinHalf/cosHalf，所以t不超过任一邻边的1/4；相邻两个圆角最多合计占半条原边，切点不会相交或越过尖端。
- 仅q=p-C同时满足dot(q,u)<0和dot(q,w)<0时应用圆SDF限制；该扇区正是朝外的两切点法线之间。其余区域沿旧直边SDF，扇区边界上的圆/直边距离一致，过渡相切。
- 例如24px图标右侧尾角v=(24,18)为90度角，r=1.5，C=(24-1.5*sqrt(2),18)，两切点沿邻边各距v为1.5，和代码公式一致；这是几何示例，不是执行shader的像素结果。
- 最终max(旧polygon SDF,圆角限制)不小于旧距离，覆盖只能向内收，不扩张外框/AA范围。邻接尖端的远端切点受1/4边长约束，原笔头保持尖角。
- 半径、相邻边长、sinHalf/cosHalf均先检查>1e-4；sin平方项先saturate，cosHalf门控还保证bisectorLength>2e-4。退化/极窄描述跳过对应圆角，原多边形分母保护保持，避免新增除零/负sqrt或索引越界。数组索引仍模5。

未发现阻断缺陷。圆角在目标DPI/亮暗背景的真实可见程度需实际画面验收。

## 实际ASM隔离与Win7同步

正式PS与guarded ASM均在273行比较type21，277行进入其条件分支；圆角数学在该分支内，508行else只处理原Rectangle/Circle公式。已核实际指令，而非仅相信[branch]标记；Circle/Rectangle/Eraser不会连带执行新四角计算。这里只确认静态执行范围，不声称测得性能或GPU视觉。

独立核验最终身份：

- 正式PS源SHA256：CDB41DD4DED5B230154BE2E04566365058604386551E15F3D2BB73A330AC0E0C。
- 正式参考PS CSO SHA256：D6C9BE02F6F238B5BD6D9FC2D21A0E72C1444492C838C49E1B98DC78C656E31E。
- guarded数组独立提取36380 bytes，SHA256 F61BFB9DE4A86157037588D3DEEB62185318BA8AEDD83C20A625EC40EFC09327，与最终CSO/header声明/生成证据逐字节匹配。
- ISGN、OSGN、RDEF独立解析，正式与guarded一致；输入输出签名保留既有4493E3D3...与F9355CCA...身份。
- 当前生成器SHA仍19F04617EAB0BD452EBB487FE3219B78BDB2A2C633752BDFEAB030B0A505ED6A，未修改。实际generated源码逐字等于当前正式源加既有唯一正向门变换（中文注释按已有ASCII输出处理），前缀和DefaultInk尾部保持。
- DefaultInk尾部与本轮baseline完全一致，原/还原SHA均84B1C4CF3F07A25ACBB5B9C081A91B2269AA62C47B67A0AA83BFB049BD2096A5；实际ASM 1129行仍以>21门限排除新已知类型。未知<0/>21输出保持。
- 独立解析ASM得到25条derivative/discard，全部条件深度>=1，分支平衡；生成证据与实际ASM一致。
- 两个变动源文件均保留原UTF-8/BOM身份、纯CRLF和末尾换行；git diff --ignore-submodules=all --check退出0。

## 本轮验证结论与限制

最终冻结版本的两处光标数学、形状/来源门控、小尺寸coverage、圆弧切线/符号、ASM执行隔离、正式/guarded同步和范围/格式静态审查完成，没有发现需要owner追加修复的阻断问题。本代理没有修改源码。

FXC编译/反汇编由owner执行并记录在implementation-cursor-geometry.md；本代理独立检查其最终源、CSO、header、生成证据和ASM。root的本轮完整solution/headless与最终PE资源数据核验另记，本报告完成时不预先写运行成功。此前两类型修订通过不算本轮结果。

guarded已从原31160 bytes/43C7...变为新36380 bytes/F61B...，不能沿用旧Win7硬件验收。当前没有GUI授权，真实圆角、左右高亮、DPI/背景及Win7 WARP实际像素仍待目标设备验证；源码/FXC/签名/无窗口逻辑均不能替代这些结果。
