# Win7 双分界诊断实现记录

日期：2026-10-04。对应 `.trellis/tasks/10-02-win7-main-canvas/implement.md` 最新“第二台 Win7 11:38:50 实测反馈”。本记录是实现者静态/生成验证，不代替主会话完整 solution、自检、collector 和独立审查记录。

## 边界

新设备的独立 WARP 自检 Clear 正确，正式/关闭混合的 Cursor Draw 全透明且 VS/PS 有调用；正常应用使用另一条 VMware Hardware 设备路径，有非零源/DIB，但用户报告切到橡皮才刷新。新增诊断分别取证，不把两条设备路径合成同一根因。没有修改正式 shader、输入、交换链、渲染调度或缺省 ULW 更新规则；仍未证明唯一生产缺陷。

## 修改文件

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`：原六例后增加 b0/t0 字节读回和五项小 PS 对照，复用生产 VS/上传函数/目标、原纹理 readback 和 pipeline statistics。
- `Draw3.PixelDiagnosticShaders.h`：三个诊断专用 SM5 PS 的生成 byte arrays，只由以上自检文件 include；不添加工程/资源项。
- `Draw3.TransparentPresentation.cpp`：一次 Init 读取的显式 `INKEYS_DRAW3_ULW_FULL_UPDATE=1` 控制，只将 `UpdateLayeredWindowIndirect.prcDirty` 置空；限频像素日志增加实际参数和三段时间。
- `research/Collect-Win7.ps1`：identity 加入显式环境控制和 d3d11/d3d10warp/dxgi/user32 文件版本，probe 上限统一为 90000ms。
- `research/Run-Win7-Full-ULW.cmd`：setlocal 设置控制后调用原正常采集，endlocal 恢复调用者环境。
- `research/Win7-PixelDiagnostic.hlsl`、`research/Generate-Win7-PixelDiagnostic.ps1`：诊断源和可复现生成/真实签名检查，不进入正式 shader 构建链。

## 自检行为与解释

原顺序仍为 backbuffer/offscreen Clear、正式 Cursor、两项关闭混合 Cursor。`production_cases=4` 和原失败/设备检查仍决定退出值；对照成功不会掩盖原正式失败。

新增 `diagnostic_cases=7`、`diagnostic_failures` 独立 summary：

| case | 检查 | 通过条件 |
| --- | --- | --- |
| `upload.b0` | 最后一次正式 Cursor Draw 后立即复制当前 b0，48 字节 | 每个 expected/actual 32-bit word 完全相同 |
| `upload.t0` | 同时复制 t0 前两个已写 InkPoint，32 字节 | 相同；不读取 DISCARD 后未定义的后续区域 |
| `diagnostic.constant.blend_disabled` | 生产 VS + 常色 PS，关闭混合，普通 offscreen | 1296 个非零 alpha 像素，中心白色预乘约128，透明角 |
| `diagnostic.constant.production_blend` | 同上，生产 ONE/SRC1 resolve | 同上 |
| `diagnostic.fields.blend_disabled` | 生产 VS + 全字段/PS b0 校验 PS，关闭混合 | 中心 alpha255、透明角、所有已写像素 alpha255，错误 RGB union 全零 |
| `diagnostic.cursor_only.blend_disabled` | 生产 VS + 仅 shape4 Circle 方程 PS，关闭混合 | 沿用正式白色半透明 Cursor 验收 |
| `diagnostic.cursor_only.production_blend` | 同上，生产 resolve | 同上 |

b0 expected words 对应 width128、height128、shape4、offset0、color(1,1,1,0.5)、Draw0、outline RGB 各184/255；t0 对应 (64,64,16,16) 与 (outlineWidth0.5,fill1,0,0)。字节 Map/Event/Create 失败有阶段 HRESULT，upload 最终 `ERROR`；成功读回但字节不符是 `FAIL`。缺少诊断 PS 给明确 `diagnostic-shader-unavailable ERROR`。新诊断错误不改变正式四例退出定义，必须同时检查两个 summary。

buffer readback 使用 no-bind/no-misc STAGING CPU_READ buffer、显式非空 byte BOX、EVENT/Flush、最多3秒等待与 DO_NOT_WAIT Map。全部比较完成后才使用新 PS；每个 Draw 恢复原 shader/纹理/RTV/blend。每个新对照仍有 viewport、目标 canonical identity、像素和统计记录。

validator 固定 alpha1，无 discard。通过值是全黑 opaque；不以 RGB 非零判通过。CPU 对所有 alpha 非零像素做 RGB bitwise union，防止只看中心漏掉其它像素错误：

| 通道 | bit 含义 |
| --- | --- |
| R | 1=shape、2=color、4=r1/outlineWidth、8=r2/fill |
| G | 1=p1、2=p2、4=pixPos 与 SV_POSITION.xy、8=uv、16=SV_POSITION.zw |
| B | 1=PS b0 screen、2=shape/offset、4=color、8=operator、16=outlineRGB |

所有 float 近似检查包含 asuint 指数位有限性检查与 0.001 误差界；真实 /O3 dump 中保留相关 bit 指令。VS Cursor 的 uv=0，PS position z=0,w=1。本例固定 fixture 的全部正式 PS_INPUT 字段都被 validator 使用。

常色 PS 只输出 Add/Retain 各0.5，不依赖 stage 输入。Cursor-only 只保留正式 shape4 Circle 的距离、覆盖率、outline/fill、premultiplied Add/Retain 和 alpha discard；没有授权作为正式替换 shader。它与正式大 PS 的差异仍须 Win7 现场验证。

## 诊断 shader 生成和真实合同检查

维护机使用已安装 Windows SDK ARM64 FXC10.1，`/T ps_5_0 /E <entry> /O3`。原 HLSL 使用 UTF8 BOM+CRLF；脚本仅写本诊断源的 ASCII 无BOM 临时副本。目标 Win7 不加载运行时 D3DCompiler。生成输出在独立目录，正式八个 CSO 与资源301–304没有改动。

运行形式（SDK 版本不写死，ProductionVertexCso 为已有正式产物，只读）：

```powershell
$fxc = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/bin' -Recurse -Filter fxc.exe |
    Where-Object { $_.Directory.Name -eq 'arm64' } | Sort-Object FullName | Select-Object -Last 1
