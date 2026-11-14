# 多设备漫游现场回归：修复与验证

用户反馈所有接触设备不能拖动、PTP仅偶尔可用后，已授权本轮修复。既定功能/默认/hover规则不变。前轮PASS保留历史，本轮不GUI、不commit/push/归档。

## 证据与修复

附件2692行只有native运动，893 pan-step均held0/samples0，没有contact Register/consume；日志截段与rts-batch排Pen的限制见research/multi-input-regression-log-analysis.md。实际软件Present年龄正常，优先修系统输入前端而非渲染/摩擦。

- 完整设置MANUALUPDATE/DISABLEPIXELSNAPPING/INPUT/EXPLICITHITTEST，整类DM hit-test由应用处理，仅明确PTP关联，非目标不落默认DM路径。
- SetContact S_OK仅关联，不再伪Running；真实Interaction BEGIN/END驱动NeedsUpdate，确认Status才held。Content先到缓存，初始/重复READY不提前退休候选，同步回调/失败/取消不会由S_OK尾部复活。
- 固定配置仅Init，避免系统处理中反复ActivateConfiguration；真实BEGIN可抓停旧自有coast，但待识别不挡Pen/Touch/Mouse。
- 新增共享实际setup/hit-test/association/status/interaction routine的生产probe，以及四类真实Coordinator三事件/倒置/Up与旧策略负对照；不再只有末端bridge注入。

## 本轮实际结果

| 验证 | exit | 耗时 |
| --- | --- | --- |
| build | 0 | 112.672秒 |
| control-fence | 0 | 13.797秒 |
| pan-feel | 0 | 2.719秒 |
| headless-pan | 0 | 2.875秒 |
| diagnostic-output | 0 | 2.313秒 |
| 完整Headless同一产物复核 | 0 | 494.547秒 |

首个build exit1/291.875秒，唯一有意义错误LNK1000 IncrBuildImage。仅清理已核对绝对路径的生成Inkeys.ilk，未改项目/SDK/工具链；原日志保留multi-input-regression-build-attempt1.*。重建通过，未出现源码编译错误。

完整Headless首次exit1/550.938秒，唯一失败为未修改render_scheduler_tests.cpp约1038的“pending idle diagnostics flush...”时序/精确次数合取。新frontend/contact后续测试无失败。原日志与结果保留headless-all-attempt1.log / all-attempt1-results.json；未修改相关源码/断言。独立check确认该测试在新contact测试之前执行，无本轮Touchpad/Window/Controller初始化依赖。随后完全同一source/EXE SHA复核完整套件exit0/494.547秒，末行PASS animation correctness。确认两次之间没有源码/断言/产物变化；首轮具体触发原因未确定，原始失败不能因重跑成功被隐藏。

## 最小增量与独立审查

修前42源码全部匹配前轮冻结SHA，原字节快照multi-input-regression-before-source.json。修复只变更WindowControl cpp/cppm、TouchpadNavigation.h、Controller.cpp、draw3_contact_tests.cpp，新增TouchpadInput.h，共6文件；UI/config/i18n/摩擦/原RTS/工程未改。五个既有文件BOM与修前一致，全43产品/测试源码UTF8+CRLF，最终哈希multi-input-regression-artifact-hashes.json。

最终当前产物：
- Inkeys.exe SHA256 a3eb4d899448630159ec0fcbcd28eb9d022bda6bec9e4b98d75ca94bb6f9b805
- InkeysHeadlessTests.exe SHA256 84bd3f690989c70102966d3bc29fe40dca22edecaad3c40a7ddcaa7578e5e23e

独立审查research/multi-input-regression-review.md未发现剩余确定修复源码缺陷。canvas-pan/input-and-ink/cross-layer guide已同步前端所有权、NeedsUpdate与held分离、真实回调、初始化READY及测试证据边界；仓库不存在模板树，未创建模板系统。

## 验证方式与边界

