# Win7 全面 GPU 矩阵实现与冻结（2026-10-06）

本次继续 `10-02-win7-main-canvas`。用户要求全面单 CMD 包，尽量在两台 Win7 上一次回传完成分界。本实现仅独立隐藏窗口的早退 probe；不改变产品渲染、正式 HLSL、8 个 CSO 或资源编号。主会话负责完整 solution 编译、真实 CLI 和单 CMD 打包。没有运行 GUI、solution build、commit 或 push。

## 实际矩阵与退出合同

每个 driver 成功初始化后共 87 个不同 case：保留原 18 个结果，再增加 69 个 matrix 结果。

| 项目 | 结果数 |
|---|---:|
| 原 clear / 四个 Draw / 上传 / 三个精简 PS 对照 / 五个 full 序列 | 18 |
| CompactIntegerRoute、CompactFloatRoute、FullEarlyConstant、FullFloatRoute，各 baseline / fully bound | 8 |
| 原 PS 四组资源的 16 个 mask，逐项恢复原绑定并重测同一个原 PS | 32 |
| 原 PS fully bound，额外 finite Laser CB1 对照 | 1 |
| CursorFamilyOnly，加单群 / 移除单群六组，共 13 PS，各 baseline / fully bound | 26 |
| 最后原 PS 与 CursorOnly 控制 | 2 |
| 合计 | 87 |

新增 export：`int RunRendererHardwarePixelTest() noexcept`；既有 `RunRendererPixelTest()` 仍请求 WARP，两个 wrapper 共用同一 fixture。硬件严格请求 FL11_0，无 fallback。仅 D3D11CreateDevice 返回 `DXGI_ERROR_UNSUPPORTED` / `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE` 分类 `status=unavailable`、exit 3；其他初始化失败 `status=init-error`、exit 1，保留原 HRESULT。成功后记录实际 feature level，以及 DXGI adapter Description / VendorId / DeviceId。不能将任意失败归为硬件 unavailable。

在创建任何窗口/设备之前固定输出：

```text
[Draw3PixelTest] probe_schema=comprehensive-20261006 driver=WARP expected_cases=87 matrix_expected=69
```

硬件行仅 driver 改为 `Hardware`。随后原小写 `driver=warp/hardware requested=...` 保留。采集器先通过 schema 再调用新增硬件 CLI，避免旧 EXE 把未知参数当普通产品启动。

原 production4 与设备移除错误仍决定进程 exit；diagnostic7、sequence5、matrix69 分列摘要，不掩盖正式 PS 的失败。新 matrix 逐项只有一次 `case=... result=...` 终态，之前的像素/查询日志使用 `pixel_status` / `binding_status`；终态同时合并像素与绑定是否符合预期。设备/初始化失败会导致未执行项，不伪造完整 87 PASS。

每个 GPU query 等待仍有独立 3 秒边界。87 项含 2 个 clear、2 个 upload、83 个 draw：最坏有界 query 等待约 `(2+2+83*2)*3 = 510` 秒。采集器 600 秒/driver，多出的 90 秒仅用于 CPU/资源操作/调度估算，不是对系统实时调度的保证；parent 超时保留已经输出的原始流。

## 路由控制与资源实证

CompactIntegerRoute 维持真实 `shapeType + .5 -> ftoi -> >=4 / <=6 -> ==4`，CompactFloatRoute 对照 float gate。FXC 实际汇编分别保留 `ftoi/ige/ieq/if_nz` 与 `ge/eq/if_nz`；两个错误路径也输出非透明、不同的 BGR，以区别 gate 走错与零输出。正确 type4 哨兵 add/retain 都是 .5，对应 fixture 中白色 128、36×36 覆盖。

FullEarlyConstant 在 main 入口、类型转换之前，使用实际 CB0 的 `globalShapeType==4 && globalColor.a>0` 运行期 gate 输出 .5；剩余完整生产 PS 保留。FXC 验证 CB0 gate 与常色输出实际在 ftoi 之前，且全部 17 个生产资源表项仍在。FullFloatRoute 将全程序 shape 路由改为 float，其他工具/资源保留；实际汇编不保留原 shape ftoi，且有 v4.z float eq。二者都是诊断变体，不用于替换正式 PS。

资源 mask 位：1=s0 sampler；2=b1..3；4=八个 Texture2D（t1,t2,t4,t5,t6,t7,t9,t14）；8=四个 Texture2DArray（t10..13）。zero CB1/2/3 分别 112/48/48 字节，immutable、初始字节全零。dummy float4 texture 为已初始化全零 1×1、一个 mip；array 两层，SRV 维度正确，BindFlags 只有 SHADER_RESOURCE，不与 RTV 别名。finite 对照保持这些资源，仅将 CB1 的 112 字节替换为非退化 finite Laser 参数，其余 CB 仍零。NULL 资源的定义零语义与默认 NULL sampler 不是非法调用；本矩阵比较旧执行环境在合法绑定变化下的行为，不预设未绑定资源一定是根因。

