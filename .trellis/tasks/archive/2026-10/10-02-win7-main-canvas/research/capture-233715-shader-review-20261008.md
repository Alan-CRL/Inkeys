# 23:37:15 Win7 矩阵：DefaultInk 关联与静态候选审查（2026-10-08）

继续 `.trellis/tasks/10-02-win7-main-canvas`。本分工只读实际采集、冻结源码/字节码与 Git 历史，只新增本报告；经主会话补充授权，在新的 visualization `shader-static-candidates/` 内写入两份临时候选及独立 FXC 输出。未改产品源、原生成头/脚本、正式或旧诊断 CSO、冻结质量报告；没有 Solution build、运行 Inkeys/GUI、commit/push，也没有派生子代理。

结论：**本次 WARP 的完整 PS 透明输出已收敛到 DefaultInk（type 0/3）这一组程序存在所触发的兼容异常。增加/删除该组在相同 Cursor fixture 上产生反向结果；具体原因仍不能在“尾部导数/早退控制”与“细笔补偿/SV_Position 使用及其编译布局”等相关变量之间唯一选择。** 普通输入、ULW、缺额外资源、简单类型转换、普遍不支持导数/discard 或仅 shader 太大，均不足以解释这套对照。该结论限定本次 Win7 WARP，不直接覆盖 VMware 硬件光学刷新问题。

## 原始证据与计数

