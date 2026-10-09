# 完整数学 guarded shader 实机候选：GPU 实施（2026-10-08）

继续旧任务；用户要求 shader 效果不变。本分工仅 Renderer.cpp、HiddenWindowTest.cpp、新 Win7GuardedShader.h、生成器及本报告。未操作 GUI、构建、commit/push、正式 HLSL/CSO、资源编号、旧诊断数组、采集器或旧包。其他代理同时修改采集器，未回退他人修改。

## 候选与效果保持证据

`Generate-Win7-GuardedShader.ps1` 从当前完整 `Assets/inkPixelShader.hlsl` 和三个当前 include 生成临时 ASCII 副本；只将 type7 后 DefaultInk 输出尾部缩进进正向 `type==0 || type==3 || type<0 || type>20` 分支。所有 helper、0/3 距离、细笔真实 SV_Position 中心、有限双边覆盖、AA/smoothstep、所有 Laser/Shape/operator/history/snapshot 分支和双源 Add/Retain 数学保持。

相对10月8日前一个静态候选，未知 type 继续采用原 d=0 尾部公式；不会静默改成透明。type1..20 的其他功能已有提前返回，剩余 fallback 只属于不应到达的路径。不是 NoThin、FullWithoutDefaultInk 或 CursorOnly 生产替换。

真实编译：SDK ARM64 FXC 10.0.26100.0，`/T ps_5_0 /E main /O3`。新 CSO **28980 bytes**，SHA256 **2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384**。879 instruction slots/temp8；17资源声明保留，实际 ISGN/OSGN payload 与正式 PS 逐字节相同。末尾 ASM 中 type0/3/<0/>20 合并成 `if_nz`，DefaultInk 两导数、thin 分支及两个 discard 全在该正向 if 内；未被重新移回无条件尾部。

生成器对 tail 缩进后逆向还原逐字符相同，prefix/suffix 原样保留。独立 `verify_guarded_static.py` 重新构造唯一允许的 wrapper 与整个实际 ASCII source 比较，解析 header 字节与 CSO 相等，核签名和正式源/CSO/旧23诊断头 SHA；通过。该证据证明源码数学保留，不能单独证明 Win7 执行效果恢复。

