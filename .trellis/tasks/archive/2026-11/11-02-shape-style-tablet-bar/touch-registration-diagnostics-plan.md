# Touch注册与Pointer来源诊断补齐（用户已授权）

日期：2026-11-03。用户要求先补调试输出，再做下一轮设备测试。当前目标是区分窗口注册失败/注册状态变化与数位板笔输入未走Touch兼容通道；不改变当前五窗Touch/Pointer/Mouse业务、过滤、捕获、租约或haptics。

## 最小改动边界

复用Experimental.Inkeys3.ConsoleOutput.Haptics（测试触控反馈，重启生效）和UiPointerDiagnostics/DiagnosticOutput。预计仅UiPointerDiagnostics.h、IdtMain.cpp的原注册接点、原Raw诊断接点（必要才动Bar/Page）及ui_pointer_tests.cpp。保持原BOM/CRLF与现有未提交修复，不动Hook/Draw3输入、布局、项目配置、驱动设置；不commit/push，不GUI。

## 输出合同

1. 五个UI窗注册时：与原来一样恰好一次RegisterTouchWindow(hwnd,0)，成功与失败均不改变后续流程、不重试。输出role/HWND、实际BOOL结果、失败GLE（成功明确0且标识适用性）、请求flags、调用tid、窗口owner tid/pid、IsWindow/可见/可用以及IsTouchWindow实际读回值与flags。诊断查询/输出保持调用后原GetLastError，不把成功后的旧GLE当失败。
2. 每个窗口真实WM_POINTERDOWN进入WndProc时，复查IsTouchWindow与窗口归属状态，和启动注册记录关联；不受触觉API/控制器/设备是否支持震动影响。所有Down可见，Move仍沿现有有界限频；Bar的channel_callback与WndProc不重复额外查询/输出此快照。不要新增业务状态或将“已注册”误标成“已收到Touch”。
3. Pointer来源诊断：在原生窗口线程当次消息内动态查询GetPointerInfo（兼容可选API），记录API是否存在、BOOL、仅失败GLE、原始pointer id/type/flags、sourceDevice、hwndTarget、系统时间与两套screen坐标；失败不猜Mouse。来源不从兼容extra低位反推。能以小范围可选API取得device类型/厂商信息则明确其来源，不能据此声称读取了Windows Ink驱动开关。本轮最低需要源设备句柄与Pointer目标，避免无边界扩展设备枚举框架。
4. 诊断关闭时不新增窗口/设备查询、分配或格式化，RegisterTouchWindow原调用仍必执行；启动已在开关初始化之后。无Send/Post输入、无额外捕获、无改变默认处理、不新增Win8+硬导入。非拥有线程/查询失败/可选API缺失都只记录，不影响UI。
5. 继续v1兼容基础字段，可增加明确stage/reason及reg结果/快照/pointer详情字段。控制每行长度，必要分为registration/window snapshot/pointer三条，禁止关键字段被1536行缓冲截断。不要用device=0推断TOUCHINPUT.hSource。

## 验证与交付

- 在现有headless UiPointer诊断测试中验证真实helper：关闭时查询/输出均不发生、成功不误报旧GLE、查询失败/可选API缺失字段、原GLE保留、Pointer消息身份/来源，不创建HWND或GUI，不复制诊断算法。
- 独立trellis-check核注册恰好一次/原流程不变、锁与线程、错误码、开关off路径与optional API、测试和编码。若可测接缝只需header原纯helper，不增加无关通用框架。
- 完整InkeysRepo.sln Debug|ARM64，ARM64原生MSBuild、同PowerShell单一Path、900s；五个早退生产探针与headless --no-window 900s。源冻结后Release|x64同solution构建1800s，核PE/新诊断标识/hash。全部无GUI。
- 给用户同一CMD重定向方法，使用M6-touch-registration.txt避免覆盖旧日志；记录这版仍包含此前五窗Touch修复，Pointer-only业务缺口仍未修。将来只在同设备同驱动设置基础上采集注册+Pointer证据；需切Windows Ink时分开两文件并重启驱动/程序，当前不要求无环境用户立刻测试。