& '.trellis/tasks/10-02-win7-main-canvas/research/Generate-Win7-PixelDiagnostic.ps1' `
    -Fxc $fxc.FullName `
    -ProductionVertexCso 'Inkeys/Inkeys/Drawing/Draw3/Assets/inkVertexShader.cso' `
    -OutputDirectory '<独立生成输出目录>'
```

本轮实际生成产物/反汇编证据：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-split-diagnostic-fxc/`。

| entry | bytes | SHA256 |
| --- | --- | --- |
| ConstantOutput | 520 | C37897071ADF96CF0BDEC87C0663A69C583A2E0BEACAF20E5D06A7F0B2163AA7 |
| ValidateFields | 4556 | 7807CDE410BC0F6C8A7D2694DFD09F4162F2BA55728B12774410AB01FC14EABE |
| CursorOnly | 2256 | 4C6D92DF2138FB1E9E7EFFFD5776EC4C97E67813D702F51D1E4247C9D02A13EA |

脚本实际核验生产 VS OSGN 与两个接收输入 PS 的全部9项 ISGN register/mask/type：SV_POSITION v0.xyzw，pixPos v1.xy，uv v1.zw，color v2.xyzw，p1 v3.xy，p2 v3.zw，r1 v4.x，r2 v4.y，shape v4.z。validator 全字段 dcl 为 position linear noperspective、pixPos/uv linear，其余 constant；CursorOnly 所使用字段保持同样插值。未仅凭 semantic 同名推断兼容。ConstantOutput 无输入签名，是有意独立常色对照。

## ULW 控制与时序

缺省/空/其它字符串都保持原 dirty 参数规则，只有严格单字符 `1` 启用。每个 presenter Init 读取一次，Reset 清该标志。开启时无论普通 Draw3 输出开关状态，记录一条 `[ulw-control]` 激活行；原 source/finalDib 像素采样开关和限频保持。

源 GPU copy 范围、DIB 部分写入、presentFull 参数、底层 alpha、帧调度不扩大。全窗 USER32 更新从已有完整 DIB 提交。`lastObservation.usedDirtyRect` 反映实际 prcDirty；日志同时保留请求 `full`、实际 `dirtyParameter=null/rect` 与 `fullUpdateOverride`。

在现有最多每秒一次像素采样记录中：

- `copy_map_ms`：CopySubresourceRegion 前至阻塞 Map 完成；可能包括排队的先前 GPU 工作，不能单独解释为 CPU copy 时间。
- `rows_ms`：Map 后至既有 CopyAndInspectUlwDirtyRows 结束，含像素扫描、underlay、可能全帧扫描和 Unmap。
- `ulw_ms`：UpdateLayeredWindowIndirect 调用前后。保留原 `tick` 的 Map 后语义；GetLastError 在计时/日志前锁存。

不开输出开关时不执行新增 GetTickCount64；控制启用本身不强开像素统计。GetTickCount64 的低精度只适合区分多毫秒/多秒阶段，不用于精密性能结论。

## 已运行检查与未覆盖项

- 三个 FXC 编译实际成功，真实签名/插值检查通过；独立输出仅生成本诊断资产。
- collector PowerShell parser 无错误；90秒提示、WaitForExit90000 和 identity timeout 值一致，原 child 精确清理/ProbeOnly/失败继续采集分支未改变。
- `git diff --check` 通过；CPP保留原编码/换行，脚本ASCII CRLF，新HLSLBOM CRLF，全部 BareLF=0。
- 无 GUI、无主应用启动、无 Git mutation、无其它代理递归；保留其它会话正在改动的 spec/task/doc。
- 完整 solution 两配置、GPU早退 CLI、Headless、实际Windows PowerShell collector 和独立检查由主会话进行并记录；实现者不宣称这些或真实Win7 PASS。
- 本次 metadata 只补 DLL 版本，没有执行 broad WMI/GetHotFix；不能由此认定设备恰好只装KB2670838。

仍须 Win7 原设备/新设备上的两个分界结果。正式PS与精简PS差异最多缩小执行范围，不直接证明某条opcode/JIT/驱动根因；Full-ULW opt-in的视觉变化也必须与像素/时序一同解释，不能单凭环境变量启用就宣称修复。
