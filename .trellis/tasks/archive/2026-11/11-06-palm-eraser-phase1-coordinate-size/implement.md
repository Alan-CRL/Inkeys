# 实施计划（2026-11-06 已批准）

## 0. 实施前门禁

- [x] 用户已选择 A，并于2026-11-06通过对话提问明确批准最终规划并开始实施。
- [x] 核对当前任务、branch、git diff；不覆盖其它会话或用户修改，不commit/push。
- [x] 加载native-desktop输入、palm、构建、C++/资源规范和本任务研究；原生子代理上下文失败时从manifest补读。
- [x] 记录拟修改文件的编码/BOM/CRLF；不修改历史demo、第三方依赖或无关配置。

## 1. 坐标映射与生产回归入口（R1/R5，AC1/AC4/AC6）

- [x] 在真实生产RTS模块实现目标HWND HDC→S查询，GetDC/ReleaseDC同线程，浮点dpi/2540，明确失败状态和可选动态DPI作用域。
- [x] 插件持有正确HWND，替换首context倍率来源；覆盖Enable、全量重建、增量恢复、InRange/Down补建和InAir光标。
- [x] 实施design.md的窗口失效revision→有界唤醒→取消/慢路径重建；不逐包COM，不在WndProc等输入writer，验证Shutdown交错。
- [x] 公共映射初始化失败经既有停产后Win32Mouse回退；运行期失败仅取消并等待恢复，不启动第二producer。
- [x] 新增生产早退CPU probe及CLI，复用真实helper/cache/decoder，不全局打开DRAW3_TESTING。未确认flag已编译进当前EXE之前禁止执行该新flag。
- [x] 回归矩阵：96/144/192及非整百分比HDC dpi、X/Y不同；设备自身C变化与context顺序变化位置不变；无效HDC/dpi、缺失可选API、映射构建期间再次失效、重复通知、设备增删、cancel/Up、关闭期间刷新。
- [x] 输出数值按浮点误差设置容差（通常1e-4px级；大坐标按float ULP检查），不用复制公式的测试替代设备计数不变量。

主要文件：Draw3.RealtimeStylus.cpp/.cppm、Draw3.Host.cpp、必要的Draw3.RtsStartup.h、Inkeys/IdtMain.cpp。只有真实需要时加入窄header helper，不创建通用映射框架或新工程。

## 2. 接触长度完整换算（R2/R6，AC2/AC4/AC5）

- [x] ResolveContactLengthTransform显式接收S和C；更新PrepareAreaTransforms及全部生产/测试调用，保留原单位和范围检查。
- [x] 关键中文注释解释digitizer→ink→canvas；只对长度使用abs(S/C)，位置只乘S。
- [x] 补充正常/边界/非法测试：C<1、=1、>1、轴与跨度不同单位、非零logicalMin、非有限/零C、转换溢出、缺失元数据与raw范围端点。
- [x] 等价设备计数重编码测试：同时改变axis resolution和C后尺寸相等；以已知display span构造完整轴跨度映射，不把任意未配对数据强行匹配屏幕。
- [x] 拆分旧RtsSharedScaleCompatibility的两条预期；以同样packet验证位置(10,10)、宽高(0.625,2/3)，生产probe实际执行。
- [x] 更正缓存诊断含义；验证调试、Palm与Speed面积辅助都获得同一contactSize，无额外DPI或UI倍数。

主要文件：Draw3.SpeedEraser.cpp/.h、Draw3.RealtimeStylus.cpp、InkeysHeadlessTests/speed_eraser_tests.cpp；必要时更新状态枚举的真实消费者。

## 3. 椭圆等面积圆（R3/R4，AC3/AC5）

