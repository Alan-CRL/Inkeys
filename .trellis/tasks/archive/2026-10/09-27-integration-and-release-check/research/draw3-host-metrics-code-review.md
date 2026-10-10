# Draw3 U3-H Host metrics 独立源码 / 生命周期 / smoke safety 复审

Active task: `.trellis/tasks/09-27-integration-and-release-check`

日期：2026-10-01。仅写本报告；真实AGENTS/check/任务与native-desktop规范、已批准§7合同/设计、四源及调用者只读。未自修源码、build/run/Git/GUI/Computer Use/递归，其他writer的工作不被回退。UInk命名报告 `A10C0AE4020A310931D4B78E2B89AC98698E732E716E40C76A9F4D49590D3450` 保留，未扩大cleanup附录。

## 当前判决

**SCOPED_STATIC_GREEN / SAFETY_CLEAR_SPECIFIC_U3H_SMOKE。** 审查中发现一个晚Start异常的指标资格漏标，已由Root接管Host.cpp两处最小修补，实际新hash与调用链已核，见Findings。当前本单元无剩余must-fix。

本结论仅放行 Root 在全部依赖源码停写、新完整 `InkeysRepo.sln Debug|ARM64` Build0和适用core回归后，对新稳定私有root执行准确 `--draw3-host-metrics-smoke --output-root <absolute-root>`。该smoke创建四个自己PID的隐藏HWND，**不是严格no-window**。无本版本compiler/动态PASS，不授权F、普通主栏或其它GUI流程，不关闭16+200/phase/CPU/Move-Up/Laser/全链性能与恢复门。

## 当前实际身份

| 输入 | bytes / SHA256 |
| --- | --- |
| Draw3.Host.h | 12379 / `88F13F2A85E83CF71BC9AFC4F618B99BE26B0D1B1A795D1147398693163064A1` |
| Draw3.Host.cpp（Root晚异常修补后） | 99471 / `5B1D26939146C7ED05B2700EEA14E0E561D7DDF5792ED43A7066A71299C62642` |
| Draw3.HiddenWindowTest.h | 813 / `479F1FB68899A5060D1D8400D02B3A8C4E8AF7B99EA220C4F123C27F192BE350` |
| Draw3.HiddenWindowTest.cpp | 167062 / `F930AFB0A867CECCBD67D5D487EB0E0E7B2E9F7F2C4976E751430AFEC4702200` |
| Root Main 当前实码 | `00C13D99A4F73915F8375AD38FB14DB6B5AFBADAF971855D9826C2F191FE70C3` |
| implementation 当前含Root接管追加 | `74ED453FF04EDF6562DEEDB1D492ADFC4013C6239A66AEB9412B37819A67FA4E` |
| draw3-content-and-host-contract.md | `4F6839B450AAE10E298F34B28F8B54C4D77CC8AF9988B5AF6B4E0F67E171545F` |
| draw3-content-and-host-design-review.md | `4B15F39BE9C1ED21FBB7D826D613EE713FCD7A04369D5D0AB00731D27E2F2A19` |

四源UTF-8无BOM/CRLF、bareLF0；Main保原BOM/CRLF。初dispatch implementation986346…6234B/Host.cpp1F694…864F59是修补前身份，Main9F800…2EB78是旧prefix解析身份，不冒作当前通过。Root晚异常补丁仅在内存反向恢复两处，完整cpp精确还原 `1F6940648B0FB4FE956883EEF112400EC9DDD9BA3EBA41149365D432BA864F59`；未写该还原结果。没有Git diff运行或声称恢复未提供的整U3 preimage。

## Findings (fixed)：晚Subscribe异常不能导出失败Start

