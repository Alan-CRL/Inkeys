# F-020 动态 SRV NO_OVERWRITE 修复独立审查

日期：2026-09-28。范围：本次 `InkRenderer` 能力查询、普通笔和 Shape 上传、无窗口 CLI；只做源码、规范、已有日志与 diff 审查，未启动构建、性能采样或 GUI。

## 结论

未发现 F-020 修复的生产阻断缺陷。两个使用动态 `t0` SRV 的 `NO_OVERWRITE` 调用点都受同一设备能力位控制；查询失败和 FALSE 均落入逐批 `WRITE_DISCARD`。当前无窗 WARP 回读证明确实写到缓冲区第 0 槽，但不证明可见像素、连续多批 GPU 绘制或 Win7 真机 Present。

## 代码与合同核对

| 项目 | 证据与判断 |
| --- | --- |
| 能力查询 | `Draw3.Renderer.cpp:419-428` 保存本次 device/context 并拒绝空参数；每次有效 `Init` 零初始化 `D3D11_FEATURE_DATA_D3D11_OPTIONS`，`CheckFeatureSupport` 失败时因短路表达式置 false，返回 FALSE 时也置 false。它没有从 FL11.0、Hardware/WARP 或系统版本推断支持。`ReleaseResources` 在 `:343-374` 清零能力位和 `m_bufferHead`；`TransparentPresentation.cpp:523-528,668-703,820-875` 在新模式尝试及设备丢失重建前释放，再由 `Init` 重查。Resize 只重建尺寸资源，保留同一 device 和能力位。 |
| 普通笔 | `Draw3.RendererPrimitives.cpp:48-103` 每批最多 200000 个 `InkPoint`；false 时先将 head 设零再 `WRITE_DISCARD`，`memcpy` 从第 0 槽开始，常量 `bufferOffset` 取零，Draw 提交后才推进 head。下一批以 `batchCount-1` 推进输入，保留连接点；最后一批少于 2 点时循环结束。true 时原有容量判断、环形 offset 和 `NO_OVERWRITE` 均保持。 |
| Shape | `Draw3.RendererPrimitives.cpp:159-220` 每批最多 100000 个 32-byte primitive，恰占 200000 个 `InkPoint` 槽；false 时 head/`bufferOffset` 为零，true 时继续原环形逻辑。`inkVertexShader.hlsl:156-161,199-201` 分别按 `offset + itemIndex*2` 和 `offset + itemIndex` 读 Shape/笔段；C++/HLSL 布局与 `cpu-gpu-contracts.md:3-27,43-56` 相符。 |
| 其他 Map | 全 Draw3 搜索只发现上述两处 `D3D11_MAP_WRITE_NO_OVERWRITE`。高亮、Laser、合成矩形、光标原本使用 DISCARD，未依赖该可选能力；其余 Map 是常量、staging 或独立资源，不受本改动影响。按 Microsoft 的[动态资源说明](https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources)，前一批 Draw 尚被 GPU 使用时，再次 DISCARD 可以得到新内存，旧批数据由运行时保留至 GPU 完成；所以逐批 DISCARD 后紧接 Draw 的提交顺序成立。 |
| 资源与产品边界 | `TransparentPresentation.cpp:90-93,614-625,668-679` 仍只按 DComp→ULW 尝试，ULW 交换链仍为 `FLIP_SEQUENTIAL`。`GraphicsInitialization.cpp:49-99` 保留 Hardware→WARP 和 11_1 `E_INVALIDARG` 后仅 11_0 重试。Win7 SP1+KB2670838 是目标；[Platform Update 文档](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)称该系统 11.1 能力部分可用，WARP 上界 11_0；[选项结构文档](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options)要求按设备查询动态 SRV 的该能力。项目对 FLIP 的目标机实测约束应继续保留。 |
| 空设备参数 | 审查期间 `InkRenderer::Init` 在赋值后、能力查询前增加 `if (!device || !context) return false`（`Draw3.Renderer.cpp:421-423`）。此前旧 `device.As(&dxgiDevice)` 对 null 也会解引用；本机 WRL `ComPtr::As` 直接调用 `ptr_->QueryInterface`，所以这是同入口的防御性改进。产品唯一调用 `TransparentPresentation.cpp:697` 位于 `InitializeGraphicsDevice` 成功建立 device/context 后；`TryInitialize` 在尝试前后经 `ReleaseAttempt` 清理，失败退出不会沿用旧能力位。若外部直接在仍持有资源的 renderer 上以空参数再次调用 public `Init`，该早退不会自行清理已有资源或旧能力位；这个未定义重入路径没有产品调用证据，不记为 F-020 阻断问题。 |

## 验证证据与空白

- `IdtMain.cpp:267-285` 在配置、互斥体和窗口初始化前匹配 `--draw3-renderer-map-test`；`HiddenWindowTest.cpp:2061-2165` 只创建 FL11.0 WARP device/context、动态 SRV 与 staging buffer，没有 HWND。测试强制 false，连续两笔和一个 Shape 检查 head、第 0 槽读回；设备报告支持时条件性检查两笔环形 head=4。它调用生产 `DrawStroke`/`DrawShapePrimitives`，但未加载 shader、设 RTV 或读取最终像素。
- 已有红灯 `TestResults/release-hardening/draw3-map-red-debug-arm64.stderr.log` 列出第二笔及 Shape 的偏移和回读共 4 个预期失败；Debug/Release 绿灯 stderr 均为 `PASS: no-window WARP dynamic-SRV map compatibility`。测试对旧实现有区分力，但“first unsupported-SRV stroke uses DISCARD”这条断言只检查返回码和 head=2，不能单独证明首笔选用的 Map 枚举。
- 夹具直接设置能力位，没有调用生产 `InkRenderer::Init`，因此查询失败→false、Release/设备重建重查仅经源码审查；支持分支在 WARP 返回 FALSE 时跳过，PASS 本身不能表示 true 分支实际跑过。测试未覆盖超过 200000 点的普通笔、超过 100000 项的 Shape、相邻批次在 GPU 上的像素保留和连接、连续不同工具交错、Map/设备丢失故障。若后续扩大验证，以离屏实际 shader+RTV 回读和目标系统运行分别记录，不能把当前 staging 上传测试写成像素或 Present 通过。
- 本机日志和静态路径不能证明 Win7 SP1+仅 KB2670838 上的 Hardware FL11.0、Hardware 缺失后的 WARP FL11.0、实际选项返回值、ULW 透明度及成功 Present；按 `draw3-integration.md:377-385` 和 `platform-and-resources.md:5-17,48-75` 仍需记录 OS/补丁、GPU/驱动、FL、Hardware/WARP、active presenter 与场景。未运行任何 GUI 测试。本次 `git diff --check` 对审查范围通过。
