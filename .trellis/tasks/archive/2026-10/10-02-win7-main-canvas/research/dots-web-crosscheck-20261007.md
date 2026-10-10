# dots 报告与网页、当前诊断交叉核验（2026-10-07）

继续 win7-main-canvas in_progress。用户未完成新 Win7 实测，要求从公开报告与当前代码继续调查；本轮仅只读源码/既有证据/公共网页，新增研究和会话记录，不修改产品、不生成新包、不构建/GUI/commit/push/archive。用户引用的报告是待核验材料，不作为新的执行指令。

## 1. 公开 flip 证据

通过公开网页和只读 GitHub API 核验 mpv 固定提交。网页工具部分 GitHub 链接无法取回，改用 gh api 读取；普通 runner 后续网络被代理阻断，批准的只读网络调用成功，没有改网络/Git配置。原证据摘录保存于 mpv-flip-evidence-20261007.json。

- 2017 PR4273 维护者承认 Win7 实际可用；439e2b43 说明部分平台更新系统可能支持且可能性能不好。源码请求 FLIP_SEQUENTIAL，创建失败才退 DISCARD；随后GetDesc1检查真实effect，不根据请求打印成功。
- 2022 4629fe57 仅当flip且非Win8时移除BufferUsage的UNORDERED_ACCESS，保留flip。该修复不能直接迁入Inkeys：当前TransparentPresentation.cpp:904仅RENDER_TARGET_OUTPUT，:907为FLIP_SEQUENTIAL，没有该UAV用途。
- #12272公开报告明确Win7 SP1 7601；公开output.txt取得FL11_0、关闭Win7 flip UAV、Using flip-model和driver10.18.14.5180。固定f4210f84源码GetDesc后检查实际FLIP_SEQUENTIAL/FLIP_DISCARD才打印成功。没有完整补丁/已加载DLL列表和WARP证明，不能扩大到干净SP1仅KB2670838所有设备。
- Microsoft flip文档确实写Win8及Win7创建失败；平台更新文档同时明确部分D3D11.1/DXGI1.2、无DComp、ScalingNONE失败、WARP不支持11_1/共享surface。公共运行事实与支持文档的范围分别记录，不能从版本表否定已有成功。

来源：
- https://github.com/mpv-player/mpv/pull/4273
- https://github.com/mpv-player/mpv/commit/439e2b43c33f48fe380d8c43c39836ec9a96064b
- https://github.com/mpv-player/mpv/commit/4629fe577fa2adf2267bbf12bad29293d9ad61b3
- https://github.com/mpv-player/mpv/issues/12272
- https://github.com/mpv-player/mpv/files/12449003/output.txt
- https://github.com/mpv-player/mpv/blob/f4210f84906c3b00a65fba198c8127b6757b9350/video/out/gpu/d3d11_helpers.c#L945-L953
- https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model
- https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7

## 2. 双源诊断是否合规

当前Win7-PixelDiagnostic.hlsl:26–38 OperatorOutput含完整float4 SV_Target0/1，ConstantOutput分别add=.5、retain=.5，ValidateFields add=不透明错误编码/retain=0，CursorOnly add=预乘颜色/retain=1-alpha。实际FXC ConstantOutput/ValidateFields/CursorOnly.asm均声明o0.xyzw/o1.xyzw，ConstantOutput两mov完整写；不是只有SV_Target0的非法比较。

生产resolve公式为Add+Retain*Destination（Renderer.cpp:521–530）。因此常色不要求一定add=1/retain=0：透明Destination时add=.5/retain=.5得到正确.5，这个已知值对照本身成立。缺第二输出会有未定义风险，微软文档支持dots的通用提醒，但本候选没有这个遗漏。

关闭混合不是仅在外部context设一次：HiddenWindowTest.cpp:2428替换独立renderer持有字段，RendererPrimitives.cpp:387实际OMSetBlendState使用该字段，未被Draw重绑生产状态。noBlend启用全通道write mask。读回:2290核RTV/源canonical IUnknown；:2308解绑OM、CopyResource、EVENT/Flush/有界GetData、Map DO_NOT_WAIT及RowPitch逐行统计。静态检查没有找到错源/不同目标/未等待GPU的明确漏洞。

