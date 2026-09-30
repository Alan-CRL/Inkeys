# H0→HF 候选独立静态复审

审查截止：2026-09-28 16:37:38 UTC；`HEAD=8b156fca59f0337a6afc6d722941666fcf143080`。对 `.trellis/spec/`、`Inkeys/`、`InkeysHeadlessTests/`、`inkStrokeModelerTestTests/` 中 82 个已跟踪改动和 8 个未跟踪源码，按「相对路径、NUL、文件内容 SHA-256」排序拼接再取 SHA-256，得到 `831b5256b4b801f5f8c04af73d52d9fda6e4221c0eb31f17494206223ad67eaa`。其中 `Draw3.PresentationAutoSave.cpp` 文件 SHA-256 为 `1bb545fdf163bbcbe8c70f570e50b507148dff0a28f1b36f961d5342dd6142f5`，`presentation_autosave_tests.cpp` 为 `967d98dbd382a5229ebd121d64f0bc482057cc63d87064831d9b6f65fd68a8e1`。Storage 测试在审查期间仍被其他 agent 修改；下述结论只对应此指纹，晚期修补须重审受影响链并重跑测试。

范围：检查了全部 82 路径的 diff 目录、工程登记、生产接口与相关测试入口；深读退出/异常/窗口可见性、ContactInput→Host→Controller 命令屏障、PPT 双轨/版本索引及 load/save、更新下载与本地指令、UI3 状态快照、DComp/ULW 与资源配置。长篇无窗口夹具的样本构造、性能 probe 和全部断言未逐行复核；没有把这种路径覆盖当成动态验证。没有修改产品、测试、spec 或共享任务文件。

## Findings (fixed)

- 无。本轮按分工只写此报告。

## Findings (not fixed)

### P1：正式退出后画布可见性仍可逆转（F-027/F-042 交叉）

- `Inkeys/IdtMain.cpp:256-279` 的 `SetOffSignal` 接受关闭/重启后先启动监督器；`Helper.CrashHandler.cppm:57-67` 只向 Window Service **异步**排入 HideAll。此时 Draw3 在 `StopProduct` 完成前仍可 `ProductRunning()==true`。`Inkeys/IdtState.cpp:378-429,545-546` 的可见性预检和 owner 回调只检查 Host running、bridge/mode revision，未检查正式退出意图；`Inkeys/Inkeys/Window/Window.cpp:1383-1386,1472-1479,1682-1751` 的 HideAll 也不是单调关门。若已排队的旧显示命令或并发 `SyncDraw3State` 在 HideAll 后执行，主 Drawpad 可重新显示并拦截桌面，直到正常停机或 15 秒强退。这个时序由代码支持，尚未做 owner 交错故障注入，不能称用户截图的已复现根因。
- 建议：在正式 offSignal 接受点或 Window Service owner 加单调隐藏门，使其后的 Primary/Presentation 请求失效；用可控 owner 队列对抗测试验证 `HideAll → 迟到显示`，真 HWND 的 capture/双表面读回列人工门禁。此项跨状态/窗口公共合同，本 reviewer 不擅改。

