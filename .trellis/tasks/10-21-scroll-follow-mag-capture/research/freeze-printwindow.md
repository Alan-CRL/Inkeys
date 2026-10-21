# 原 Freeze Mag / PrintWindow 软件内验证

日期：2026-10-21。当前用户最终范围：直接共用原定格 Mag，移除独立捕获原型，先在真实软件内验证最小读回能力；普通 Freeze 线程设计不调整。本报告取代旧独立 service/lease PoC 的当前结论，旧日志只保留为历史。

## 结果先说

**不注册 scaling callback 也能获取正确画面，callback 只是可选的提前尝试信号。** 实际的 `Inkeys.exe --freeze-mag-printwindow-test` 已经从原 Freeze 窗口读到了正确图像。两个全新进程从未不透明显示过的原隐藏窗口对开始，经过有限预热后，7 个不同图案阶段和静止重复图像均通过。蓝色遮挡窗口在桌面上确实盖住图案，但原 Freeze 排除逻辑后的读回图像仍是图案。原定格在读回过程中两次打开、关闭也成功。

无回调验证覆盖每轮最初的阶段 1–3（阶段 1 连续 3 次静止读回）和恢复 nullptr 后的阶段 7；每轮共 6 个无回调接受样本均 63/63 匹配，保存的阶段 1–3、7 PNG 逐像素也全部一致。首次等待和有效性检查本身已足以在本机取得画面，不依赖 callback 才能完成；首个不完整样本可有限重试，允许延迟。

`MagSetImageScalingCallback` 不是只登记成功：两轮各真实进入、退出 6 次，而且回调返回后的 PrintWindow 图像对应当前图案。DWM 的只读查询为开启；测试没有改变其状态。这是本机实测事实，不能把 6 次图案读回外推为所有场景的整帧完成保证。

## 本轮代码与范围

- 删除旧 `Graphics/MagCaptureService.h/.cpp`、`MagnificationRuntime.h/.cpp`、`InkeysHeadlessTests/mag_capture_tests.cpp` 和项目、filters、runner 登记，约 1,940 行独立原型被移除。
- `IdtMagnification.cpp` 的运行库寿命恢复 `1b8cb4b6` 原来的 `MagInitialize / MagUninitialize`；未完成的 Window 销毁接线和 Coordinator timed-wait 回到 HEAD，二者没有最终 diff。
- 保留 WindowService 的 `RunGroup(STA) → PrepareMagnifierWindow → Host/Child Created → readyPromise`。没有另建 Mag owner、第二套 Mag 窗口、runtime lease、异步捕获 service 或普通 Freeze owner RPC。
- 新增严格早退且由 `INKEYS_ENABLE_UI3_PRESENTATION_HARNESS` 控制的软件内入口；参数错误返回 2，harness 关闭也返回 2。入口先于配置、单实例、注册表和普通业务初始化；fixture 使用 null logger 并在结束后恢复旧 logger，没有调用真实配置写入、PPT、更新或自启路径。
- 新增的 Host 定时消息执行小型 Prepare/Filter/Source/Invalidate/Redraw/PrintWindow 读回；首帧采用 800ms 策略，随后 100ms；callback 退出计数变化可提前尝试读回。等待期间 owner 正常泵消息，没有在其线程 Sleep。阶段最多 5 秒，测试进程另有 45 秒最终截止；本次未触发强制退出。
- 没有新增 Tracker、Draw3、Bar 产品功能、Viewport、WGC 或禁止捕获属性，也没有 commit/push。

## 软件内路径和测试差异

fixture 通过真实进程级 WindowService 创建原 `MagnifierHost / WC_MAGNIFIER`，直接安装真实 `PrepareMagnifierWindow / MagnifierHostCreated / MagnifierChildCreated / ShutdownMagnifierWindow` 回调，使用真实 `Win32MagnifierOperations`。这不是另一个复制 Mag 初始化的独立程序。

原 Host 样式仍为 `WS_POPUP | WS_CLIPCHILDREN`，扩展样式为 layered/noactivate/toolwindow；Child 仍为 `WS_CHILD | WS_VISIBLE | MS_CLIPAROUNDCURSOR`。Host 使用原 DefWindowProc，不与 SOURCE 共用图案绘制过程。透明读回期间 alpha=0，但 Host/Child 都保持可绘制、可见生命周期；普通 Freeze Reveal 时 alpha=255，Conceal 时隐藏。

最小 WindowSpecs 只含原 Mag 窗口对和专用蓝色 `Bar` role 窗口；普通软件的 Drawpad/PPT/Setting 等窗口未创建。SOURCE 是另一个专用、已知、同进程非角色顶层窗口，由测试主线程绘制。它刻意不在原十角色排除表中，蓝色 Bar role 则在表中。本次真实过滤表只有两个有效项（Host、Bar），没有声称 GUI 实测覆盖完整普通软件所有角色。既有无窗口 Coordinator/filter tests 随 `--no-window` 回归。

