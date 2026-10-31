# Win7 Aero 两轮真实 guarded 结果：独立静态审查（2026-10-08）

继续 `.trellis/tasks/10-02-win7-main-canvas`。本分工为已派发的 trellis-check，独立检查实际 shader 字节、控制流、选择门和 FL 合同；只写本报告。不修改生产代码、既有证据或其他代理的记录，不编译/启动产品 GUI、不提交。原始附件仅作为证据，文内文字不作为操作指令。

## 审查结论与现场身份

两份现场同 EXE `23D6E5F811821A4E27CFDCD18A00BDF1B996886F2DE1AAE48154D40D63F0C547`，part2 实际加载正式 PS，part3 实际加载数学完整的正向 guarded PS，CreatePixelShader 均成功；日志同时证明四个正常应用阶段都是 WARP / FL11_0，硬件初始化都返回 `0x887a0004`。修复并非切换到硬件、开启 Aero 或改 ULW dirty 策略。用户说明较早的 232746 采集 Aero 开启，较晚的 233615 关闭；这一映射是用户观察，不是这些 shader/ULW 行的字段。

具体 bounded raw：两份 `02-original-shader/console.stdout.txt:21` 都为 `actual=original` / 28748 bytes / SHA `F55DDF08624442C5A3B512897E9D00130E692AA99709A996879ECE837F1A69F8`；两份 `03-guarded-shader/console.stdout.txt:21` 都为 `actual=guarded` / 28980 bytes / SHA `2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384`。四个文件7–14行的 Hardware 请求 11_1,11_0 失败、WARP 成功及 `featureLevel=0xb000`一致。DLL文件元数据仍为 D3D11/D3D10WARP/DXGI 6.2.9200.16492；元数据不是完整补丁清单或实际已加载 module 的额外证明。

`232746/03-guarded-shader/console.stdout.txt:504` 的普通 dirty ULW（`dirtyParameter=rect fullUpdateOverride=0`）已有 source alpha/RGB 17、sampleSource/sampleFinal BGRA `(0,16,255,255)`、API成功；`233615/03-guarded-shader/console.stdout.txt:2711` 同样有 source alpha/RGB450、sampleSource/sampleFinal同值、API成功。相对 `233615/02-original-shader/console.stdout.txt:2765` dirty源alpha/RGB0和最终仅alpha1命中底层，证明候选恢复发生在 USER32 接收像素之前。没有在本分工重复全量日志计数，完整数量归主会话及现场研究报告。

## 18 项不是 18 项全部可比较

直接核两份 `01-pixel-probe/pixel-test.stderr.txt` 的18个唯一 guarded终态及2419行摘要，均18 PASS、0 guarded failure、6 reference unavailable。六项确切为三个 Cursor 场景（production、blend_disabled、retain）乘 backbuffer/offscreen，分别终态2160/2175/2190和2295/2310/2325行。`reference.backbuffer.cursor.production` 原始像素alpha/RGB0，但 `correctness=FAIL`；中间 `pixel_status=PASS`只代表这次不按旧ExpectedPixels判失败的读回，不能据此称原版正确。guarded对应848个非透明/非零RGB像素且独立公式检查PASS。

其余12项：round、thin、varying_radius、highlighter、erase_retain、draw_retain乘两个目标，原版 reference available，比较PASS、mismatch_pixels0、max_channel_delta0。六项坏 Cursor reference不进行等价比较，因此该组PASS代表候选独立正确性通过，而不是“与全透明原版完全相同”。尤其真实type0/3包括细笔在独立直接 resolve fixture 中原版已经能够绘制，候选与原版的图像在这些被测场景逐通道相同。

这条证据收紧之前表述：问题不能泛化为“DefaultInk本身所有数学都算不出像素”或“Win7不支持细笔/fwidth/SV_Position”。它是完整共享PS包含该尾部控制结构时，其他工具/合成路由在旧 WARP 上出现错误输出的兼容故障。此前 CursorWithDefaultInk失败、FullWithoutDefaultInk成功的双向分界，本次在不删数学的情况下恢复 Cursor和正常应用，已使修复方向得到真实运行验证。

