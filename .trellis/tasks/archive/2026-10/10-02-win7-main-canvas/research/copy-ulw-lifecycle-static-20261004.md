# Research: Win7 生产 ULW 拷贝、资源寿命与提交约束静态审计

- Query: 已有 RTS/controller 输入正证据与 staging 全零读回，继续查生产 D3D11 到 ULW 的资源复用、同步、线程、DIB/DC 和 dirty/full 生命周期；寻找能直接解释当前故障的静态缺陷。
- Scope: mixed（生产源码、22:48 Win7 日志、Microsoft API 合同）
- Date: 2026-10-04

## Findings

### 结论与边界

没有找到能静态证明本次持续透明读回的资源寿命或 ULW 参数错误。最重要的边界不是“USER32 返回成功但没显示”，而是 **在进入 USER32 之前，GPU staging 读回结果已经是零**。纯粹的 DIB/ULW dirty/窗口层级故障不能单独解释这个证据；仍须区分 GPU 绘制/写入和 GPU Copy/readback。

不能将 document/Stored/history 的成功追加称为“已证明 GPU 生成非透明笔迹”。已有 Cursor 目标和绑定正证据、RTS/controller/文档正证据，但 Draw(void) 不证明 shader 执行或颜色写入。

另发现两项有界生命周期风险：flip swapchain 替换时 Flush 的位置，以及窗口尺寸恰在读回前变化导致 DIB 重建但本帧仍局部提交。它们都缺少本次触发证据，且后一项发生在 staging 数据之后，不能冒充当前唯一根因。不建议为这些推测改动今晚已交付的诊断候选。

### Files Found

| 文件 | 本次核查内容 |
| --- | --- |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp` | staging、DIB/DC、dirty Copy/Map/Unmap、最终 USER32 参数、presenter 初始化/释放/恢复 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp` | swapchain GetBuffer(0)、RTV、尺寸资源建立和 ResizeBuffers、绑定解除与 Flush 顺序 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp` | 独立 D3D11CreateDevice/immediate context、BGRA 标志、设备重建 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp` | 图形初始化、Run 与 cleanup 的同一绘制线程所有权 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp` | 两窗目标变更、full 提交、resize/恢复时机、最终 Present 入口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp` | WM_SIZE、WM_PAINT、WM_WINDOWPOSCHANGED 只发请求，不直接调用 D3D |
| `G:/Inkeys/Inkeys-Win7-Gpu-Probe-x64-Release/Inkeys-Win7-Gpu-Probe-x64-Release/20261003-224836-752/console.stdout.txt` | 本次首个 full ULW、尺寸/交换链描述、是否有资源恢复/resize失败 |

### 1. 源像素统计的时间点排除了“ULW 把原始源清零”解释

`TransparentPresentation.cpp:517-520` 先 CopySubresourceRegion 再 Map(READ, flags=0)。`:528-538` 的 CPU copy/scan 调用中，原始统计直接读取 mapped staging 的 `sourceRow`（`:212-225`），最终 DIB 统计在 CPU alpha=1 底层合成后另行读取。`:238` 调用 afterCopy，立即 Unmap。仅随后 `:548-573` 构造参数并调用 UpdateLayeredWindowIndirect。

诊断打印在 USER32 返回后（`:575`），但打印的是已在调用之前保存的值；没有再次读取 staging，也不把 USER32 的任何结果回写成源像素。故日志的 `sourceAlphaNonzero=0 sourceRgbNonzero=0` 是 **ULW 调用前** 的实际 staging 证据。这一点比 API success/可见性更有判别价值。

已有报告 `gpu-pipeline-narrow-20261004.md` 中同 tick 的白色填充橡皮实例，36×36 dirty 覆盖完整光标，其数据也遵循这一顺序。本次不重复简单 RowPitch/offset 数学审计。

### 2. staging 和最终纹理的静态描述符合基本 Copy 合同