- **问题与真实证据：** 原Host.cpp1463–1464在drawing initialized时立即把startupFailed清false，而caller最后1600–1605仍调用非noexcept `Display::Subscribe`。真实 `Display.cpp:833` make_shared、`:840` vector push_back均可抛；该处抛出后调用方真Stop/Seal会得到joined/sealed且runFailed=false，原WriteRuntimeMetrics可将没有成功返回的Start导出为成功run。已及时报Root，未把静态异常路径称为动态复现。
- **Root最小修补：** worker `Host.cpp:1463–1464` 只在!initialized且enabled时置startupFailed=true；caller在实际Subscribe成功后 `:1606–1608` 才清false，随后return true。BeginRuntimeMetricsRun原默认failed覆盖所有新run false/throw；晚异常经Stop仍失败标记，导出拒绝。Display/Stop/C3/样本/flags未顺改，两处byte还原吻合上表原hash。
- **未动态覆盖：** 新晚Subscribe异常未注入，成功Subscribe新smoke也尚未运行。修补只在实码上闭合资格错误，不冒称已测分配失败或provider/GPU失败。

## Session / owner / 启停的真实链

| 阶段 | 实际符号与判定 |
| --- | --- |
| 排他Start与新run | `Host.cpp:1275–1278` 先拒running/任何attached/thread joinable，再Begin；因此不释放活Controller的借用。Begin263–299释放旧已停Session、清本run值，valid1..32768及未耗尽serial才产生新runSerial。0/32769/耗尽不钳population，只unavailable。旧report读完再下一Start是Header串行owner合同。 |
| defaultoff / 准备 | options尾默认enable=false、max32768。Begin关闭路径不构造Session；Prepare301–328门关闭即返，无新QPF/heap。显式开启才make_unique预分配，异常或共同预算不足关闭诊断并继续产品。 |
| 共同32MiB | Prepare309–320先预留64KiB Controller上限+Host RuntimeMetricsState+Session外壳；核Session实际allocatedBytes余量后才向Controller交指针。State静态<=4096，Controller实际state静态<=64KiB并另执行FitsBudget；非allocator/所有产品内存预算，不漏把helper当全部Controller。 |
| Controller借用 | RuntimeMetricsState在Impl136声明，drawing165/thread166在后，逆析构顺序也保借用。`:1421–1422` 传真实Controller第七参session.get；Controller cpp6204–6224 / cppm91–94签名一致，Prepare(nullptr)不建旁挂，准备失败只关闭指标。startup ClearCanvas后1432–1437用实际非零frameSerial/presentAttempts拒空接线。 |
| owner与数据发布 | 构造前Prepare/构造后GPU owner callback/真join后Seal三个阶段各有Session读取权。`ObservePresented:616–620` 在真实Presenter返回/生产U2记录之后由drawing owner读Session并发布atomic进度；无GUI线程读Session plain或GPU。 |
| Start参数/Attach/thread创建失败 | 参数/DWM拒绝1280–1285、Attach1301–1308、thread catch1510–1526均Seal(true)。ownerCreated只在真实jthread构造返回后1508为true；无owner的失败不能伪称joined。 |
| 两类已建owner启动失败 | graphics失败1533–1547和Controller/RTS失败1579–1598沿原failedCleanup.Begin、producer/worker清理与实际drawingThread.join；owner退出1494–1499先保final GPU tuple再destroy drawing、释放presentation/renderer/graphics。Seal在join/清理后，不提前释放Session。 |
| Run异常 | 1477–1489新增runFailed拒报告，原RequestExit/日志/资源释放顺序保持；borrow覆盖Run和drawing析构。 |
| 正常Stop及早退 | 正常Stop命令关闭→RTS Shutdown→退出保存屏障→两worker drain→RequestExit/wake/request_stop→join→detach/清gate→Seal。早退1612–1621要求无attached/running/joinable再drain/Seal；重复Stop不复用或重封已sealed run。 |
| 真封口 | Seal371–399还要求!joinable且drawing已销毁，才InvalidatePending、finalSnapshot/finalInput、atomic进度与sealed release；running=false/stop_requested/inactive不是替代条件。 |
| 离线导出 | Header299–300限定串行生命周期owner。Write402–428先拒off/unavailable/未joined/未sealed/startupFailed/runFailed/无Session、相对/drive-relative路径，再try/catch调用原Session::WriteJson，排序/分配/格式化失败返回false而非terminate。RuntimeMetrics.cpp225–234实际CREATE_NEW，不覆盖已有report，写/Flush失败不会被当成功。 |

