# Research: 单入口 Win7 诊断候选独立只读审查

- Query: 审核追加原 PS 反向复验、完整 PS 变体和单 CMD 三阶段采集是否保留生产行为、退出合同、PS2 兼容与真实结果。
- Scope: internal；research 角色代行只读审查，作者自行修补；不执行重复 solution 构建、普通应用 GUI 或 Git 操作。
- Date: 2026-10-05

## Findings

状态：**FINAL PASS（限定当前诊断代码、独立 FXC、本机构建/自检/Headless、实际 WinPS 采集 mock 与资料保留范围）**。未发现需要作者修补的本轮缺陷；Win7 两设备光学恢复仍待实测，不能将本机 PASS 写成已修复。最终交付 ZIP/唯一 CMD/manifest 一致性由主会话包装后核验，本报告不预先声称尚未生成的包通过。

### 已确认

- IdtMain.cpp SHA256 `340A40BC16C0A79B861477E79F4D763D26DF5C4BAFAC819E5CCD21266BCEF4E8`。
- `Inkeys/IdtMain.cpp:2442` 在已有非 IDT_RELEASE 初始化内只接受精确一字符环境值 1，局部 Draw3/Cursor 开关 OR 既有配置，无配置字段赋值或保存。其它诊断开关保持。
- `IdtMain.cpp:1046` 原早退 GPU 参数仍由 argv 判断，count 必须 2，进入普通窗口初始化前返回；本次环境 opt-in 不扩大该早退路径。
- 旧 ULW 诊断开关仍仅改变 prcDirty，source/DIB/GPU 拷贝保持，未凭 shader 实验替换正式资源 301–304。

### 冻结 GPU 与字节码审查

- HiddenWindowTest.cpp SHA256 `731C2EE8FA89B680D793550698696587EE73974A2DBB8C1F830588C2C6480CAF`，header `CACA1C9DA939786F07EC4BE44D2300C325CC78BDC31EFD798DA5F7CE99DA74FE`。
- 原 6 项、上传 2 项、微型 PS 5 项保留；新增 5 项是精简之后原 PS、完整原样复编、完整 Cursor 入口常色、完整 Cursor 前移、最后原 PS。最终 18 项的生产分母仍 4；sequence/diagnostic 计数独立，不把新对照成功覆盖原失败。
- `HiddenWindowTest.cpp:2558` sequenceProbe 使用非 null 的 override，保持生产 blend、VS、上传和同一 offscreen；drawProbe 的 `:2390` 与失败计数条件因此不增加 production_cases 或 failures。纹理/RTV/blend/PS 字段逐例恢复；每次独立透明 clear、EVENT/Map 读回。创建新 PS 前即执行第一次原对象 retry，进一步降低创建新 PS 的时序扰动。
- 主动从生产 HLSL 独立重构三个变体，直接 SDK ARM64 FXC ps_5_0/O3 复编，没有调用作者 generator 或重写 live header。三份字节块均逐字节匹配 live header；原三数组与上轮旧包逐字节相同。
- 独立临时证据在 `research/check-one-click-fxc-20261005/`，`recompile.py`、各 HLSL/ASM/CSO 和 `results.json` 可复核。所有临时写入局限该任务 research。
- 三个完整 PS 保留九项 ISGN register/mask、constant/linear 插值、两个输出和生产全部 17 resource rows；各 870/876/870 条非声明 listing 行，仍含 texture2darray 和其它完整路由，不是常色小 PS 冒充完整程序。

| 变体 | 字节数 | 独立复编 SHA256 |
| --- | ---: | --- |
| FullRecompiled | 28748 | 3D4701811FF9A5F556C408FA4BC9885444697C793A8059EF63C9A92626E1CE85 |
| FullCursorEntryConstant | 28860 | A728EEB1FCE900D43398D03F2D881E2D759EB09E969B49711294D0CD76B5FEAF |
| FullCursorAhead | 28748 | 2889BB484C69C9AF5B9B5B71AAF981C923512E7D3B60690C0ECC8399EF65F647 |

独立解析 DXBC chunk 证明 FullRecompiled 的 ISGN/OSGN/SHEX/STAT 与正式 PS 全同，只有 RDEF metadata 不同。因此原样复编恢复不能宣传成“指令修复”，必须保留对象创建/metadata/JIT 状态的解释边界。

### 冻结采集脚本与实际 WinPS mock 审查