| 项 | 生产路径及证据 | 审计结果 |
| --- | --- | --- |
| 源资源 | `Renderer.cpp:311-314` GetBuffer(0) 并创建同资源 RTV；presenter 每次从当前 renderer 取 backBufferTexture（`TransparentPresentation.cpp:1244-1252`） | 没有每帧缓存另一个旧 backbuffer 指针；ULW 分支不调用 Present1，不能从源码推出逐帧 flip 轮换 |
| 源交换链预期描述 | `TransparentPresentation.cpp:880-889` BGRA8、Count=1、Quality=0、双 buffer、RENDER_TARGET_OUTPUT | readback 不需要源同时具备 SHADER_INPUT；Copy 不执行采样或 shader 转换 |
| 目标 staging | `:378-405` BGRA8、1 mip、1 array、Count=1/Quality=0、STAGING、CPU_READ、BindFlags/MiscFlags=0 | DEFAULT→STAGING 的 Copy 是官方支持的用途；没有 mapped 后再 Copy 的正常路径 |
| 复用 | `:381` 只有纹理非空且宽高相同才直接复用；失败创建返回 false，Present 对空纹理早退（`:479-480`） | 没有创建失败后靠旧宽高继续读空纹理的成功分支 |
| resize/reset | `:362-375` 释放 DIB、staging 和 device/context，清尺寸；`:447-458` 重新捕获新 device/context 再创建资源 | 正常恢复不会把新 renderer 和旧 staging/context 混用 |
| 拷贝边界 | `:487-518` 当前 GetDesc 尺寸，与 staging/DIB 尺寸共同裁剪，front/back=0/1，目标 offset 与源 dirty 坐标一致 | 排除明确的越界/空 box 未拷贝；FL9_x 的特定 Copy 限制不适用于现场 FL11_0 |

Win7 原始日志 `console.stdout.txt:20` 的运行时 swapchain 描述为 `1920×1080 B8G8R8A8_UNORM(87), bufferCount=2, FLIP_SEQUENTIAL, UNSPECIFIED, flags=0`。其 first full ULW `:31/:45` 记录 DIB/staging/texture 同为1920×1080。因此不能用不同尺寸解释已观察的稳定光标实例。

仍有一个诊断限制：运行期 ULW 只直接读取/记录源 GetDesc 的宽高，并不核对源和 staging 的完整实际描述或 GetDevice 身份。源码建资源时这些值一致；若怀疑特定 Win7 runtime 返回或丢弃 Copy，成功 Map 不能证明前一个 void Copy 参数被接受。独立 Clear/readback 是比猜测格式更直接的判别。

### 3. 异步 Copy 不等于需要手动 Present/Flush 才能读到数据

官方 CopySubresourceRegion 是异步调用，但 Map 获取 CPU 访问并禁止 GPU 同时访问该子资源；DO_NOT_WAIT 是允许在 GPU 尚忙时立即返回错误的选择。生产 READ Map 的 flags=0，不是那种无等待读回。

更直接地，Microsoft Flush 文档明确说明 runtime 会在需要时自动提交，例如映射资源；Flush 自身仍是异步，单独添加它也不能证明 GPU 完成。因此从“ULW 分支没有 Present1/显式 Flush”推断“Draw 永远没发给 GPU”，缺少合同依据。为了实验可比较 query/event，同步缺陷尚须 Win7 正反证据，不能静态宣称修复。

Copy 合同约束同类型、兼容格式、在界内且未 mapped；没有规定 **作为源的 RTV 必须先解绑**。当前 copy 保留源 RTV 绑定、自检 copy 先解绑，确实是可控实验差异，但不能先把当前调用判为非法。

### 4. 同一 immediate context 的 owner 和重建顺序没有发现并发 writer

- `GraphicsInitialization.cpp:75-77` 用 D3D11CreateDevice 直接取得 context，产品 Draw3 未找到 CreateDeferredContext/ExecuteCommandList 分支。
- `Host.cpp:1444-1491` 在 runDrawing 内创建图形与 presenter；renderer Init 和两套 ULW Initialize 都接收同一 graphics.device/context（`TransparentPresentation.cpp:918-933,984`）。随后同一 runDrawing 调用 `drawing->Run()`（`Host.cpp:1625`）；`:1645-1649` 在该 owner 上释放 controller/presenter/renderer/device。
- `WindowControl.cpp:1538-1550` WM_SIZE 仅发布原子尺寸请求；`:1515-1535` WM_PAINT/SHOW/POSCHANGED 只请求 full present。UI/RTS 不调用本路径 Copy/Map/Draw。
- `DrawingController.cpp:7192-7209` 先 renderer Resize，再 presenter Resize，均成功后才 CommitSize；失败立即标记恢复。循环 `:12203` 在 graphicsRecoveryPending 时跳过本帧呈现。
- `DrawingController.cpp:12027-12037` 先释放旧 GPU history，再调用恢复；`TransparentPresentation.cpp:1162-1175` 先 ReleaseAttempt，deviceLost 时再重建独立 Graphics；正常重新 TryInitialize 后两套 staging 重新取得新 context。