原生ARM64 MSBuild由vswhere定位，InkeysRepo.sln Debug|ARM64 /m:1 /nr:false，同一PowerShell规范PATH并禁node reuse，1800秒预算。CLI全部CREATE_NO_WINDOW及既有提前返回入口；未创建交互窗口、启动主应用/设置/浏览器或Computer Use。测试/日志/结果前缀Build/Logs/multi-input-regression-*，不覆盖前轮记录。

本轮production函数/真实Coordinator/参数与状态契约已验证；OS递送前半使用官方规则的策略模型，非Windows/RTS硬件输入实测，整类DM消息不落默认路径由源代码核对。修正确定API/状态缺陷不等于现场所有设备已验收，真实PTP/物理设备/RTS共存仍待用户运行新产物验证。首轮调度断言具体触发原因未确定，不以环境归因掩盖失败，也不修改无关代码。任务继续in_progress，未提交。

## 最终结论与续接

本轮源码/API输入契约缺口已修正；最终完整ARM64构建、control-fence、2112行pan-feel、Headless pan、diagnostic-output均exit0。完整Headless首轮一项未修改调度时序断言失败，原样复核exit0，保留两次记录。独立review无剩余确定修复源码缺陷，但这不等于真实设备已验收。

43源码/两EXE最终SHA再次核对一致；6文件修复增量与原BOM/CRLF检查通过。任务仍in_progress，等待用户运行新构建后的现场反馈，不自动GUI、commit/push或归档。当前修复产物路径 Build/ARM64/Debug/Inkeys.exe。


## 2026-11-12 RTS/DM 根因与 quarantine 修复（当前轮）

前述“补 viewport flags/真实 Running 即恢复”的现场判断已被新系统探针推翻。RTS 先启用后同 HWND DM Activate 返回 0x80070005，反序 RTS Enable 同样失败；DM 失败后原 RTS Disable 异常。独立 DM HWND 仅隔离初始化，真实合成 PT_TOUCHPAD 在未注册主窗口时不到达，注册后跨 HWND SetContact 仍返回 0x80070490。证据与原始日志见 research/dm-rts-coexistence-probe.md。

用户选择先停用 PTP 扩展。当前唯一产品修复增量位于 Draw3.WindowControl.cpp：kTouchpadDirectManipulationQuarantined=true，TouchpadNavigationOwner::Initialize 在窗口写入、COM、CreateViewport、Activate 之前返回 false；既有生产 frontend probe 新增 quarantine 断言。其它设备、RTS 后端、动力学、配置/UI 不改；PTP 双指和系统惯性暂停，Touchpad 设置仅保留偏好。

| 验证 | 结果 |
| --- | --- |
| 完整 InkeysRepo.sln Debug|ARM64（含本轮代码） | exit0，wall 38.493s，0 errors |
| 恢复 WindowControl UTF-8 BOM 后最终完整 Solution | exit0，wall 11.477s，0 errors |
| --draw3-control-fence-test（含 quarantine 生产断言） | exit0 |
| --draw3-canvas-pan-feel-test | exit0，2112 rows / 0 failures |
| --draw3-diagnostic-output-test | exit0，production queue / real pipe / no HWND |
| Headless --no-window --canvas-pan-only | exit0 |
| 完整 Headless --no-window | exit0，wall 494.208s；末行 PASS animation correctness，无失败记录 |
| task.json 解析、git diff --check、源码 BOM/CRLF | 通过 |

当前产物：Build/ARM64/Debug/Inkeys.exe，SHA256 f12e5853586cd0be65f128b9b0df57d0151fe749fe797daa87e692bc4d0dd339；InkeysHeadlessTests.exe SHA256 658462f08ac14615cc91b89da04ee249720dbb400bcbc21febff6ccf53f309f4；WindowControl 源码 SHA256 1b535190e51347d2ba23684dd6d6abfca09039c028176ac6b6b292e2d37f2ea2。日志前缀 Build/Logs/dm-rts-quarantine-*。

独立探针获用户明确授权并只操作测试窗口；未启动正式产品 GUI。真实 Mouse/Touch/Pen/PenTail 拖动→回绘制仍需用户使用当前产物验收，不能把自动测试或探针称为真机恢复。任务继续 in_progress，不 commit/push/archive。
