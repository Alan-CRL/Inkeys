# Win7 guarded 候选质量门（2026-10-08，冻结）

**本次允许交付实机候选：本机代码、编译、隔离 GPU 回归与采集合同 PASS。Win7 修复验收仍为 pending；任务保持 in_progress。**

审查者为 trellis-check，读取 check.jsonl、PRD/design/implement、guarded-package-plan及适用 native/shader 规范。实施者与主会话负责各自文件，本审查不回退他人修改；未运行产品 GUI、未更改正式 HLSL/CSO、未 commit/push/archive。细节见同目录 check-guarded-20261008.md。本文在最终 ZIP 前冻结，ZIP/manifest/CRC 身份由另行交付核验记录，避免包中 source hash 自引用循环。

## Findings (fixed)

- 文件：Draw3.HiddenWindowTest.cpp:2961。新固定Circle场景整图断言原先假定白色 RGB=alpha，误判灰色描边。真实首轮 ARM64/WARP与Hardware均有4例新光标FAIL，原版和候选同时失败；中心128×4、848覆盖及四角正确，只有396个formula errors。独立审查及时通知文件所有者，由其协调修复为按像素中心/圆distance、outer/inner覆盖、灰184/255描边/白填充计算全部四通道 Add，以及opaque Destination下Retain的精确CPU oracle；容差1.01字节，仍保留中心/覆盖/四角/整图对照。产品shader及候选字节没有改变。首次失败双流保存在 ARM64-Debug/tests/first-cursor-predicate-failure；父会话另保留 tests-first-predicate。
- 环境：首轮 Headless 7个失败均涉及 systemTMP中的atomic/config写入。主会话只在测试runner子进程将TEMP/TMP定向到仓库内按架构隔离的短可写目录，未修改产品实现/配置或用户全局环境。重测两架构全部通过，原始失败仍保留。

## Findings (not fixed)

无剩余机械实现问题。以下为证据和验收边界，保留而不以代码改动伪造完成：

- 未在本次操作实际 Win7 SP1/KB2670838；WinPS5.1/mock及Win11像素PASS不能替代Win7 PS2、WARP恢复或用户实际画布可见性。原版/候选两应用阶段需用户执行。
- 105 probe明确清除guarded开关后直接创建新PS，因此GPU回归不执行正常应用LoadShaders的guarded选择分支。本机不开交互GUI；该分支严格gate、实际SHA/创建失败传播已做源码检查，Win7正常阶段将提供实际ShaderSelection日志。
- 新18项用实际resolve blend直接检查Add+Retain*D，不覆盖产品完整MAX/MIN多层/history/snapshot流程；撤回重做、激光/橡皮、选择穿透和定格返回仍按同包实际操作验收。
- 现场证据将旧WARP问题定位到DefaultInk程序群/控制结构；尚未唯一证明某个JIT opcode、derivative、discard或薄线helper。既有VMware硬件非零源但切橡皮才刷新是另一路径，不因本候选本机PASS宣告解决。

## Verification

### 静态、数学与编码

- Lint：限定git diff --check exit0；最终四个C++/header/generator格式检查及冻结collector格式检查通过。已有PRD/design Git LF/CRLF提示不属于本次生产source错误。未套用格式化器或无关清理。
- TypeCheck：C++实际通过现有完整 InkeysRepo.sln Debug|ARM64 和 Release|x64 编译；未另造构建系统、不单独错误构建Inkeys.vcxproj。原生ARM64 MSBuild由vswhere查找，same PowerShell规范PATH、node reuse关闭，每次900秒预算。
- 候选真实FXC `/T ps_5_0 /E main /O3` exit0；28980 bytes，SHA256 **2171DDA6E709A3F4CD444E6281AB64635C3F969CD71722662B04DA0DD8055384**。独立重建整个允许的wrapper源与实际源一致，header数组=真实CSO；ISGN/OSGN payload与正式PS逐字节相同，17资源声明保持。实际ASM的DefaultInk两导数及两个discard都处于positive tail if内。
- 数学完整：所有helper/工具/DefaultInk/薄线/AA/光影/双源公式保留，只增加 `type==0 || type==3 || type<0 || type>20` 正向块。未知type的原d=0尾部行为保留，正常应用不采用NoThin或FullWithoutDefaultInk。
- 两个诊断env必须各为精确单字符1；否则正式资源。真实blob SHA及Create HRESULT记录，Create失败不静默回退。probe/env RAII、wrapper阶段空/空/1和finally恢复核对通过。
- 38个正式/旧包/原始21证据文件SHA前后保持，旧23诊断数组逐个size/hash保持；两最终EXE中的SHADER301–304作为DATAFILE读取，逐字节等于正式CSO。正式PS SHA F55DDF08624442C5A3B512897E9D00130E692AA99709A996879ECE837F1A69F8。
- input-and-ink.md新增7节完整guarded合同，105/69/18、900000ms、两严格开关、原版/候选三stage、reference unavailable及本机/Win7边界同步，独立阅读确认一致。