- [x] State::Observe只将直径换成sqrt(W×H)；识别阈值、最大面积、窗口/锁定/续接逻辑不改。
- [x] 100×100→100，160×90→120；交换宽高/同比缩放一致。覆盖识别阈值正好相等、略低、略高以阻止误改成πWH/4。
- [x] 保留25cm²/10000系统DIP²及倍率、设置预览口径；无需i18n或设置页面写入。
- [x] 生产Palm probe验证光标/后续几何用同一新直径，旧点不回写；长停顿/续接/并行与邻域沿用既有策略。

主要文件：Draw3.PalmEraser.cpp、InkeysHeadlessTests/palm_eraser_tests.cpp、Draw3.DrawingController.cpp内实际需要更新的probe断言。

## 4. 验证命令与证据

先定位Visual Studio安装的ARM64原生MSBuild，不硬编码版本；优先vswhere，读取实际安装后选择MSBuild/Current/Bin/arm64/MSBuild.exe。在同一PowerShell invocation内：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $taskMsbuildPath InkeysRepo.sln /m /nr:false /p:Configuration=Debug /p:Platform=ARM64
$taskBuildExitCode = $LASTEXITCODE
```

- 完整solution构建至少允许5分钟，长任务用session继续等，不因几十秒无输出重启；记录退出码与第一个有意义错误。本机环境错误不通过改源码/依赖绕过。
- 编译完成后从当前输出目录验证本次构建产物，再运行：

```powershell
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window --palm-eraser-only
& .\Build\ARM64\Debug\InkeysHeadlessTests.exe --no-window
& .\Build\ARM64\Debug\Inkeys.exe --draw3-rts-coordinate-test
& .\Build\ARM64\Debug\Inkeys.exe --draw3-palm-eraser-test
```

- 新RTS flag只有实现并核实后才能使用，防止未知参数落入正常GUI。所有CPU probes检查真实退出码；必要的标准流重定向与错误对话框屏障沿用现有runDraw3SelfTest，不新建可见控制台。
- 源码/API调用与导入检查：新增GetWindowDpiAwarenessContext/SetThreadDpiAwarenessContext不得静态导入；x86/x64发布产物可得时用对应dumpbin /imports核对。若需新增跨架构构建，仅在兼容风险无法由现有证据排除时补充，不能把ARM64导入检查当成Win7实机。
- 独立记录未执行的Surface/异常设备、多显示器混合DPI、Win7 x86/x64真实运行；默认不启动GUI或Computer Use，不能以CPUprobe通过宣称硬件验收通过。
- git diff --ignore-submodules=all --check；检查统计和逐文件diff、编码/EOL。HiMsg内部权限限制如仍存在，单列未检查，不能宣称子模块干净。

## 5. 质量检查与合同同步

- [x] 按工作流由trellis-implement实现、trellis-check进行全范围审查；dispatch首行包含Active task，明确文件责任且不得撤销其他人的编辑，不递归派发implement/check。
- [x] 主会话通过trellis-update-spec定向更新input-and-ink的旧面积公式/首context语义和palm-eraser的定尺合同；保留历史记录原文。
- [x] 审查所有消费者/诊断/测试及.cppm签名一致，原有启动/停止/输入后端唯一性未变。
- [x] 验收报告区分编译、纯CPU、源码/导入和实机证据；只完成已授权范围，不commit/push，不因流程默认自行归档尚待实机验收的问题。

## 6. 风险与回到规划的触发条件

- owner/RTS窗口互等、映射revision发布次序或HDC坐标契约出现矛盾，先给出证据并修订设计；不靠屏幕裁剪或固定倍率补丁消除表现。
- 若需要更换输入后端、改变识别阈值/预览、引入额外最低系统要求或新的产品级失败行为，使用对话提问后再实施。
- 普通helper命名、窄测试接缝位置、无行为差异的资源守卫写法由实现依据项目规范确定，不额外询问。

## 2026-11-06 执行结果
实现、检查子代理、完整ARM64构建、定向及全量无窗口测试、可选API导入检查和规范同步均完成；详见validation.md。真实Win7/Surface/异常机/多屏验收未执行，任务暂不归档。没有提交或推送。