所有 mask 从 `PSGet*` 保存的原始实际绑定恢复后设置；每项后恢复，16 项每项都跟原 PS baseline 重测。每个新 draw 结束后 `PSGetShader/ConstantBuffers/Samplers/ShaderResources` 再取实际 PS、b0..3、s0、t1..14；检查对象、CB 字节数、SRV 维度/array 层数、与 offscreen RTV 的 canonical IUnknown 别名。中间 getter 日志不能与 `case result` 混淆。

## 生产功能群拆分证据

始终保留原 Cursor4..6。六群是 Laser7..13（不含 Cursor）、Operators1/2、History14/15、TrustedSnapshot20、Shape16..19、DefaultInk0/3。只取 reviewed main top-level 源范围删去未选群，保留各群生产逻辑和相对顺序；所有 helper 定义保持生产源码，未用 helper 由 FXC 删除。移除 DefaultInk 时返回透明 fallback；保留 DefaultInk 时给移除类型增加透明 guard，避免移除类型误走原 `d=0` tail。Laser7 独立 tail 随 Laser 群保留/移除。完整源、source hash、原块 hash、保留群和 actual DXBC 均保存。

生成器验证每个 family 实际资源声明精确为所选群并集：

| 群 | 额外资源（全部还有 CB0） |
|---|---|
| Laser | CB1、s0、t6/t7/t9 |
| Operators | s0、t1/t2/t4/t5 |
| History | CB2、s0、t10..13 |
| TrustedSnapshot | CB3、s0、t14 |
| Shape / DefaultInk | 无额外资源 |

同时检查实际 integer Cursor4..6 范围、ftoi、if_nz；Shape16..19 range 只在该群保留时存在；Laser/Shape/DefaultInk 的实际 derivative body 与选择一致；每个 main 原块的原文保留/移除与请求一致。不是凭 shader 名字判断 compiled 范围。13 family 及其余 5 full 变体都保留完整输入语义签名；实际优化后的未使用字段声明可以消失，证据如实记录。

## 静态与 FXC 验证

使用从本机 SDK 定位的 ARM64 FXC `10.0.26100.0/arm64/fxc.exe`，全部 23 个 PS `/T ps_5_0 /O3` 成功。保存临时 ASCII HLSL/includes、ASM、CSO、`full-source-evidence.json`、`generate.log`。正式 UTF8 BOM 源仅复制读取，没有写入正式源码/CSO。实际 PS 输入签名与生产 VS 逐项语义/索引/掩码/寄存器匹配；全部 23 个 DXBC OSGN payload hash 与原生产 PS 相同。FullRecompiled 的 ISGN/OSGN/SHEX/STAT payload hash 与正式原 CSO 相同。

原 6 arrays 与旧 `win7-one-click-20261005/gpu-fxc` 的 CSO 逐字节相同；本次 header 23 arrays 与新 FXC 各 CSO 逐字节相同。原三精简 PS 虽然同源文件新增函数，原字节没有改变。文件格式静态核验通过：C++/H 无 BOM、UTF8 CRLF；header/生成器 ASCII CRLF；诊断 HLSL 原 UTF8 BOM CRLF。限定文件 `git diff --check` 通过。

证据根：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-comprehensive-20261006/gpu-fxc`。`static-verification.json` 保存计数、23 CSO hashes、原 6 byte equality、编码和下列源码 SHA。

## 冻结文件 SHA256

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`: `97B5458043F99D2FB5C813B42DEEC01F03C23559109BA08C443C828A359A1056`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.h`: `730D1810E8EEAB27207D182E129F5DB77362C7E1949E3696334B63886376EFE8`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PixelDiagnosticShaders.h`: `A0473D42BC094BE2A097DC59059D3F4036372AFF4BCC53C798BF28DC3DD961A8`
- `.trellis/tasks/10-02-win7-main-canvas/research/Generate-Win7-PixelDiagnostic.ps1`: `AFBC11A878B5D3514A36E428EB69ADDA22564EF21A8A80B645A86288B77E9ACD`
- `.trellis/tasks/10-02-win7-main-canvas/research/Win7-PixelDiagnostic.hlsl`: `906B04CA86FDFE57682D1EAB35C7289148012B64BF7BF0D69A8DA03C959D6726`


后续主会话须按仓库要求完整 solution 编译，再检查两种后端的真实早退 fixture。这里没有把 FXC/静态成功说成 Win7 根因已证明；对 Win7 的路线/绑定/family 因果结论必须依赖新实测，包括恢复后的反向结果。单功能群结果也仍可能包含编译程序规模、控制流和资源声明的混合差异，不能直接等同该工具的运行分支被执行。
