# 第一阶段代码与 API 证据

日期：2026-11-06。基线：42f4b9a8，分支 bugfix/hand-erase。仅静态调查；未运行产品、GUI 或硬件采样。

## 1. 生产调用链与修改边界

以下短文件名均位于 Inkeys/Inkeys/Drawing/Draw3/。

| 锚点 | 当前事实 | 规划影响 |
| --- | --- | --- |
| Draw3.RealtimeStylus.cpp:508 | positionScaleX/Y 与 contactScaleX/Y 同时缓存 | 后者是 API 的 ink→digitizer，不是接触宽高直接乘的像素倍率 |
| Draw3.RealtimeStylus.cpp:526 | PrepareAreaTransforms 仅传 positionScale | 面积函数需要当前 context 的逆变换参数 |
| Draw3.RealtimeStylus.cpp:730 | PublishStaged 同代发布 decoder 和面积变换 | 保留缓存、writer/reader gate、generation 合同 |
| Draw3.RealtimeStylus.cpp:916 | GetPacketDescriptionData 查询属性和当前设备 scale | 查询发生在 decoder 建立路径，不增加逐包 COM 查询 |
| Draw3.RealtimeStylus.cpp:977 | X/Y 乘 positionScale，面积输出 contactSize/rawContactSize/contactAreaUnits | 坐标和长度进入变换前的空间不同 |
| Draw3.RealtimeStylus.cpp:1538 | UpdateMapping 清理旧接触并重建 decoder | 新映射接入既有生命周期，不绕过 callback lease |
| Draw3.RealtimeStylus.cpp:1744、1762、1796、1810、1829 | 查询首项、启用发布、全量重建、增量恢复均涉及共享倍率 | 不能只改一个查询点，遗漏恢复路径 |
| Draw3.RealtimeStylus.cpp:2406 | RtsSharedScaleCompatibilityForTesting 同时断言位置和旧面积 | 拆开验证两个单位契约 |
| Draw3.RealtimeStylus.cpp:2943、2972 | 诊断仍描述 sharedPositionScale 和未补逆转换的路径 | 诊断含义必须同步修正，保持有界缓存和异步/限频输出 |
| Draw3.SpeedEraser.cpp:501 | spanToAxis=Rx/Rw×Uw/Ux；spanToCanvas=spanToAxis×abs(S) | 接口/实现/所有调用者及错误状态要一并检查 |
| Draw3.PalmEraser.cpp:61、109 | W×H 判断阈值及增长，D=2sqrt(area/π) | 定尺改为 sqrt(W×H)，是否改识别指标由用户决定 |
| Draw3.Host.cpp:3039 | WM_WINDOWPOSCHANGED/WM_DPICHANGED/WM_DISPLAYCHANGE 标记 displayScaleDirty | 这只证明显示标尺更新入口，不能等同 RTS decoder 已重建 |
| Draw3.WindowControl.cpp:2029 | WM_DPICHANGED 请求重建呈现 | 需追踪 HWND/DPI 变化如何有序更新输入映射 |

只改当前生产链。Win32Mouse 路径收到的客户端像素不应再套 RTS ink→pixel 变换；历史 IdtRts 不在当前产品输入主链。

## 2. 本轮核对的官方依据