只读复制目录：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-evidence-20261008/20261007-233715-414/01-pixel-probe/`。关键内容在 `pixel-test.stderr.txt`，不是只有三行 cursor-gpu 的 stdout。

- identity：EXE SHA `5D5254AAFE523640EC4F47E4AF132E25D43060B99FF426CD34666821672E672C`；Win7 NT6.1.7601 SP1、PS2；schema `comprehensive-20261006`。WARP exit1，Hardware exit3 unavailable。系统 DLL 文件元数据不等于已加载模块或完整补丁清单。
- WARP stderr：300481 bytes，SHA `E3BF9FF30FEDC26EA76458DB37575347F8ACC5B5A27D69C2FC3539F86923B53E`。独立脚本解析得到 **87 个不同 case 的唯一终态，28 PASS / 59 FAIL**；其中 matrix69为19 PASS / 50 FAIL。没有 ERROR 或重复终态。getter 与终态各有 binding_status，因此文本里138条 PASS不能误作138项。
- stderr1–18：WARP FL11_0初始化、真实 Software Adapter；普通 HWND swapchain 创建0，GetDesc1实际 effect=3、BGRA、buffers2、flags0。Hardware stderr4–5：`D3D11CreateDevice.Hardware.FL11_0=0x887A0004`，没有偷偷 WARP 回退，也没有硬件87项结果。
- 两目标 Clear（23、28）均16384正确像素；原 PS 两目标及关混合四 Draw（37、45、53、61）全透明。b0/t0逐word上传（69、76）PASS；常色两blend各1296、Fields1296 opaque/errorRGB0、CursorOnly两blend各848。
- 原 PS成功对照后重测、完整复编/入口常色/前移/最终重测（128–166）均透明；removedReason（2147）0。现有同步读回/query/API路径没有显示对应失败。

## 加回/删除 DefaultInk 的反向分界

所有 family 都使用实际生产 VS、同一 type4 Cursor 上传/目标/resolve blend；不是在测试各功能自己的图元。基线 mask00 和全绑定 mask15 结果一致：

| 同 Cursor 保留的程序 | mask00 / mask15像素 | 终态行 |
| --- | --- | --- |
| CursorFamilyOnly | 848 / 848，PASS | 1387、1415 |
| CursorWithLaser | 848 / 848，PASS | 1443、1471 |
| CursorWithOperators | 848 / 848，PASS | 1555、1583 |
| CursorWithHistory | 848 / 848，PASS | 1667、1695 |
| CursorWithTrustedSnapshot | 848 / 848，PASS | 1779、1807 |
| CursorWithShape | 848 / 848，PASS | 1891、1919 |
| **CursorWithDefaultInk** | **0 / 0，FAIL** | **2003、2031** |
| FullWithoutLaser/Operators/History/TrustedSnapshot/Shape | 均0 / 0，FAIL | 1499–1975相应终态 |
| **FullWithoutDefaultInk** | **848 / 848，PASS** | **2059、2087** |
| 最后原 PS / CursorOnly | 0 FAIL / 848 PASS | 2115、2143 |

原 PS全部16种资源 mask及逐项恢复原状态再测（463–1331）仍透明；有限 CB1+全绑定（1359）也透明。69个 matrix终态均 binding_status=PASS，包括实际 PS/b0/s0/b1..3/t1..14、CB尺寸、SRV维度和非RTV别名。因此不存在某一绑定失败被误算成像素故障的证据；全绑定不能恢复，也不能再把 NULL额外资源直接定为应用错误。

CompactIntegerRoute/CompactFloatRoute各两mask均PASS（239–323）；完整 FullEarlyConstant/FullFloatRoute各两mask仍透明（351–435）。整数转换/gate的小程序可用，改为float或在完整程序最早按CB0条件输出常色仍不足以恢复。它支持全程序执行/链接/编译上下文关联，不证明实际进入哪个完整程序分支。

## 实现和真实字节码核验

`Draw3.HiddenWindowTest.cpp:2414` 的 drawProbe只替换独立renderer的PS/目标/混合字段；`:2431`先清目标透明，再调用正式DrawTransientDrawingCursor。`:2761` matrixProbe设置资源后通过真实Draw，接着PSGet验证实际绑定，最后恢复。`:2831` family双mask；`:2835`恢复原PS/末尾精简控制。PSGet发生在renderer字段恢复后但context的PS未被重新绑定，所以读到的仍是实际刚画的PS；没有发现该恢复顺序冒领绑定结果。

生成器 `Generate-Win7-PixelDiagnostic.ps1:181` 列原main top-level块；`:200–219`把DefaultInk距离与输出tail作为一组保留/删除。DefaultInk family保留原helper定义和type0/3完整数学，并在输出前加了 `if(type!=0 && type!=3) return transparentIdentity`。所以“仅加一个负向早退guard就解决”已被这次失败变体反证；它与把完整输出放进正向block是不同结构。移除群会改变优化/实际声明，不是对原生产字节的单指令修补。

直接从当前生成header解析并比对八个关键CSO（CursorFamilyOnly、CursorWithDefaultInk、CursorWithShape、CursorWithLaser、FullWithoutDefaultInk、FullRecompiled、FullEarlyConstant、ValidateFields）：字节相同，SHA与日志/保存证据相符。八个OSGN payload全部与生产相同 `F9355CCAD20A6284685169A043421A10B2BDC8395B02145240D94A39524F3128`，完整 Target0/1 xyzw。未见只写一个output造成对照无效。

| 实际PS | 指令槽 / temp /最大if深度 | derivative / discard | PS读取SV_Position |
| --- | --- | --- | --- |
| CursorFamilyOnly | 73 / 6 / 1 | 0 / 1 | 无 |
| CursorWithDefaultInk（FAIL） | 295 / 6 / 2 | 2 / 3 | v0.xy |
| CursorWithShape（PASS） | 221 / 6 / 4 | 2 / 3 | 无 |
| CursorWithLaser（PASS） | 439 / 6 / 2 | 16 / 1 | 无 |
| FullWithoutDefaultInk（PASS） | 657 / 6 / 4 | 18 / 3 | 无 |
| FullRecompiled（FAIL） | 869 / 7 / 4 | 20 / 5 | v0.xy |
| FullEarlyConstant（FAIL） | 877 / 7 / 4 | 20 / 5 | v0.xy |
| ValidateFields（PASS） | 89 / 3 / 0 | 0 / 0 | v0.xyzw |

1. **尾部布局是具体候选。** CursorWithDefaultInk.asm134在Cursor block内ret，227在负向fallback内ret；229/230的DefaultInk derivative与347/353 discard处于main非条件尾部。CursorWithShape相同的导数/两次discard则在type16..19正向block内（256/257、268/274）；Laser/FullWithoutDefaultInk的导数也都在各自被守卫的功能block。返回之后尾部指令在正确语义下不应污染已返回Cursor；现在没有执行trace证明哪个discard真的杀死了Cursor，不把这个候选写成已知WARP实现错误。
2. **细笔/SV_Position是另一个相关候选。** 全部失败family都保留DefaultInk细笔补偿，声明并读取position.xy；全部成功family都不读position。然而ValidateFields实际声明position.xyzw、检查pixPos≈pos.xy与pos.zw=(0,1)，结果全正确（HLSL:57–59；ASM109、122–127）。因此“Win7没有SV_Position或传值错误”不成立；xy-only声明、控制流/优化与该读数结合的特定问题仍未隔离。
3. **泛指令或泛复杂度解释明显不足。** passing Shape与failing DefaultInk有同样2导数/3discard且同temp6，前者还更深；passing Laser和FullWithoutDefaultInk比failing295-slot程序更大。FullWithoutDefaultInk仍有原资源声明与Laser合法NULL绑定，不支持“有未选Laser数学或资源就必定零输出”。
4. **唯一因果范围是程序群存在与特定字节码组合。** source中DefaultInk在type4路由之后，type4应早退且不执行type0薄线数学；当前测试并未画DefaultInk自己的shape0/3。因此不能仅据Cursor恢复就说普通笔、荧光笔、Erase、Retain和历史合成全部已修复。

## 历史核对

`fc9a8b86`（2026-08-16 integrate Draw3）已经有同样default fwidth/smoothstep/discard尾部，但没有SV_Position细笔段。`d13712dc`（2026-09-20）新增GetInkLocalRadius 21行及main中14行细笔补偿，数学读取真实像素中心input.pos.xy；这早于10/2初次Win7故障，所以时间上可能相关，却没有对应Win7修复前/后对照证明。当前demo HLSL也已同步这段，不能把当前demo文本当作“当初Win7可工作的旧程序”。没有重新找到旧Win7可工作EXE/CSO的可验证源身份。

## 两个隔离静态候选

新目录：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-evidence-20261008/shader-static-candidates/`。沿用此前SDK ARM64 FXC10.0.26100.0及ASCII临时include规则；没有运行会覆盖原header的生成器。每个候选有HLSL、UTF8 unified diff、compile.log、CSO/ASM；JSON `static-candidate-evidence.json`记录原文件hash、命令、chunk哈希、指令guard路径与限制。

