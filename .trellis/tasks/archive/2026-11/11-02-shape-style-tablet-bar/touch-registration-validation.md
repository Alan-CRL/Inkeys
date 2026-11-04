# Touch注册/Pointer来源诊断验证

日期：2026-11-03；任务shape-style-tablet-bar，分支bugfix/color。当前源已冻结，完整Debug ARM64和全部无窗口回归通过；Release x64构建与静态核验通过，设备现场待后续采集。

## 修改与独立检查

- 本轮产品/测试仅IdtMain.cpp、UiPointerDiagnostics.h、ui_pointer_tests.cpp：原五窗注册结果/失败GLE、窗口状态/线程及IsTouchWindow读回、每个原生Pointer Down的注册复查与GetPointerInfo来源。
- 原注册仍每窗一次flags0，保留后续边缘手势处理与Touch adapter登记；不新增Pointer业务，不改变filter/capture/lease/haptics/布局/驱动配置。
- 沿原“测试触控反馈”开关，off不新增窗口/Pointer查询或格式化。可选GetPointerInfo动态解析，原native消息线程立即取证；查询/输出恢复调用者GLE。
- startup role和原Raw一致：bar/bottom-left/bottom-right/middle-left/middle-right。
- 独立check修正启动窗口快照误报phase=down；非PointerDown的窗口状态快照输出phase=none/source=none。已加入真实helper断言。
- 具体字段与测试边界见research/touch-registration-diagnostics-implementation.md；审查见research/touch-registration-diagnostics-check.md。

## 验证结果

沿既有InkeysRepo.sln，vswhere动态定位ARM64原生MSBuild；同一PowerShell规范化单一Path并关闭节点复用。源码与工程配置未因本机故障修改。

| 项目 | 结果 |
| --- | --- |
| 初次Debug ARM64 | exit1，175.57秒；源码编译无error，MSVC链接器IncrBuildImage内部崩溃LNK1000/异常C0000005 |
| 保留增量缓存后完整Solution重试 | exit0，8.27秒；仅把已验证路径Build/ARM64/Debug/Inkeys.ilk移至本阶段TestResults归档，使链接器重新生成缓存；没有改代码/项目/工具链 |
| --ui3-input-worker-test | exit0，0.77秒 |
| --state-mode-production-test | exit0，0.11秒 |
| --pen-width-production-test | exit0，0.11秒 |
| --draw3-pen-width-test | exit0，0.07秒 |
| --draw3-control-fence-test | exit0，0.45秒 |
| InkeysHeadlessTests --no-window | exit0，260.41秒，无超时，最终PASS animation correctness；包含新增真实诊断helper回归 |
| Release x64完整Solution | exit0，332.72秒，无超时；旧IPDB/IOBJ不存在，完整编译59900函数后链接成功 |
| diff/格式 | git diff --ignore-submodules --check通过；三源保持原BOM与CRLF，无bare LF |
| Trellis上下文 | implement/check各9条有效引用，task.py validate通过 |

初次链接器故障属于本机构建链内部失败，已用重新生成本地增量缓存恢复；不将其当作源码缺陷。旧x64输出目录在本轮开始时已不存在，未作旧包导入表对比；新包仍会独立核PE与动态API边界。

原始日志与exit/timedOut/seconds JSON在ignored TestResults/touch-registration/、touch-registration-x64/；首轮失败日志initial-failed-*与原ilk均保留，不覆盖旧Touch修复验证记录。

## 新日志识别

仍沿ui_schema/ui_input v1，新增stage：
- touch_registration：registered/register_failed，registration_result/error/error_applicable/requested_flags。
- touch_window_snapshot：after_registration/pointer_down，owner_tid/pid/owner_matches/is_touch/flags/flags_valid/is_touch_error；received_touch=unknown。
- pointer_info：queried/query_failed/api_unavailable，pointer_ok/type/flags/source_device/hwnd_target及原生时间/processed与raw坐标；touch_hsource=unknown。

所有关键字段拆为短行，PointerDown无新增Move高频查询，Bar channel_callback不重复新增快照。测试验证开关off零查询、GLE保护、成功/失败/缺API、未知来源、原始Pointer字段与不截断。

## 设备边界与交付

未启动GUI/Computer Use，未操作实体数位板，也没有读取/修改驱动Windows Ink配置。此包用于区分注册失败、注册状态变化与Pen未走Touch兼容通道；当前Pointer-only按钮业务缺口仍存在，不把新增诊断当作修复完成。

采集命令与最小操作见touch-registration-capture.md，输出文件M6-touch-registration.txt，避免覆盖旧日志。任务保持in_progress、不归档，本轮不commit/push。

## 最终产物核验

- 程序：Build/x64/Release/Inkeys.exe；2026-11-03 08:29:10（本机时间），39,878,656字节，PE Machine=0x8664。
- SHA256：BCB1D4BE8E823D81B9C150E71EDB008F5430840EB2892EBFE0A09B61CD3970EC。
- ui_schema、touch_registration、touch_window_snapshot、pointer_info、registration_result、is_touch_flags_valid标识均存在；实际dumpbin /imports未发现GetPointerInfo硬导入。记录见TestResults/touch-registration-x64/artifact.json、final-imports.txt。
- 这是本轮新构建的完整测试包；没有在本机启动其GUI，也没有据此宣称数位板或Windows 7实际运行通过。
- 无窗口回归后源未再改动；spec/任务/采集说明与Trellis会话记录同步，不commit/push或归档。