源码没有独立 asynchronous readback worker，也没有多套 immediate context 共享同一 staging 的分工。主窗口线程在 ULW 引起消息时最多发布下轮请求，未发现 reentrant D3D 写入路径。这是静态所有权结论，现场日志没有 thread-id/context 身份采样。

### 5. Map/Unmap 和 GDI 生命周期没有把源指针留到 USER32

成功 Map 后的 CPU 处理只有 bounded raw pointer 扫描/memcpy；统计对象不分配资源。返回 CopyAndInspect 前就 Unmap（`:238`），随后不再保存或使用 mapped.pData。Map 失败在 `:521` 提前返回并触发 presenter 失败/恢复，不读取未成功映射的内存。

DIB 创建与选入发生在绘制 owner：`EnsureWindowDib :408-444` 使用 CreateCompatibleDC(NULL)、top-down 32-bit BI_RGB CreateDIBSection、SelectObject，并持有 bitmap/DC/bits 到下一次 resize/reset。宽×4 天然 DWORD 对齐。`ReleaseDib :349-359` 先选回原 bitmap、再 DeleteObject、再 DeleteDC，清 bits 和尺寸。CreateCompatibleDC(NULL) 的线程寿命要求也由该 owner cleanup 覆盖。

Microsoft CreateDIBSection 提到 GDI 完成对 bitmap 的绘制后才能自行写其 bits。当前私有 memoryDC 除选 bitmap 和作为 ULW 的 source 外，没有 BitBlt/AlphaBlend/其它 GDI drawing **写这个 DIB** 的生产调用；不能凭没有 GdiFlush 就确定 DIB 写入竞态。无论 DIB 同步是否还需实测，它发生在 source 统计之后，不会把此前的 mapped staging 统计改成零。

### 6. full/dirty 与双窗目标首次呈现具有正常重置条件

`DrawingController.cpp:11968-11974` 在同 owner 处理 selection/workspace 的目标变更；`:12015` 将 outputTargetChanged 作为 forceFullPresent 条件；`:13596` full 时替换成整画布 dirty。恢复 `:12108`、resize `:12201`、WM_PAINT 请求都会促进 full 提交。两套 presenter 拥有独立 staging 和 DIB，共享同一最终纹理，没有复制 Primary alpha=1 DIB 回 GPU 或辅助 DIB 的分支。

当前参数 `TransparentPresentation.cpp:555-572` 为 HWND screen top-left、完整 DIB size、源点(0,0)、AC_SRC_OVER/255/AC_SRC_ALPHA、ULW_ALPHA；full 的 prcDirty=NULL，partial 指向已裁剪的 dirty。这些栈变量直到同步 API 返回仍有效。UPDATELAYEREDWINDOWINFO 的 dirty 参数从 Vista 起有正式 API 合同，不能把 dirty ULW 泛称为 Win8 才支持。

22:48 日志两个角色首次 success 都是 full=1（`:31/:45`）；没有证据支持“从未先提交 full，因此只发 dirty 被 Win7 忽略”这个特定假设。USER32 之后的实际桌面可见性仍未被日志证明，但它不能独立解释本次 source=0。

### 7. 单独记录的潜在风险，不是当前透明读回定因

**7a. flip swapchain 替换后的 deferred destruction 合同**

`Renderer.cpp:357-360` 解绑 OM/VS/PS 并 Flush，随后才 reset backBufferRTV/backBufferTexture（`:362-363`）；`TransparentPresentation.cpp:789-791` 再释放整个 renderer 和 swapChain。没有在所有引用实际释放后再 Flush 的显式步骤。Microsoft Flush 文档要求替换同 HWND 的 flip swapchain 前完成旧对象销毁，推荐释放引用、解绑再 Flush。当前顺序有可能尚未确保这一合同。

可能症状是恢复/同 HWND 重新 CreateSwapChainForHwnd 失败，不能直接推导“成功创建的现有链持续 Copy 全零”。现场普通链创建成功；本次 stdout 中 `Recovered Draw3 graphics`、恢复/resize 失败以及 ULW event=failure 均为0。waitable create 本次直接失败，未建立待销毁旧链的正证据。因此该风险只记录给将来 recovery 专项，不在此处改动。