### 实际构建与隔离 GPU / Headless

以下由主会话执行，独立读取真实build-result、tests/results、完整raw逐项核验，未仅看尾行推测。

| 最终配置 | Solution exit | WARP | Hardware | Headless --no-window |
| --- | --- | --- | --- | --- |
| Debug ARM64 | 0，18.55秒（修正oracle后） | 0；105唯一终态全部PASS | 0；105唯一终态全部PASS | 0，65.163秒 |
| Release x64 | 0，50.61秒（修正oracle后） | 0；105唯一终态全部PASS | 0；105唯一终态全部PASS | 0，12.402秒 |

首次完整ARM64/x64 build也分别exit0（38.11/224.95秒）；修正后仍调用同一完整Solution。构建保留既有不相关转换warnings和postbuild `pwsh.exe` PATH提示；changed Renderer/Hidden文件未见warning，不为零warning修改无关代码。

每条GPU raw含105独立唯一终态=原87+guarded18，旧matrix69及production4/diagnostic7/sequence5均通过。新增18项全部reference available、comparison PASS、mismatch0、**最大通道差0**；每条raw的36个原版/候选独立公式检查也全部PASS、formula/corner errors0。device removed HRESULT0，fixture visible0。四组真实像素共420项通过，不能推广为所有产品工具光学结果。

最终EXE SHA256：

- ARM64 Debug：0BFB2882BC384D0C4028ACAF1FE7D9FD593BCCAE0F999236236961C609D61198。
- x64 Release：23D6E5F811821A4E27CFDCD18A00BDF1B996886F2DE1AAE48154D40D63F0C547。

### 最终采集脚本与真实CLI

- 实际Windows PowerShell5.1 mock47项PASS；冻结源码核心3项再次PASS并证实原本不存在的父环境仍不存在。缺/重复header、terminal、summary、计数、manifest、driver、reference/comparison矛盾全部如实拒绝/incomplete；原版失败、candidate失败/ERROR、hardware unavailable和失败继续分别保留分类。
- 精确owned-child timeout注入PASS：900000ms调用值保持，实际console mock到ready后测试专用ScriptMethod立即模拟截止，精确PID killed/5000ms await/Dispose、child_gone；后续Hardware18仍PASS。没有真实等待15分钟，也没有按名称杀进程。该证据只测试deadline处理，不证明底层驱动永远不会阻塞。
- 实际WinPS5.1最终 x64 EXE `Collect-Win7.ps1 -ProbeOnly` exit0；两后端各105独立唯一终态与18guarded均PASS，reference_unavailable0；identity采集schema/105/69/18、准确EXE SHA、真实退出和两个独立driver齐全。每个真实stdout记录 actual=original、实际正式PS SHA F55D...69F8、28748bytes、Create HRESULT0；probe未被候选env污染。
- 独立核最终collector当前source SHA与actual fixture副本相同；未产生普通应用console/idt文件、没有运行正常GUI。首轮root解析helper把 `reference_unavailable=0` 子串误当Hardware unavailable，已改按identity exit3分类并重解析同一原始capture，无重复实际运行；生产脚本未受影响。
- 最终脚本SHA：Collect 13B3A7B27D8733D5FEDC317E63E0B5783C055C858A27FEFD6E97E7C410EC639F；wrapper D00102B5C238CF87BE5FCB9E1AFC878CE7C9ECCB1CC06292092D63822DF6BEED；唯一CMD F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D；README 630C9142EC2D98CBC23EA918176E0D2DA17DE9C0F165D6C580AE7CD151997951。
- WinPS parser及PS2 API子集静态检查PASS，明确不是实际Win7 PS2运行。

## 冻结身份与最终交付范围

源冻结SHA：HiddenWindowTest **4A87B344B9BEA01F6CF4ECAC0F99496B42272CDC2E82E791D4805EA08914DF2C**（UTF8无BOM+CRLF）；Renderer **7405244718A01EB05070BA08D80EFEECE5C231ACAF35C9B2B7470A1F473BF998**；新guarded header **7B4070188CFB007CA59AE5639F3CA91D247D6D4A3F87D811ACC459415DB9F563**；generator **8E12BAF7D999760AFD1C09ADEE1D9E1888F929FECD757BF520503B2228C5B8AF**。后三者UTF8 BOM+CRLF。数学与资源既定身份未变。

证据根目录为 `C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-guarded-20261008/`：gpu-static-verification、binary-and-preservation、actual-probe-result、两archbuild/tests、collector-test run-1/frozen-recheck/timeout/final-source-identity。

质量门允许主会话基于这些冻结bytes创建独立 Inkeys-Win7-Guarded-x64-Release 单CMD验证ZIP。后续包identity核对单独保存，本文不再写入ZIP hash。Win7实际恢复及完整效果仍待用户实测，不标任务complete。