C3 owning failedCleanup及startup各Begin位置、成功present的两个借用event与其原gate、NotFound/HiddenPersistenceSnapshot合同时序已核保留；不让metrics替代失败cleanup监督、RTS成功callback静止或保存正确性。普通Draw3/UI3独立设备、两DWM禁用、ULW/FLIP与Win7基线没有改动。

## defaultoff、baseline与metadata范围

- `input.EnableDiagnostics(hidden || metrics):1321` 保留旧hidden诊断，metrics开关不启用hidden注入、改变Record准入、普通鼠标捕获或窗口输入规则。普通两门都关时仍原关闭路径；没有Session/Controller旁挂额外分配、metrics QPC/ThreadTimes/adapter查询或文件I/O。固定Impl状态及初始化原子/小锁不被描述为零CPU成本；Main启动argv解析不被混入每run capture的零heap声明。
- `input.ResetForNextRun` 的真实源码1053–1080没有将累积诊断全清零。Host在Reset/Enable后1322记本run baseline，真join后Seal385–396减10个累积counter；slotCapacity/occupiedSlots取最后事实。第二run报告不能沿用总计2。counter倒退被保守归零的实现不当作无限并发/外部Reset证明；实际生命周期不允许producer并行Reset。
- CaptureMetadata348–369仅drawing owner访问真实graphics.driverType/featureLevel/adapter、presenter/output/rawRevision；启动/退出前GetDesc，普通opt-in presented callback只抄数值。QPF为准备期真实API正值，保存最后真实GPU tuple后才释放资源。128 wchar array和metadataMutex保护caller纯值读取，没有外泄COM/GPU对象；raw outputRevision0仍可保真。
- RuntimeSnapshot1719只从fixed atomic进度和锁保护的metadata复制，不读live Session plain。进度是等待辅助，不是一致逐笔receipt/延迟。sealed JSON仍schema2，Host runSerial/driver metadata没有假称已进新raw schema或完整manifest。

## Specific smoke 实码安全与方法

### Main与输出

当前Main00C的argv解析 `:978–991` 在配置、单实例、普通UI之前识别准确argv[1] selector；合法引号写法也可识别。准确selector下只能argc4且argv[2]==--output-root，否则return2；LocalFree在同步smoke返回后。不会因该selector的缺失/重复/未知尾参数落入普通GUI。其它mode或外部F流程不在本许可。

`HiddenWindowTest.cpp:2504–2527` 拒已有ProductRunning、空/相对/根目录/非lexically-normal/过长root；逐现存parent当前属性非reparse，固定child `u3-h-host-metrics`只接受本轮CreateDirectory成功、拒复用。只向该child固定report路径写CREATE_NEW，不枚举删除，不打开旧EXE旁数据、用户配置或Office。

**准确边界：** directorySafe用GetFileAttributes瞬时检查，不是HANDLE lease/FILE identity或OS sandbox。specific CLEAR限Root自己create-new、稳定、可信的任务私有父树；不能外推任意路径或并发换reparse的防护。Root保存所有失败目录/当前PE/source/stdout/stderr；潜在低层Start/Stop调用不返回时只能按既有own-process超时机制记录FAIL，不把强杀视为PASS。

### 自身窗口与上下文