蓝窗覆盖源图案中央三分之一区域；先用屏幕 GetPixel 确认中心真为蓝色，再调用原 SubmitFilter。63 个分布采样点包括覆盖区内的 9 个点，因此成功匹配源图案也验证了实际排除，不能仅靠 API BOOL 宣称排除成功。

继承的几何被明确记录：主屏/source fixture 为 2880×1920；Child client 和 DIB 为 **2880×1919**；原 Mag source 为 **(0,0,2879,1919)**。没有把它伪报成完整 2880×1920 主屏，也未在本任务顺手更改原避全屏尺寸规则。

## 两轮最终冷启动结果

最终源码对应的两个新进程，均先在 alpha0 hidden pair 上完成阶段 1–3；随后运行原 Freeze 的显示/隐藏，再登记 callback 运行 4–6；恢复旧 callback 后运行 7，再运行原 Freeze 显示/隐藏。第一次不透明 Reveal 发生在阶段 3 已读回之后。

| 项目 | run2 / PID 73216 | run3 / PID 81464 |
| --- | --- | --- |
| 进程退出 | 0 / PASS | 0 / PASS |
| 外部计时总耗时 | 6,221ms | 4,527ms |
| 原 HWND owner TID | 28916 | 60852 |
| first read 之前 opaque Reveal | 无 | 无 |
| 原始 PrintWindow 样本 | 10 | 9 |
| 完整当前图案 63/63 的接受样本 | 9 | 9 |
| 首次未完整匹配 | 62/63，拒绝并预热重试一次 | 无 |
| 不同阶段 / 同图静止接受 | 7 / 3 | 7 / 3 |
| 允许的旧阶段样本 | 0 | 0 |
| callback 登记 / Get 核对 | TRUE / 匹配 | TRUE / 匹配 |
| callback 进入 / 退出 / 活动 | 6 / 6 / 0 | 6 / 6 / 0 |
| callback TID / 直通失败 | 28916 / 0 | 60852 / 0 |
| 恢复旧 callback | nullptr，读回核对一致 | nullptr，读回核对一致 |
| 原 Freeze Reveal/Conceal | 2/2 成功 | 2/2 成功 |
| 原窗口对最终销毁 / Uninitialize | 是 / TRUE，同 owner | 是 / TRUE，同 owner |
| GDI / USER 计数 | 11→13 / 4→5 | 11→13 / 4→5 |

run2 首个 62/63 的样本虽有图像信息，仍未被当成有效初始图案，也未保存为图案 PNG。后续有限重试得到完整匹配；没有隐藏这个失败样本。总计 18 个被接受的样本通过 1,134 个分布点，另有 1 个被拒绝的启动样本。

每轮保存 7 张阶段 PNG（静止重复只保存最终一张）。对这 14 张最终 PNG 另外用 Pillow 按实际 9×7 tile 边界逐块核对 RGB 直方图；每张 **5,526,720 / 5,526,720** 像素与对应阶段公式严格相同，14 张均 mismatch=0。像素范围是实际 2880×1919 DIB；alpha 仅在保存 PNG 前设为 255。此离线核对不包含未保存的两个静止重复样本和被拒绝的启动样本。

原普通 Freeze 更新仍由既有 MagnifierThread 执行；日志中的 Filter/Source/Redraw TID 与 owner 不同，这属于保留的原设计，API 和显示/隐藏实测均成功。新增的更新/PrintWindow、callback 注册/恢复、Initialize/Created/transform/color/Uninitialize 则实际发生在该原 HWND owner。没有把生产线程结构改成此次诊断的定时器结构。

## 延时和 callback 的具体证据

计时来自 GetTickCount64，约有 15.6ms 量化，不能当作精密性能基准。

| 读回阶段 | run2：更新后等待 / PW 耗时（ms） | run3：更新后等待 / PW 耗时（ms） |
| --- | --- | --- |
| 首次采样 | 875 / 47，62/63，拒绝 | 875 / 47，63/63 |
| run2 首帧重试 | 844 / 31，63/63 | 不需要 |
| callback 阶段 4 | 31 / 63，63/63 | 31 / 31，63/63 |
| callback 阶段 5 | 31 / 62，63/63 | 31 / 47，63/63 |
| callback 阶段 6 | 31 / 31，63/63 | 31 / 63，63/63 |
| callback 恢复后阶段 7 | 156 / 47，63/63 | 156 / 47，63/63 |
| 全部 PW mean / min / max | 46.9 / 31 / 63 | 47.0 / 31 / 63 |

回调只按相同格式、尺寸和 stride 安全直通系统要求的目标缓冲并做原子计数，不阻塞、不日志、不留存借用图像。对外验证的数据来自 `PrintWindow(child, dc, 2)`，没有拿 callback 图像作为捕获输出。记录进入/退出与回调线程，并在真正退出后才把其当作提前尝试读回的候选信号；后续仍核对真实图案。