### P1：图形 fatal 的活动 contact 与异常保存链未闭合（F-026）

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:8371-8399` 在 presenter/history 恢复最终失败前新增 `captureExitAutoSave(true)`，这能尝试提交**已完成** document/history；但 `captureExitAutoSave` 在 `:6958-7025` 只读取这些槽，没有把仍活动的 `RuntimeStroke` 和已接受未终结 contact CPU 封口。`Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1205-1228` 的 `drawing->Run()` 异常分支直接 `RequestExit()` 后销毁 controller，也没有同一快照屏障；随后 `Stop():1325-1355` 的 final marker 可能只看到 `running=false`。因此当前只能声明「两类明确恢复失败尽力排入已完成页」，不能声明活动笔迹或 Run 异常路径持久化。15 秒强退按用户决定允许丢未 durable 请求，不能替代正常 fatal 封口合同。
- 建议：以隔离的生产 Controller/Host 故障注入分别验证已完成笔迹、活动 Down→Move、Run 异常、worker drain 与最终 UInk/index；若 CPU 封口不能证明安全，保持此项发布阻塞。跨 HWND generation 无损恢复另需真机设计/人工验证。

### P2：PPT 双轨决定与规范/测试残留相反（F-041/F-044）

- 用户已选择旧 page-index 文件原样保留、新 StableSlideId 会话独立存储。当前生产 `Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.cpp:368-377` 禁止 ordinal 升级，`Draw3.PresentationAutoSave.cpp:493-650` 选择 base/sidecar，`:419-455,458-483` 写 index v2 和版本化新文件。可是 `.trellis/spec/native-desktop/draw3-integration.md:183,193,209,221,228` 仍要求 fallback→Stable 按 ordinal 原位升级、同文件覆盖/「单文件」。`Draw3.PresentationAutoSave.cppm:45-50` 还导出返回 `true` 的旧迁移 helper，其唯一调用 `inkStrokeModelerTestTests/presentation_autosave_tests.cpp:2515-2520` 仍断言迁移。该 helper 未被生产调用，但可运行套件会给出与新安全决定相反的绿灯。
- 建议：接口稳定后把 spec 的身份、文件与 index 合同改为双轨/版本化；移除或改写旧 helper 和断言，使测试证明「不按 ordinal 迁移」。这涉及公开辅助接口和任务合同，本 reviewer 不擅改。

### P2：更新规范与用户保留 HTTP 回退的决定冲突（F-014/F-053）

- `.trellis/spec/native-desktop/errors-logging-and-resources.md:230` 新增「正式更新只接受 HTTPS，TLS 失败不得回退 HTTP」。但 `Inkeys/Inkeys/Net/Net.Update.Download.cpp:155-164,212-224` 在 HTTPS 获取版本 JSON 或包失败时主动 HTTP 重试，父任务 `findings.md` 的 F-014 也记录用户明确要求 HTTP/HTTPS 均保留。实现是用户决定下的当前行为，规范却会误导后续 reviewer 将它视为新缺陷，或误删回退。URL/体积/ZIP 路径限制不提供发布者身份认证；该残余由父任务按用户决定单列，不应擅自改为 HTTPS-only。
- 建议：按用户最终范围修正 spec 与发布风险说明，并用隔离 HTTP/HTTPS 重定向、TLS 失败及实际 CDN 测试记录真实行为；不要把同源 MD5/SHA-256 写为身份认证。

### P2：发布物与实际运行边界尚未有本指纹证据

- `inkStrokeModelerTest/{inkPixelShader,inkVertexShader,laserParticleEmitCS,laserParticleUpdateCS}.cso` 仍为未跟踪的构建字节码；仓库 shader 规范把 `.cso` 认定为生成物，不应混入最终提交/包。`Inkeys/Inkeys.vcxproj` 已登记新增 ShutdownSupervisor 与两个 probe，`InkeysHeadlessTests.vcxproj` 已登记更新安全测试；源码登记本身不能证明 DLL/TLB、shader 资源、三架构导入和运行时依赖已打包。
- 本次只读审查发现 DDB 的统一启动门 `Inkeys/IdtPlug-in.cpp:1150-1179` 在 hash 后按路径 `ShellExecuteW`，没有固定被校验文件的启动身份；需结合插件目录 ACL/并发替换和 `runas` 实测定性。父 F-034 已列此 TOCTOU 边界，不能把静态 hash 门写成可写目录的完整执行保证。来源签名/发布公钥按用户修订范围不在本次强制修复内，但风险需保留原口径。

## 611/611 历史审计核对

- `audit-coverage.tsv` 实有 **611 行、611 个唯一 SHA**，其中 `in_h0=yes` 587、`no` 24；每行 `diff_evidence`、`review_depth`、`conclusion` 非空。`git rev-list 0da01f3e299d1cf56d96b84374e7a7f9406e71a8..H0` 恰为 587，和表中 587 行逐 SHA 相等、无缺失/额外行。24 行是按父表定义的「未合入近期候选」，不属于 H0 产品；本轮未独立重读其全部提交补丁。
- 因此 **611/611 只证明该定义下可枚举历史 SHA 的静态记录齐备**；review_depth 从文档简审到 22 个专项深审不等，不证明每个 commit 同深度无缺陷。它不覆盖本次仍变化的 HF 工作树、不可见/删除 refs、用户安装的 HC/H2 二进制、动态运行、Win7/Office/GPU。`audit-coverage.md` 对这些缺口的声明准确，不能把该比例作为发布通过率。

## Verification

- `git diff --check H0 --`：exit 0；仅检查已跟踪 diff 空白错误。
- 静态：核对了主 Solution/新增源码项目登记、无窗口测试入口注册、611 行集合与 H0 rev-list；未运行 GUI、编译、lint/type-check、测试或性能采样，遵守本次只读分工。此前其他任务的 PASS 不对应上述审查截止指纹。
- 仍需最终修补后的完整 Solution Debug/Release 和可得架构复建、Headless/Draw3/PptCOM/隔离重启及存储测试；Win7 SP1 仅 KB2670838 的 Hardware FL11.0 有/无、WARP、DComp 不可用→ULW FLIP、透明/capture/resize/device-lost、真实 Office 与关机交错均保持人工或环境门禁。无真机结果不得写 PASS。

独立结论：当前指纹不具备可证明的发布通过结论。PPT 双轨及版本索引修补仍在变化，退出画布门禁和 fatal 活动 contact 存在上述静态缺口；完成修补、规范/测试对齐并取得最后一次改动后的构建/运行证据后才能重新判定。