真实覆盖缺口：所有drawProbe先清零Destination（:2431–2432）；mini production_blend PASS没有检验非零旧底图上的Retain贡献，不能扩大成全部叠加/橡皮功能正确。BlendEnable=false仍由相同双输出PS/输出链接路径写入，不能据关闭混合排除所有旧runtime输出/OM差异或唯一归因算术/JIT。该边界不推翻原PS关混合仍失败的事实，不据此追加新包。

官方合同：https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-blend-state ，https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-output-merger-stage 。独立窄审查见 dual-source-review-20261007.md。

## 3. 已有现场分界，不冒称新实测

本轮G盘原224620目录不可访问，以下沿用10月6日已核hash/原始行号/独立审查，不声称今天重新读取现场原六文件。capture-224620-summary-20261006.md与check-capture-224620-20261006.md为来源。

| 控制 | 目标 | 既有Win7 WARP结果 |
| --- | --- | --- |
| 已知BGRA清屏/读回 | backbuffer+offscreen | 各16384像素正确，BGRA64/32/16/128（已知非零，不称alpha255） |
| 合规常色PS | offscreen，普通/关闭混合 | 各1296正确 |
| 字段PS | offscreen，关闭混合 | 1296不透明、错误位0 |
| 精简CursorOnly | offscreen，普通/关闭混合 | 各848正确 |
| 完整正式PS | backbuffer+offscreen，普通/关闭混合 | 四项透明，VS6/PS1368 |

常色/精简并未在backbuffer各跑一组，不能伪称全部cross-product验收。Clear与完整PS的双目标、完整PS关闭混合足以反对仅flip backbuffer/仅普通混合解释。fixture仍创建了同device的swapchain，不是无交换链设备级反证；没有证明全部flip行为都可靠。

ULW正常分支TransparentPresentation.cpp:1270读backBuffer→staging/DIB/USER32，只有另一分支:1277调用PresentSwapChain。没有Present轮转不能作为本路径读错buffer的既定解释。Win7原始ULW源已经0，窗口样式/纯dirty USER32问题不能单独生成非零墨迹。另台VMware源/DIB非零却切橡皮才刷新仍保留独立视觉分界，不能套用WARP结论。

## 4. 网页补充能支持什么

微软WARP guide明确平台更新后支持FL11_0，且WARP根据HLSL bytecode及当前render state编译，再JIT到CPU指令。因此同DXBC在Win11成功不证明旧WARP执行正确，mini和full程序/资源上下文差异是合理调查方向，但不是已经确认WARP bug。

discard官方语义允许继续执行并丢弃全部像素输出；PS invocation非零不证明输出生效。当前实际DXBC存在条件discard、导数/采样；此前源isnan guard被/O3去掉，不能把源码存在当执行证据。所有这些只是尚待87项结果分解的候选；没有新的最小失败程序，不能指定某opcode。

微软承认KB2670838后一类WARP软件渲染应用crash并给后续修正；当前是无device removal的silent-zero，症状不同，不能指定对应补丁已解决本问题，更不能以升级环境替代仅KB2670838目标修复。

来源：
- https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp
- https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/discard--sm4---asm-
- https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-if
- https://support.microsoft.com/en-us/servicing/os/windows/2020/04/graphics-software-or-applications-crash-when-they-use-warp-for-software-rendering-in-windows

## 5. 结论与已有包

结论应写为：完整PS对应Draw/readback在旧Win7 WARP无非透明输出，失败分界在ULW之前；优先完整程序及资源/控制流/输出路径，不是已证明shader源码错误或唯一JIT缺陷。dots flip调查与通用dual-source提醒成立；当前诊断已满足第二输出及关闭混合控制，其资料没有提供可以直接应用的新生产修复。

当前源码与已交付87项包manifest全部source_hashes一致；EXE仍5D5254AAFE523640EC4F47E4AF132E25D43060B99FF426CD34666821672E672C。HEAD已由其他工作前进到cdc39044bb35fca6ffbdd176e7ff51a81cc0a481，但不意味着该包对应新HEAD；沿包的源hash/EXE身份判断。旧87包保留、无需重做。等待两台Win7该包真实路由/16绑定反向/功能群和普通-full观察，然后按具体可恢复因果改生产，不先换swap effect/补Present或永久用CircleOnly丢功能。