- Collect SHA `D953566323033132B004511DABF1116728968D7FE6AAA865C98D9DC2039FA20E`，编排 PS SHA `9FB7B6C2CC3F82B37B574654FB5B52D62EF51B31433131D585B53024980E2607`，CMD SHA `F555222FCED3A2D8A9F18B3272FB5082694334739653BCBD9F6043EE5CE53C9D`，均独立核当前文件一致。
- 一次 probe 再普通与 full 两应用阶段，每阶段同 EXE 路径，根摘要 EXE hash，阶段 identity 保留实际退出/版本/环境；ProbeOnly/SkipProbe 冲突拒绝。未传选项旧默认路线保持。
- normal/full 均显式设置环境，普通清除继承 full1、末轮设1，finally 恢复原 trace/full。只传给进程，不改产品持久化配置、GPU 后端或输入。
- collector 保存自有 Process.Handle、等待退出再读 ExitCode，移除正常路径旧 Refresh；异常只尝试 kill 同一 Process，没有按名称终止。探针等待150000ms与提示/identity一致，单步 GPU 3s不改。
- 路径空格通过变量参数和 CMD `%~dp0` 引号保留；API/语法只使用 PS2 子集，中文 PS 带 BOM。初始缺文件保留诚实 incomplete 汇总；非零采集 captured-with-failure、继续后续；缺终态身份或双流文件 incomplete，不伪造成功。
- `stageComplete` 是退出身份和双流存在的采集状态，不是 18case 或光学 PASS。摘要使用 captured/completed；观察文件明确是用户观察，不能由 completed 宣布 GPU 或画布恢复。
- 独立阅读 `win7-one-click-20261005/collector-tests/final-validation/results.json`、`cmd-results.json`、`restore-result.json` 以及失败/不完整的原 summary 与正常 observations：实际 WinPS5.1 mock 空格路径、三次调用、环境 unset/unset/1、trace1、probe1/app5/final0、缺输出不完整、阶段异常继续、缺 EXE、直接默认/ProbeOnly/SkipProbe/冲突选项均有记录；单 CMD exit0/1 传播 PASS，原环境同 host finally 恢复 PASS。
- 未重复运行这些 mock 或产品 GUI。WinPS5.1 实测与 PS2 静态子集不等于 Win7 PS2 已执行；150s实际timeout及机器断电未模拟，不能宣传覆盖。

### 读取上下文

已读 workflow、prd/design/implement 及 research/one-click-plan-20261005.md，native-desktop index/build-and-compatibility/errors-logging/draw3-shaders/input-and-ink 及 shaders index/cpu-gpu-contracts/build-and-encoding。研究角色明确禁止加载 implement/check.jsonl，本审查直接加载相关文档，保留角色隔离。

### 完整构建与实际新 EXE 验证

独立复核主会话执行的原始证据，根目录 `C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-one-click-20261005/`：

- `ARM64-Debug/build-result.json`：完整 InkeysRepo.sln Debug|ARM64 exit0/40.03s，超时900s；`x64-Release/build-result.json`：同solution Release|x64 exit0/185.05s。检查对应 build.stdout/stderr 与目标路径，而非只看 JSON 状态。复核 build.ps1 使用 vswhere 定位 ARM64 native MSBuild、同 invocation PATH 规范化、PptCOM ZhjOutputDir 与 OutDir 隔离，命令行 LinkIncremental=false 未改项目配置。
- 编译 warnings 为既有 hash 库转换、IdtMain 既有其它行转换、Headless 转换/局部隐藏和 ARM64 LNK4075 等；未见新增诊断段的编译错误，不为零 warning 清理无关源码。
- `ARM64-Debug/tests/results.json`：pixel exit0/1.021s、Headless --no-window exit0/71.657s。`x64-Release/tests/results.json`：pixel exit0/0.811s、Headless exit0/14.335s；Headless stderr均0字节，stdout末尾明确PASS。
- 两架构原始 pixel.stderr 的18个case均PASS：原6、上传2、微型PS5、序列5；正式和精简Cursor848非透明、常色1296、fields opaque黑1296且error union0；summary production4/0、diagnostic7/0、sequence5/0、deviceRemoved0、hidden HWND visible0。
- `actual-probe-result.json` 与实际原目录 `actual WinPS x64 collector/Win7-Diagnostics/20261005-135058-608/`：Windows PowerShell5.1 使用最终 x64 EXE 和冻结 Collect 执行 ProbeOnly，exit0/2.163s，18项PASS，visible0，完整identity、双流保存；没有普通产品GUI。独立核测试副本 Collect SHA 与live相同，实际 EXE SHA `1CB7D8887FD7AD85B923314566909198AC13EE1396B48FD0007E2791D0C8C0FA`。
- 对两份实际 EXE 独立以 LoadLibraryExW AS_DATAFILE 读取SHADER资源301–304，不执行入口或启动GUI；每份资源逐字节等于既有正式 CSO，原生产 PS SHA `F55DDF08624442C5A3B512897E9D00130E692AA99709A996879ECE837F1A69F8`。私有证据 `research/check-one-click-fxc-20261005/binary-result.json` 与根 binary-and-preservation.json一致。
- 完成构建后再次独立核7份source冻结hash、BOM状态和CRLF/bareLF0；preservation-before全部16份原采集/8CSO/旧包hash仍相同。缺失的旧D盘capture在missing-before预先记录，没有将缺失目录算为保留成功。私有 files-result.json保留核验。
- 复核根最新 input-and-ink.md 的最小更新：新CLI参数、单CMD三阶段、18项/production4及metadata同执行块边界、精确局部env值、诚实采集状态、150s与Win7待测边界与实现一致。

## Caveats / Not Found

- GPU/collector 已冻结，本文件包含最终完整 solution/build、新产品自检与Headless证据。构建/测试由主会话执行，审查者独立复核原输出，未重复solution构建或启动普通产品GUI。
- 最终ZIP/文件manifest/唯一CMD门禁由主会话交付包装时执行；当前报告不为未生成ZIP提前签署通过。
- 本轮只写指定 research 报告及该任务 research 下私有 FXC 复编证据；不改作者文件、正式 CSO、生产 shader 或配置。
- 一次审查 shell 调用在 PowerShell 初始化时抛出 System.OutOfMemoryException，未执行命令；轻量无 profile shell 恢复后独立复编通过，属于本机资源/runner 错误，无代码绕过。
- 独立 review script 首次资源行校验因其私有 CRLF regex 包含末尾 CR 失败，修正仅自身 script 后通过；未将该私有检查错误误记为作者 shader 缺陷。
- Win7 两台设备的真实光学恢复仍待实测，任何本机 PASS 不升级为已修复。