两轮登记期间的 6 个阶段读回都在 callback 退出计数增加后取得正确图案，恢复旧 callback 后计数均保持 6/6，普通定格仍成功。但官方说明 callback 针对 GDI 合成前的 bitmap，返回后还可能有变换，因此这个有限样本不能证明“一个 callback 退出就代表整个最终图像完成”。若没有 callback 或它不触发，仍按定时等待和有效性检查回退；登记失败但旧值未改变也不会当作必须依赖的能力。

微软文档：[MagSetImageScalingCallback](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nf-magnification-magsetimagescalingcallback)、[MagImageScalingCallback](https://learn.microsoft.com/en-us/windows/win32/api/magnification/nc-magnification-magimagescalingcallback)。文档标注其已废弃且仅在 DWM 关闭时工作；本机 `dwm_enabled=1` 的同时真实发生 6 次回调，报告保留这一实测差异，不据此扩大支持承诺，也未关闭 DWM。

## 构建、回归和原始证据

- 采用 vswhere 定位当前 VS，ARM64 原生 MSBuild；同一 PowerShell 规范化单一 Path，设置 `MSBUILDDISABLENODEREUSE=1`，完整 `InkeysRepo.sln /m:1 /nr:false /p:BuildInParallel=false /p:Configuration=Debug /p:Platform=ARM64`。正常宿主 require_escalated 执行，没有删除 PDB 或修改工具链/依赖来规避旧 sandbox C1902。
- 首次完整 solution：exit 0，40.88 秒，62 个既有 warning，0 error；最终源码完整 solution 增量：exit 0，19.09 秒，3 个既有 hashlib++ C4267，0 error。本次 IdtMagnification 没有新增编译 warning。
- 重建后的 `InkeysHeadlessTests.exe --no-window`：exit 0，末尾 `PASS animation correctness`；包含既有 Freeze/Coordinator 回归。`TEMP/TMP` 使用仓库内 `Build/ScrollFollow/TestTemp`。
- `Scripts/i18n.ps1 check`：exit 0，en-US/zh-TW 均 425/425 PASS。
- `--freeze-mag-printwindow-test --unexpected`：exit 2，stdout/stderr 空，未创建 fixture。
- `git diff --check --ignore-submodules=all`、七个修改源/工程文件的 BOM+CRLF、三份项目 XML 解析通过；普通 Window.cpp 和 Coordinator 无最终修改。
- ARM64 dumpbin imports：原 Magnification/PrintWindow 存在；callback 两 API 动态解析，未硬导入；无 WGC / OpenCV 动态 DLL 新依赖。最终 EXE SHA-256：`E616D3D903D9F9535AE6C6C786B9EE6D8E13C304171062DDBEC2C2DBB1407F07`。

原始日志与图案均在未跟踪的 Build 目录：

- `Build/ScrollFollow/freeze-printwindow-final-build-arm64.log` / `-console.log`。
- `Build/ScrollFollow/freeze-printwindow-run2.log` / `-run3.log` 及同名 stderr；进程 PID 73216 / 81464。
- `Build/ScrollFollow/TestTemp/FreezeMagPrintWindow-73216/` 和 `...-81464/`：result.log、phase-1.png 至 phase-7.png。
- `Build/ScrollFollow/freeze-printwindow-offline-pixels.json`：14 张 PNG 的逐像素核对计数。
- `Build/ScrollFollow/freeze-printwindow-headless.log`、`freeze-printwindow-i18n.log`、`freeze-printwindow-imports.log`。
- `freeze-printwindow-run1.log` / PID 44812 是早期预热顺序：先 opaque Freeze 再 PW，9/9 接受样本通过，但它不能作为冷 hidden pair 的证据；最终结论以上面 run2/run3 为准。

## 清理与尚未验证的边界

正常软件退出顺序保持 Coordinator Stop → MagnifierThread join → WindowService StopAndJoin，实际返回后原 child/host 已销毁，MagUninitialize 在原 owner 返回 TRUE。两个专用测试进程均正常 exit 0，最终截止/父进程 kill 均未触发；logger 被恢复，专用窗口随正常清理和进程退出结束。GDI +2、USER +1 的计数变化如实保留，本轮没有证明长期资源零增长或持续捕获有界，不能把进程退出当作长期泄漏测试。

只验证当前 Win11 ARM64 环境和受控 GDI 图案。未运行其他架构、Win7/8.1、真实浏览器/Word/PDF/视频、实际笔/触摸、用户桌面业务或 Computer Use。未建立连续 20FPS 服务或跨显示变化协议；PNG 保存和阶段定时也使该测试不构成采样帧率基准。正式 Tracker/Draw3/Bar 接入仍未进行。本次可确认的是原 Freeze Mag 的最小软件内 PrintWindow 读回、实际角色排除、有限预热、原定格前后正常，以及 callback 在本机的有限正向证据。