1. [IRealTimeStylus::GetPacketDescriptionData](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-irealtimestylus-getpacketdescriptiondata)：pfInkToDeviceScaleX/Y 是 ink space 到 digitizer 坐标的倍率。示例读取首项只是查询示例，不保证首项是鼠标或目标画布像素映射。
2. [PROPERTY_METRICS](https://learn.microsoft.com/en-us/windows/win32/api/tpcshrd/ns-tpcshrd-property_metrics)：Units 表示属性物理单位，resolution 表示每单位计数；逻辑范围为设备报告值。
3. [RealTimeStylus Plug-in Sample](https://learn.microsoft.com/en-us/windows/win32/tablet/realtimestylus-plug-in-sample)：RTS 包位置按 ink space/HIMETRIC 解释，并换算到像素。
4. [IInkRenderer::InkSpaceToPixel](https://learn.microsoft.com/en-us/windows/win32/api/msinkaut/nf-msinkaut-iinkrenderer-inkspacetopixel)：用指定 HDC 将 ink 位置转换到像素；会涉及 renderer object/view transform，最早支持 XP Tablet PC。用于坐标转换的独立 identity renderer 不要求替换 Draw3 D3D 渲染器。API 的最低版本不等于任意机器 COM 激活必定成功。
5. [IStylusPlugin::UpdateMapping](https://learn.microsoft.com/en-us/windows/win32/api/rtscom/nf-rtscom-istylusplugin-updatemapping)：显示 DPI/方向变化通知，文档明确关联 WM_DISPLAYCHANGE；不能据此假定所有跨屏/逐显示器 DPI 变化都自动覆盖。
6. [GetDeviceCaps](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdevicecaps)：LOGPIXELSX/Y 在多显示器环境具有全局语义，不能机械替换为 UI 的逐显示器 DPI。
7. [WPF WispStylusTouchDevice](https://source.dot.net/PresentationCore/System/Windows/Input/Stylus/Wisp/WispStylusTouchDevice.cs.html)：宽高描述接触包围框；此实现按属性分辨率/单位换算，且注释明确固定 96 DPI 的历史行为。可证明包围框模型，不能直接复制其 DPI 规则到 Draw3。

用户引用的固定提交 CustomRenderer.cpp 本轮 Web 抓取失败；不把它标记为本轮已复核证据。上面的 Microsoft Learn 文档已经提供主要坐标契约。未采纳专利作为决定性 API 证据。

## 3. 换算关系和测试不变量

设 S 为目标窗口 ink→canvas 像素比例，C 为当前 context ink→digitizer 比例：

```text
positionPx = inkPosition × S
spanToDigitizer = axisResolution / spanResolution × spanUnitCm / axisUnitCm
spanPx = rawSpan × spanToDigitizer × abs(S / C)
```

这是由前述 API 方向和当前 spanToAxis 定义推出的空间组合。当前代码把 digitizer 长度直接传给要求 ink 长度的 S，遗漏除 C；不是再乘 C，也不能把除 C 套到已是 ink 的 X/Y 上。

- 以同一目标映射为前提，同时把轴原生计数的分辨率与 C 放大 k 倍，正确输出必须不变。构造与物理单位自洽的 fixture，不只按代码公式抄预期。
- 固定现有测试数据 S=(0.25,0.5)、C=(2,3)、packet=(40,20,5,4)，位置仍为(10,10)，长度为(0.625,2/3)。位置与长度分别断言。
- 当 0<C<1 时，补逆变换可能让尺寸变大；本任务不是统一缩小系数。Surface 回归需要实测，不能因为过去观感正常就固定旧数值。
- position 的平移不进入长度；不减 WIDTH/HEIGHT 的 logicalMin。不得用屏幕边界裁剪位置掩盖转换错误。

## 4. 技术方案收敛（第二轮规划）

官方main分支原生RTS示例现已成功读取，结合Learn浮点公式，决定采用目标客户端HDC的LOGPIXELSX/Y除2540作为缓存位置比例；不新增IInkRenderer对象和整数基向量近似。资源、DPI上下文、映射更新、故障边界及生产probe已在design.md明确，补充官方依据见research/mapping-design-evidence.md。初次固定提交抓取失败的事实保留，不冒充已经读取该固定版本。

技术问题由源码/API证据收敛；实机结果独立作为回归证据，不让用户选择乘除方向，也不以缺少异常机日志停止正式修复。

## 5. 历史证据与现有合同

- .trellis/tasks/archive/2026-08/draw3-source/active/07-18-rts-multicontact-rendering/research/first-device-test-failures.md:12：Touch 原始(24175,19508)经当前 context 倍率成(17378,14022)，曾超出画布。其首 context 方案是历史兼容措施。
- .trellis/tasks/archive/2026-08/draw3-source/archive/2026-08/08-07-rts-packet-decoder-hot-path/design.md:64：保留 GetAllTabletContextIds 首项，不混同 Enabled 回调数组顺序。新方案替代的是倍率来源，不能破坏代际/线程合同。
- .trellis/tasks/archive/2026-09/09-12-speed-eraser-physical-scale/validation-contact-area-metadata-20260914.md:34：Surface 原生范围27388×18258，C约0.718845/0.718819；隐藏窗口 S约0.0377953。文档明确没有真实手指包与 monitor 关联，仅算术回放，不能作为正常窗口标定。
- .trellis/spec/native-desktop/input-and-ink.md:295：旧面积公式和冻结位置规则需要在实现完成后定向替换。
- .trellis/spec/native-desktop/palm-eraser.md：矩形等面积圆合同需同步更新；用户已选择A，识别/设置预览口径保持；实现时再定向更新公共规范。
- 用户提供的完整 Q1/A1、Q2/A2、Q3/A3 是研究背景，不把历史回答中的确定性措辞升级成实机保证。

## 6. 验证入口及缺口

- 主产品构建：原生 ARM64 MSBuild，InkeysRepo.sln Debug|ARM64，至少允许5分钟；同一 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue，再设置 MSBUILDDISABLENODEREUSE=1。不单独构建 Inkeys.vcxproj，不硬编码 VS 版本。
- InkeysHeadlessTests/speed_eraser_tests.cpp:1796 起：实际生产长度转换测试，补充倍率/计数重编码、元数据和异常边界。
- InkeysHeadlessTests/palm_eraser_tests.cpp：实际生产 State、阈值和锁定测试。现有 --no-window --palm-eraser-only 可独立运行，完整 --no-window 覆盖 speed 相关测试。未发现 --speed-eraser-only，不能编造 flag。
- Inkeys.exe --draw3-palm-eraser-test：IdtMain.cpp:1897 的早退 CPU probe，不创建 HWND；验证实际 controller/helper 的尺寸消费与续接。
- 当前 RtsSharedScaleCompatibilityForTesting 的显式调用在 inkStrokeModelerTestTests/contact_input_tests.cpp:651；该项目引用独立 demo 工程，主产品 Headless 工程没有编译 RealtimeStylus。不得用独立 demo 测试代替产品 decoder 回归。设计阶段选择最小的生产无窗口 probe/共用纯 helper 测试接缝，并验证实际执行。
- 输入位置端到端、COM/HDC 转换、Surface/异常机、多显示器及 Win7 x86/x64 实机验收单列；默认不运行任何 GUI/Computer Use。Win7 导入检查不能只检查 ARM64，因为 Win7 不支持该目标。
- 本轮仅文档，不编译、不运行测试。完成任务工件链接、manifest、状态及 diff 范围检查即可。

## 7. 本轮环境记录

默认 python 命中了不可运行的 WindowsApps alias；已使用现有 C:/Program Files/KiCad/10.0/bin/python.exe 执行 Trellis，无安装或配置改动。git status 在 HiMsg 子模块权限处失败，改用 git status --short --ignore-submodules 检查主仓库；不声称检查过子模块内部脏状态。