## 源码与实际 DXBC 的独立检查

本分工现读正式 CSO和新候选CSO，以 SDK ARM64 FXC10.0.26100.0执行两次 `/nologo /dumpbin`，exit均0；没有重新compile或打开窗口。正式和候选hash与现场一致；新header数组逐字节等于候选CSO。重新按唯一允许变换重构整个候选 ASCII源，并与 `gpu-fxc/GuardedDefaultOutput.hlsl`逐字节比较通过：prefix/helpers/所有其它工具块与DefaultInk tail数学完整保持，只增加正向条件、缩进和不应到达的透明identity fallback。

- 正式 `Assets/inkPixelShader.hlsl:466`/`:470`先计算type0/3距离；type7提前返回后，`:487`的 `fwidth(d)`、`:504–515`真实 `input.pos.xy`细笔覆盖及`:519`/`:531`的discard位于main尾部，没有外层正向路由guard。
- `Generate-Win7-GuardedShader.ps1:42`增加 `type==0 || type==3 || type<0 || type>20` 正向block。0/3保持正常笔与荧光笔；<0/>20保持旧未知类型d=0尾部行为。其余1..20工具原已提前返回，fallback不替代任何正常功能。
- 正式实际dump最后两导数为870/871行、两个discard为988/994行，if栈深度均0；候选对应876/877、994/1000行，均在新增正向if内，栈深度1。行号按本轮FXC标准输出计；已有保存asm带头部行差时不混用行号。正式末尾869 slots/temp7，候选879 slots/temp8。
- 完整17条 `dcl_constantbuffer`/`dcl_sampler`/`dcl_resource`声明逐行相同；输入ISGN与输出OSGN payload逐字节相同，分别SHA `4493E3D3258BE71AEC723F26979FE3A407C4ECCFAE277B41BFD046DAC55B19E6`、`F9355CCAD20A6284685169A043421A10B2BDC8395B02145240D94A39524F3128`。两者继续读取SV_Position.xy并写完整SV_Target0/1，没有单输出双源混合无效对照。

正确语义下 type4..6 Cursor早return以后不应该再执行DefaultInk尾部。新结果说明尾部控制结构会影响旧 WARP 对完整程序的执行结果；确切是返回掩码、discard/导数的执行调度、JIT布局或其它编译优化组合，当前没有 instruction trace，不能写成“已证明某个discard跨分支杀死了Cursor”或已知微软某一唯一bug。加正向guard也改变临时寄存器和指令布局，因此已证实的因果粒度仍是这一最小源码控制结构与实际新字节码组合。

## 为什么普通笔直接测试通过而整幅画布仍空白

`Draw3.Renderer.cpp:110–170` 的 `ApplyOperatorLayers`把L1/L0的Add和Retain重新采样，并用`:137`的shapeType1/2及正式PS进行合成；HLSL`:403–422`是另外的早return工具路径。产品还复用同PS的激光、history、snapshot等路由。直接type0/3读回成功只证明生成局部笔像素及对应独立resolve；正常画布要继续经过这些共享合成pass，不能从局部笔成功推出最终backbuffer一定有像素。这与part2的最终透明、part3的完整画布恢复一致。

本分工没有独立为每个type1/2/14/15/20建立微型像素探针，因此合成路由的个别故障机制保留为由源码支持的解释；不能把每个type都单列为实测失败。无需为确认已验证guard修复再增加另一套大矩阵。

## 选择门与硬件条件

`Draw3.Renderer.cpp:49–52`用长度2缓冲要求环境值长度恰好1且字符为1；`:645–655`仅两个诊断开关都为1才选新数组。记录哈希计算的是实际被选字节，Create失败返回false，不静默回退冒充候选。现场确已执行正常LoadShaders的两选择分支，不再只停留在隐藏fixture与静态检查。