- MakeHiddenSpec96–116给四role 320×240、-32000、WS_POPUP/NOACTIVATE/TOOLWINDOW、visible=false/bindMessages=false；主Drawpad legacy，无NOREDIRECTION出生；auxiliary LAYERED|TRANSPARENT。smoke强制ULW、禁DComp。2555–2561逐HWND核own PID和不可见，不操作现用户窗口或全局输入。
- Service按值接收specs并move入自己的slot（Window.cpp155–174），不是借用smoke局部vector。StyleContext与Service先声明，StopGuard2537–2541后声明；所有exception/早return都先StopProduct→Service.StopAndJoin，再释放style/Service。真实Window StopUnlocked715–716 join overlay/setting两线程；Host已有自己的drawing真join。未知SendMessage延迟也没有栈payload借用。
- 真实生产WndProc是编译中的Draw3.Product.cpp200–222；旧IdtDrawpad.cpp是工程None项，不能混用历史capture行为。ForwardProductMessage有productCallMutex/drain，StopProduct先拒新调用、排完已进入WndProc，再Host.Stop；style上下文保持到这条链完成。
- RTS在Host::Start实际Initialize，并在自身RealtimeStylus.cpp2432 CoInitializeEx/2681配对Uninitialize；smoke不用普通Main的后期COM初始化。这里只证明调用/所有权，成功HRESULT不是callbacks静止证明。
- autoSaveRoot保持空，Host1315/1318明确不启动保存service；SolidLine state.autoSaveEnabled=false。未调用旧RunMode的capture/持久化/显示流程，不写普通自动保存树。

### 有界检查与归因

- U3H01未Start拒export且无文件；U3H03真defaultoff Start/Stop要求allocated0、无metrics attempt/report；U3H04两非法capacity产品继续start，unavailable拒空报告。
- U3H05同ProductHost独立两次on（128、默认32768），每次新runSerial/零旧contact、实际startup frame/attempt、driver/adapter/QPF事实、共同32MiB、live export拒绝。首report读完才下一Start；原Session未Reset重用。
- U3H06–07由当前own Drawpad `SendMessageTimeoutW(2s)` 的值参数依次Down40,48→Move160,132→Up；隐藏gate→真实Coordinator→Controller/U2→实际Present→Session确认，等待原子seen1/confirmed1及终态recycled/content后真正Stop/Seal。timeout不重复Down，等待时间不充延迟；这是synthetic输入经生产链，不是物理RTS设备正确性。
- U3H08–09离线真WriteJson/fresh parse限4MiB，核schema2、一contact/landing、presentAttempts和本run input Down/Move/terminal/recycled各1、occupied0；重复同路径拒覆盖并完整JSON不变。无mock正确metrics算法、假Present或GPU/光学性能结论。
- U3H10 nullptr真实早参数失败不许导出上一个sealed run；它未动态覆盖上方晚Subscribe异常、GPU/RTS/thread失败路径。所有assert失败计FAIL、guard真Stop/join，未通过不能放宽计数/期限/输入/产品规则凑绿。

## Findings (not fixed) / 未验证边界与后续

本source snapshot没有剩余must-fix。晚Subscribe异常只有静态修补证据；本版本四源/Main尚未新完整编译或运行smoke。每run input/metadata/预算/Start-Stop结果需Root动态核，不预记PASS。directory属性检查的稳定父树前提、RTS成功quiescence、低层API不返回、normal input/真实设备与全部其它架构/Win7分别保留边界。

16+200、phase边界/新JSON字段、Move/Up、thread CPU、Laser生命周期/成本、owner checkpoint/整BGRA/功能等价、fresh生产UInk/Office恢复、GPU/光学、三轮Release/Win7两GPU矩阵仍未实现或未跑于本单元。当前smoke只U3-H；新metrics接口不让未实现U3-F得到许可。

Root后续可在现draw3-integration规范最小记录Host opt-in、1..32768/unavailable、每run新Session/owner借用、真join后sealed与串行离线create-new导出；task §7已批准，不另造demo规范或性能阈值。当前其他源码仍可能半写，Root确认冻结后才完整solution构建，不单独Host/项目绕依赖。

## Verification

- Findings fixed：Root晚Start两处资格修补已实际读核/精确旧hash恢复；本reviewer没有源修改。
- 静态 / safety：SCOPED_STATIC_GREEN、SAFETY_CLEAR_SPECIFIC_U3H_SMOKE，限上列源身份/生命周期/稳定新私有root和准确selector。
- Lint：未运行产品lint；编码/EOL、声明/定义/实际调用与资源作用域静态核。
- TypeCheck / Build / Tests：全部本版本未执行，不沿用UInk/H1或旧U3候选Build0；Root需新完整DebugARM64、适用Headless/parked/PptCOM再specific hidden smoke，保存实际exit/产物/最新hash。