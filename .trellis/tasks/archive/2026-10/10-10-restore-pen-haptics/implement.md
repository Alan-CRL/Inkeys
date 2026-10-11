# 实施与验证计划

## 开始门
- [x] 用户授权创建新任务，当前 bugfix/vibration 分支已有，无需新建 worktree/branch。
- [x] Git 历史、生产调用链、后端兼容与现有输入边界调查。
- [x] 完成 research/bar-haptic-path.md，核对严格关联/token与既有标记合同；跨驱动关联不被文档保证，失败仅跳过触觉。
- [x] PRD最终收敛通读；design/implement与jsonl上下文验证。
- [x] 2026-10-11 用户在最终方案之后回复“是的，批注并实现”，随后回复“继续”；批准实施并在关键逻辑添加中文注释。

## 执行顺序
1. trellis-implement 子代理加载两份research、prd/design/implement和 implement.jsonl；按 trellis-before-dev 读取目标规范。所有代理保持最小diff、原编码/换行、不改他人文件，不commit/push，不开GUI。
2. Host绘制线程创建/初始化/注入/清理 PenHapticFeedback；可选初始化失败不影响启动。生产后端精确绑定当前Pen，禁用后台GetForCurrentThread泛设备回退；保留现有系统库安全动态加载、波形回退与默认强度。
3. 增加最小 detach/重置过期身份及预设反馈状态收敛；Controller在选择/隐藏/不准入/Laser/Pan/退出相关边界停止，不改变RTS/光标事件所有权。
4. Bar只被动观察 Pen WM_POINTER，沿当前 WM_TOUCH 翻译保留按接触生成的有界token，保持触摸/取消/屏幕坐标与HiMsg去重语义。工具/类型按钮的既有接受点发布一次Click。请求走专用产品入口/Host控制mailbox，不转发为画布消息，不挤占画布/保存命令队列。不能唯一关联或已过期则只省略触觉。
5. 在现有headless测试入口补充生产实际策略/必要的窄fake seam回归，确保旧实现能暴露本次缺陷；不复制实现到测试，不增加新的构建系统。
6. 运行最小充分验证和diff/编码检查，随后 trellis-check 子代理核对产品调用链/失败路径/线程/光标/测试，并只修本任务缺陷。
7. 主会话按 trellis-update-spec 保存适量触觉迁移及来源/lifecycle合同；按trellis-finish-work记录任务状态与验证。用户未授权commit，跳过提交，不push；真实设备未验证部分明确保留。

## 预期修改边界
- 必需生产：Draw3.Host.cpp，Draw3.HapticFeedback.cpp/.cppm；Controller触觉状态/停止逻辑，以及WindowController触觉mailbox的必要边界。
- UI接线：Bar.Interaction.cpp、Bar.Button.cpp有效业务接受点与其私有状态/marker helper，Draw3.Product.h/.cpp、Draw3.Host.h/.cpp专用触觉请求API；如需helper，仅加入局部、生产实际使用的身份/token策略，不改HiMsg第三方定义。
- headless：优先扩展现有draw3_bridge_tests/draw3_contact_tests；若必须编译实际haptic module或独立小测试源，仅更新已有InkeysHeadlessTests.vcxproj登记，不改SDK/依赖/solution结构。
- 不涉及墨迹算法/HLSL、UInk、用户配置、PptCOM接口或第三方源码。

## 关键回归矩阵
| 场景 | 预期 |
| --- | --- |
| Pen/HardPen/Shape、Highlighter、Eraser、倒转笔尾 | 使用原波形映射，缺可选波形走基础回退 |
| Laser、Mouse/Touch绘制 | 无连续笔震动 |
| Hover预设后Selection/隐藏/Laser/Pan/不准入 | 显式停止与旧request失效，现有cursor策略不变 |
| 新不支持的笔/同数字Pointer ID新代际 | 不复用旧controller/失败缓存；重新有效绑定或no-op |
| 有效Pen工具/类型点击 | 每个被接受操作最多一次Click，动作次数不变 |
| 取消、拖出、程序刷新、Mouse/Touch、兼容鼠标副本 | 不发送Click |
| token/请求满、超时、身份歧义、Host停止/重启 | 只丢可选触觉；原UI/保存/绘制不中断 |
| 缺DLL/导出/factory/property/controller/基础波形、发送失败 | false/no-op，不抛出到Host启动或输入链 |
| Run异常/启动失败/正常停止 | 同线程Stop/Shutdown、COM初始化配对，无旧请求跨Host代际 |

## 验证入口
本机实际构建系统为 Visual Studio/MSBuild，生产验证按用户规则使用完整 InkeysRepo.sln Debug|ARM64；已通过vswhere定位到ARM64原生MSBuild，无需硬编码版本到项目。

在同一个PowerShell调用中：
~~~powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
$vsLocator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nativeMsbuild = & $vsLocator -latest -products '*' -find 'MSBuild\Current\Bin\arm64\MSBuild.exe' | Select-Object -First 1
& $nativeMsbuild InkeysRepo.sln /m:1 /p:Configuration=Debug /p:Platform=ARM64 /nr:false
~~~

构建总等待至少5分钟，长时间无输出不重复重启；检查真实进程退出码和第一个有意义错误。环境失败独立报告，不污染项目配置/SDK/签名。构建后执行已有Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window（先核实实际输出路径），不启动Inkeys.exe主窗口或隐藏窗口harness。

补充：相关fake/生产策略回归、原输入光标过滤测试、git diff --check；检查既有文件BOM/CRLF没有变化，dumpbin /imports静态检查没有新增WinRT/新Pointer API加载时依赖。额外Win32/x64构建仅当本次平台相关变化或ARM64证据无法覆盖实际问题时按风险扩大，不把ARM64结果当Win7实测。

## 回滚与未自动验收项
按Host注入、Bar/token/API旁路和连续状态补丁逐块回退；无持久化迁移。实体笔体感、多笔交接、真实Win7/非震动硬件和混合设备光标仍需目标设备验收；本次授权只做无GUI验证。研究/编译/静态结果分别记录，不声称硬件反馈已实测。

## 完成记录（2026-10-11）
- [x] Host/后端/连续反馈与 UI3 已接受按钮触觉接线，关键逻辑中文注释。
- [x] 独立 trellis-check 全范围审阅并修复旧绑定、消息交错与 marker 有符号边界。
- [x] 完整 solution Debug|ARM64 编译退出 0；完整 --no-window 退出 0。
- [x] 静态 imports、git diff --check、15 既有源码 BOM/CRLF 对照通过。
- [x] 新增 native-desktop/pen-haptics.md 并更新索引，完整证据见 validation.md。
- [x] 按用户规则保留未提交改动；归档和日志均 --no-commit。
- [ ] 实体震感、真实 Win7、跨驱动关联与混合设备 GUI：本轮授权范围外，保留设备验收。