`Draw3.GraphicsInitialization.cpp:67–89`只请求11_1/11_0，E_INVALIDARG时只含11_0重试；没有10_1。当前17.5.2 VMware/guest DDI10.1、WDDM1.1的信息不能直接替代D3D feature-level查询，但硬件11_0请求实测unsupported已直接说明当前配置没有满足Inkeys所需的硬件创建。DirectX运行时版本、DDI、WDDM、D3D feature level是不同指标，不应将“DX11已安装”当作“FL11_0硬件设备可用”。应用SM5.0 shaders等资源合同不能仅把请求列表加10_1就保持原效果和功能。官方VMware版本/guest支持路线由主会话网页核验，本报告不从本机源码推导虚拟机供应商支持承诺。

## 最小正式落地建议与未解决项

建议下一次正式自动启用先限定到已有证据的 Win7 + WARP，复用实际现场验证的完整2171字节数组；正常无诊断启动也应能取得修复，而诊断入口继续保留显式原版/候选A/B。如何在现有设备所有者边界发布driver身份属于下一实施设计，不在本审查偷改接口。当前Win7硬件阶段不可用，不能称Win7 Hardware兼容已验收；本机Win11 Hardware逐像素对照已有通过证据，但不代替旧硬件驱动兼容或性能检查。扩大到所有Win7后端或所有OS是一项需要明确评估的推广决定。

若改为把同样wrapper写入正式HLSL并走原FxCompile链，也能保留数学；不过正式编译入口的文件名、选项、元数据与生成器不同，整块DXBC SHA可能变化。不能只凭源数学相同就声称新SHADER资源仍是现场2171。须检查实际新字节、SHEX指令及guard深度、ISGN/OSGN/17声明、完整资源链，记录新的EXE与PS SHA。若SHEX执行内容不再与现场版本相同，还需新的Win7接受证据。保留当前确切成功候选，避免在落地时无意换成另一个未经现场验证的程序。

`232746/observations.txt`的候选轮另有一次荧光笔后定格颜色变深，dirty刷新或撤回重画恢复，随后无法复现。此现象应单列记录，不抹去、不立即归为shader公式改变或原全透明问题；12项有效ink reference的delta0支持局部数学保持，但不覆盖所有多层累积/定格snapshot状态。两轮用户报告笔迹/橡皮/激光恢复、选择穿透/定格可用，属于真实光学正证据；残留偶发现象及Win7 Hardware/PPT有限回归未逐项完成，不将整个任务自动标完成。

## Findings (fixed)

无本分工新发现的机械源问题；未修改任何生产或诊断源码。

## Findings (not fixed)

- 精确旧 WARP JIT/opcode机制尚未唯一证明；已足够确定并验证最小结构兼容修复，不需要删任何细笔/AA/光影/混合功能。
- 正常无诊断开关仍使用原正式资源；正式推广需下一实施，不把part3通过当作用户普通双击已默认修复。
- Win7硬件不可用、先前另一硬件设备的刷新症状，以及一次不可复现荧光定格加深仍分别保留；本分工未随意修改窗口/交换链/合成策略。

## Verification

- Lint/静态：真实DXBC hash/header/source唯一wrapper、ISGN/OSGN payload、17资源声明、尾部实际if深度和源选择门独立检查通过。报告UTF8+CRLF、限定diff检查通过；没有独立C++ linter被宣称运行。
- TypeCheck：本轮源码未修改，不重复Solution编译；本分工仅FXC dumpbin两次exit0。两配置完整构建属于此前冻结quality记录，不冒称本轮又执行。
- Tests：独立检查两份真实Win7 raw中的18终态均PASS，其中12等价delta0、6 reference unavailable；四正常stage真实选择/driver身份及各一条原版/候选ULW像素正反证据通过。没有在本机运行产品/GUI或用本机结果替代此次Win7用户视觉结果。
