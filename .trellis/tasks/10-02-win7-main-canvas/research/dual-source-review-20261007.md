# 双源自检有效性只读审查（2026-10-07）

结论：未发现 ConstantOutput/CursorOnly 第二输出未定义、分量漏写、关混合被生产 Draw 覆盖或拷贝错误目标的源码缺陷。既有“该 Win7 WARP 完整 PS 对应 Draw/readback 无非透明输出、分界在 ULW 之前”仍成立；不能把它扩大为唯一 PS 算术/JIT 根因，也不能把零背景上的对照 PASS 当成双源 Retain 合成已经全面通过。

只新增本报告；没有改冻结质量报告、源码/spec/脚本/包，没有构建、GUI、运行 probe、联网、spawn、commit/push/archive。公开 API 合同与 flip 支持范围由主会话另行核查。本报告只据实际源码、已有 DXBC/身份资料判断。

## 已确认

1. **两个输出都完整赋值。** [诊断 HLSL](D:/Project/Inkeys/Repo/Inkeys-draw/.trellis/tasks/10-02-win7-main-canvas/research/Win7-PixelDiagnostic.hlsl:26) 定义两个 float4 SV_Target0/1；ConstantOutput 的 add/retain 均为0.5；[CursorOnly](D:/Project/Inkeys/Repo/Inkeys-draw/.trellis/tasks/10-02-win7-main-canvas/research/Win7-PixelDiagnostic.hlsl:73) 在 discard 后的存活路径给 add完整float4和retain四分量赋值。实际 [ConstantOutput.asm](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-comprehensive-20261006/gpu-fxc/ConstantOutput.asm:21) 有 dcl_output o0/o1.xyzw 和两条 mov全部写；[CursorOnly.asm](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-comprehensive-20261006/gpu-fxc/CursorOnly.asm:88) 的 mul o0.xyz、mov o0.w、add o1.xyzw 合起来完整写两输出。o0拆成两条指令不是部分输出缺失。两个真实输出签名都是 Target0/1 xyzw；没有单o0被误拿去测生产双源的情形。正式 [type4..6 Cursor源路径](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:305) 也完整赋值 add/retain；这不证明实际 Win7 已进入该路径。

2. **blend-disabled覆盖发生在生产 Draw 所用字段，顺序正确。** [HiddenWindowTest.cpp](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:2400) 建立零初始化desc、RT0写mask全开的noBlend，BlendEnable=false；缺noBlend则该comparison失败，不能沿原态冒称已关闭。每次 [drawProbe:2428](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:2428) 在调用Draw前替换renderer.operatorResolveBlendState。实际 [DrawTransientDrawingCursor:387](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.RendererPrimitives.cpp:387) 以该字段OMSetBlendState，随后RSSetState→Draw(6)，没有重绑保存的原blend。fixture在读回之后才恢复字段。生产resolve的 [RT0配置](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:521) 为ONE + SRC1_COLOR/ALPHA乘Destination，IndependentBlend=false，RT0全部分量可写；[SetOMTarget](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp:152) 绑定一个RTV，没有意外绑定第二RTV代替双源用途。

3. **RTV和被读回纹理身份一致，同一context有明确完成边界。** drawProbe将texture/rtv字段一起换成目标；Draw用SetOMTarget绑定该rtv。[readPixels:2285](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:2285) 从RTV GetResource，再将两资源QueryInterface为canonical IUnknown，身份不一致不会PASS。staging从source desc复制，仅改Usage/CPUAccess/Bind/Misc，保留BGRA尺寸/mip/sample。随后 [2308](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:2308) 解绑OM→CopyResource→End(EVENT)→Flush→GetData直到S_OK且ready，再DO_NOT_WAIT Map；失败/超时立即返回false，而非继续扫描旧数据。扫描使用RowPitch，不按紧密行假设。pipeline统计的第二等待发生在readPixels后，仅记录执行次数，不代替像素判定。这不是缺Flush、异步Copy后立即误读、或对错误纹理扫描的已证源码缺陷。

