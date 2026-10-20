# Research: Win7 DXGI FLIP 的声明、实际资源与 ULW 呈现边界（2026-10-04）

## 范围与直接结论

本报告只读当前主产品源码、22:48 Win7采集和Microsoft主要资料。用户请求晚间实测前静态定位；保留现有候选与FLIP合同，不实施backend或swap effect改变。

已确认的区别：本次Win7 WARP普通FLIP交换链创建成功，实际GetDesc1返回FLIP_SEQUENTIAL；但ULW分支没有调用Present1。它以swapchain backbuffer作为GPU目标，复制到staging并读回CPU DIB，最后通过UpdateLayeredWindowIndirect显示。该创建与读写链不是已经验证DXGI FLIP→DWM的直接呈现链。

## 源码与现场事实

- `Draw3.GraphicsInitialization.cpp:97-111`：BGRA设备，Hardware失败后WARP。同一device经adapter取得factory，不从另一个UI设备混用factory。
- 现场`console.stdout.txt:7-14`：Hardware 0x887A0004；WARP成功FL11_0；Software Adapter，UMD rawhigh0x60002/low0x23f0406c。这是当前实际WARP链，不能拿只验证Hardware的历史结果代替。
- `Draw3.TransparentPresentation.cpp:878-899`：B8G8R8A8_UNORM、SampleCount1、BufferCount2、ScalingSTRETCH、FLIP_SEQUENTIAL，ULW alphaUNSPECIFIED。先try waitable，失败后普通flags0；不是Win7不支持的ScalingNONE或Composition链。
- 现场stdout16-20：waitable 0x887A0001；普通swapchain成功，1920×1080、B8G8R8A8、count2、FLIP_SEQUENTIAL、UNSPECIFIED、scaling0、flags0。
- `Draw3.Diagnostics.cpp:799-810`：日志确实调用swapChain->GetDesc1，不是只把申请参数再次打印。
- `Draw3.Renderer.cpp:311-314`：GetBuffer(0)取得texture并创建同资源RTV；当前CursorGPU canonical identity实测匹配。
- `Draw3.TransparentPresentation.cpp:1241-1253`：Selection和PrimaryULW均读renderer.backBufferTexture；只有非ULW的else在1258调用PresentSwapChain→Present1（1018）。
- 当前copy为517-520的dirty CopySubresourceRegion + Map(READ,flags0)；没有实际flip轮转的Present1调用。因此Present后RTV解绑、buffer轮转或首次Present后GDI禁用的合同，不能无条件套到本次ULW时序。

## 官方合同及正确解读

| 官方资料 | 声明 | 对本案的限制 |
| --- | --- | --- |
| DXGI_SWAP_EFFECT | D3D11 FLIP_SEQUENTIAL从Windows8支持 | 不能据此覆盖现场实际Create/GetDesc成功；也不能把成功升格成Win7通用FLIP呈现保证 |
| DXGI flip model | Windows8新增DXGI flip，与Win7 D3D9Ex FLIPEX不同 | 用户以ULW显示的绿色实验不必意味着其调用了DXGI Present1；模式名字不能代替执行路径 |
| Win7 Platform Update | D3D11.1/DXGI1.2部分功能；Win7 WARP最高FL11_0、不支持shared surfaces | runtime/driver具体链需实测；这里不等于已证明创建成功的backbuffer无法Clear/Draw/Copy |
| CreateSwapChainForHwnd | Win7 Platform Update可用；ScalingNONE不支持 | 本案用STRETCH，非CoreWindow/Composition；不能仅见Factory2就判为Win8专有调用错误 |
| ID3D11DeviceContext::Flush | Map是runtime自动提交命令的时机；Flush自身异步 | “ULW没有Present1或显式Flush，所以Draw永远未执行”不是静态成立的推断；显式Flush也不单独证明完成 |

没有找到官方材料证明“此Win7 WARP创建成功的FLIP backbuffer会被静默清零”。保留为可证伪的资源路径疑点，不能作为确定根因或直接换bitblt的依据。

## 当前可用的分界与行动

现有修正包已经含相同device/FL11_0/普通FLIP的两目标Clear和正式CircleDraw分界。它同时读offscreen和backbuffer：

- offscreen成功、backbuffer失败：swapchain-backed资源差异成为直接正证据。
- 两Clear成功而两Draw失败：共同绘制数据/管线/输出先于USER32；读回基础链已经有颜色正证据。
- 正式混合失败而关闭混合成功：进一步研究正式blend执行；不能把隔离比较态直接用于产品修复。
- 两正式目标都通过：仍须检查生产EraserGrip shape6、1920×1080及dirty/阻塞Map相对独立Circle128×128全Copy/显式同步的差异。

在晚间现场结果前，只形成源码/字节码对照及优先级，不重新生成诊断包、改生产交换模式或重复本机GPU测试。

## 引用（2026-10-04核验）

- [Microsoft DXGI_SWAP_EFFECT](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect)
- [Microsoft DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model)
- [Microsoft Platform Update for Windows 7](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)
- [Microsoft CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd)
- [Microsoft Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)

关联规范：native-desktop/input-and-ink.md、build-and-compatibility.md、native/platform-and-resources.md、guides/cross-layer-thinking-guide.md。用户Win7既有FLIP实测事实与公开声明分开保留，未推翻前者；实际缺陷仍待读回分界。