**7b. DIB 被并发尺寸变化重建，但本帧不自动升级 full**

Present 每次重新 GetClientRect；如果窗口 owner 恰在绘制线程消费 resize 后再次改变尺寸，`EnsureWindowDib :415-443` 可重建并重新填空 DIB。Present 没有使用“DIB 本次重建”的状态来强制 full，仍沿用调用者 presentFull，且 copy 范围取旧/新资源交集。

这条边界可导致一次新 DIB 的部分更新/窗外区域暂时丢失，等待下一轮 resize/full 请求；同时 `WM_SIZE/WM_WINDOWPOSCHANGED` 仍会发布请求。它不能解释已复制区域的源统计为0，也与本次相同1920×1080稳定采样不符。无需扩展本轮最小诊断包。

### 最小后续判别，保持今晚候选不变

先读取现有六项 Win7 自检结果：两个 Clear 把源资源/Copy/readback 与 draw 分开，两个正式 Cursor 与两个关闭混合对照把 draw/blend 分开。

如六项全通过，才考虑同款 shape=6、1920×1080、非原点 dirty CopySubresourceRegion、保留/解绑源 RTV，以及 flags=0 Map 对照。当前独立自检包含 OM 解绑、CopyResource 和显式 query/Flush，它的 PASS 不能覆盖上述运行期差异。纯 CPU 填色 DIB→ULW 的可见测试只有在 staging/DIB 已非零但画面仍不出现时才直接回答最后一步；它不应替代当前 GPU 分界。

### External References（2026-10-04，PRIMARY Microsoft）

- [CopySubresourceRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion)：异步、边界、资源类型/格式、未 mapped 与 FL9_x 特定限制；无源 RTV 必须解绑的条款。本文关于 Copy 基本合同和判别限制依据该页。
- [Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map) 与 [D3D11_MAP_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_map_flag)：CPU 访问和 GPU 互斥、DO_NOT_WAIT 与 busy 错误；生产使用 flags=0。
- [D3D11_USAGE](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_usage)：STAGING 支持 GPU/CPU copy，不用于普通 render binding；本次创建描述符合该用途。
- [Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)：Map 时 runtime 自动提交、Flush 异步、flip swapchain 替换的 deferred destruction 要求；本文第3和7a节依据该页。
- [CreateDIBSection](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection) 与 [CreateCompatibleDC](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatibledc)：CPU bits、bitmap/DC 释放与线程寿命、GDI 绘制后同步要求。
- [UPDATELAYEREDWINDOWINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-updatelayeredwindowinfo)：source DC、dst/size、blend/dirty 参数及 minimum client Vista。直接打开 UpdateLayeredWindowIndirect 方法页两次失败，未用失败页作来源；该结构页可独立支持本文参数合同。

### Related Specs

已读取 `.trellis/workflow.md`、本任务 `prd.md/design.md/implement.md` 和既有 `research/gpu-pipeline-narrow-20261004.md`。相关合同来自 `.trellis/spec/native/{index,runtime-and-rendering,platform-and-resources}.md`、`.trellis/spec/native-desktop/{index,draw3-integration,errors-logging-and-resources,input-and-ink,draw3-shaders}.md`、`.trellis/spec/shaders/{index,cpu-gpu-contracts}.md`。维持独立 renderer/context、绘制 owner、premultiplied top-down DIB、CPU alpha=1 仅用于 Primary、普通 FLIP_SEQUENTIAL、Win7 实测不由 Win11 PASS 替代。

## Caveats / Not Found

- 本分工仅写此 research 文件；没有改产品、spec、脚本、G: 现场、诊断候选，未构建、启动 GUI 或执行任何 Git 操作。
- 没有读取实现/检查角色专用 jsonl；native hook 已给出当前任务和 research context，task 自身没有独立 research.jsonl。
- 未发现可证明唯一根因。将 runtime Copy 丢弃、WARP/shader/blend 失败列为可检验区间，不等于这些情况已发生。
- 首次 full ULW、无 recovery/resize 失败只按本次22:48 stdout统计；不能当作所有版本/机器都没有生命周期问题的证明。
- 当前缺失仍是 Win7 已交付六项分界自检结果；不以今天静态调查宣布修复，不要求在今晚之前重复实机或增加未经证据支持的生产变化。