4. **offscreen失败不依赖backbuffer显示。** [offscreen创建](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:2265) 是独立128×128 BGRA默认纹理和RTV；与backbuffer用同一Draw/readback流程。fixture没有ULW调用，也不要求Present后才允许读回。实际应用 [ULW前copy/map:534](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:534) 已取得staging原始源；源统计先于Primary alpha底层处理和Unmap，后续 [ULW:591](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp:591) 才调用USER32。日志虽然在返回后打印，统计并非返回后才取样。因此“USER32单独把这些已保存原始source像素变零”不是对应代码时序。

## 确实存在的覆盖边界

- **零Destination不能证明SRC1 Retain贡献正确。** 所有 [drawProbe:2431](D:/Project/Inkeys/Repo/Inkeys-draw/Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp:2431) 都先清RTV为(0,0,0,0)。在ONE + SRC1*D公式中，正确有限retain的D项因此为0。常色/CursorOnly的production_blend PASS证明该测试输出在此配置下可写入，但没有检验非零背景上的attenuation/Retain，也没有检验全部工具、颜色、混合交互。若需要直接证明SRC1数值合成，应在后续有针对性实验中用已知非零Destination并断言Add+Retain*D；当前没有这项结果，不能补写成已通过。
- **当前关混合对照仍保留两个SV_Target输出。** 它隔离blend运算，不是单输出PS控制；不能独立证明旧runtime对完整程序输出链接/OM边界都正常。没有单o0变体并不使现有观察无效，原因是成功mini同样有完整o0/o1，且正式PS关混合也透明；这些证据限制了“普通双源混合配置单独解释全部失败”的说法，却未唯一隔离shader算术、控制流、输出链接、JIT或runtime实现。
- **没有每例OMGetBlendState实证。** 关混合可由上述实际调用路径确认，日志comparison=1本身不是getter证据；当前GPU诊断查询RTV/VS/PS/CB/SRV但未查询blend。因此不声称日志已逐例证明旧driver内部执行了blend desc。没有已发现的覆盖/恢复源码错误，不据这个观测缺口改生产代码或新造候选包。

## 身份及旧现场限制

本轮直接从当前header解析两数组，分别与全面候选CSO、上轮OneClick CSO核hash：ConstantOutput 520字节 SHA `C37897071ADF96CF0BDEC87C0663A69C583A2E0BEACAF20E5D06A7F0B2163AA7`；CursorOnly 2256字节 SHA `4C6D92DF2138FB1E9E7EFFFD5776EC4C97E67813D702F51D1E4247C9D02A13EA`，三者均相同。当前HiddenWindowTest cpp/h、header和诊断HLSL均与全面包manifest source_hashes匹配。包EXE SHA `5D5254AAFE523640EC4F47E4AF132E25D43060B99FF426CD34666821672E672C`；ZIP SHA `E7E534FC1988A6702DE8C00DF17656B96596B24912B4B137A62B9D719874E018`，本轮直接重新hash一致。冻结quality报告SHA仍为 `01EEF9F9360BC443DD1D3D41D8A60CE5051E16169A52E6CCDCD9DEA597120DC5`，未修改。

G盘目前不存在。22:46现场只能复用 [此前只读汇总](D:/Project/Inkeys/Repo/Inkeys-draw/.trellis/tasks/10-02-win7-main-canvas/research/capture-224620-summary-20261006.md:17) 和独立核验，不能声称本轮重新读了raw：旧EXE为1CB7...C0FA、18项；常色两blend各1296、CursorOnly两blend各848非零像素，正式PS双目标/两blend四项全透明；原PS反向重测仍透明，normal WARP源10采样为0。它与新87项全面包是不同EXE身份；mini DXBC相同只证明这两对照字节保持，不等于整EXE或全部C++相同。历史结果仍支持上述观测分界，不证明Win7已经恢复，也不覆盖VMware硬件源非零而光学延迟的另一路径。

## 审查结果

- Findings fixed：无；只读审查。
- Findings not fixed：非零Destination/SRC1合成、单输出链接控制和每例实际blend getter未覆盖，属于定位能力边界，没有已证明必须修补的候选源码缺陷。
- Verification：源码/现有DXBC及冻结hash核对完成；本轮不跑lint/type-check/build/GUI/probe，因为没有代码修改且主会话明确限制为只读。文档保持UTF8、CRLF。任务继续in_progress。