输出目录：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-guarded-20261008/gpu-fxc/`。包含候选 HLSL/三个 include/CSO/ASM/guarded-source-evidence.json；父目录含 gpu-static-verification.json。

正式 source SHA **A22D1A7250F01836ECE2261B479632548758AA400F09977EDD458347DD0648A2**、正式 PS SHA **F55DDF08624442C5A3B512897E9D00130E692AA99709A996879ECE837F1A69F8**、旧诊断头 SHA **A0473D42BC094BE2A097DC59059D3F4036372AFF4BCC53C798BF28DC3DD961A8** 保持。

## 正常应用选择

Renderer 只有两个环境值 `INKEYS_DRAW3_WIN7_DIAGNOSTICS` 和 `INKEYS_DRAW3_WIN7_GUARDED_SHADER` 同时严格单字符1才创建完整候选 PS。任一缺失/其它值均读取正式 SHADER 资源；不按OS自动选择、不改持久配置。VS/Compute/其他资源保持。

诊断初始化输出 `[Draw3ShaderSelection] actual=original|guarded sha256=<实际字节SHA> bytes=<N> create_hr=<HRESULT>`。SHA 通过 Win7已有 BCrypt CreateHash/HashData/FinishHash 链对实际所选 bytes 计算；固定4096-byte对象缓冲、noexcept，hash失败明确unavailable。CreatePixelShader失败原样返回初始化失败，不偷偷回退原版伪造候选成功。未开诊断不做SHA/日志。

## 105 项 probe 合同

首行 `probe_schema=guarded-20261008 driver=WARP|Hardware expected_cases=105 matrix_expected=69 guarded_expected=18`。保留原87和69matrix、不变的旧23数组。显式probe RAII保存/清除guarded环境并在退出还原，保证即使调用者继承guarded=1也先测原正式资源。

新增18项=9场景×backbuffer/offscreen，每项在完全相同fixture先画原版再画候选：cursor.production、cursor.blend_disabled、cursor.retain；ink.round、ink.thin、ink.varying_radius、ink.highlighter、ink.erase_retain、ink.draw_retain。

- type0 使用正式 DrawStroke/VS/t0上传，位置40→88、y64.5；r4正常笔、r.35亚像素细笔、r.35→3变半径。type3使用正式 DrawHighlighterPrimitives/VS/t3和固定(1.25,25)半尺寸；颜色(1,.25,.125,.5)，保留真实薄线/AA代码。
- 此隔离绘制把 fixture 的 strokeOperatorBlendState 暂时设为正式 resolve blend，直接验证双源 `Add + Retain * Destination`；不是产品 MAX/MIN 多层累积路径，也不宣称覆盖全部 history/operator 回归。各项结束恢复原字段。
- known nonzero Destination 为 RGBA(64/255,128/255,192/255,1)，验证 cursor Retain、普通 draw Retain 与 erase。中心分别预期BGRA(224,192,160,255)、(112,96,160,255)、(0,0,0,0)，四角必须保持底图。erase所有像素核RGB/Alpha成比例且出现边缘部分保留。
- 透明目标核中心、有效覆盖数量、四角透明及整个图像 RGB/Alpha 预乘比例；thin 必须有真实 fractional alpha、变半径左右覆盖有差异。OMGetBlendState核实际blend对象和sample mask，不把renderer字段值当实际状态。
- readPixels复用同canonical RTV identity、staging CopyResource、EVENT/Flush、3秒有界wait、DO_NOT_WAIT Map和RowPitch；新增可选紧密BGRA snapshot供整幅对比。旧预期检查保持。
- 原版独立正确性失败记 reference_status=unavailable/comparison=unavailable，候选仍须独立数学与像素检查。原版正确时全图每channel容差1；任何不符使候选case失败。终态每项唯一 `case=guarded.<target>.<name> result=PASS|FAIL|ERROR reference_status=... comparison=...`，readback中间行仅pixel_status，无重复终态。
- 独立摘要 `guarded_cases=18 guarded_failures=<N> reference_unavailable=<N> result=PASS|FAIL driver=warp|hardware`。CLI保留原production四项/设备失败，还把guarded18完整及失败纳入退出；不会掩盖Win7原版基线失败。diagnostic/sequence/matrix各自摘要仍独立。
- 硬件初始化不可用exit3及不回退WARP保持。新增18原/候选各一个EVENT，无额外statistics，共36个3秒query；旧170query=510秒，加108秒=618秒+初始化CPU，采集器应为每后端900000ms（已通知根代理/collector）。

## 本分工检查和待办

- FXC候选compile/dump exit0；生成器guard/signature/math assertions通过；独立header/DXBC/source/旧身份检查exit0；限定git diff --check exit0。
- 格式：Renderer与新header/generator UTF8 BOM+CRLF；HiddenWindowTest沿用UTF8无BOM+CRLF。没有大面积换行diff。
- 完整Solution Debug ARM64、Release x64、实际双驱动105项、Headless、采集器mock/实际ProbeOnly由根代理执行，未在本报告冒称通过。
- Win7 WARP恢复、真实笔/细笔/荧光笔/橡皮/激光/撤回重做/选择穿透/定格回归待同包实测。前一VMware硬件非零像素光学刷新属于另一路径，当前候选不宣称已解决。任务继续in_progress，无commit。

根代理首次ARM64构建发现HiddenWindowTest.cpp:3025 Windows max宏展开导致C2589/C2059；本分工确认后仅把std::max调用括为(std::max)，不修改全局宏。diffcheck和静态身份检查再次exit0；完整重建由根代理执行。

根代理首次Win11 ARM64 WARP/Hardware实际probe均运行105项，4项新cursor透明底图assert错误（两目标production/noBlend，原版/候选都失败）：误把灰色描边也按白色RGB=alpha核算。正式shader保持不动；修正oracle为逐像素Circle距离、outer/inner线性Coverage、灰描边/白填充和Retain的精确独立BGRA数学，容差仍1.01，非零底图cursor也加强同公式。原失败四双流已保存ARM64-Debug/tests/first-cursor-predicate-failure，WARP stderr SHA A4FB15C364F6A4F8BED7928607D701E55EB1A721F3F741379CA2E547F19D8F2D，Hardware stderr SHA 6E5636FEB8A37905B719AD4526F1FA2C634EA9CC9565F1960EE99123EDA7D74D。重新编译/运行由根代理执行，尚不冒称修正后PASS。