命令模式：`fxc.exe /nologo /T ps_5_0 /E main /O3 /Fo <new-name.cso> <new-name.hlsl>`，随后 `/dumpbin`；两个compile/dump exit均0。

- **GuardedDefaultOutput**：从type7之后的 `//float aaWidth`到最后return，保留全部原数学；整体缩进进 `if(type==0 || type==3)`，最后透明identity fallback。supported type0..20的输出公式不变，保留thin真实中心；无效未知type由原默认d=0输出改为fallback，若进入生产必须明确这一边界。diff +62/-54主要是原54行缩进，新增正向gate/fallback8行；去掉新增indent后tail原文完全相同，其它prefix原样保留。CSO **28868 bytes**，SHA **8C21933FD15BD848B9622F76D1126A762D9517B8622948100B2B4415DF80BB45**，875 slots/temp8。17资源声明、ISGN/OSGN payload与生产逐字节相同；SV_Position.xy仍存在。真实ASM869–871为type==3与原type==0条件OR后的正向if，DefaultInk derivative872/873、discard990/996全部位于该if内。**尚未在Win7执行，不能宣称这已恢复输出。**
- **FullNoThin**：只移除main thin补偿block，helper源码仍保留但由FXC按未用优化；diff +1/-13。CSO **25744 bytes**，SHA **C9714B94F436FF3D0C17918F609E1866CDE480BCBB6EE208D288F2C7A81CEF33**，764 slots/temp7。17资源声明与OSGN相同；输入语义、Mask/Register布局保留，但ISGN Used位变化，SV_Position不再读取/declaration消失；DefaultInk derivative870/871和discard882/888仍无guard。这个变体同时去掉薄线数学及position读取，恢复时仍不能唯一归因为其中某个因素。**会改变亚像素细笔效果，只是诊断负载，禁止直接作为生产降级修复。**

这两项保留常色/完整原PS/精简成功控制即可构成有针对性的下一小轮，不必重复堆叠另一套大矩阵。若Guarded版本成功，优先以保留数学和position的最小结构修正做same-EXE完整功能回归；若只有NoThin成功，再以最小position-only sentinel和保留薄线但改变position使用的对照拆分，不能删除细笔当作验收。若都失败，独立DefaultInk-only加真实shape0/3 Draw可判断单功能本身还是跨路由组合；所有恢复必须在成功后回到原PS反测。可复用原单CMD，主会话决定何时实现/交付，本报告未更新新包或CLI。

## Findings (fixed)

- 无产品/诊断源机械缺陷被本分工证实或修补。新静态候选是因果判别材料，不是已验证修复。

## Findings (not fixed)

- DefaultInk触发范围已证实；具体旧WARP执行/JIT、输入声明或尾部布局原因尚未唯一隔离，属于需要运行证据的判断，不静默修改正式shader。
- 原matrix全部画type4 Cursor，缺实际DefaultInk0/3和非零Destination Retain回归；不能把FullWithoutDefaultInk PASS直接交付生产，因为该变体删除必需功能。
- 微型Fields已证明position值正确但不隔离xy-only声明/复杂程序；NoThin会同时改变数学与实际输入使用，需保留解释边界。
- Guarded候选改变无效未知type fallback，temp7→8；支持类型的原数学/输入签名不变仍不是位级同一字节码或实机效果证明。
- VMware硬件光学刷新仍属不同分界；本次Hardware不可用，不能以WARP定位覆盖此前硬件非零源/DIB的故障。

## Verification

- Lint/静态：限定 `git diff --check`（冻结fixture/生成器）exit0；87唯一终态、69 matrix、69终态实际binding通过、八关键header/CSO字节和OSGN、正向guard实际指令位置、17资源声明及candidate输入输出payload检查通过。新报告UTF8/CRLF核验；无独立C++ linter被宣称执行。
- TypeCheck：两个新临时HLSL由已有ARM64 FXC ps_5_0/O3 compile和dump exit0；未更改C++或工程，按主会话限制不重复Solution build。不能将静态编译记为Win7恢复。
- Tests：实际Win7原包WARP87项28 PASS/59 FAIL，hardware unavailable；本分工不运行本机/Win7probe或GUI。两个候选运行验证pending，不声称PASS。
- 保持：本分工结束再次核生产HLSL `A22D1A...648A2`、生产PS `F55DDF...69F8`、fixturecpp `97B545...1056`、生成header `A0473D...61A8`匹配原冻结身份；三include、正式CSO读取前后hash相同。旧报告/原始采集未写入。任务保持in_progress。
